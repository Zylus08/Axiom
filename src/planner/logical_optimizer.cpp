#include "axiom/planner/logical_optimizer.h"
#include <filesystem>

namespace axiom {

void LogicalOptimizer::split_conjuncts(const BoundExpr* expr, std::vector<std::unique_ptr<BoundExpr>>& conjuncts) {
    if (!expr) return;
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        if (b->op() == BinaryOp::And) {
            split_conjuncts(b->left(), conjuncts);
            split_conjuncts(b->right(), conjuncts);
            return;
        }
    }
    conjuncts.push_back(expr->clone());
}

std::unique_ptr<BoundExpr> LogicalOptimizer::combine_conjuncts(std::vector<std::unique_ptr<BoundExpr>> conjuncts) {
    if (conjuncts.empty()) return nullptr;
    if (conjuncts.size() == 1) return std::move(conjuncts[0]);

    auto result = std::move(conjuncts[0]);
    for (size_t i = 1; i < conjuncts.size(); ++i) {
        result = std::make_unique<BoundBinaryExpr>(BinaryOp::And, std::move(result), std::move(conjuncts[i]), TypeKind::Boolean);
    }
    return result;
}

bool LogicalOptimizer::references_columns_in_range(const BoundExpr* expr, size_t min_col, size_t max_col) {
    if (!expr) return true;
    if (expr->type() == BoundExprType::ColumnRef) {
        const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
        return col->col_index() >= min_col && col->col_index() < max_col;
    }
    if (expr->type() == BoundExprType::Literal) {
        return true;
    }
    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        return references_columns_in_range(u->operand(), min_col, max_col);
    }
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        return references_columns_in_range(b->left(), min_col, max_col) &&
               references_columns_in_range(b->right(), min_col, max_col);
    }
    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        return references_columns_in_range(c->child(), min_col, max_col);
    }
    return false;
}

std::unique_ptr<BoundExpr> LogicalOptimizer::shift_columns(const BoundExpr* expr, int offset) {
    if (!expr) return nullptr;
    if (expr->type() == BoundExprType::ColumnRef) {
        const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
        size_t new_idx = static_cast<size_t>(static_cast<int>(col->col_index()) + offset);
        return std::make_unique<BoundColumnRefExpr>(col->name(), new_idx, col->return_type(), col->table_alias());
    }
    if (expr->type() == BoundExprType::Literal) {
        return expr->clone();
    }
    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        return std::make_unique<BoundUnaryExpr>(u->op(), shift_columns(u->operand(), offset), u->return_type());
    }
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        return std::make_unique<BoundBinaryExpr>(b->op(), shift_columns(b->left(), offset), shift_columns(b->right(), offset), b->return_type());
    }
    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        return std::make_unique<BoundCastExpr>(shift_columns(c->child(), offset), c->return_type());
    }
    return expr->clone();
}

std::shared_ptr<LogicalOperator> LogicalOptimizer::pushdown_predicates(std::shared_ptr<LogicalOperator> op) {
    if (!op) return nullptr;

    // Check if current operator is Filter above Join
    if (op->type() == LogicalOperatorType::Filter) {
        const auto* filter = static_cast<const LogicalFilter*>(op.get());
        auto child = op->children()[0];

        if (child->type() == LogicalOperatorType::Join) {
            auto join = std::static_pointer_cast<LogicalJoin>(child);
            auto left = pushdown_predicates(join->children()[0]);
            auto right = pushdown_predicates(join->children()[1]);

            size_t left_cols = left->output_schema().column_count();
            size_t total_cols = join->output_schema().column_count();

            std::vector<std::unique_ptr<BoundExpr>> all_conjuncts;
            split_conjuncts(filter->predicate(), all_conjuncts);

            std::vector<std::unique_ptr<BoundExpr>> left_conjuncts;
            std::vector<std::unique_ptr<BoundExpr>> right_conjuncts;
            std::vector<std::unique_ptr<BoundExpr>> join_conjuncts;

            for (auto& c : all_conjuncts) {
                if (references_columns_in_range(c.get(), 0, left_cols)) {
                    left_conjuncts.push_back(std::move(c));
                } else if (references_columns_in_range(c.get(), left_cols, total_cols)) {
                    right_conjuncts.push_back(shift_columns(c.get(), -static_cast<int>(left_cols)));
                } else {
                    join_conjuncts.push_back(std::move(c));
                }
            }

            if (!left_conjuncts.empty()) {
                auto left_pred = combine_conjuncts(std::move(left_conjuncts));
                left = std::make_shared<LogicalFilter>(std::move(left), std::move(left_pred));
            }
            if (!right_conjuncts.empty()) {
                auto right_pred = combine_conjuncts(std::move(right_conjuncts));
                right = std::make_shared<LogicalFilter>(std::move(right), std::move(right_pred));
            }

            auto new_join = std::make_shared<LogicalJoin>(std::move(left), std::move(right),
                                                          join->join_type(),
                                                          join->condition()->clone(),
                                                          join->output_schema());

            if (!join_conjuncts.empty()) {
                auto remaining_pred = combine_conjuncts(std::move(join_conjuncts));
                return std::make_shared<LogicalFilter>(std::move(new_join), std::move(remaining_pred));
            }
            return new_join;
        }
    }

    // Recursively optimize children
    if (op->type() == LogicalOperatorType::Join) {
        const auto* join = static_cast<const LogicalJoin*>(op.get());
        auto left = pushdown_predicates(op->children()[0]);
        auto right = pushdown_predicates(op->children()[1]);
        return std::make_shared<LogicalJoin>(std::move(left), std::move(right),
                                             join->join_type(),
                                             join->condition()->clone(),
                                             join->output_schema());
    }

    if (op->type() == LogicalOperatorType::Filter) {
        const auto* filter = static_cast<const LogicalFilter*>(op.get());
        auto child = pushdown_predicates(op->children()[0]);
        return std::make_shared<LogicalFilter>(std::move(child), filter->predicate()->clone());
    }

    if (op->type() == LogicalOperatorType::Projection) {
        const auto* proj = static_cast<const LogicalProjection*>(op.get());
        auto child = pushdown_predicates(op->children()[0]);
        std::vector<BoundSelectItem> items;
        for (const auto& item : proj->select_list()) {
            items.push_back(BoundSelectItem{item.expr->clone(), item.name});
        }
        return std::make_shared<LogicalProjection>(std::move(child), std::move(items), proj->output_schema());
    }

    if (op->type() == LogicalOperatorType::Aggregate) {
        const auto* agg = static_cast<const LogicalAggregate*>(op.get());
        std::shared_ptr<LogicalOperator> child = nullptr;
        if (!op->children().empty()) {
            child = pushdown_predicates(op->children()[0]);
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
        return std::make_shared<LogicalAggregate>(std::move(child), std::move(group_by), std::move(aggs), agg->output_schema());
    }

    if (op->type() == LogicalOperatorType::Sort) {
        const auto* sort = static_cast<const LogicalSort*>(op.get());
        auto child = pushdown_predicates(op->children()[0]);
        std::vector<BoundOrderItem> order_by;
        for (const auto& item : sort->order_by()) {
            order_by.push_back(BoundOrderItem{item.expr->clone(), item.is_asc, item.nulls_first});
        }
        return std::make_shared<LogicalSort>(std::move(child), std::move(order_by));
    }

    if (op->type() == LogicalOperatorType::Limit) {
        const auto* limit = static_cast<const LogicalLimit*>(op.get());
        auto child = pushdown_predicates(op->children()[0]);
        return std::make_shared<LogicalLimit>(std::move(child), limit->limit(), limit->offset());
    }

    return op;
}

std::shared_ptr<LogicalOperator> LogicalOptimizer::select_join_sides(std::shared_ptr<LogicalOperator> op) {
    if (!op) return nullptr;

    if (op->type() == LogicalOperatorType::Join) {
        const auto* join = static_cast<const LogicalJoin*>(op.get());
        auto left = select_join_sides(op->children()[0]);
        auto right = select_join_sides(op->children()[1]);

        // If left is Scan and right is Scan, inspect file sizes
        if (left->type() == LogicalOperatorType::Scan && right->type() == LogicalOperatorType::Scan) {
            const auto* scan_left = static_cast<const LogicalScan*>(left.get());
            const auto* scan_right = static_cast<const LogicalScan*>(right.get());

            std::error_code ec1, ec2;
            auto sz_left = std::filesystem::file_size(scan_left->table_def().file_path, ec1);
            auto sz_right = std::filesystem::file_size(scan_right->table_def().file_path, ec2);
            (void)sz_left;
            (void)sz_right;

            // In Axiom hash join, the right child is the build side.
            // If left table is significantly smaller than right table, keep note:
            // Since our physical join places right side in hash table, right side being smaller is already optimal.
        }

        return std::make_shared<LogicalJoin>(std::move(left), std::move(right),
                                             join->join_type(),
                                             join->condition()->clone(),
                                             join->output_schema());
    }

    return op;
}

std::shared_ptr<LogicalOperator> LogicalOptimizer::optimize(std::shared_ptr<LogicalOperator> root) {
    if (!root) return nullptr;
    root = pushdown_predicates(root);
    root = select_join_sides(root);
    return root;
}

} // namespace axiom

