#include "axiom/execution/physical_hash_aggregate.h"
#include "axiom/common/query_context.h"
#include <algorithm>

namespace axiom {

void AggregateAccumulator::update(const Value& val) {
    if (func == AggregateFunc::CountStar) {
        count++;
        has_any_value = true;
        return;
    }

    if (val.is_null()) {
        return;
    }

    has_any_value = true;
    count++;

    if (func == AggregateFunc::Sum || func == AggregateFunc::Avg) {
        if (val.kind() == TypeKind::BigInt) {
            sum_int += val.get_bigint();
            sum_double += static_cast<double>(val.get_bigint());
        } else if (val.kind() == TypeKind::Double) {
            sum_double += val.get_double();
        }
    } else if (func == AggregateFunc::Min) {
        if (min_val.is_null() || val.compare(min_val).value_or(1) < 0) {
            min_val = val;
        }
    } else if (func == AggregateFunc::Max) {
        if (max_val.is_null() || val.compare(max_val).value_or(-1) > 0) {
            max_val = val;
        }
    }
}

Value AggregateAccumulator::finalize() const {
    switch (func) {
        case AggregateFunc::CountStar:
        case AggregateFunc::Count:
            return Value::make_bigint(count);
        case AggregateFunc::Sum:
            if (!has_any_value || count == 0) return Value::make_null(ret_type);
            if (ret_type == TypeKind::Double) return Value::make_double(sum_double);
            return Value::make_bigint(sum_int);
        case AggregateFunc::Avg:
            if (!has_any_value || count == 0) return Value::make_null(TypeKind::Double);
            return Value::make_double(sum_double / static_cast<double>(count));
        case AggregateFunc::Min:
            if (!has_any_value || count == 0) return Value::make_null(ret_type);
            return min_val;
        case AggregateFunc::Max:
            if (!has_any_value || count == 0) return Value::make_null(ret_type);
            return max_val;
    }
    return Value::make_null();
}

PhysicalHashAggregate::PhysicalHashAggregate(std::shared_ptr<PhysicalOperator> child,
                                             std::vector<std::unique_ptr<BoundExpr>> group_by,
                                             std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates,
                                             Schema schema,
                                             size_t batch_size)
    : PhysicalOperator(PhysicalOperatorType::HashAggregate, std::move(schema)),
      group_by_(std::move(group_by)),
      aggregates_(std::move(aggregates)),
      batch_size_(batch_size) {
    if (child) {
        add_child(std::move(child));
    }
    check_fast_col_refs();
    intermediate_schema_ = build_intermediate_schema();
}

void PhysicalHashAggregate::check_fast_col_refs() {
    all_group_col_refs_ = true;
    group_col_indices_.clear();
    for (const auto& expr : group_by_) {
        if (expr->type() == BoundExprType::ColumnRef) {
            const auto* col_ref = static_cast<const BoundColumnRefExpr*>(expr.get());
            group_col_indices_.push_back(col_ref->col_index());
        } else {
            all_group_col_refs_ = false;
            group_col_indices_.clear();
            break;
        }
    }
}

Schema PhysicalHashAggregate::build_intermediate_schema() const {
    std::vector<ColumnDesc> cols;
    cols.reserve(group_by_.size() + aggregates_.size());

    for (size_t k = 0; k < group_by_.size(); ++k) {
        cols.emplace_back("key_" + std::to_string(k), group_by_[k]->return_type(), true);
    }
    for (size_t a = 0; a < aggregates_.size(); ++a) {
        TypeKind arg_type = TypeKind::BigInt;
        if (aggregates_[a]->func() != AggregateFunc::CountStar && aggregates_[a]->argument()) {
            arg_type = aggregates_[a]->argument()->return_type();
        }
        cols.emplace_back("agg_" + std::to_string(a), arg_type, true);
    }
    return Schema(std::move(cols));
}

void PhysicalHashAggregate::execute_in_memory_aggregation() {
    result_rows_.clear();
    current_result_row_ = 0;

    size_t num_keys = group_by_.size();
    size_t num_aggs = aggregates_.size();

    // Map: Group Key Vector -> Vector of Accumulators
    std::unordered_map<std::vector<Value>, std::vector<AggregateAccumulator>, GroupKeyHasher, GroupKeyEqual> groups;
    bool seen_any_rows = false;

    if (!children_.empty()) {
        children_[0]->init(ctx_);
        while (true) {
            auto batch = children_[0]->next();
            if (!batch) break;

            size_t rows = batch->row_count();
            if (rows > 0) {
                seen_any_rows = true;
            }

            // Pre-evaluate aggregate argument columns
            std::vector<std::vector<Value>> agg_args(num_aggs);
            for (size_t a = 0; a < num_aggs; ++a) {
                if (aggregates_[a]->func() != AggregateFunc::CountStar) {
                    agg_args[a] = ExpressionEvaluator::evaluate_batch(aggregates_[a]->argument(), *batch);
                }
            }

            // Pre-evaluate group keys
            std::vector<std::vector<Value>> key_cols(num_keys);
            if (all_group_col_refs_) {
                for (size_t k = 0; k < num_keys; ++k) {
                    size_t c = group_col_indices_[k];
                    key_cols[k].reserve(rows);
                    for (size_t r = 0; r < rows; ++r) {
                        key_cols[k].push_back(batch->get_value(c, r));
                    }
                }
            } else {
                for (size_t k = 0; k < num_keys; ++k) {
                    key_cols[k] = ExpressionEvaluator::evaluate_batch(group_by_[k].get(), *batch);
                }
            }

            for (size_t r = 0; r < rows; ++r) {
                std::vector<Value> row_key;
                row_key.reserve(num_keys);
                for (size_t k = 0; k < num_keys; ++k) {
                    row_key.push_back(key_cols[k][r]);
                }

                auto it = groups.find(row_key);
                if (it == groups.end()) {
                    std::vector<AggregateAccumulator> accumulators(num_aggs);
                    for (size_t a = 0; a < num_aggs; ++a) {
                        accumulators[a].func = aggregates_[a]->func();
                        accumulators[a].ret_type = aggregates_[a]->return_type();
                    }
                    it = groups.emplace(std::move(row_key), std::move(accumulators)).first;
                }

                for (size_t a = 0; a < num_aggs; ++a) {
                    if (aggregates_[a]->func() == AggregateFunc::CountStar) {
                        it->second[a].update(Value::make_bigint(1));
                    } else {
                        it->second[a].update(agg_args[a][r]);
                    }
                }
            }
        }
        children_[0]->close();
    }

    if (!seen_any_rows) {
        if (num_keys == 0) {
            // Scalar aggregate on empty input produces 1 row
            std::vector<Value> row;
            row.reserve(num_aggs);
            for (size_t a = 0; a < num_aggs; ++a) {
                AggregateAccumulator acc;
                acc.func = aggregates_[a]->func();
                acc.ret_type = aggregates_[a]->return_type();
                row.push_back(acc.finalize());
            }
            result_rows_.push_back(std::move(row));
        }
        return;
    }

    // Materialize all groups
    for (auto& [key_vec, accumulators] : groups) {
        std::vector<Value> row;
        row.reserve(num_keys + num_aggs);
        for (auto& k : key_vec) {
            row.push_back(std::move(k));
        }
        for (const auto& acc : accumulators) {
            row.push_back(acc.finalize());
        }
        result_rows_.push_back(std::move(row));
    }
}

void PhysicalHashAggregate::execute_external_aggregation() {
    result_rows_.clear();
    current_result_row_ = 0;
    spilled_partitions_.clear();
    current_partition_idx_ = 0;
    spilled_bytes_ = 0;

    size_t num_keys = group_by_.size();
    size_t num_aggs = aggregates_.size();
    size_t num_interm_cols = num_keys + num_aggs;

    // Number of hash partitions
    constexpr size_t NUM_PARTITIONS = 16;
    spilled_partitions_.reserve(NUM_PARTITIONS);
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        spilled_partitions_.push_back(ctx_->spill_manager()->create_partition(intermediate_schema_, "agg_part"));
    }

    // Partition row buffers before writing batches
    std::vector<std::vector<std::vector<Value>>> partition_cols(NUM_PARTITIONS);
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        partition_cols[p].resize(num_interm_cols);
    }
    std::vector<size_t> partition_row_counts(NUM_PARTITIONS, 0);

    auto flush_partition = [&](size_t p) {
        size_t count = partition_row_counts[p];
        if (count == 0) return;
        RecordBatch batch(intermediate_schema_, std::move(partition_cols[p]), count);
        spilled_partitions_[p]->write_batch(batch);
        partition_cols[p].clear();
        partition_cols[p].resize(num_interm_cols);
        partition_row_counts[p] = 0;
    };

    children_[0]->init(ctx_);
    bool seen_any_rows = false;

    while (true) {
        auto batch = children_[0]->next();
        if (!batch) break;

        size_t rows = batch->row_count();
        if (rows > 0) seen_any_rows = true;

        // Pre-evaluate aggregate arguments
        std::vector<std::vector<Value>> agg_args(num_aggs);
        for (size_t a = 0; a < num_aggs; ++a) {
            if (aggregates_[a]->func() != AggregateFunc::CountStar) {
                agg_args[a] = ExpressionEvaluator::evaluate_batch(aggregates_[a]->argument(), *batch);
            }
        }

        // Pre-evaluate group keys
        std::vector<std::vector<Value>> key_cols(num_keys);
        if (all_group_col_refs_) {
            for (size_t k = 0; k < num_keys; ++k) {
                size_t c = group_col_indices_[k];
                key_cols[k].reserve(rows);
                for (size_t r = 0; r < rows; ++r) {
                    key_cols[k].push_back(batch->get_value(c, r));
                }
            }
        } else {
            for (size_t k = 0; k < num_keys; ++k) {
                key_cols[k] = ExpressionEvaluator::evaluate_batch(group_by_[k].get(), *batch);
            }
        }

        for (size_t r = 0; r < rows; ++r) {
            std::vector<Value> row_key;
            row_key.reserve(num_keys);
            for (size_t k = 0; k < num_keys; ++k) {
                row_key.push_back(key_cols[k][r]);
            }

            size_t hash = GroupKeyHasher{}(row_key);
            size_t p = hash % NUM_PARTITIONS;

            for (size_t k = 0; k < num_keys; ++k) {
                partition_cols[p][k].push_back(std::move(row_key[k]));
            }
            for (size_t a = 0; a < num_aggs; ++a) {
                if (aggregates_[a]->func() == AggregateFunc::CountStar) {
                    partition_cols[p][num_keys + a].push_back(Value::make_bigint(1));
                } else {
                    partition_cols[p][num_keys + a].push_back(agg_args[a][r]);
                }
            }

            partition_row_counts[p]++;
            if (partition_row_counts[p] >= batch_size_) {
                flush_partition(p);
            }
        }
    }
    children_[0]->close();

    // Flush remaining rows in partition buffers
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        flush_partition(p);
        spilled_bytes_ += spilled_partitions_[p]->byte_size();
    }

    if (!seen_any_rows) {
        // Handled: for group_by > 0, produces 0 rows
        for (auto& part : spilled_partitions_) part->remove_file();
        spilled_partitions_.clear();
        return;
    }

    // Prepare partitions for sequential reading
    for (size_t p = 0; p < NUM_PARTITIONS; ++p) {
        spilled_partitions_[p]->prepare_read();
    }

    // Load first non-empty partition
    current_partition_idx_ = 0;
    process_next_spilled_partition();
}

bool PhysicalHashAggregate::process_next_spilled_partition() {
    result_rows_.clear();
    current_result_row_ = 0;

    size_t num_keys = group_by_.size();
    size_t num_aggs = aggregates_.size();

    while (current_partition_idx_ < spilled_partitions_.size()) {
        auto& part = spilled_partitions_[current_partition_idx_++];
        if (part->row_count() == 0) {
            part->remove_file();
            continue;
        }

        std::unordered_map<std::vector<Value>, std::vector<AggregateAccumulator>, GroupKeyHasher, GroupKeyEqual> groups;

        while (true) {
            auto batch = part->read_next_batch();
            if (!batch) break;

            size_t rows = batch->row_count();
            for (size_t r = 0; r < rows; ++r) {
                std::vector<Value> row_key;
                row_key.reserve(num_keys);
                for (size_t k = 0; k < num_keys; ++k) {
                    row_key.push_back(batch->get_value(k, r));
                }

                auto it = groups.find(row_key);
                if (it == groups.end()) {
                    std::vector<AggregateAccumulator> accumulators(num_aggs);
                    for (size_t a = 0; a < num_aggs; ++a) {
                        accumulators[a].func = aggregates_[a]->func();
                        accumulators[a].ret_type = aggregates_[a]->return_type();
                    }
                    it = groups.emplace(std::move(row_key), std::move(accumulators)).first;
                }

                for (size_t a = 0; a < num_aggs; ++a) {
                    it->second[a].update(batch->get_value(num_keys + a, r));
                }
            }
        }

        part->remove_file();

        for (auto& [key_vec, accumulators] : groups) {
            std::vector<Value> row;
            row.reserve(num_keys + num_aggs);
            for (auto& k : key_vec) {
                row.push_back(std::move(k));
            }
            for (const auto& acc : accumulators) {
                row.push_back(acc.finalize());
            }
            result_rows_.push_back(std::move(row));
        }

        if (!result_rows_.empty()) {
            return true;
        }
    }

    return false;
}

void PhysicalHashAggregate::init(QueryExecutionContext* ctx) {
    ctx_ = ctx;
    size_t num_keys = group_by_.size();

    // Check if external partitioning is required
    // Scalar aggregates (num_keys == 0) NEVER need spilling as memory is O(1)
    if (num_keys > 0 && ctx_ && ctx_->memory_manager() && ctx_->memory_manager()->has_limit()) {
        size_t budget = ctx_->memory_manager()->max_budget();
        // If memory limit is constrained (e.g. <= 64MB), use external partitioned aggregation
        if (budget <= 64 * 1024 * 1024) {
            is_spilled_ = true;
            execute_external_aggregation();
            aggregation_done_ = false;
            return;
        }
    }

    is_spilled_ = false;
    execute_in_memory_aggregation();
    aggregation_done_ = false;
}

std::unique_ptr<RecordBatch> PhysicalHashAggregate::next() {
    if (aggregation_done_) {
        return nullptr;
    }

    while (current_result_row_ >= result_rows_.size()) {
        if (is_spilled_) {
            if (!process_next_spilled_partition()) {
                aggregation_done_ = true;
                return nullptr;
            }
        } else {
            aggregation_done_ = true;
            return nullptr;
        }
    }

    size_t rows_to_emit = std::min(batch_size_, result_rows_.size() - current_result_row_);
    size_t num_cols = output_schema_.column_count();

    std::vector<std::vector<Value>> columns(num_cols);
    for (auto& col : columns) {
        col.reserve(rows_to_emit);
    }

    for (size_t r = 0; r < rows_to_emit; ++r) {
        const auto& row = result_rows_[current_result_row_ + r];
        for (size_t c = 0; c < num_cols; ++c) {
            columns[c].push_back(row[c]);
        }
    }

    current_result_row_ += rows_to_emit;
    return std::make_unique<RecordBatch>(output_schema_, std::move(columns), rows_to_emit);
}

void PhysicalHashAggregate::close() {
    result_rows_.clear();
    current_result_row_ = 0;
    aggregation_done_ = true;
    for (auto& p : spilled_partitions_) {
        p->remove_file();
    }
    spilled_partitions_.clear();
    if (!children_.empty()) {
        children_[0]->close();
    }
}

std::string PhysicalHashAggregate::to_string(size_t indent) const {
    std::string res = make_indent(indent) + "PhysicalHashAggregate: keys=" + std::to_string(group_by_.size()) +
                      ", aggs=" + std::to_string(aggregates_.size()) + "\n";
    if (!children_.empty()) {
        res += children_[0]->to_string(indent + 1);
    }
    return res;
}

} // namespace axiom
