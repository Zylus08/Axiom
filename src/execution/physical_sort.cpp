#include "axiom/execution/physical_sort.h"
#include "axiom/common/query_context.h"
#include <algorithm>

namespace axiom {

PhysicalSort::PhysicalSort(std::shared_ptr<PhysicalOperator> child,
                           std::vector<BoundOrderItem> order_by,
                           size_t batch_size,
                           std::optional<int64_t> limit,
                           std::optional<int64_t> offset)
    : PhysicalOperator(PhysicalOperatorType::Sort, child->output_schema()),
      order_by_(std::move(order_by)),
      batch_size_(batch_size),
      limit_(limit),
      offset_(offset) {
    add_child(std::move(child));
    check_col_refs();
}

void PhysicalSort::check_col_refs() {
    all_col_refs_ = true;
    order_col_indices_.clear();
    for (const auto& item : order_by_) {
        if (item.expr->type() == BoundExprType::ColumnRef) {
            const auto* col_ref = static_cast<const BoundColumnRefExpr*>(item.expr.get());
            order_col_indices_.push_back(col_ref->col_index());
        } else {
            all_col_refs_ = false;
            order_col_indices_.clear();
            break;
        }
    }
}

bool PhysicalSort::compare_rows(const std::vector<Value>& a, const std::vector<Value>& b) const {
    size_t num_orders = order_by_.size();
    for (size_t o = 0; o < num_orders; ++o) {
        Value va;
        Value vb;
        if (all_col_refs_) {
            size_t c = order_col_indices_[o];
            va = a[c];
            vb = b[c];
        } else {
            // Need a 1-row temporary RecordBatch to evaluate arbitrary BoundExpr
            // Fast check: if not col ref, evaluate
            // But we already pre-evaluated in sort_in_memory_rows, so compare_rows is used during merge
            // For merge, rows come from batches, handled in compare_cursors_greater
        }

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
}

bool PhysicalSort::compare_cursors_greater(const MergeCursor* a, const MergeCursor* b) const {
    size_t num_orders = order_by_.size();
    for (size_t o = 0; o < num_orders; ++o) {
        Value va;
        Value vb;
        if (all_col_refs_) {
            size_t c = order_col_indices_[o];
            va = a->current_batch->get_value(c, a->current_row);
            vb = b->current_batch->get_value(c, b->current_row);
        } else {
            va = ExpressionEvaluator::evaluate_row(order_by_[o].expr.get(), *a->current_batch, a->current_row);
            vb = ExpressionEvaluator::evaluate_row(order_by_[o].expr.get(), *b->current_batch, b->current_row);
        }

        if (va.exact_equal(vb)) {
            continue;
        }

        bool is_asc = order_by_[o].is_asc;
        bool nulls_first = order_by_[o].nulls_first;

        // Note: For min-heap, we want the "greater" cursor to have lower priority
        if (is_asc) {
            return vb.sort_less_than(va, nulls_first);
        } else {
            return va.sort_less_than(vb, !nulls_first);
        }
    }
    return a->partition_idx > b->partition_idx; // tie-breaker for deterministic stability
}

void PhysicalSort::sort_in_memory_rows(std::vector<std::vector<Value>>& rows) {
    if (rows.empty() || order_by_.empty()) {
        return;
    }

    size_t num_cols = output_schema_.column_count();
    size_t num_orders = order_by_.size();
    size_t num_rows = rows.size();

    // Vectorized evaluation of order keys
    std::vector<std::vector<Value>> order_keys(num_orders);
    if (all_col_refs_) {
        for (size_t o = 0; o < num_orders; ++o) {
            size_t c = order_col_indices_[o];
            order_keys[o].reserve(num_rows);
            for (size_t r = 0; r < num_rows; ++r) {
                order_keys[o].push_back(rows[r][c]);
            }
        }
    } else {
        std::vector<std::vector<Value>> full_columns(num_cols);
        for (size_t c = 0; c < num_cols; ++c) {
            full_columns[c].reserve(num_rows);
            for (size_t r = 0; r < num_rows; ++r) {
                full_columns[c].push_back(rows[r][c]);
            }
        }
        RecordBatch full_batch(output_schema_, std::move(full_columns), num_rows);
        for (size_t o = 0; o < num_orders; ++o) {
            order_keys[o] = ExpressionEvaluator::evaluate_batch(order_by_[o].expr.get(), full_batch);
        }
    }

    std::vector<size_t> row_indices(num_rows);
    for (size_t i = 0; i < num_rows; ++i) {
        row_indices[i] = i;
    }

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

    std::vector<std::vector<Value>> sorted;
    sorted.reserve(num_rows);
    for (size_t idx : row_indices) {
        sorted.push_back(std::move(rows[idx]));
    }
    rows = std::move(sorted);
}

void PhysicalSort::spill_current_run() {
    if (in_memory_rows_.empty()) {
        return;
    }

    sort_in_memory_rows(in_memory_rows_);

    if (!ctx_ || !ctx_->spill_manager()) {
        throw AxiomException(ErrorCategory::Execution, "Cannot spill sort run: no spill manager configured");
    }

    auto part = ctx_->spill_manager()->create_partition(output_schema_, "sort_run");
    size_t num_cols = output_schema_.column_count();
    size_t total_rows = in_memory_rows_.size();

    for (size_t start = 0; start < total_rows; start += batch_size_) {
        size_t chunk_rows = std::min(batch_size_, total_rows - start);
        std::vector<std::vector<Value>> cols(num_cols);
        for (size_t c = 0; c < num_cols; ++c) {
            cols[c].reserve(chunk_rows);
            for (size_t r = 0; r < chunk_rows; ++r) {
                cols[c].push_back(std::move(in_memory_rows_[start + r][c]));
            }
        }
        RecordBatch chunk_batch(output_schema_, std::move(cols), chunk_rows);
        part->write_batch(chunk_batch);
    }

    part->finish_write();
    spilled_bytes_ += part->byte_size();
    total_spilled_runs_++;
    spilled_runs_.push_back(std::move(part));
    in_memory_rows_.clear();
    if (reservation_) {
        reservation_->release_all();
    }
}

void PhysicalSort::collect_runs() {
    in_memory_rows_.clear();
    spilled_runs_.clear();
    current_row_idx_ = 0;
    spilled_bytes_ = 0;
    is_external_merge_ = false;

    if (ctx_ && ctx_->memory_manager()) {
        reservation_ = ctx_->memory_manager()->create_reservation();
    }

    children_[0]->init(ctx_);
    size_t num_cols = output_schema_.column_count();

    // Check if Top-K heap optimization can be used
    if (limit_.has_value()) {
        int64_t lim = limit_.value();
        int64_t off = offset_.value_or(0);
        if (lim >= 0 && off >= 0) {
            size_t k = static_cast<size_t>(lim + off);
            if (k > 0 && k <= 50000) {
                if (reservation_) {
                    (void)reservation_->try_reserve(k * 128);
                }
                // Top-K Bounded Max-Heap: maintains the best K rows in memory without spilling
                auto heap_cmp = [this](const std::vector<Value>& a, const std::vector<Value>& b) {
                    return compare_rows(a, b);
                };
                std::priority_queue<std::vector<Value>, std::vector<std::vector<Value>>, decltype(heap_cmp)> max_heap(heap_cmp);

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

                        if (max_heap.size() < k) {
                            max_heap.push(std::move(row));
                        } else if (compare_rows(row, max_heap.top())) {
                            max_heap.pop();
                            max_heap.push(std::move(row));
                        }
                    }
                }
                children_[0]->close();

                in_memory_rows_.resize(max_heap.size());
                for (size_t i = max_heap.size(); i > 0; --i) {
                    in_memory_rows_[i - 1] = std::move(const_cast<std::vector<Value>&>(max_heap.top()));
                    max_heap.pop();
                }

                is_external_merge_ = false;
                return;
            }
        }
    }

    // Determine memory limit threshold for spilling
    size_t run_budget = 64 * 1024 * 1024; // Default 64MB per run if unlimited
    if (ctx_ && ctx_->memory_manager() && ctx_->memory_manager()->has_limit()) {
        size_t total_budget = ctx_->memory_manager()->max_budget();
        // Use up to 40% of budget per sort run to leave headroom for buffering and multi-way merge
        run_budget = std::max<size_t>(64, total_budget * 2 / 5);
    }

    size_t current_accumulated_bytes = 0;

    while (true) {
        auto batch = children_[0]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        size_t batch_bytes = BinarySerializer::estimate_batch_size(*batch);

        if (current_accumulated_bytes + batch_bytes > run_budget && !in_memory_rows_.empty()) {
            spill_current_run();
            current_accumulated_bytes = 0;
        }

        if (reservation_) {
            (void)reservation_->try_reserve(batch_bytes);
        }

        for (size_t r = 0; r < rows; ++r) {
            std::vector<Value> row;
            row.reserve(num_cols);
            for (size_t c = 0; c < num_cols; ++c) {
                row.push_back(batch->get_value(c, r));
            }
            in_memory_rows_.push_back(std::move(row));
        }
        current_accumulated_bytes += batch_bytes;
    }
    children_[0]->close();

    if (spilled_runs_.empty()) {
        // Entire input fits in memory
        sort_in_memory_rows(in_memory_rows_);
        is_external_merge_ = false;
    } else {
        // Spill any remaining in-memory rows as the final run
        if (!in_memory_rows_.empty()) {
            spill_current_run();
        }
        is_external_merge_ = true;
        init_multiway_merge();
    }
}

void PhysicalSort::init_multiway_merge() {
    active_cursors_.clear();
    for (size_t i = 0; i < spilled_runs_.size(); ++i) {
        spilled_runs_[i]->prepare_read();
        auto cursor = std::make_unique<MergeCursor>();
        cursor->partition_idx = i;
        cursor->current_batch = spilled_runs_[i]->read_next_batch();
        cursor->current_row = 0;
        if (cursor->has_row()) {
            active_cursors_.push_back(std::move(cursor));
        }
    }
}

std::unique_ptr<RecordBatch> PhysicalSort::next_merged_batch() {
    if (active_cursors_.empty()) {
        return nullptr;
    }

    size_t num_cols = output_schema_.column_count();
    std::vector<std::vector<Value>> output_columns(num_cols);
    for (size_t c = 0; c < num_cols; ++c) {
        output_columns[c].reserve(batch_size_);
    }

    size_t rows_collected = 0;

    while (rows_collected < batch_size_ && !active_cursors_.empty()) {
        // Find cursor with smallest head row
        size_t best_idx = 0;
        for (size_t i = 1; i < active_cursors_.size(); ++i) {
            // compare_cursors_greater(active_cursors_[best_idx].get(), active_cursors_[i].get()) returns true if best_idx > i
            if (compare_cursors_greater(active_cursors_[best_idx].get(), active_cursors_[i].get())) {
                best_idx = i;
            }
        }

        MergeCursor* best = active_cursors_[best_idx].get();
        for (size_t c = 0; c < num_cols; ++c) {
            output_columns[c].push_back(best->current_batch->get_value(c, best->current_row));
        }
        rows_collected++;

        best->current_row++;
        if (best->current_row >= best->current_batch->row_count()) {
            best->current_batch = spilled_runs_[best->partition_idx]->read_next_batch();
            best->current_row = 0;
            if (!best->has_row()) {
                active_cursors_.erase(active_cursors_.begin() + best_idx);
            }
        }
    }

    if (rows_collected == 0) {
        return nullptr;
    }

    return std::make_unique<RecordBatch>(output_schema_, std::move(output_columns), rows_collected);
}

void PhysicalSort::init(QueryExecutionContext* ctx) {
    ctx_ = ctx;
    collect_runs();
    sort_done_ = false;
}

std::unique_ptr<RecordBatch> PhysicalSort::next() {
    if (sort_done_) {
        return nullptr;
    }

    if (is_external_merge_) {
        auto batch = next_merged_batch();
        if (!batch) {
            sort_done_ = true;
            return nullptr;
        }
        return batch;
    }

    // In-memory stream path
    if (current_row_idx_ >= in_memory_rows_.size()) {
        sort_done_ = true;
        return nullptr;
    }

    size_t count = std::min(batch_size_, in_memory_rows_.size() - current_row_idx_);
    size_t num_cols = output_schema_.column_count();

    std::vector<std::vector<Value>> columns(num_cols);
    for (size_t c = 0; c < num_cols; ++c) {
        columns[c].reserve(count);
        for (size_t r = 0; r < count; ++r) {
            columns[c].push_back(in_memory_rows_[current_row_idx_ + r][c]);
        }
    }

    current_row_idx_ += count;
    return std::make_unique<RecordBatch>(output_schema_, std::move(columns), count);
}

void PhysicalSort::close() {
    in_memory_rows_.clear();
    active_cursors_.clear();
    if (reservation_) {
        reservation_->release_all();
    }
    for (auto& p : spilled_runs_) {
        p->remove_file();
    }
    spilled_runs_.clear();
    children_[0]->close();
}

std::string PhysicalSort::to_string(size_t indent) const {
    std::string res = make_indent(indent) + "PhysicalSort: [";
    for (size_t i = 0; i < order_by_.size(); ++i) {
        if (i > 0) res += ", ";
        res += order_by_[i].expr->to_string() + (order_by_[i].is_asc ? " ASC" : " DESC");
        res += (order_by_[i].nulls_first ? " NULLS FIRST" : " NULLS LAST");
    }
    res += "]\n" + children_[0]->to_string(indent + 1);
    return res;
}

} // namespace axiom
