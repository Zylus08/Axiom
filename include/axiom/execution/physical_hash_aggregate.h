#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include "axiom/storage/spill_manager.h"
#include "axiom/common/memory_manager.h"
#include <unordered_map>
#include <vector>
#include <memory>

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

    void init(QueryExecutionContext* ctx = nullptr) override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

    [[nodiscard]] size_t spilled_partitions_count() const noexcept { return spilled_partitions_.size(); }
    [[nodiscard]] size_t spilled_bytes() const noexcept { return spilled_bytes_; }

private:
    std::vector<std::unique_ptr<BoundExpr>> group_by_;
    std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates_;
    size_t batch_size_;

    // Fast column index cache if group keys and agg arguments are direct column refs
    std::vector<size_t> group_col_indices_;
    bool all_group_col_refs_{false};

    // Materialized results for current output stream
    std::vector<std::vector<Value>> result_rows_;
    size_t current_result_row_{0};
    bool aggregation_done_{false};

    // External aggregate state
    bool is_spilled_{false};
    Schema intermediate_schema_;
    std::vector<std::unique_ptr<SpillPartition>> spilled_partitions_;
    size_t current_partition_idx_{0};
    size_t spilled_bytes_{0};
    std::unique_ptr<MemoryReservation> reservation_;

    void check_fast_col_refs();
    Schema build_intermediate_schema() const;
    void execute_in_memory_aggregation();
    void execute_external_aggregation();
    bool process_next_spilled_partition();
};

} // namespace axiom
