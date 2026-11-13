#include "axiom/binder/bound_expression.h"

namespace axiom {

std::string BoundUnaryExpr::to_string() const {
    switch (op_) {
        case UnaryOp::Plus: return "+" + operand_->to_string();
        case UnaryOp::Minus: return "-" + operand_->to_string();
        case UnaryOp::Not: return "NOT (" + operand_->to_string() + ")";
        case UnaryOp::IsNull: return "(" + operand_->to_string() + " IS NULL)";
        case UnaryOp::IsNotNull: return "(" + operand_->to_string() + " IS NOT NULL)";
    }
    return "";
}

bool BoundUnaryExpr::is_equal(const BoundExpr& other) const {
    if (other.type() != BoundExprType::Unary) return false;
    const auto& u = static_cast<const BoundUnaryExpr&>(other);
    return op_ == u.op_ && operand_->is_equal(*u.operand_);
}

std::string BoundBinaryExpr::to_string() const {
    std::string op_str;
    switch (op_) {
        case BinaryOp::Add: op_str = " + "; break;
        case BinaryOp::Subtract: op_str = " - "; break;
        case BinaryOp::Multiply: op_str = " * "; break;
        case BinaryOp::Divide: op_str = " / "; break;
        case BinaryOp::Modulo: op_str = " % "; break;
        case BinaryOp::Equal: op_str = " = "; break;
        case BinaryOp::NotEqual: op_str = " != "; break;
        case BinaryOp::LessThan: op_str = " < "; break;
        case BinaryOp::LessEqual: op_str = " <= "; break;
        case BinaryOp::GreaterThan: op_str = " > "; break;
        case BinaryOp::GreaterEqual: op_str = " >= "; break;
        case BinaryOp::And: op_str = " AND "; break;
        case BinaryOp::Or: op_str = " OR "; break;
    }
    return "(" + left_->to_string() + op_str + right_->to_string() + ")";
}

bool BoundBinaryExpr::is_equal(const BoundExpr& other) const {
    if (other.type() != BoundExprType::Binary) return false;
    const auto& b = static_cast<const BoundBinaryExpr&>(other);
    return op_ == b.op_ && left_->is_equal(*b.left_) && right_->is_equal(*b.right_);
}

std::string BoundAggregateExpr::to_string() const {
    switch (func_) {
        case AggregateFunc::CountStar: return "COUNT(*)";
        case AggregateFunc::Count: return "COUNT(" + argument_->to_string() + ")";
        case AggregateFunc::Sum: return "SUM(" + argument_->to_string() + ")";
        case AggregateFunc::Avg: return "AVG(" + argument_->to_string() + ")";
        case AggregateFunc::Min: return "MIN(" + argument_->to_string() + ")";
        case AggregateFunc::Max: return "MAX(" + argument_->to_string() + ")";
    }
    return "";
}

bool BoundAggregateExpr::is_equal(const BoundExpr& other) const {
    if (other.type() != BoundExprType::Aggregate) return false;
    const auto& a = static_cast<const BoundAggregateExpr&>(other);
    if (func_ != a.func_) return false;
    if (argument_ == nullptr && a.argument_ == nullptr) return true;
    if (argument_ == nullptr || a.argument_ == nullptr) return false;
    return argument_->is_equal(*a.argument_);
}

} // namespace axiom
