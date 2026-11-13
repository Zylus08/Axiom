#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include <unordered_map>
#include <vector>

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

    void init() override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    JoinType join_type_;
    std::unique_ptr<BoundExpr> condition_;
    size_t batch_size_;

    // Equi-join key info if condition contains equality
    bool is_equi_join_{false};
    std::unique_ptr<BoundExpr> left_key_expr_;
    std::unique_ptr<BoundExpr> right_key_expr_;

    // In-memory hash table for build side (right child)
    // Maps join key to list of right-side row vectors
    std::unordered_multimap<Value, std::vector<Value>, ValueVectorHasher, ValueVectorEqual> hash_table_;
    std::vector<std::vector<Value>> all_build_rows_; // For non-equijoin fallback

    // Probe state
    std::unique_ptr<RecordBatch> current_probe_batch_;
    size_t current_probe_row_idx_{0};
    bool probe_finished_{false};

    void analyze_condition();
    void build_hash_table();
};

} // namespace axiom

