#include "axiom/parser/ast.h"
#include <sstream>

namespace axiom {

std::string UnaryExpr::to_string() const {
    switch (op_) {
        case UnaryOp::Plus: return "+" + operand_->to_string();
        case UnaryOp::Minus: return "-" + operand_->to_string();
        case UnaryOp::Not: return "NOT (" + operand_->to_string() + ")";
        case UnaryOp::IsNull: return "(" + operand_->to_string() + " IS NULL)";
        case UnaryOp::IsNotNull: return "(" + operand_->to_string() + " IS NOT NULL)";
    }
    return "";
}

std::string BinaryExpr::to_string() const {
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

std::string AggregateExpr::to_string() const {
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

std::string JoinTableRef::to_string() const {
    std::string res = left_->to_string() + " INNER JOIN " + right_->to_string();
    if (on_condition_) {
        res += " ON " + on_condition_->to_string();
    }
    return res;
}

std::string SelectStatement::to_string() const {
    std::ostringstream oss;
    if (is_explain) oss << "EXPLAIN ";
    oss << "SELECT ";
    for (size_t i = 0; i < select_list.size(); ++i) {
        if (i > 0) oss << ", ";
        if (select_list[i].is_star) {
            if (select_list[i].star_table.has_value()) {
                oss << *select_list[i].star_table << ".*";
            } else {
                oss << "*";
            }
        } else {
            oss << select_list[i].expr->to_string();
            if (select_list[i].alias.has_value()) {
                oss << " AS " << *select_list[i].alias;
            }
        }
    }

    if (from_table) {
        oss << " FROM " << from_table->to_string();
    }
    if (where_clause) {
        oss << " WHERE " << where_clause->to_string();
    }
    if (!group_by.empty()) {
        oss << " GROUP BY ";
        for (size_t i = 0; i < group_by.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << group_by[i]->to_string();
        }
    }
    if (having_clause) {
        oss << " HAVING " << having_clause->to_string();
    }
    if (!order_by.empty()) {
        oss << " ORDER BY ";
        for (size_t i = 0; i < order_by.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << order_by[i].expr->to_string() << (order_by[i].is_asc ? " ASC" : " DESC");
        }
    }
    if (limit.has_value()) {
        oss << " LIMIT " << *limit;
    }
    if (offset.has_value()) {
        oss << " OFFSET " << *offset;
    }
    return oss.str();
}

} // namespace axiom
