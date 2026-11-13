#include "axiom/execution/physical_hash_join.h"
#include "axiom/common/query_context.h"
#include <algorithm>

namespace axiom {

static bool references_columns_in_range(const BoundExpr* expr, size_t min_col, size_t max_col) {
    if (!expr) return true;
    if (expr->type() == BoundExprType::ColumnRef) {
        const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
        return col->col_index() >= min_col && col->col_index() < max_col;
    }
    if (expr->type() == BoundExprType::Literal) {
        return true;
    }
    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        return references_columns_in_range(u->operand(), min_col, max_col);
    }
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        return references_columns_in_range(b->left(), min_col, max_col) &&
               references_columns_in_range(b->right(), min_col, max_col);
    }
    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        return references_columns_in_range(c->child(), min_col, max_col);
    }
    return false;
}

static std::unique_ptr<BoundExpr> shift_columns(const BoundExpr* expr, int offset) {
    if (!expr) return nullptr;
    if (expr->type() == BoundExprType::ColumnRef) {
        const auto* col = static_cast<const BoundColumnRefExpr*>(expr);
        size_t new_idx = static_cast<size_t>(static_cast<int>(col->col_index()) + offset);
        return std::make_unique<BoundColumnRefExpr>(col->name(), new_idx, col->return_type(), col->table_alias());
    }
    if (expr->type() == BoundExprType::Literal) {
        return expr->clone();
    }
    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        return std::make_unique<BoundUnaryExpr>(u->op(), shift_columns(u->operand(), offset), u->return_type());
    }
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        return std::make_unique<BoundBinaryExpr>(b->op(), shift_columns(b->left(), offset), shift_columns(b->right(), offset), b->return_type());
    }
    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        return std::make_unique<BoundCastExpr>(shift_columns(c->child(), offset), c->return_type());
    }
    return expr->clone();
}

PhysicalHashJoin::PhysicalHashJoin(std::shared_ptr<PhysicalOperator> left,
                                   std::shared_ptr<PhysicalOperator> right,
                                   JoinType join_type,
                                   std::unique_ptr<BoundExpr> condition,
                                   Schema schema,
                                   size_t batch_size)
    : PhysicalOperator(PhysicalOperatorType::HashJoin, std::move(schema)),
      join_type_(join_type),
      condition_(std::move(condition)),
      batch_size_(batch_size) {
    add_child(std::move(left));
    add_child(std::move(right));
    analyze_condition();
}

void PhysicalHashJoin::analyze_condition() {
    size_t left_col_count = children_[0]->output_schema().column_count();
    size_t total_col_count = output_schema_.column_count();

    if (condition_->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(condition_.get());
        if (b->op() == BinaryOp::Equal) {
            if (references_columns_in_range(b->left(), 0, left_col_count) &&
                references_columns_in_range(b->right(), left_col_count, total_col_count)) {
                is_equi_join_ = true;
                left_key_expr_ = b->left()->clone();
                right_key_expr_ = shift_columns(b->right(), -static_cast<int>(left_col_count));
                return;
            }
            if (references_columns_in_range(b->right(), 0, left_col_count) &&
                references_columns_in_range(b->left(), left_col_count, total_col_count)) {
                is_equi_join_ = true;
                left_key_expr_ = b->right()->clone();
                right_key_expr_ = shift_columns(b->left(), -static_cast<int>(left_col_count));
                return;
            }
        }
    }
    is_equi_join_ = false;
}

void PhysicalHashJoin::build_in_memory_hash_table() {
    hash_table_.clear();
    all_build_rows_.clear();

    children_[1]->init(ctx_);
    while (true) {
        auto batch = children_[1]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        size_t cols = batch->column_count();

        for (size_t r = 0; r < rows; ++r) {
            std::vector<Value> row_vals;
            row_vals.reserve(cols);
            for (size_t c = 0; c < cols; ++c) {
                row_vals.push_back(batch->get_value(c, r));
            }

            if (is_equi_join_) {
                Value key = ExpressionEvaluator::evaluate_row(right_key_expr_.get(), *batch, r);
                if (!key.is_null()) {
                    hash_table_.emplace(std::move(key), std::move(row_vals));
                }
            } else {
                all_build_rows_.push_back(std::move(row_vals));
            }
        }
    }
    children_[1]->close();
}

void PhysicalHashJoin::execute_grace_hash_partitioning() {
    build_partitions_.clear();
    probe_partitions_.clear();
    current_partition_idx_ = 0;
    spilled_bytes_ = 0;

    const auto& right_schema = children_[1]->output_schema();
    const auto& left_schema = children_[0]->output_schema();

    size_t right_cols = right_schema.column_count();
    size_t left_cols = left_schema.column_count();

    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        build_partitions_.push_back(ctx_->spill_manager()->create_partition(right_schema, "build_part"));
        probe_partitions_.push_back(ctx_->spill_manager()->create_partition(left_schema, "probe_part"));
    }

    // Partition build side (right child)
    std::vector<std::vector<std::vector<Value>>> build_cols(NUM_PARTITIONS);
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        build_cols[p].resize(right_cols);
    }
    std::vector<size_t> build_row_counts(NUM_PARTITIONS, 0);

    auto flush_build_part = [&](size_t p) {
        size_t count = build_row_counts[p];
        if (count == 0) return;
        RecordBatch batch(right_schema, std::move(build_cols[p]), count);
        build_partitions_[p]->write_batch(batch);
        build_cols[p].clear();
        build_cols[p].resize(right_cols);
        build_row_counts[p] = 0;
    };

    children_[1]->init(ctx_);
    while (true) {
        auto batch = children_[1]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        for (size_t r = 0; r < rows; ++r) {
            Value key = ExpressionEvaluator::evaluate_row(right_key_expr_.get(), *batch, r);
            if (key.is_null()) continue; // NULL cannot match in inner equijoin

            size_t p = key.hash() % NUM_PARTITIONS;
            for (size_t c = 0; c < right_cols; ++c) {
                build_cols[p][c].push_back(batch->get_value(c, r));
            }
            build_row_counts[p]++;
            if (build_row_counts[p] >= batch_size_) {
                flush_build_part(p);
            }
        }
    }
    children_[1]->close();
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        flush_build_part(p);
        spilled_bytes_ += build_partitions_[p]->byte_size();
    }

    // Partition probe side (left child)
    std::vector<std::vector<std::vector<Value>>> probe_cols(NUM_PARTITIONS);
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        probe_cols[p].resize(left_cols);
    }
    std::vector<size_t> probe_row_counts(NUM_PARTITIONS, 0);

    auto flush_probe_part = [&](size_t p) {
        size_t count = probe_row_counts[p];
        if (count == 0) return;
        RecordBatch batch(left_schema, std::move(probe_cols[p]), count);
        probe_partitions_[p]->write_batch(batch);
        probe_cols[p].clear();
        probe_cols[p].resize(left_cols);
        probe_row_counts[p] = 0;
    };

    children_[0]->init(ctx_);
    while (true) {
        auto batch = children_[0]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        for (size_t r = 0; r < rows; ++r) {
            Value key = ExpressionEvaluator::evaluate_row(left_key_expr_.get(), *batch, r);
            if (key.is_null()) continue; // NULL cannot match in inner equijoin

            size_t p = key.hash() % NUM_PARTITIONS;
            for (size_t c = 0; c < left_cols; ++c) {
                probe_cols[p][c].push_back(batch->get_value(c, r));
            }
            probe_row_counts[p]++;
            if (probe_row_counts[p] >= batch_size_) {
                flush_probe_part(p);
            }
        }
    }
    children_[0]->close();
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        flush_probe_part(p);
        spilled_bytes_ += probe_partitions_[p]->byte_size();
    }

    current_partition_idx_ = 0;
    load_next_partition();
}

bool PhysicalHashJoin::load_next_partition() {
    partition_hash_table_.clear();
    current_partition_probe_batch_.reset();
    current_partition_probe_row_ = 0;

    size_t right_cols = children_[1]->output_schema().column_count();

    while (current_partition_idx_ < NUM_PARTITIONS) {
        size_t p = current_partition_idx_++;
        auto& build_p = build_partitions_[p];
        auto& probe_p = probe_partitions_[p];

        // If either partition is empty, no matches can exist in this partition pair
        if (build_p->row_count() == 0 || probe_p->row_count() == 0) {
            build_p->remove_file();
            probe_p->remove_file();
            continue;
        }

        build_p->prepare_read();

        // Build in-memory hash table for partition p
        while (true) {
            auto batch = build_p->read_next_batch();
            if (!batch) break;

            size_t rows = batch->row_count();
            for (size_t r = 0; r < rows; ++r) {
                Value key = ExpressionEvaluator::evaluate_row(right_key_expr_.get(), *batch, r);
                if (!key.is_null()) {
                    std::vector<Value> row_vals;
                    row_vals.reserve(right_cols);
                    for (size_t c = 0; c < right_cols; ++c) {
                        row_vals.push_back(batch->get_value(c, r));
                    }
                    partition_hash_table_.emplace(std::move(key), std::move(row_vals));
                }
            }
        }

        build_p->remove_file(); // Build partition file can be removed immediately once loaded!

        probe_p->prepare_read();
        current_partition_probe_batch_ = probe_p->read_next_batch();
        current_partition_probe_row_ = 0;
        return true;
    }

    return false;
}

std::unique_ptr<RecordBatch> PhysicalHashJoin::next_spilled() {
    size_t left_col_count = children_[0]->output_schema().column_count();
    size_t right_col_count = children_[1]->output_schema().column_count();
    size_t total_col_count = output_schema_.column_count();

    std::vector<std::vector<Value>> output_columns(total_col_count);
    for (auto& col : output_columns) {
        col.reserve(batch_size_);
    }
    size_t rows_emitted = 0;

    while (rows_emitted < batch_size_) {
        // If current probe batch exhausted, read next batch from probe partition
        if (!current_partition_probe_batch_ || current_partition_probe_row_ >= current_partition_probe_batch_->row_count()) {
            if (current_partition_idx_ > 0 && current_partition_idx_ <= NUM_PARTITIONS) {
                size_t active_p = current_partition_idx_ - 1;
                current_partition_probe_batch_ = probe_partitions_[active_p]->read_next_batch();
                current_partition_probe_row_ = 0;
            }

            if (!current_partition_probe_batch_) {
                // Active partition exhausted
                if (current_partition_idx_ > 0 && current_partition_idx_ <= NUM_PARTITIONS) {
                    probe_partitions_[current_partition_idx_ - 1]->remove_file();
                }
                // Load next partition
                if (!load_next_partition()) {
                    break; // All partitions finished
                }
            }
        }

        while (current_partition_probe_batch_ && current_partition_probe_row_ < current_partition_probe_batch_->row_count() && rows_emitted < batch_size_) {
            size_t pr = current_partition_probe_row_;
            Value probe_key = ExpressionEvaluator::evaluate_row(left_key_expr_.get(), *current_partition_probe_batch_, pr);

            if (!probe_key.is_null()) {
                auto range = partition_hash_table_.equal_range(probe_key);
                for (auto it = range.first; it != range.second; ++it) {
                    const auto& build_row = it->second;

                    for (size_t c = 0; c < left_col_count; ++c) {
                        output_columns[c].push_back(current_partition_probe_batch_->get_value(c, pr));
                    }
                    for (size_t c = 0; c < right_col_count; ++c) {
                        output_columns[left_col_count + c].push_back(build_row[c]);
                    }
                    rows_emitted++;
                    if (rows_emitted >= batch_size_) {
                        break;
                    }
                }
            }
            current_partition_probe_row_++;
        }
    }

    if (rows_emitted == 0) {
        return nullptr;
    }

    return std::make_unique<RecordBatch>(output_schema_, std::move(output_columns), rows_emitted);
}

void PhysicalHashJoin::init(QueryExecutionContext* ctx) {
    ctx_ = ctx;

    // Check if Grace Hash Join should be used
    if (is_equi_join_ && ctx_ && ctx_->memory_manager() && ctx_->memory_manager()->has_limit()) {
        size_t budget = ctx_->memory_manager()->max_budget();
        // If memory is constrained (<= 64MB), use Grace Hash Join
        if (budget <= 64 * 1024 * 1024) {
            is_spilled_ = true;
            execute_grace_hash_partitioning();
            return;
        }
    }

    is_spilled_ = false;
    build_in_memory_hash_table();
    children_[0]->init(ctx_);
    current_probe_batch_.reset();
    current_probe_row_idx_ = 0;
    probe_finished_ = false;
}

std::unique_ptr<RecordBatch> PhysicalHashJoin::next_in_memory() {
    if (probe_finished_) {
        return nullptr;
    }

    size_t left_col_count = children_[0]->output_schema().column_count();
    size_t right_col_count = children_[1]->output_schema().column_count();
    size_t total_col_count = output_schema_.column_count();

    std::vector<std::vector<Value>> output_columns(total_col_count);
    for (auto& col : output_columns) {
        col.reserve(batch_size_);
    }
    size_t rows_emitted = 0;

    while (rows_emitted < batch_size_) {
        if (!current_probe_batch_ || current_probe_row_idx_ >= current_probe_batch_->row_count()) {
            current_probe_batch_ = children_[0]->next();
            current_probe_row_idx_ = 0;
            if (!current_probe_batch_) {
                probe_finished_ = true;
                break;
            }
        }

        while (current_probe_row_idx_ < current_probe_batch_->row_count() && rows_emitted < batch_size_) {
            size_t pr = current_probe_row_idx_;

            if (is_equi_join_) {
                Value probe_key = ExpressionEvaluator::evaluate_row(left_key_expr_.get(), *current_probe_batch_, pr);
                if (!probe_key.is_null()) {
                    auto range = hash_table_.equal_range(probe_key);
                    for (auto it = range.first; it != range.second; ++it) {
                        const auto& build_row = it->second;

                        for (size_t c = 0; c < left_col_count; ++c) {
                            output_columns[c].push_back(current_probe_batch_->get_value(c, pr));
                        }
                        for (size_t c = 0; c < right_col_count; ++c) {
                            output_columns[left_col_count + c].push_back(build_row[c]);
                        }
                        rows_emitted++;
                        if (rows_emitted >= batch_size_) {
                            break;
                        }
                    }
                }
            } else {
                for (const auto& build_row : all_build_rows_) {
                    std::vector<Value> combined_row;
                    combined_row.reserve(total_col_count);
                    for (size_t c = 0; c < left_col_count; ++c) {
                        combined_row.push_back(current_probe_batch_->get_value(c, pr));
                    }
                    for (size_t c = 0; c < right_col_count; ++c) {
                        combined_row.push_back(build_row[c]);
                    }

                    RecordBatch tmp_batch(output_schema_);
                    tmp_batch.append_row(combined_row);

                    Value cond_res = ExpressionEvaluator::evaluate_row(condition_.get(), tmp_batch, 0);
                    if (!cond_res.is_null() && cond_res.kind() == TypeKind::Boolean && cond_res.get_bool()) {
                        for (size_t c = 0; c < total_col_count; ++c) {
                            output_columns[c].push_back(combined_row[c]);
                        }
                        rows_emitted++;
                        if (rows_emitted >= batch_size_) {
                            break;
                        }
                    }
                }
            }

            current_probe_row_idx_++;
        }
    }

    if (rows_emitted == 0) {
        return nullptr;
    }

    return std::make_unique<RecordBatch>(output_schema_, std::move(output_columns), rows_emitted);
}

std::unique_ptr<RecordBatch> PhysicalHashJoin::next() {
    if (is_spilled_) {
        return next_spilled();
    }
    return next_in_memory();
}

void PhysicalHashJoin::close() {
    children_[0]->close();
    children_[1]->close();
    hash_table_.clear();
    all_build_rows_.clear();
    partition_hash_table_.clear();
    current_probe_batch_.reset();
    current_partition_probe_batch_.reset();

    for (auto& p : build_partitions_) {
        p->remove_file();
    }
    build_partitions_.clear();

    for (auto& p : probe_partitions_) {
        p->remove_file();
    }
    probe_partitions_.clear();
}

std::string PhysicalHashJoin::to_string(size_t indent) const {
    return make_indent(indent) + "PhysicalHashJoin: " + condition_->to_string() + "\n" +
           children_[0]->to_string(indent + 1) +
           children_[1]->to_string(indent + 1);
}

} // namespace axiom
