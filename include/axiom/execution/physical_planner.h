#pragma once

#include "axiom/planner/logical_plan.h"
#include "axiom/execution/physical_operator.h"
#include <memory>

namespace axiom {

class PhysicalPlanner {
public:
    explicit PhysicalPlanner(size_t batch_size = 1024);

    [[nodiscard]] std::shared_ptr<PhysicalOperator> plan(const std::shared_ptr<LogicalOperator>& logical_plan);

private:
    size_t batch_size_;
};

} // namespace axiom

