#include "axiom/common/query_engine.h"
#include "axiom/storage/csv_reader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace {

void print_help() {
    std::cout << "Axiom Analytical SQL Engine (Milestone M1)\n"
              << "Usage: axiom [options]\n\n"
              << "Options:\n"
              << "  -e, --query <sql>       Execute SQL query string\n"
              << "  -f, --file <path>       Execute SQL query from file\n"
              << "  -t, --table <spec>      Register table: <name>=<csv_path>[:col1:type,col2:type,...]\n"
              << "  --batch-size <n>        Execution batch size (default: 1024)\n"
              << "  --format <fmt>          Output format: table, csv, json (default: table)\n"
              << "  --explain               Prefix query with EXPLAIN\n"
              << "  --version               Display version\n"
              << "  -h, --help              Display this help message\n\n"
              << "Types: bigint (int), double (float), varchar (string), boolean (bool)\n\n"
              << "Examples:\n"
              << "  axiom -t users=users.csv:id:bigint,name:varchar,age:bigint -e \"SELECT * FROM users WHERE age > 30\"\n"
              << "  axiom -t orders=orders.csv:id:bigint,user_id:bigint,amount:double -f query.sql --format csv\n";
}

void print_version() {
    std::cout << "axiom version 0.1.0-m1 (x86_64-windows-msvc C++20)\n";
}

axiom::Schema infer_schema_from_csv(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        throw axiom::AxiomException(axiom::ErrorCategory::Storage, "Failed to open CSV file for schema inference: " + csv_path);
    }

    std::string header_line;
    if (!std::getline(file, header_line)) {
        throw axiom::AxiomException(axiom::ErrorCategory::Storage, "CSV file is empty: " + csv_path);
    }

    // Split header line
    std::vector<std::string> col_names;
    std::string token;
    std::istringstream hss(header_line);
    while (std::getline(hss, token, ',')) {
        // Trim whitespace and quotes
        size_t start = token.find_first_not_of(" \t\r\n\"");
        size_t end = token.find_last_not_of(" \t\r\n\"");
        if (start != std::string::npos && end != std::string::npos) {
            col_names.push_back(token.substr(start, end - start + 1));
        } else {
            col_names.push_back(token);
        }
    }

    // Read first row to infer types
    std::string data_line;
    std::vector<axiom::TypeKind> col_types(col_names.size(), axiom::TypeKind::VarChar);

    if (std::getline(file, data_line)) {
        std::vector<std::string> row_vals;
        std::istringstream dss(data_line);
        while (std::getline(dss, token, ',')) {
            size_t start = token.find_first_not_of(" \t\r\n\"");
            size_t end = token.find_last_not_of(" \t\r\n\"");
            if (start != std::string::npos && end != std::string::npos) {
                row_vals.push_back(token.substr(start, end - start + 1));
            } else {
                row_vals.push_back(token);
            }
        }

        for (size_t i = 0; i < std::min(col_names.size(), row_vals.size()); ++i) {
            const std::string& val = row_vals[i];
            if (val.empty() || val == "NULL" || val == "null") {
                continue;
            }
            // Try int
            try {
                size_t idx = 0;
                (void)std::stoll(val, &idx);
                if (idx == val.size()) {
                    col_types[i] = axiom::TypeKind::BigInt;
                    continue;
                }
            } catch (...) {}

            // Try double
            try {
                size_t idx = 0;
                (void)std::stod(val, &idx);
                if (idx == val.size()) {
                    col_types[i] = axiom::TypeKind::Double;
                    continue;
                }
            } catch (...) {}

            // Try bool
            std::string lower = val;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "true" || lower == "false") {
                col_types[i] = axiom::TypeKind::Boolean;
                continue;
            }

            col_types[i] = axiom::TypeKind::VarChar;
        }
    }

    std::vector<axiom::ColumnDesc> cols;
    cols.reserve(col_names.size());
    for (size_t i = 0; i < col_names.size(); ++i) {
        cols.emplace_back(col_names[i], col_types[i], true);
    }
    return axiom::Schema(std::move(cols));
}

axiom::Schema parse_explicit_schema(std::string_view schema_str) {
    std::vector<axiom::ColumnDesc> cols;
    std::istringstream ss{std::string(schema_str)};
    std::string item;

    while (std::getline(ss, item, ',')) {
        size_t colon = item.find(':');
        if (colon == std::string::npos) {
            throw axiom::AxiomException(axiom::ErrorCategory::Storage, "Invalid column specification in schema: " + item + " (expected name:type)");
        }
        std::string name = item.substr(0, colon);
        std::string type_str = item.substr(colon + 1);

        auto type_opt = axiom::parse_type_kind(type_str);
        if (!type_opt.has_value()) {
            throw axiom::AxiomException(axiom::ErrorCategory::Storage, "Unknown type in schema: " + type_str);
        }

        cols.emplace_back(std::move(name), *type_opt, true);
    }

    return axiom::Schema(std::move(cols));
}

void register_table_spec(axiom::QueryEngine& engine, const std::string& spec) {
    // Format: name=csv_path[:col1:type,col2:type]
    size_t eq_pos = spec.find('=');
    if (eq_pos == std::string::npos) {
        throw axiom::AxiomException(axiom::ErrorCategory::Storage, "Invalid table spec: " + spec + " (expected name=path[:schema])");
    }

    std::string name = spec.substr(0, eq_pos);
    std::string rest = spec.substr(eq_pos + 1);

    size_t colon_pos = rest.find(':');
    std::string csv_path;
    axiom::Schema schema;

    if (colon_pos == std::string::npos) {
        csv_path = rest;
        schema = infer_schema_from_csv(csv_path);
    } else {
        csv_path = rest.substr(0, colon_pos);
        schema = parse_explicit_schema(rest.substr(colon_pos + 1));
    }

    axiom::CsvOptions opts;
    opts.has_header = true;
    engine.catalog().register_table(name, std::move(schema), csv_path, opts);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        std::string query;
        std::string file_path;
        std::vector<std::string> table_specs;
        std::string format = "table";
        size_t batch_size = 1024;
        bool explain_flag = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                print_help();
                return 0;
            }
            if (arg == "--version") {
                print_version();
                return 0;
            }
            if ((arg == "-e" || arg == "--query") && i + 1 < argc) {
                query = argv[++i];
            } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
                file_path = argv[++i];
            } else if ((arg == "-t" || arg == "--table") && i + 1 < argc) {
                table_specs.push_back(argv[++i]);
            } else if (arg == "--format" && i + 1 < argc) {
                format = argv[++i];
            } else if (arg == "--batch-size" && i + 1 < argc) {
                batch_size = std::stoull(argv[++i]);
            } else if (arg == "--explain") {
                explain_flag = true;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                print_help();
                return 1;
            }
        }

        axiom::QueryEngine engine(batch_size);

        for (const auto& spec : table_specs) {
            register_table_spec(engine, spec);
        }

        if (!file_path.empty()) {
            std::ifstream f(file_path);
            if (!f.is_open()) {
                std::cerr << "Error: Could not open query file: " << file_path << "\n";
                return 1;
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            query = buffer.str();
        }

        if (!query.empty()) {
            if (explain_flag && query.rfind("EXPLAIN", 0) != 0 && query.rfind("explain", 0) != 0) {
                query = "EXPLAIN " + query;
            }

            auto result = engine.execute(query);

            if (format == "csv") {
                std::cout << result.format_csv();
            } else if (format == "json") {
                std::cout << result.format_json();
            } else {
                std::cout << result.format_table();
            }
            return 0;
        }

        // Interactive REPL mode
        std::cout << "Axiom Analytical SQL Engine Interactive Shell\n"
                  << "Type SQL statement ending with ';' or 'exit' / 'quit' to exit.\n\n";

        std::string current_cmd;
        while (true) {
            std::cout << (current_cmd.empty() ? "axiom> " : "   ... ");
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }

            std::string trimmed = line;
            size_t s = trimmed.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) trimmed = trimmed.substr(s);
            if (trimmed == "exit" || trimmed == "quit") {
                break;
            }

            current_cmd += " " + line;

            if (line.find(';') != std::string::npos) {
                try {
                    auto result = engine.execute(current_cmd);
                    if (format == "csv") {
                        std::cout << result.format_csv();
                    } else if (format == "json") {
                        std::cout << result.format_json();
                    } else {
                        std::cout << result.format_table();
                    }
                } catch (const axiom::AxiomException& e) {
                    std::cerr << e.what() << "\n";
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
                current_cmd.clear();
            }
        }

        return 0;
    } catch (const axiom::AxiomException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
        return 1;
    }
}
