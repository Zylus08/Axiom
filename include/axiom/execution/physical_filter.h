#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"

namespace axiom {

class PhysicalFilter : public PhysicalOperator {
public:
    PhysicalFilter(std::shared_ptr<PhysicalOperator> child, std::unique_ptr<BoundExpr> predicate)
        : PhysicalOperator(PhysicalOperatorType::Filter, child->output_schema()),
          predicate_(std::move(predicate)) {
        add_child(std::move(child));
    }

    void init() override {
        children_[0]->init();
    }

    std::unique_ptr<RecordBatch> next() override {
        while (true) {
            auto batch = children_[0]->next();
            if (!batch) {
                return nullptr;
            }

            size_t rows = batch->row_count();
            std::vector<bool> selection(rows, false);
            size_t selected = 0;

            for (size_t r = 0; r < rows; ++r) {
                Value cond = ExpressionEvaluator::evaluate_row(predicate_.get(), *batch, r);
                if (!cond.is_null() && cond.kind() == TypeKind::Boolean && cond.get_bool()) {
                    selection[r] = true;
                    selected++;
                }
            }

            if (selected > 0) {
                return batch->filter(selection);
            }
            // All rows filtered out in this batch, try next batch from child
        }
    }

    void close() override {
        children_[0]->close();
    }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        return make_indent(indent) + "PhysicalFilter: " + predicate_->to_string() + "\n" +
               children_[0]->to_string(indent + 1);
    }

private:
    std::unique_ptr<BoundExpr> predicate_;
};

} // namespace axiom
