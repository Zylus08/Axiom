#pragma once

#include "axiom/catalog/catalog.h"
#include "axiom/parser/ast.h"
#include "axiom/binder/bound_statement.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace axiom {

struct TableBinding {
    std::string alias;
    TableDef table_def;
    size_t column_offset{0};
};

class Binder {
public:
    explicit Binder(const Catalog& catalog);

    [[nodiscard]] std::unique_ptr<BoundSelectStatement> bind_select(const SelectStatement& stmt);

private:
    const Catalog& catalog_;
    std::vector<TableBinding> active_tables_;
    std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates_;
    std::vector<std::pair<std::string, const BoundExpr*>> select_aliases_;

    bool in_where_clause_{false};
    bool in_aggregate_{false};
    bool is_aggregate_query_{false};

    std::unique_ptr<BoundTableRef> bind_table_ref(const TableRef* table_ref, size_t& current_col_offset);
    std::unique_ptr<BoundExpr> bind_expression(const Expr* expr);
    std::unique_ptr<BoundExpr> bind_column_ref(const ColumnRefExpr* col_ref);
    std::unique_ptr<BoundExpr> bind_unary(const UnaryExpr* unary);
    std::unique_ptr<BoundExpr> bind_binary(const BinaryExpr* binary);
    std::unique_ptr<BoundExpr> bind_aggregate(const AggregateExpr* agg);

    void validate_group_by(const BoundExpr* expr, const std::vector<std::unique_ptr<BoundExpr>>& group_by, const SourceLocation& loc);
    static bool expr_matches_group_by(const BoundExpr* expr, const std::vector<std::unique_ptr<BoundExpr>>& group_by);

    static std::string to_lower(std::string_view str);
};

} // namespace axiom

