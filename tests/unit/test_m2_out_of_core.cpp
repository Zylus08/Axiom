#include "tests/test_helpers.h"
#include "axiom/common/memory_manager.h"
#include "axiom/storage/binary_serializer.h"
#include "axiom/storage/spill_manager.h"
#include "axiom/common/query_context.h"
#include "axiom/execution/physical_sort.h"
#include "axiom/execution/physical_hash_aggregate.h"
#include "axiom/execution/physical_hash_join.h"
#include <sstream>

namespace {

using namespace axiom;

// Helper mock operator that emits fixed batches in tests
class MockBatchOperator : public PhysicalOperator {
public:
    MockBatchOperator(Schema schema, std::vector<std::unique_ptr<RecordBatch>> batches)
        : PhysicalOperator(PhysicalOperatorType::Scan, std::move(schema)),
          batches_(std::move(batches)) {}

    void init() override {
        cursor_ = 0;
    }

    void init(QueryExecutionContext* ctx) override {
        ctx_ = ctx;
        cursor_ = 0;
    }

    std::unique_ptr<RecordBatch> next() override {
        if (cursor_ >= batches_.size()) return nullptr;
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
        return make_indent(indent) + "MockBatchOperator\n";
    }

private:
    std::vector<std::unique_ptr<RecordBatch>> batches_;
    size_t cursor_{0};
};

void test_memory_manager_basic() {
    auto qmm = std::make_shared<QueryMemoryManager>(1024 * 1024); // 1 MB budget
    ASSERT_TRUE(qmm->has_limit());
    ASSERT_EQ(qmm->max_budget(), 1024 * 1024);
    ASSERT_EQ(qmm->current_bytes(), 0);
    ASSERT_EQ(qmm->peak_bytes(), 0);

    ASSERT_TRUE(qmm->try_allocate(500 * 1024));
    ASSERT_EQ(qmm->current_bytes(), 500 * 1024);
    ASSERT_EQ(qmm->peak_bytes(), 500 * 1024);

    // Try allocating more than remaining budget
    ASSERT_FALSE(qmm->try_allocate(600 * 1024));
    ASSERT_EQ(qmm->current_bytes(), 500 * 1024); // Unchanged

    qmm->deallocate(200 * 1024);
    ASSERT_EQ(qmm->current_bytes(), 300 * 1024);
    ASSERT_EQ(qmm->peak_bytes(), 500 * 1024); // Peak preserved

    qmm->deallocate(300 * 1024);
    ASSERT_EQ(qmm->current_bytes(), 0);
}

void test_memory_reservation_raii() {
    auto qmm = std::make_shared<QueryMemoryManager>(1000);
    {
        auto res = qmm->create_reservation();
        ASSERT_TRUE(res->try_reserve(400));
        ASSERT_EQ(res->reserved_bytes(), 400);
        ASSERT_EQ(qmm->current_bytes(), 400);

        ASSERT_TRUE(res->try_reserve(300));
        ASSERT_EQ(res->reserved_bytes(), 700);
        ASSERT_EQ(qmm->current_bytes(), 700);

        ASSERT_FALSE(res->try_reserve(400)); // Would exceed 1000
        ASSERT_EQ(res->reserved_bytes(), 700);

        res->release(200);
        ASSERT_EQ(res->reserved_bytes(), 500);
        ASSERT_EQ(qmm->current_bytes(), 500);
    } // RAII auto release
    ASSERT_EQ(qmm->current_bytes(), 0);
}

void test_memory_parsing() {
    ASSERT_EQ(parse_memory_size("1024"), 1024);
    ASSERT_EQ(parse_memory_size("1KB"), 1024);
    ASSERT_EQ(parse_memory_size("64KB"), 64 * 1024);
    ASSERT_EQ(parse_memory_size("256MB"), 256 * 1024 * 1024);
    ASSERT_EQ(parse_memory_size("2GB"), 2ULL * 1024 * 1024 * 1024);
    ASSERT_EQ(parse_memory_size("  512 mb  "), 512 * 1024 * 1024);
}

void test_binary_serializer_roundtrip() {
    Schema schema({
        {"id", TypeKind::BigInt, false},
        {"name", TypeKind::VarChar, true},
        {"score", TypeKind::Double, true},
        {"is_active", TypeKind::Boolean, true}
    });

    std::vector<std::vector<Value>> cols(4);
    cols[0] = {Value::make_bigint(1), Value::make_bigint(2), Value::make_bigint(3)};
    cols[1] = {Value::make_varchar("Alpha"), Value::make_null(TypeKind::VarChar), Value::make_varchar("Gamma")};
    cols[2] = {Value::make_double(95.5), Value::make_double(82.0), Value::make_null(TypeKind::Double)};
    cols[3] = {Value::make_bool(true), Value::make_bool(false), Value::make_null(TypeKind::Boolean)};

    RecordBatch original(schema, std::move(cols), 3);

    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    BinarySerializer::write_batch(ss, original);

    auto restored = BinarySerializer::read_batch(ss, schema);
    ASSERT_TRUE(restored != nullptr);
    ASSERT_EQ(restored->row_count(), 3);
    ASSERT_EQ(restored->column_count(), 4);

    // Verify values and nullness
    ASSERT_EQ(restored->get_value(0, 0).get_bigint(), 1);
    ASSERT_EQ(restored->get_value(1, 0).get_varchar(), "Alpha");
    ASSERT_EQ(restored->get_value(2, 0).get_double(), 95.5);
    ASSERT_EQ(restored->get_value(3, 0).get_bool(), true);

    ASSERT_EQ(restored->get_value(0, 1).get_bigint(), 2);
    ASSERT_TRUE(restored->get_value(1, 1).is_null());
    ASSERT_EQ(restored->get_value(2, 1).get_double(), 82.0);
    ASSERT_EQ(restored->get_value(3, 1).get_bool(), false);

    ASSERT_EQ(restored->get_value(0, 2).get_bigint(), 3);
    ASSERT_EQ(restored->get_value(1, 2).get_varchar(), "Gamma");
    ASSERT_TRUE(restored->get_value(2, 2).is_null());
    ASSERT_TRUE(restored->get_value(3, 2).is_null());
}

void test_spill_partition_lifecycle() {
    Schema schema({
        {"x", TypeKind::BigInt, false},
        {"y", TypeKind::Double, true}
    });

    auto sm = std::make_shared<SpillManager>("", 10 * 1024 * 1024);
    auto part = sm->create_partition(schema, "test_part");
    ASSERT_TRUE(part != nullptr);

    // Write batch 1
    {
        std::vector<std::vector<Value>> c(2);
        c[0] = {Value::make_bigint(10), Value::make_bigint(20)};
        c[1] = {Value::make_double(1.5), Value::make_double(2.5)};
        RecordBatch b1(schema, std::move(c), 2);
        part->write_batch(b1);
    }
    // Write batch 2
    {
        std::vector<std::vector<Value>> c(2);
        c[0] = {Value::make_bigint(30)};
        c[1] = {Value::make_double(3.5)};
        RecordBatch b2(schema, std::move(c), 1);
        part->write_batch(b2);
    }

    ASSERT_EQ(part->row_count(), 3);
    ASSERT_TRUE(part->byte_size() > 0);

    // Read back
    part->prepare_read();
    auto rb1 = part->read_next_batch();
    ASSERT_TRUE(rb1 != nullptr);
    ASSERT_EQ(rb1->row_count(), 2);
    ASSERT_EQ(rb1->get_value(0, 0).get_bigint(), 10);
    ASSERT_EQ(rb1->get_value(0, 1).get_bigint(), 20);

    auto rb2 = part->read_next_batch();
    ASSERT_TRUE(rb2 != nullptr);
    ASSERT_EQ(rb2->row_count(), 1);
    ASSERT_EQ(rb2->get_value(0, 0).get_bigint(), 30);

    auto rb3 = part->read_next_batch();
    ASSERT_TRUE(rb3 == nullptr); // EOF

    // Test rewind
    part->rewind();
    auto rb_rewound = part->read_next_batch();
    ASSERT_TRUE(rb_rewound != nullptr);
    ASSERT_EQ(rb_rewound->row_count(), 2);

    part->remove_file();
}

void test_spill_disk_quota_enforcement() {
    Schema schema({
        {"val", TypeKind::BigInt, false}
    });

    // Manager with small 200 byte limit
    auto sm = std::make_shared<SpillManager>("", 200);
    auto part = sm->create_partition(schema, "quota_test");

    std::vector<std::vector<Value>> c(1);
    for (int i = 0; i < 50; ++i) {
        c[0].push_back(Value::make_bigint(i));
    }
    RecordBatch big_batch(schema, std::move(c), 50);

    bool threw = false;
    try {
        part->write_batch(big_batch);
    } catch (const AxiomException& e) {
        threw = true;
        ASSERT_EQ(e.category(), ErrorCategory::Storage);
    }
    ASSERT_TRUE(threw);
    part->remove_file();
}

void test_external_sort_execution() {
    Schema schema({
        {"num", TypeKind::BigInt, false}
    });

    // Create 40 batches of 5 rows each in reverse order
    std::vector<std::unique_ptr<RecordBatch>> batches;
    for (int b = 0; b < 40; ++b) {
        std::vector<std::vector<Value>> cols(1);
        for (int r = 0; r < 5; ++r) {
            cols[0].push_back(Value::make_bigint(200 - (b * 5 + r)));
        }
        batches.push_back(std::make_unique<RecordBatch>(schema, std::move(cols), 5));
    }

    auto mock = std::make_shared<MockBatchOperator>(schema, std::move(batches));
    std::vector<BoundOrderItem> order_by;
    order_by.push_back(BoundOrderItem{
        std::make_unique<BoundColumnRefExpr>("num", 0, TypeKind::BigInt, ""),
        true, // ASC
        false // NULLS LAST
    });

    PhysicalSort sort_op(mock, std::move(order_by), 4);

    QueryOptions opts;
    opts.memory_limit = 300; // Force tiny budget to trigger spilling
    QueryExecutionContext ctx(opts);

    sort_op.init(&ctx);

    size_t total_rows = 0;
    int64_t last_val = -1;

    while (true) {
        auto batch = sort_op.next();
        if (!batch) break;
        for (size_t r = 0; r < batch->row_count(); ++r) {
            int64_t v = batch->get_value(0, r).get_bigint();
            ASSERT_TRUE(v > last_val); // Strictly ascending
            last_val = v;
            total_rows++;
        }
    }
    sort_op.close();

    ASSERT_EQ(total_rows, 200);
    ASSERT_TRUE(sort_op.spilled_runs_count() > 0);
    ASSERT_TRUE(sort_op.spilled_bytes() > 0);
}

} // namespace

void run_m2_out_of_core_tests() {
    std::cout << "\n--- Milestone M2 Out-of-Core Subsystem Tests ---\n";
    test_memory_manager_basic();
    test_memory_reservation_raii();
    test_memory_parsing();
    test_binary_serializer_roundtrip();
    test_spill_partition_lifecycle();
    test_spill_disk_quota_enforcement();
    test_external_sort_execution();
}

