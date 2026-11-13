#pragma once

#include "axiom/execution/physical_operator.h"

namespace axiom {

class PhysicalLimit : public PhysicalOperator {
public:
    PhysicalLimit(std::shared_ptr<PhysicalOperator> child,
                  std::optional<int64_t> limit,
                  std::optional<int64_t> offset)
        : PhysicalOperator(PhysicalOperatorType::Limit, child->output_schema()),
          limit_(limit),
          offset_(offset) {
        add_child(std::move(child));
    }

    void init(QueryExecutionContext* ctx = nullptr) override {
        ctx_ = ctx;
        children_[0]->init(ctx);
        current_offset_ = 0;
        emitted_rows_ = 0;
        done_ = false;
    }

    std::unique_ptr<RecordBatch> next() override {
        if (done_) {
            return nullptr;
        }

        int64_t target_offset = offset_.value_or(0);

        while (true) {
            auto batch = children_[0]->next();
            if (!batch) {
                done_ = true;
                return nullptr;
            }

            int64_t batch_rows = static_cast<int64_t>(batch->row_count());

            // Handle offset
            if (current_offset_ < target_offset) {
                int64_t needed_skip = target_offset - current_offset_;
                if (batch_rows <= needed_skip) {
                    current_offset_ += batch_rows;
                    continue; // entire batch skipped
                }
                // Partial skip
                size_t skip_count = static_cast<size_t>(needed_skip);
                current_offset_ += needed_skip;
                batch = batch->slice(skip_count, batch->row_count() - skip_count);
                batch_rows = static_cast<int64_t>(batch->row_count());
            }

            // Handle limit
            if (limit_.has_value()) {
                int64_t remaining_limit = *limit_ - emitted_rows_;
                if (remaining_limit <= 0) {
                    done_ = true;
                    return nullptr;
                }

                if (batch_rows > remaining_limit) {
                    batch = batch->slice(0, static_cast<size_t>(remaining_limit));
                    emitted_rows_ += remaining_limit;
                    done_ = true; // Limit reached!
                    return batch;
                }

                emitted_rows_ += batch_rows;
                if (emitted_rows_ >= *limit_) {
                    done_ = true;
                }
                return batch;
            }

            // No limit, just emit
            emitted_rows_ += batch_rows;
            return batch;
        }
    }

    void close() override {
        children_[0]->close();
        done_ = true;
    }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        std::string res = make_indent(indent) + "PhysicalLimit: ";
        if (limit_.has_value()) res += "limit=" + std::to_string(*limit_) + " ";
        if (offset_.has_value()) res += "offset=" + std::to_string(*offset_);
        res += "\n" + children_[0]->to_string(indent + 1);
        return res;
    }

private:
    std::optional<int64_t> limit_;
    std::optional<int64_t> offset_;
    int64_t current_offset_{0};
    int64_t emitted_rows_{0};
    bool done_{false};
};

} // namespace axiom

