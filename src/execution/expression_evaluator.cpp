#include "axiom/execution/expression_evaluator.h"

namespace axiom {

Value ExpressionEvaluator::evaluate_row(const BoundExpr* expr, const RecordBatch& batch, size_t row_idx) {
    if (!expr) {
        return Value::make_null();
    }

    switch (expr->type()) {
        case BoundExprType::Literal: {
            const auto* lit = static_cast<const BoundLiteralExpr*>(expr);
            return lit->value();
        }
        case BoundExprType::ColumnRef: {
            const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
            return batch.get_value(col->col_index(), row_idx);
        }
        case BoundExprType::Unary: {
            const auto* u = static_cast<const BoundUnaryExpr*>(expr);
            Value val = evaluate_row(u->operand(), batch, row_idx);
            switch (u->op()) {
                case UnaryOp::Plus: return val;
                case UnaryOp::Minus: return val.negate();
                case UnaryOp::Not: return val.sql_not();
                case UnaryOp::IsNull: return val.sql_is_null();
                case UnaryOp::IsNotNull: return val.sql_is_not_null();
            }
            break;
        }
        case BoundExprType::Binary: {
            const auto* b = static_cast<const BoundBinaryExpr*>(expr);
            Value left = evaluate_row(b->left(), batch, row_idx);
            Value right = evaluate_row(b->right(), batch, row_idx);

            switch (b->op()) {
                case BinaryOp::Add: return left.add(right);
                case BinaryOp::Subtract: return left.subtract(right);
                case BinaryOp::Multiply: return left.multiply(right);
                case BinaryOp::Divide: return left.divide(right);
                case BinaryOp::Modulo: return left.modulo(right);
                case BinaryOp::Equal: return left.sql_equals(right);
                case BinaryOp::NotEqual: return left.sql_not_equals(right);
                case BinaryOp::LessThan: return left.sql_less_than(right);
                case BinaryOp::LessEqual: return left.sql_less_than_or_equal(right);
                case BinaryOp::GreaterThan: return left.sql_greater_than(right);
                case BinaryOp::GreaterEqual: return left.sql_greater_than_or_equal(right);
                case BinaryOp::And: return left.sql_and(right);
                case BinaryOp::Or: return left.sql_or(right);
            }
            break;
        }
        case BoundExprType::Cast: {
            const auto* c = static_cast<const BoundCastExpr*>(expr);
            Value val = evaluate_row(c->child(), batch, row_idx);
            return val.cast_to(c->return_type());
        }
        case BoundExprType::Aggregate: {
            throw AxiomException(ErrorCategory::Execution, "AggregateExpr should not be evaluated as row expression");
        }
    }

    return Value::make_null();
}

std::vector<Value> ExpressionEvaluator::evaluate_batch(const BoundExpr* expr, const RecordBatch& batch) {
    size_t rows = batch.row_count();
    std::vector<Value> result;
    result.reserve(rows);

    if (rows == 0) {
        return result;
    }

    if (!expr) {
        result.resize(rows, Value::make_null());
        return result;
    }

    // Optimization: ColumnRef direct column copy
    if (expr->type() == BoundExprType::ColumnRef) {
        const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
        return batch.get_column(col->col_index());
    }

    // Optimization: Literal constant replicate
    if (expr->type() == BoundExprType::Literal) {
        const auto* lit = static_cast<const BoundLiteralExpr*>(expr);
        result.resize(rows, lit->value());
        return result;
    }

    // General row-by-row fallback
    for (size_t r = 0; r < rows; ++r) {
        result.push_back(evaluate_row(expr, batch, r));
    }

    return result;
}

} // namespace axiom
