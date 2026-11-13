#include "axiom/planner/logical_plan.h"
#include <sstream>

namespace axiom {

std::string LogicalScan::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalScan: " << table_def_.name;
    if (alias_ != table_def_.name) {
        oss << " AS " << alias_;
    }
    oss << " " << output_schema_.to_string() << "\n";
    return oss.str();
}

std::string LogicalFilter::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalFilter: " << predicate_->to_string() << "\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

std::string LogicalJoin::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalJoin (INNER): " << condition_->to_string() << "\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

std::string LogicalAggregate::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalAggregate: keys=[";
    for (size_t i = 0; i < group_by_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << group_by_[i]->to_string();
    }
    oss << "], aggs=[";
    for (size_t i = 0; i < aggregates_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << aggregates_[i]->to_string();
    }
    oss << "]\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

std::string LogicalSort::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalSort: ";
    for (size_t i = 0; i < order_by_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << order_by_[i].expr->to_string() << (order_by_[i].is_asc ? " ASC" : " DESC");
    }
    oss << "\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

std::string LogicalLimit::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalLimit: ";
    if (limit_.has_value()) oss << "limit=" << *limit_ << " ";
    if (offset_.has_value()) oss << "offset=" << *offset_;
    oss << "\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

std::string LogicalProjection::to_string(size_t indent) const {
    std::ostringstream oss;
    oss << make_indent(indent) << "LogicalProjection: [";
    for (size_t i = 0; i < select_list_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << select_list_[i].expr->to_string() << " AS " << select_list_[i].name;
    }
    oss << "]\n";
    for (const auto& child : children_) {
        oss << child->to_string(indent + 1);
    }
    return oss.str();
}

} // namespace axiom

