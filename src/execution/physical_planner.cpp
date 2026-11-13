#include "axiom/execution/physical_planner.h"
#include "axiom/execution/physical_scan.h"
#include "axiom/execution/physical_filter.h"
#include "axiom/execution/physical_projection.h"
#include "axiom/execution/physical_hash_join.h"
#include "axiom/execution/physical_hash_aggregate.h"
#include "axiom/execution/physical_sort.h"
#include "axiom/execution/physical_limit.h"

namespace axiom {

PhysicalPlanner::PhysicalPlanner(size_t batch_size) : batch_size_(batch_size) {}

std::shared_ptr<PhysicalOperator> PhysicalPlanner::plan(const std::shared_ptr<LogicalOperator>& logical_plan) {
    if (!logical_plan) return nullptr;

    switch (logical_plan->type()) {
        case LogicalOperatorType::Scan: {
            const auto* scan = static_cast<const LogicalScan*>(logical_plan.get());
            return std::make_shared<PhysicalScan>(scan->table_def(), scan->alias(), scan->output_schema(), batch_size_);
        }
        case LogicalOperatorType::Filter: {
            const auto* filter = static_cast<const LogicalFilter*>(logical_plan.get());
            auto child = plan(logical_plan->children()[0]);
            return std::make_shared<PhysicalFilter>(std::move(child), filter->predicate()->clone());
        }
        case LogicalOperatorType::Join: {
            const auto* join = static_cast<const LogicalJoin*>(logical_plan.get());
            auto left = plan(logical_plan->children()[0]);
            auto right = plan(logical_plan->children()[1]);
            return std::make_shared<PhysicalHashJoin>(std::move(left), std::move(right),
                                                      join->join_type(), join->condition()->clone(),
                                                      join->output_schema(), batch_size_);
        }
        case LogicalOperatorType::Aggregate: {
            const auto* agg = static_cast<const LogicalAggregate*>(logical_plan.get());
            std::shared_ptr<PhysicalOperator> child = nullptr;
            if (!logical_plan->children().empty()) {
                child = plan(logical_plan->children()[0]);
            }
            std::vector<std::unique_ptr<BoundExpr>> group_by;
            for (const auto& gb : agg->group_by()) {
                group_by.push_back(gb->clone());
            }
            std::vector<std::unique_ptr<BoundAggregateExpr>> aggs;
            for (const auto& a : agg->aggregates()) {
                aggs.push_back(std::make_unique<BoundAggregateExpr>(
                    a->func(),
                    a->argument() ? a->argument()->clone() : nullptr,
                    a->return_type(),
                    a->agg_index()
                ));
            }
            return std::make_shared<PhysicalHashAggregate>(std::move(child), std::move(group_by),
                                                           std::move(aggs), agg->output_schema(), batch_size_);
        }
        case LogicalOperatorType::Sort: {
            const auto* sort = static_cast<const LogicalSort*>(logical_plan.get());
            auto child = plan(logical_plan->children()[0]);
            std::vector<BoundOrderItem> order_items;
            for (const auto& item : sort->order_by()) {
                order_items.push_back(BoundOrderItem{item.expr->clone(), item.is_asc, item.nulls_first});
            }
            return std::make_shared<PhysicalSort>(std::move(child), std::move(order_items), batch_size_);
        }
        case LogicalOperatorType::Limit: {
            const auto* limit = static_cast<const LogicalLimit*>(logical_plan.get());
            auto child = plan(logical_plan->children()[0]);
            return std::make_shared<PhysicalLimit>(std::move(child), limit->limit(), limit->offset());
        }
        case LogicalOperatorType::Projection: {
            const auto* proj = static_cast<const LogicalProjection*>(logical_plan.get());
            auto child = plan(logical_plan->children()[0]);
            std::vector<BoundSelectItem> items;
            for (const auto& item : proj->select_list()) {
                items.push_back(BoundSelectItem{item.expr->clone(), item.name});
            }
            return std::make_shared<PhysicalProjection>(std::move(child), std::move(items), proj->output_schema());
        }
    }

    throw AxiomException(ErrorCategory::Planner, "Unknown logical operator type in physical planner");
}

} // namespace axiom
