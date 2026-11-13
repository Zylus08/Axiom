#pragma once

#include "axiom/common/record_batch.h"
#include <memory>
#include <string>
#include <vector>

namespace axiom {

class QueryExecutionContext;

enum class PhysicalOperatorType {
    Scan,
    Filter,
    Projection,
    HashJoin,
    HashAggregate,
    Sort,
    Limit
};

class PhysicalOperator {
public:
    virtual ~PhysicalOperator() = default;

    [[nodiscard]] PhysicalOperatorType type() const noexcept { return type_; }
    [[nodiscard]] const Schema& output_schema() const noexcept { return output_schema_; }

    virtual void init() { init(nullptr); }
    virtual void init(QueryExecutionContext* ctx) { ctx_ = ctx; }
    [[nodiscard]] virtual std::unique_ptr<RecordBatch> next() = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual std::string to_string(size_t indent = 0) const = 0;

    [[nodiscard]] const std::vector<std::shared_ptr<PhysicalOperator>>& children() const noexcept { return children_; }
    void add_child(std::shared_ptr<PhysicalOperator> child) { children_.push_back(std::move(child)); }

protected:
    PhysicalOperator(PhysicalOperatorType type, Schema schema)
        : type_(type), output_schema_(std::move(schema)) {}

    PhysicalOperatorType type_;
    Schema output_schema_;
    std::vector<std::shared_ptr<PhysicalOperator>> children_;
    QueryExecutionContext* ctx_{nullptr};

    static std::string make_indent(size_t indent) {
        return std::string(indent * 2, ' ');
    }
};

} // namespace axiom

