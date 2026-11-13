#include "axiom/execution/physical_sort.h"

namespace axiom {

PhysicalSort::PhysicalSort(std::shared_ptr<PhysicalOperator> child,
                           std::vector<BoundOrderItem> order_by,
                           size_t batch_size)
    : PhysicalOperator(PhysicalOperatorType::Sort, child->output_schema()),
      order_by_(std::move(order_by)),
      batch_size_(batch_size) {
    add_child(std::move(child));
}

void PhysicalSort::collect_and_sort() {
    sorted_rows_.clear();
    current_row_idx_ = 0;

    children_[0]->init();
    size_t num_cols = output_schema_.column_count();

    // 1. Collect all rows from child
    while (true) {
        auto batch = children_[0]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        for (size_t r = 0; r < rows; ++r) {
            std::vector<Value> row;
            row.reserve(num_cols);
            for (size_t c = 0; c < num_cols; ++c) {
                row.push_back(batch->get_value(c, r));
            }
            sorted_rows_.push_back(std::move(row));
        }
    }
    children_[0]->close();

    if (sorted_rows_.empty()) {
        return;
    }

    // 2. Pre-evaluate order by keys for all rows to avoid redundant evaluation during sort!
    size_t num_orders = order_by_.size();
    std::vector<std::vector<Value>> order_keys(num_orders);

    // Create a temporary batch containing all collected rows for vectorized evaluation
    std::vector<std::vector<Value>> full_columns(num_cols);
    for (size_t c = 0; c < num_cols; ++c) {
        full_columns[c].reserve(sorted_rows_.size());
        for (size_t r = 0; r < sorted_rows_.size(); ++r) {
            full_columns[c].push_back(sorted_rows_[r][c]);
        }
    }
    RecordBatch full_batch(output_schema_, std::move(full_columns), sorted_rows_.size());

    for (size_t o = 0; o < num_orders; ++o) {
        order_keys[o] = ExpressionEvaluator::evaluate_batch(order_by_[o].expr.get(), full_batch);
    }

    // Index vector for sorting
    std::vector<size_t> row_indices(sorted_rows_.size());
    for (size_t i = 0; i < row_indices.size(); ++i) {
        row_indices[i] = i;
    }

    // Stable sort based on evaluated order keys
    std::stable_sort(row_indices.begin(), row_indices.end(), [&](size_t a, size_t b) {
        for (size_t o = 0; o < num_orders; ++o) {
            const Value& va = order_keys[o][a];
            const Value& vb = order_keys[o][b];

            if (va.exact_equal(vb)) {
                continue;
            }

            bool is_asc = order_by_[o].is_asc;
            bool nulls_first = order_by_[o].nulls_first;

            if (is_asc) {
                return va.sort_less_than(vb, nulls_first);
            } else {
                return vb.sort_less_than(va, !nulls_first);
            }
        }
        return false;
    });

    // Reorder sorted_rows_ based on row_indices
    std::vector<std::vector<Value>> new_sorted;
    new_sorted.reserve(sorted_rows_.size());
    for (size_t idx : row_indices) {
        new_sorted.push_back(std::move(sorted_rows_[idx]));
    }
    sorted_rows_ = std::move(new_sorted);
}

void PhysicalSort::init() {
    collect_and_sort();
    sort_done_ = false;
}

std::unique_ptr<RecordBatch> PhysicalSort::next() {
    if (sort_done_ || current_row_idx_ >= sorted_rows_.size()) {
        sort_done_ = true;
        return nullptr;
    }

    size_t rows_to_emit = std::min(batch_size_, sorted_rows_.size() - current_row_idx_);
    size_t num_cols = output_schema_.column_count();

    std::vector<std::vector<Value>> columns(num_cols);
    for (auto& col : columns) {
        col.reserve(rows_to_emit);
    }

    for (size_t r = 0; r < rows_to_emit; ++r) {
        const auto& row = sorted_rows_[current_row_idx_ + r];
        for (size_t c = 0; c < num_cols; ++c) {
            columns[c].push_back(row[c]);
        }
    }

    current_row_idx_ += rows_to_emit;
    return std::make_unique<RecordBatch>(output_schema_, std::move(columns), rows_to_emit);
}

void PhysicalSort::close() {
    sorted_rows_.clear();
    current_row_idx_ = 0;
    sort_done_ = true;
    children_[0]->close();
}

std::string PhysicalSort::to_string(size_t indent) const {
    return make_indent(indent) + "PhysicalSort (" + std::to_string(order_by_.size()) + " keys)\n" +
           children_[0]->to_string(indent + 1);
}

} // namespace axiom
