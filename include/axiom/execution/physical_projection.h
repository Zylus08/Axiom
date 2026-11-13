#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/execution/expression_evaluator.h"
#include "axiom/binder/bound_statement.h"

namespace axiom {

class PhysicalProjection : public PhysicalOperator {
public:
    PhysicalProjection(std::shared_ptr<PhysicalOperator> child, std::vector<BoundSelectItem> select_list, Schema schema)
        : PhysicalOperator(PhysicalOperatorType::Projection, std::move(schema)),
          select_list_(std::move(select_list)) {
        add_child(std::move(child));
    }

    void init() override {
        children_[0]->init();
    }

    std::unique_ptr<RecordBatch> next() override {
        auto batch = children_[0]->next();
        if (!batch) {
            return nullptr;
        }

        std::vector<std::vector<Value>> new_columns;
        new_columns.reserve(select_list_.size());

        for (const auto& item : select_list_) {
            new_columns.push_back(ExpressionEvaluator::evaluate_batch(item.expr.get(), *batch));
        }

        return std::make_unique<RecordBatch>(output_schema_, std::move(new_columns), batch->row_count());
    }

    void close() override {
        children_[0]->close();
    }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        std::string res = make_indent(indent) + "PhysicalProjection: [";
        for (size_t i = 0; i < select_list_.size(); ++i) {
            if (i > 0) res += ", ";
            res += select_list_[i].expr->to_string() + " AS " + select_list_[i].name;
        }
        res += "]\n" + children_[0]->to_string(indent + 1);
        return res;
    }

private:
    std::vector<BoundSelectItem> select_list_;
};

} // namespace axiom
