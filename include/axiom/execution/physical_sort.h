#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include "axiom/binder/bound_statement.h"
#include <vector>
#include <algorithm>

namespace axiom {

class PhysicalSort : public PhysicalOperator {
public:
    PhysicalSort(std::shared_ptr<PhysicalOperator> child,
                 std::vector<BoundOrderItem> order_by,
                 size_t batch_size = 1024);

    void init() override;
    std::unique_ptr<RecordBatch> next() override;
    void close() override;

    [[nodiscard]] std::string to_string(size_t indent = 0) const override;

private:
    std::vector<BoundOrderItem> order_by_;
    size_t batch_size_;

    std::vector<std::vector<Value>> sorted_rows_;
    size_t current_row_idx_{0};
    bool sort_done_{false};

    void collect_and_sort();
};

} // namespace axiom
