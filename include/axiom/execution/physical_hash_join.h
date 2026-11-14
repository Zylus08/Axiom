#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include "axiom/storage/spill_manager.h"
#include "axiom/common/memory_manager.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace axiom {

struct ValueVectorHasher {
    size_t operator()(const Value& v) const {
        return v.hash();
    }
};

struct ValueVectorEqual {
    bool operator()(const Value& a, const Value& b) const {
        return a.exact_equal(b);
    }
};

class PhysicalHashJoin : public PhysicalOperator {
public:
    PhysicalHashJoin(std::shared_ptr<PhysicalOperator> left,
                     std::shared_ptr<PhysicalOperator> right,
                     JoinType join_type,
                     std::unique_ptr<BoundExpr> condition,
                     Schema schema,
                     size_t batch_size = 1024);

    void init(QueryExecutionContext* ctx = nullptr) override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

    [[nodiscard]] size_t spilled_partitions_count() const noexcept { return total_spilled_partitions_; }
    [[nodiscard]] size_t spilled_bytes() const noexcept { return spilled_bytes_; }

private:
    using HashTableType = std::unordered_multimap<Value, std::vector<Value>, ValueVectorHasher, ValueVectorEqual>;

    JoinType join_type_;
    std::unique_ptr<BoundExpr> condition_;
    size_t batch_size_;

    // Equi-join key info if condition contains equality
    bool is_equi_join_{false};
    std::unique_ptr<BoundExpr> left_key_expr_;
    std::unique_ptr<BoundExpr> right_key_expr_;

    // In-memory hash table for build side (right child)
    HashTableType hash_table_;
    std::vector<std::vector<Value>> all_build_rows_; // For non-equijoin fallback

    // In-memory probe state
    std::unique_ptr<RecordBatch> current_probe_batch_;
    size_t current_probe_row_idx_{0};
    bool probe_finished_{false};
    HashTableType::const_iterator in_mem_match_it_;
    HashTableType::const_iterator in_mem_match_end_;
    bool in_mem_matching_in_progress_{false};

    // External Grace Hash Join state
    bool is_spilled_{false};
    static constexpr size_t NUM_PARTITIONS = 16;
    std::vector<std::unique_ptr<SpillPartition>> build_partitions_;
    std::vector<std::unique_ptr<SpillPartition>> probe_partitions_;
    size_t current_partition_idx_{0};
    size_t active_partition_idx_{0};
    bool partition_in_progress_{false};
    bool current_partition_build_exhausted_{false};
    HashTableType partition_hash_table_;
    std::unique_ptr<RecordBatch> current_partition_probe_batch_;
    size_t current_partition_probe_row_{0};
    HashTableType::const_iterator spilled_match_it_;
    HashTableType::const_iterator spilled_match_end_;
    bool spilled_matching_in_progress_{false};
    size_t spilled_bytes_{0};
    size_t total_spilled_partitions_{0};
    std::unique_ptr<MemoryReservation> reservation_;

    void analyze_condition();
    void build_in_memory_hash_table();
    void execute_grace_hash_partitioning();
    void execute_grace_hash_partitioning_with_existing(std::shared_ptr<PhysicalOperator> right_child);
    bool advance_to_next_valid_partition();
    bool load_next_build_block(size_t p);
    std::unique_ptr<RecordBatch> next_in_memory();
    std::unique_ptr<RecordBatch> next_spilled();
};

} // namespace axiom
