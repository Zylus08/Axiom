#include "axiom/planner/logical_planner.h"

namespace axiom {

std::shared_ptr<LogicalOperator> LogicalPlanner::plan_table_ref(const BoundTableRef* table_ref) {
    if (!table_ref) return nullptr;

    if (table_ref->type() == BoundTableType::Base) {
        const auto* base = static_cast<const BoundBaseTableRef*>(table_ref);
        return std::make_shared<LogicalScan>(base->table_def(), base->alias(), base->output_schema());
    }

    if (table_ref->type() == BoundTableType::Join) {
        const auto* join = static_cast<const BoundJoinTableRef*>(table_ref);
        auto left_plan = plan_table_ref(join->left());
        auto right_plan = plan_table_ref(join->right());
        return std::make_shared<LogicalJoin>(std::move(left_plan), std::move(right_plan),
                                             join->join_type(), join->on_condition()->clone(),
                                             join->output_schema());
    }

    return nullptr;
}

std::unique_ptr<BoundExpr> LogicalPlanner::rewrite_for_aggregation(
    const BoundExpr* expr,
    const std::vector<std::unique_ptr<BoundExpr>>& group_by,
    size_t agg_start_idx) {
    if (!expr) return nullptr;

    // Check if expr matches any group_by item
    for (size_t j = 0; j < group_by.size(); ++j) {
        if (expr->is_equal(*group_by[j])) {
            return std::make_unique<BoundColumnRefExpr>("_key_" + std::to_string(j), j, group_by[j]->return_type());
        }
    }

    // Check if expr is an aggregate
    if (expr->type() == BoundExprType::Aggregate) {
        const auto* agg = static_cast<const BoundAggregateExpr*>(expr);
        size_t col_idx = agg_start_idx + agg->agg_index();
        return std::make_unique<BoundColumnRefExpr>("_agg_" + std::to_string(agg->agg_index()), col_idx, agg->return_type());
    }

    if (expr->type() == BoundExprType::Literal) {
        return expr->clone();
    }

    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        auto operand = rewrite_for_aggregation(u->operand(), group_by, agg_start_idx);
        return std::make_unique<BoundUnaryExpr>(u->op(), std::move(operand), u->return_type());
    }

    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        auto left = rewrite_for_aggregation(b->left(), group_by, agg_start_idx);
        auto right = rewrite_for_aggregation(b->right(), group_by, agg_start_idx);
        return std::make_unique<BoundBinaryExpr>(b->op(), std::move(left), std::move(right), b->return_type());
    }

    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        auto child = rewrite_for_aggregation(c->child(), group_by, agg_start_idx);
        return std::make_unique<BoundCastExpr>(std::move(child), c->return_type());
    }

    return expr->clone();
}

std::shared_ptr<LogicalOperator> LogicalPlanner::plan(std::unique_ptr<BoundSelectStatement> stmt) {
    std::shared_ptr<LogicalOperator> current = plan_table_ref(stmt->from_table.get());

    // 1. Filter (WHERE)
    if (stmt->where_clause) {
        current = std::make_shared<LogicalFilter>(std::move(current), std::move(stmt->where_clause));
    }

    // 2. Aggregate
    bool has_aggregation = !stmt->group_by.empty() || !stmt->aggregates.empty();
    size_t num_group_keys = stmt->group_by.size();

    if (has_aggregation) {
        std::vector<ColumnDesc> agg_out_cols;
        for (size_t i = 0; i < stmt->group_by.size(); ++i) {
            agg_out_cols.emplace_back("_key_" + std::to_string(i), stmt->group_by[i]->return_type(), true);
        }
        for (size_t i = 0; i < stmt->aggregates.size(); ++i) {
            agg_out_cols.emplace_back("_agg_" + std::to_string(i), stmt->aggregates[i]->return_type(), true);
        }
        Schema agg_schema(std::move(agg_out_cols));

        current = std::make_shared<LogicalAggregate>(
            std::move(current),
            std::move(stmt->group_by),
            std::move(stmt->aggregates),
            std::move(agg_schema)
        );

        // 3. HAVING
        if (stmt->having_clause) {
            // Note: In logical aggregate, group_by was moved, but we can reconstruct group_by expressions
            // from the logical aggregate operator!
            const auto* agg_op = static_cast<const LogicalAggregate*>(current.get());
            auto rewritten_having = rewrite_for_aggregation(
                stmt->having_clause.get(),
                agg_op->group_by(),
                num_group_keys
            );
            current = std::make_shared<LogicalFilter>(std::move(current), std::move(rewritten_having));
        }
    }

    // 4. Sort (ORDER BY)
    if (!stmt->order_by.empty()) {
        std::vector<BoundOrderItem> order_items;
        if (has_aggregation) {
            const auto* agg_op = static_cast<const LogicalAggregate*>(
                current->type() == LogicalOperatorType::Filter ? current->children()[0].get() : current.get()
            );
            for (auto& item : stmt->order_by) {
                auto rewritten = rewrite_for_aggregation(item.expr.get(), agg_op->group_by(), num_group_keys);
                order_items.push_back(BoundOrderItem{std::move(rewritten), item.is_asc, item.nulls_first});
            }
        } else {
            order_items = std::move(stmt->order_by);
        }
        current = std::make_shared<LogicalSort>(std::move(current), std::move(order_items));
    }

    // 5. Projection
    std::vector<BoundSelectItem> proj_items;
    if (has_aggregation) {
        // Find the LogicalAggregate in the plan
        std::shared_ptr<LogicalOperator> op = current;
        while (op && op->type() != LogicalOperatorType::Aggregate) {
            if (op->children().empty()) break;
            op = op->children()[0];
        }
        const auto* agg_op = static_cast<const LogicalAggregate*>(op.get());

        for (auto& item : stmt->select_list) {
            auto rewritten = rewrite_for_aggregation(item.expr.get(), agg_op->group_by(), num_group_keys);
            proj_items.push_back(BoundSelectItem{std::move(rewritten), item.name});
        }
    } else {
        proj_items = std::move(stmt->select_list);
    }
    current = std::make_shared<LogicalProjection>(std::move(current), std::move(proj_items), stmt->output_schema);

    // 6. Limit
    if (stmt->limit.has_value() || stmt->offset.has_value()) {
        current = std::make_shared<LogicalLimit>(std::move(current), stmt->limit, stmt->offset);
    }

    return current;
}

} // namespace axiom
