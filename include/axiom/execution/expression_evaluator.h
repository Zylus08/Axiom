#pragma once

#include "axiom/binder/bound_expression.h"
#include "axiom/common/record_batch.h"
#include <vector>

namespace axiom {

class ExpressionEvaluator {
public:
    // Evaluates an expression over an entire RecordBatch, returning a vector of Values
    [[nodiscard]] static std::vector<Value> evaluate_batch(const BoundExpr* expr, const RecordBatch& batch);

    // Evaluates an expression for a single row in the batch
    [[nodiscard]] static Value evaluate_row(const BoundExpr* expr, const RecordBatch& batch, size_t row_idx);
};

} // namespace axiom
