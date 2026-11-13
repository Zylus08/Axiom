#include "tests/test_helpers.h"
#include "axiom/execution/physical_filter.h"
#include "axiom/execution/physical_projection.h"
#include "axiom/execution/physical_hash_join.h"
#include "axiom/execution/physical_hash_aggregate.h"
#include "axiom/execution/physical_sort.h"
#include "axiom/execution/physical_limit.h"

namespace {

using namespace axiom;

// Helper mock operator that emits fixed batches in tests
class TestMemoryOperator : public PhysicalOperator {
public:
    TestMemoryOperator(Schema schema, std::vector<std::unique_ptr<RecordBatch>> batches)
        : PhysicalOperator(PhysicalOperatorType::Scan, std::move(schema)),
          batches_(std::move(batches)) {}

    void init() override {
        cursor_ = 0;
    }

    std::unique_ptr<RecordBatch> next() override {
        if (cursor_ >= batches_.size()) return nullptr;
        // Make a copy of batch
        const auto& src = batches_[cursor_++];
        std::vector<std::vector<Value>> cols;
        for (size_t c = 0; c < src->column_count(); ++c) {
            cols.push_back(src->get_column(c));
        }
        return std::make_unique<RecordBatch>(output_schema_, std::move(cols), src->row_count());
    }

    void close() override {
        cursor_ = 0;
    }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        return make_indent(indent) + "TestMemoryOperator\n";
    }

private:
    std::vector<std::unique_ptr<RecordBatch>> batches_;
    size_t cursor_{0};
};

} // namespace

void run_operators_tests() {
    using namespace axiom;

    Schema s1({{"id", TypeKind::BigInt}, {"val", TypeKind::Double}});

    // Create 1 batch of 3 rows: (1, 10.0), (2, 20.0), (3, 30.0)
    std::vector<std::vector<Value>> cols1 = {
        {Value::make_bigint(1), Value::make_bigint(2), Value::make_bigint(3)},
        {Value::make_double(10.0), Value::make_double(20.0), Value::make_double(30.0)}
    };
    std::vector<std::unique_ptr<RecordBatch>> batches1;
    batches1.push_back(std::make_unique<RecordBatch>(s1, cols1, 3));

    // 1. PhysicalFilter: id > 1
    {
        auto mock = std::make_shared<TestMemoryOperator>(s1, std::move(batches1));
        auto pred = std::make_unique<BoundBinaryExpr>(
            BinaryOp::GreaterThan,
            std::make_unique<BoundColumnRefExpr>("id", 0, TypeKind::BigInt),
            std::make_unique<BoundLiteralExpr>(Value::make_bigint(1)),
            TypeKind::Boolean
        );
        PhysicalFilter filter(mock, std::move(pred));
        filter.init();
        auto res = filter.next();
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->row_count(), 2);
        ASSERT_EQ(res->get_value(0, 0).get_bigint(), 2);
        ASSERT_EQ(res->get_value(0, 1).get_bigint(), 3);
        ASSERT_TRUE(filter.next() == nullptr);
    }

    // 2. PhysicalLimit: limit 1 offset 1
    {
        std::vector<std::unique_ptr<RecordBatch>> b;
        b.push_back(std::make_unique<RecordBatch>(s1, cols1, 3));
        auto mock = std::make_shared<TestMemoryOperator>(s1, std::move(b));
        PhysicalLimit limit_op(mock, 1, 1);
        limit_op.init();
        auto res = limit_op.next();
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->row_count(), 1);
        ASSERT_EQ(res->get_value(0, 0).get_bigint(), 2);
        ASSERT_TRUE(limit_op.next() == nullptr);
    }

    // 3. PhysicalHashJoin: Duplicate keys join multiplicity
    {
        // Left: 2 rows with id=1
        Schema left_schema({{"left_id", TypeKind::BigInt}, {"left_name", TypeKind::VarChar}});
        std::vector<std::vector<Value>> left_cols = {
            {Value::make_bigint(1), Value::make_bigint(1)},
            {Value::make_varchar("L1"), Value::make_varchar("L2")}
        };
        std::vector<std::unique_ptr<RecordBatch>> left_b;
        left_b.push_back(std::make_unique<RecordBatch>(left_schema, left_cols, 2));
        auto left_mock = std::make_shared<TestMemoryOperator>(left_schema, std::move(left_b));

        // Right: 3 rows with id=1, 1 row with id=2
        Schema right_schema({{"right_id", TypeKind::BigInt}, {"right_val", TypeKind::VarChar}});
        std::vector<std::vector<Value>> right_cols = {
            {Value::make_bigint(1), Value::make_bigint(1), Value::make_bigint(1), Value::make_bigint(2)},
            {Value::make_varchar("R1"), Value::make_varchar("R2"), Value::make_varchar("R3"), Value::make_varchar("R4")}
        };
        std::vector<std::unique_ptr<RecordBatch>> right_b;
        right_b.push_back(std::make_unique<RecordBatch>(right_schema, right_cols, 4));
        auto right_mock = std::make_shared<TestMemoryOperator>(right_schema, std::move(right_b));

        Schema join_schema({
            {"left_id", TypeKind::BigInt}, {"left_name", TypeKind::VarChar},
            {"right_id", TypeKind::BigInt}, {"right_val", TypeKind::VarChar}
        });

        auto join_cond = std::make_unique<BoundBinaryExpr>(
            BinaryOp::Equal,
            std::make_unique<BoundColumnRefExpr>("left_id", 0, TypeKind::BigInt),
            std::make_unique<BoundColumnRefExpr>("right_id", 2, TypeKind::BigInt),
            TypeKind::Boolean
        );

        PhysicalHashJoin join_op(left_mock, right_mock, JoinType::Inner, std::move(join_cond), join_schema);
        join_op.init();
        auto res = join_op.next();
        ASSERT_TRUE(res != nullptr);
        // 2 left rows * 3 right rows = 6 output rows!
        ASSERT_EQ(res->row_count(), 6);
        ASSERT_TRUE(join_op.next() == nullptr);
    }

    // 4. PhysicalHashAggregate: Scalar aggregate on empty table returns 1 row
    {
        std::vector<std::unique_ptr<RecordBatch>> empty_b;
        auto mock = std::make_shared<TestMemoryOperator>(s1, std::move(empty_b));

        std::vector<std::unique_ptr<BoundExpr>> group_by; // empty group by
        std::vector<std::unique_ptr<BoundAggregateExpr>> aggs;
        aggs.push_back(std::make_unique<BoundAggregateExpr>(AggregateFunc::CountStar, nullptr, TypeKind::BigInt, 0));
        aggs.push_back(std::make_unique<BoundAggregateExpr>(AggregateFunc::Sum, std::make_unique<BoundColumnRefExpr>("val", 1, TypeKind::Double), TypeKind::Double, 1));

        Schema agg_schema({{"_agg_0", TypeKind::BigInt}, {"_agg_1", TypeKind::Double}});

        PhysicalHashAggregate agg_op(mock, std::move(group_by), std::move(aggs), agg_schema);
        agg_op.init();
        auto res = agg_op.next();
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->row_count(), 1);
        ASSERT_EQ(res->get_value(0, 0).get_bigint(), 0);  // count(*) is 0
        ASSERT_TRUE(res->get_value(1, 0).is_null());      // sum is NULL
        ASSERT_TRUE(agg_op.next() == nullptr);
    }
}

