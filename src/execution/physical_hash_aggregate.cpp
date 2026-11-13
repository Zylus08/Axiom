#include "axiom/execution/physical_hash_aggregate.h"

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
}

void PhysicalHashAggregate::execute_aggregation() {
    result_rows_.clear();
    current_result_row_ = 0;

    size_t num_keys = group_by_.size();
    size_t num_aggs = aggregates_.size();

    // Map: Group Key Vector -> Vector of Accumulators
    std::unordered_map<std::vector<Value>, std::vector<AggregateAccumulator>, GroupKeyHasher, GroupKeyEqual> groups;

    bool seen_any_rows = false;

    if (!children_.empty()) {
        children_[0]->init();
        while (true) {
            auto batch = children_[0]->next();
            if (!batch) break;

            size_t rows = batch->row_count();
            if (rows > 0) {
                seen_any_rows = true;
            }

            // Pre-evaluate aggregate argument columns on this batch
            std::vector<std::vector<Value>> agg_args(num_aggs);
            for (size_t a = 0; a < num_aggs; ++a) {
                if (aggregates_[a]->func() != AggregateFunc::CountStar) {
                    agg_args[a] = ExpressionEvaluator::evaluate_batch(aggregates_[a]->argument(), *batch);
                }
            }

            // Pre-evaluate group keys on this batch
            std::vector<std::vector<Value>> key_cols(num_keys);
            for (size_t k = 0; k < num_keys; ++k) {
                key_cols[k] = ExpressionEvaluator::evaluate_batch(group_by_[k].get(), *batch);
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

    // Handle empty input behavior
    if (!seen_any_rows) {
        if (num_keys == 0) {
            // Scalar aggregate on empty table returns 1 row!
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
        // If num_keys > 0, GROUP BY on empty input produces 0 rows, which is correct!
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

void PhysicalHashAggregate::init() {
    execute_aggregation();
    aggregation_done_ = false;
}

std::unique_ptr<RecordBatch> PhysicalHashAggregate::next() {
    if (aggregation_done_ || current_result_row_ >= result_rows_.size()) {
        aggregation_done_ = true;
        return nullptr;
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

