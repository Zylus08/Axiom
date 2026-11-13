#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include <unordered_map>
#include <vector>

namespace axiom {

struct GroupKeyHasher {
    size_t operator()(const std::vector<Value>& keys) const {
        size_t h = 0;
        for (const auto& k : keys) {
            h ^= k.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

struct GroupKeyEqual {
    bool operator()(const std::vector<Value>& a, const std::vector<Value>& b) const {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!a[i].exact_equal(b[i])) return false;
        }
        return true;
    }
};

struct AggregateAccumulator {
    AggregateFunc func;
    TypeKind ret_type;
    int64_t count{0};
    int64_t sum_int{0};
    double sum_double{0.0};
    Value min_val{Value::make_null()};
    Value max_val{Value::make_null()};
    bool has_any_value{false};

    void update(const Value& val);
    [[nodiscard]] Value finalize() const;
};

class PhysicalHashAggregate : public PhysicalOperator {
public:
    PhysicalHashAggregate(std::shared_ptr<PhysicalOperator> child,
                          std::vector<std::unique_ptr<BoundExpr>> group_by,
                          std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates,
                          Schema schema,
                          size_t batch_size = 1024);

    void init() override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::vector<std::unique_ptr<BoundExpr>> group_by_;
    std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates_;
    size_t batch_size_;

    // Materialized results
    std::vector<std::vector<Value>> result_rows_;
    size_t current_result_row_{0};
    bool aggregation_done_{false};

    void execute_aggregation();
};

} // namespace axiom

