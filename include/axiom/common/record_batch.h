#pragma once

#include "axiom/types/schema.h"
#include <vector>
#include <memory>
#include <cassert>

namespace axiom {

class RecordBatch {
public:
    explicit RecordBatch(Schema schema)
        : schema_(std::move(schema)), num_rows_(0) {
        columns_.resize(schema_.column_count());
    }

    RecordBatch(Schema schema, std::vector<std::vector<Value>> columns, size_t num_rows)
        : schema_(std::move(schema)), columns_(std::move(columns)), num_rows_(num_rows) {
        assert(columns_.size() == schema_.column_count());
    }

    [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] size_t row_count() const noexcept { return num_rows_; }
    [[nodiscard]] size_t column_count() const noexcept { return columns_.size(); }
    [[nodiscard]] bool empty() const noexcept { return num_rows_ == 0; }

    [[nodiscard]] const Value& get_value(size_t col_idx, size_t row_idx) const {
        if (col_idx >= columns_.size() || row_idx >= num_rows_) {
            throw AxiomException(ErrorCategory::Internal, "RecordBatch access out of bounds");
        }
        return columns_[col_idx][row_idx];
    }

    [[nodiscard]] const std::vector<Value>& get_column(size_t col_idx) const {
        if (col_idx >= columns_.size()) {
            throw AxiomException(ErrorCategory::Internal, "RecordBatch column out of bounds");
        }
        return columns_[col_idx];
    }

    std::vector<Value>& get_column_mut(size_t col_idx) {
        if (col_idx >= columns_.size()) {
            throw AxiomException(ErrorCategory::Internal, "RecordBatch column out of bounds");
        }
        return columns_[col_idx];
    }

    void append_row(const std::vector<Value>& row) {
        if (row.size() != schema_.column_count()) {
            throw AxiomException(ErrorCategory::Internal, "Row column count mismatch with schema");
        }
        for (size_t i = 0; i < row.size(); ++i) {
            columns_[i].push_back(row[i]);
        }
        num_rows_++;
    }

    void reserve(size_t capacity) {
        for (auto& col : columns_) {
            col.reserve(capacity);
        }
    }

    [[nodiscard]] std::unique_ptr<RecordBatch> filter(const std::vector<bool>& selection) const;
    [[nodiscard]] std::unique_ptr<RecordBatch> project(const std::vector<size_t>& col_indices, Schema new_schema) const;
    [[nodiscard]] std::unique_ptr<RecordBatch> slice(size_t offset, size_t length) const;

private:
    Schema schema_;
    std::vector<std::vector<Value>> columns_;
    size_t num_rows_{0};
};

} // namespace axiom
