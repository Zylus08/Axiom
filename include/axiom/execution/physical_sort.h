#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include "axiom/binder/bound_statement.h"
#include "axiom/storage/spill_manager.h"
#include <vector>
#include <memory>
#include <queue>
#include <optional>

namespace axiom {

struct MergeCursor {
    size_t partition_idx{0};
    std::unique_ptr<RecordBatch> current_batch{nullptr};
    size_t current_row{0};

    [[nodiscard]] bool has_row() const noexcept {
        return current_batch != nullptr && current_row < current_batch->row_count();
    }
};

class PhysicalSort : public PhysicalOperator {
public:
    PhysicalSort(std::shared_ptr<PhysicalOperator> child,
                 std::vector<BoundOrderItem> order_by,
                 size_t batch_size = 1024,
                 std::optional<int64_t> limit = std::nullopt,
                 std::optional<int64_t> offset = std::nullopt);

    void init(QueryExecutionContext* ctx = nullptr) override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

    [[nodiscard]] size_t spilled_runs_count() const noexcept { return total_spilled_runs_; }
    [[nodiscard]] size_t spilled_bytes() const noexcept { return spilled_bytes_; }

private:
    std::vector<BoundOrderItem> order_by_;
    size_t batch_size_;
    std::optional<int64_t> limit_;
    std::optional<int64_t> offset_;

    // Fast column index cache if order expressions are direct column refs
    std::vector<size_t> order_col_indices_;
    bool all_col_refs_{false};

    // In-memory run buffer
    std::vector<std::vector<Value>> in_memory_rows_;
    size_t current_row_idx_{0};
    bool sort_done_{false};
    bool is_external_merge_{false};

    // External sort state
    std::vector<std::unique_ptr<SpillPartition>> spilled_runs_;
    std::vector<std::unique_ptr<MergeCursor>> active_cursors_;
    size_t spilled_bytes_{0};
    size_t total_spilled_runs_{0};

    void check_col_refs();
    void sort_in_memory_rows(std::vector<std::vector<Value>>& rows);
    void spill_current_run();
    void collect_runs();
    void init_multiway_merge();
    std::unique_ptr<RecordBatch> next_merged_batch();

    bool compare_rows(const std::vector<Value>& a, const std::vector<Value>& b) const;
    bool compare_cursors_greater(const MergeCursor* a, const MergeCursor* b) const;
};

} // namespace axiom
