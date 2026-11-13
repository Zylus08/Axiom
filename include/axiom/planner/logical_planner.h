#pragma once

#include "axiom/binder/bound_statement.h"
#include "axiom/planner/logical_plan.h"

namespace axiom {

class LogicalPlanner {
public:
    LogicalPlanner() = default;

    [[nodiscard]] std::shared_ptr<LogicalOperator> plan(std::unique_ptr<BoundSelectStatement> stmt);

private:
    std::shared_ptr<LogicalOperator> plan_table_ref(const BoundTableRef* table_ref);
    std::unique_ptr<BoundExpr> rewrite_for_aggregation(
        const BoundExpr* expr,
        const std::vector<std::unique_ptr<BoundExpr>>& group_by,
        size_t agg_start_idx);
};

} // namespace axiom

