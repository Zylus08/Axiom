#include "axiom/common/record_batch.h"

namespace axiom {

std::unique_ptr<RecordBatch> RecordBatch::filter(const std::vector<bool>& selection) const {
    if (selection.size() != num_rows_) {
        throw AxiomException(ErrorCategory::Internal, "Selection vector size does not match batch row count");
    }

    size_t selected_count = 0;
    for (bool b : selection) {
        if (b) selected_count++;
    }

    std::vector<std::vector<Value>> new_columns(schema_.column_count());
    for (auto& col : new_columns) {
        col.reserve(selected_count);
    }

    for (size_t r = 0; r < num_rows_; ++r) {
        if (selection[r]) {
            for (size_t c = 0; c < schema_.column_count(); ++c) {
                new_columns[c].push_back(columns_[c][r]);
            }
        }
    }

    return std::make_unique<RecordBatch>(schema_, std::move(new_columns), selected_count);
}

std::unique_ptr<RecordBatch> RecordBatch::project(const std::vector<size_t>& col_indices, Schema new_schema) const {
    std::vector<std::vector<Value>> new_columns;
    new_columns.reserve(col_indices.size());

    for (size_t idx : col_indices) {
        if (idx >= columns_.size()) {
            throw AxiomException(ErrorCategory::Internal, "Projection column index out of bounds");
        }
        new_columns.push_back(columns_[idx]);
    }

    return std::make_unique<RecordBatch>(std::move(new_schema), std::move(new_columns), num_rows_);
}

std::unique_ptr<RecordBatch> RecordBatch::slice(size_t offset, size_t length) const {
    if (offset >= num_rows_) {
        return std::make_unique<RecordBatch>(schema_);
    }

    size_t actual_len = std::min(length, num_rows_ - offset);
    std::vector<std::vector<Value>> new_columns(schema_.column_count());

    for (size_t c = 0; c < schema_.column_count(); ++c) {
        new_columns[c].reserve(actual_len);
        new_columns[c].insert(
            new_columns[c].end(),
            columns_[c].begin() + offset,
            columns_[c].begin() + offset + actual_len
        );
    }

    return std::make_unique<RecordBatch>(schema_, std::move(new_columns), actual_len);
}

} // namespace axiom
