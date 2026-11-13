#pragma once

#include "axiom/binder/bound_statement.h"
#include <vector>
#include <memory>
#include <string>

namespace axiom {

enum class LogicalOperatorType {
    Scan,
    Filter,
    Join,
    Aggregate,
    Sort,
    Limit,
    Projection
};

class LogicalOperator {
public:
    virtual ~LogicalOperator() = default;
    [[nodiscard]] LogicalOperatorType type() const noexcept { return type_; }
    [[nodiscard]] const Schema& output_schema() const noexcept { return output_schema_; }
    [[nodiscard]] virtual std::string to_string(size_t indent = 0) const = 0;

    [[nodiscard]] const std::vector<std::shared_ptr<LogicalOperator>>& children() const noexcept { return children_; }
    void add_child(std::shared_ptr<LogicalOperator> child) { children_.push_back(std::move(child)); }

protected:
    LogicalOperator(LogicalOperatorType type, Schema schema)
        : type_(type), output_schema_(std::move(schema)) {}

    LogicalOperatorType type_;
    Schema output_schema_;
    std::vector<std::shared_ptr<LogicalOperator>> children_;

    static std::string make_indent(size_t indent) {
        return std::string(indent * 2, ' ');
    }
};

class LogicalScan : public LogicalOperator {
public:
    LogicalScan(TableDef table_def, std::string alias, Schema schema)
        : LogicalOperator(LogicalOperatorType::Scan, std::move(schema)),
          table_def_(std::move(table_def)),
          alias_(std::move(alias)) {}

    [[nodiscard]] const TableDef& table_def() const noexcept { return table_def_; }
    [[nodiscard]] const std::string& alias() const noexcept { return alias_; }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    TableDef table_def_;
    std::string alias_;
};

class LogicalFilter : public LogicalOperator {
public:
    LogicalFilter(std::shared_ptr<LogicalOperator> child, std::unique_ptr<BoundExpr> predicate)
        : LogicalOperator(LogicalOperatorType::Filter, child->output_schema()),
          predicate_(std::move(predicate)) {
        add_child(std::move(child));
    }

    [[nodiscard]] const BoundExpr* predicate() const noexcept { return predicate_.get(); }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::unique_ptr<BoundExpr> predicate_;
};

class LogicalJoin : public LogicalOperator {
public:
    LogicalJoin(std::shared_ptr<LogicalOperator> left, std::shared_ptr<LogicalOperator> right,
                JoinType join_type, std::unique_ptr<BoundExpr> condition, Schema schema)
        : LogicalOperator(LogicalOperatorType::Join, std::move(schema)),
          join_type_(join_type),
          condition_(std::move(condition)) {
        add_child(std::move(left));
        add_child(std::move(right));
    }

    [[nodiscard]] JoinType join_type() const noexcept { return join_type_; }
    [[nodiscard]] const BoundExpr* condition() const noexcept { return condition_.get(); }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    JoinType join_type_;
    std::unique_ptr<BoundExpr> condition_;
};

class LogicalAggregate : public LogicalOperator {
public:
    LogicalAggregate(std::shared_ptr<LogicalOperator> child,
                     std::vector<std::unique_ptr<BoundExpr>> group_by,
                     std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates,
                     Schema schema)
        : LogicalOperator(LogicalOperatorType::Aggregate, std::move(schema)),
          group_by_(std::move(group_by)),
          aggregates_(std::move(aggregates)) {
        if (child) add_child(std::move(child));
    }

    [[nodiscard]] const std::vector<std::unique_ptr<BoundExpr>>& group_by() const noexcept { return group_by_; }
    [[nodiscard]] const std::vector<std::unique_ptr<BoundAggregateExpr>>& aggregates() const noexcept { return aggregates_; }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::vector<std::unique_ptr<BoundExpr>> group_by_;
    std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates_;
};

class LogicalSort : public LogicalOperator {
public:
    LogicalSort(std::shared_ptr<LogicalOperator> child, std::vector<BoundOrderItem> order_by)
        : LogicalOperator(LogicalOperatorType::Sort, child->output_schema()),
          order_by_(std::move(order_by)) {
        add_child(std::move(child));
    }

    [[nodiscard]] const std::vector<BoundOrderItem>& order_by() const noexcept { return order_by_; }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::vector<BoundOrderItem> order_by_;
};

class LogicalLimit : public LogicalOperator {
public:
    LogicalLimit(std::shared_ptr<LogicalOperator> child, std::optional<int64_t> limit, std::optional<int64_t> offset)
        : LogicalOperator(LogicalOperatorType::Limit, child->output_schema()),
          limit_(limit), offset_(offset) {
        add_child(std::move(child));
    }

    [[nodiscard]] std::optional<int64_t> limit() const noexcept { return limit_; }
    [[nodiscard]] std::optional<int64_t> offset() const noexcept { return offset_; }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::optional<int64_t> limit_;
    std::optional<int64_t> offset_;
};

class LogicalProjection : public LogicalOperator {
public:
    LogicalProjection(std::shared_ptr<LogicalOperator> child, std::vector<BoundSelectItem> select_list, Schema schema)
        : LogicalOperator(LogicalOperatorType::Projection, std::move(schema)),
          select_list_(std::move(select_list)) {
        add_child(std::move(child));
    }

    [[nodiscard]] const std::vector<BoundSelectItem>& select_list() const noexcept { return select_list_; }
    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::vector<BoundSelectItem> select_list_;
};

} // namespace axiom
