#pragma once

#include "axiom/planner/logical_plan.h"
#include <memory>
#include <vector>

namespace axiom {

class LogicalOptimizer {
public:
    LogicalOptimizer() = default;

    [[nodiscard]] std::shared_ptr<LogicalOperator> optimize(std::shared_ptr<LogicalOperator> root);

private:
    std::shared_ptr<LogicalOperator> pushdown_predicates(std::shared_ptr<LogicalOperator> op);
    std::shared_ptr<LogicalOperator> prune_projections(std::shared_ptr<LogicalOperator> op);
    std::shared_ptr<LogicalOperator> select_join_sides(std::shared_ptr<LogicalOperator> op);

    static void split_conjuncts(const BoundExpr* expr, std::vector<std::unique_ptr<BoundExpr>>& conjuncts);
    static std::unique_ptr<BoundExpr> combine_conjuncts(std::vector<std::unique_ptr<BoundExpr>> conjuncts);
    static bool references_columns_in_range(const BoundExpr* expr, size_t min_col, size_t max_col);
    static std::unique_ptr<BoundExpr> shift_columns(const BoundExpr* expr, int offset);
};

} // namespace axiom

