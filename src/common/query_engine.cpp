#include "axiom/common/query_engine.h"
#include "axiom/parser/lexer.h"
#include "axiom/parser/parser.h"
#include "axiom/binder/binder.h"
#include "axiom/planner/logical_planner.h"
#include "axiom/execution/physical_planner.h"
#include <sstream>
#include <iomanip>

namespace axiom {

std::string QueryResult::format_table() const {
    if (is_explain) {
        return explain_text;
    }

    if (schema.empty()) {
        return "Empty result\n";
    }

    std::vector<size_t> col_widths(schema.column_count());
    for (size_t c = 0; c < schema.column_count(); ++c) {
        col_widths[c] = schema.column(c).name.size();
    }

    for (const auto& batch : batches) {
        for (size_t r = 0; r < batch->row_count(); ++r) {
            for (size_t c = 0; c < schema.column_count(); ++c) {
                std::string s = batch->get_value(c, r).to_string();
                if (s.size() > col_widths[c]) {
                    col_widths[c] = s.size();
                }
            }
        }
    }

    std::ostringstream oss;
    // Header
    oss << "+";
    for (size_t c = 0; c < schema.column_count(); ++c) {
        oss << std::string(col_widths[c] + 2, '-') << "+";
    }
    oss << "\n|";
    for (size_t c = 0; c < schema.column_count(); ++c) {
        oss << " " << std::left << std::setw(static_cast<int>(col_widths[c])) << schema.column(c).name << " |";
    }
    oss << "\n+";
    for (size_t c = 0; c < schema.column_count(); ++c) {
        oss << std::string(col_widths[c] + 2, '-') << "+";
    }
    oss << "\n";

    // Data rows
    for (const auto& batch : batches) {
        for (size_t r = 0; r < batch->row_count(); ++r) {
            oss << "|";
            for (size_t c = 0; c < schema.column_count(); ++c) {
                std::string s = batch->get_value(c, r).to_string();
                oss << " " << std::left << std::setw(static_cast<int>(col_widths[c])) << s << " |";
            }
            oss << "\n";
        }
    }

    oss << "+";
    for (size_t c = 0; c < schema.column_count(); ++c) {
        oss << std::string(col_widths[c] + 2, '-') << "+";
    }
    oss << "\n";
    oss << "(" << total_rows << " row" << (total_rows == 1 ? "" : "s") << ")\n";

    return oss.str();
}

std::string QueryResult::format_csv() const {
    if (is_explain) {
        return explain_text;
    }

    std::ostringstream oss;
    for (size_t c = 0; c < schema.column_count(); ++c) {
        if (c > 0) oss << ",";
        oss << schema.column(c).name;
    }
    oss << "\n";

    for (const auto& batch : batches) {
        for (size_t r = 0; r < batch->row_count(); ++r) {
            for (size_t c = 0; c < schema.column_count(); ++c) {
                if (c > 0) oss << ",";
                const auto& v = batch->get_value(c, r);
                if (v.is_null()) {
                    oss << "";
                } else if (v.kind() == TypeKind::VarChar) {
                    oss << "\"" << v.get_varchar() << "\"";
                } else {
                    oss << v.to_string();
                }
            }
            oss << "\n";
        }
    }

    return oss.str();
}

std::string QueryResult::format_json() const {
    if (is_explain) {
        return "{\"explain\": \"" + explain_text + "\"}";
    }

    std::ostringstream oss;
    oss << "[\n";
    bool first_row = true;

    for (const auto& batch : batches) {
        for (size_t r = 0; r < batch->row_count(); ++r) {
            if (!first_row) oss << ",\n";
            first_row = false;
            oss << "  {";
            for (size_t c = 0; c < schema.column_count(); ++c) {
                if (c > 0) oss << ", ";
                oss << "\"" << schema.column(c).name << "\": ";
                const auto& v = batch->get_value(c, r);
                if (v.is_null()) {
                    oss << "null";
                } else if (v.kind() == TypeKind::Boolean) {
                    oss << (v.get_bool() ? "true" : "false");
                } else if (v.kind() == TypeKind::BigInt) {
                    oss << v.get_bigint();
                } else if (v.kind() == TypeKind::Double) {
                    oss << v.get_double();
                } else {
                    oss << "\"" << v.get_varchar() << "\"";
                }
            }
            oss << "}";
        }
    }
    oss << "\n]\n";
    return oss.str();
}

QueryEngine::QueryEngine(size_t batch_size) : batch_size_(batch_size) {}

QueryResult QueryEngine::execute(const std::string& sql) {
    // 1. Tokenize
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();

    // 2. Parse
    Parser parser(std::move(tokens));
    auto ast = parser.parse_select();

    bool is_explain = ast->is_explain;

    // 3. Bind
    Binder binder(catalog_);
    auto bound = binder.bind_select(*ast);

    // 4. Logical Plan
    LogicalPlanner logical_planner;
    auto logical_plan = logical_planner.plan(std::move(bound));

    // 5. Physical Plan
    PhysicalPlanner physical_planner(batch_size_);
    auto physical_plan = physical_planner.plan(logical_plan);

    if (is_explain) {
        QueryResult res;
        res.is_explain = true;
        std::ostringstream oss;
        oss << "=== Logical Plan ===\n" << logical_plan->to_string()
            << "\n=== Physical Plan ===\n" << physical_plan->to_string();
        res.explain_text = oss.str();
        return res;
    }

    // 6. Execute
    QueryResult res;
    res.schema = physical_plan->output_schema();
    physical_plan->init();

    while (true) {
        auto batch = physical_plan->next();
        if (!batch) break;
        res.total_rows += batch->row_count();
        res.batches.push_back(std::move(batch));
    }

    physical_plan->close();
    return res;
}

} // namespace axiom

