#include "tests/test_helpers.h"
#include "axiom/common/memory_manager.h"
#include "axiom/storage/binary_serializer.h"
#include "axiom/storage/spill_manager.h"
#include "axiom/common/query_context.h"
#include "axiom/execution/physical_sort.h"
#include "axiom/execution/physical_hash_aggregate.h"
#include "axiom/execution/physical_hash_join.h"
#include <sstream>
#include <filesystem>
#include <fstream>

namespace {

using namespace axiom;

// Helper mock operator
class ForensicsMockScan : public PhysicalOperator {
public:
    ForensicsMockScan(Schema schema, std::vector<std::unique_ptr<RecordBatch>> batches)
        : PhysicalOperator(PhysicalOperatorType::Scan, std::move(schema)),
          batches_(std::move(batches)) {}

    void init() override { cursor_ = 0; }
    void init(QueryExecutionContext* ctx) override { ctx_ = ctx; cursor_ = 0; }

    std::unique_ptr<RecordBatch> next() override {
        if (cursor_ >= batches_.size()) return nullptr;
        const auto& src = batches_[cursor_++];
        std::vector<std::vector<Value>> cols;
        for (size_t c = 0; c < src->column_count(); ++c) {
            cols.push_back(src->get_column(c));
        }
        return std::make_unique<RecordBatch>(output_schema_, std::move(cols), src->row_count());
    }

    void close() override { cursor_ = 0; }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        return make_indent(indent) + "ForensicsMockScan\n";
    }

private:
    std::vector<std::unique_ptr<RecordBatch>> batches_;
    size_t cursor_{0};
};

// 1. Skewed Partition Join: All keys identical under tiny memory limit
void test_skew_identical_keys_join() {
    Schema schema({
        {"id", TypeKind::BigInt},
        {"val", TypeKind::BigInt}
    });

    constexpr size_t BUILD_ROWS = 300;
    constexpr size_t PROBE_ROWS = 300;

    // Both build and probe have key = 42 for all rows
    std::vector<std::vector<Value>> build_cols(2);
    for (size_t i = 0; i < BUILD_ROWS; ++i) {
        build_cols[0].push_back(Value::make_bigint(42));
        build_cols[1].push_back(Value::make_bigint(static_cast<int64_t>(100 + i)));
    }
    std::vector<std::unique_ptr<RecordBatch>> build_batches;
    build_batches.push_back(std::make_unique<RecordBatch>(schema, std::move(build_cols), BUILD_ROWS));

    std::vector<std::vector<Value>> probe_cols(2);
    for (size_t i = 0; i < PROBE_ROWS; ++i) {
        probe_cols[0].push_back(Value::make_bigint(42));
        probe_cols[1].push_back(Value::make_bigint(static_cast<int64_t>(1000 + i)));
    }
    std::vector<std::unique_ptr<RecordBatch>> probe_batches;
    probe_batches.push_back(std::make_unique<RecordBatch>(schema, std::move(probe_cols), PROBE_ROWS));

    auto probe_op = std::make_shared<ForensicsMockScan>(schema, std::move(probe_batches));
    auto build_op = std::make_shared<ForensicsMockScan>(schema, std::move(build_batches));

    Schema join_schema({
        {"p_id", TypeKind::BigInt},
        {"p_val", TypeKind::BigInt},
        {"b_id", TypeKind::BigInt},
        {"b_val", TypeKind::BigInt}
    });

    auto cond = std::make_unique<BoundBinaryExpr>(
        BinaryOp::Equal,
        std::make_unique<BoundColumnRefExpr>("p_id", 0, TypeKind::BigInt),
        std::make_unique<BoundColumnRefExpr>("b_id", 2, TypeKind::BigInt),
        TypeKind::Boolean
    );

    // Memory limit of 10 KB to force Grace Hash Join and multiple block passes
    QueryOptions opts;
    opts.memory_limit = 10 * 1024;
    opts.temp_dir = "tests/fixtures/temp_skew_join";
    QueryExecutionContext ctx(opts);

    PhysicalHashJoin join_op(probe_op, build_op, JoinType::Inner, std::move(cond), join_schema, 64);
    join_op.init(&ctx);

    size_t total_joined_rows = 0;
    while (true) {
        auto batch = join_op.next();
        if (!batch) break;
        total_joined_rows += batch->row_count();
    }
    join_op.close();

    // Cross product of 300 x 300 = 90,000 matches
    ASSERT_EQ(total_joined_rows, BUILD_ROWS * PROBE_ROWS);
    ASSERT_TRUE(join_op.spilled_partitions_count() > 0);
    ASSERT_TRUE(join_op.spilled_bytes() > 0);
}

// 2. Skewed Partition Join: Dominant Key (Zipfian skew)
void test_skew_dominant_key_join() {
    Schema schema({
        {"id", TypeKind::BigInt},
        {"name", TypeKind::VarChar}
    });

    // 80% dominant key (key=1), 20% distinct keys (key=2..50)
    std::vector<std::vector<Value>> build_cols(2);
    for (size_t i = 0; i < 500; ++i) {
        int64_t k = (i < 400) ? 1 : static_cast<int64_t>(i - 400 + 2);
        build_cols[0].push_back(Value::make_bigint(k));
        build_cols[1].push_back(Value::make_varchar("b_" + std::to_string(i)));
    }
    std::vector<std::unique_ptr<RecordBatch>> build_batches;
    build_batches.push_back(std::make_unique<RecordBatch>(schema, std::move(build_cols), 500));

    std::vector<std::vector<Value>> probe_cols(2);
    for (size_t i = 0; i < 500; ++i) {
        int64_t k = (i < 400) ? 1 : static_cast<int64_t>(i - 400 + 2);
        probe_cols[0].push_back(Value::make_bigint(k));
        probe_cols[1].push_back(Value::make_varchar("p_" + std::to_string(i)));
    }
    std::vector<std::unique_ptr<RecordBatch>> probe_batches;
    probe_batches.push_back(std::make_unique<RecordBatch>(schema, std::move(probe_cols), 500));

    auto probe_op = std::make_shared<ForensicsMockScan>(schema, std::move(probe_batches));
    auto build_op = std::make_shared<ForensicsMockScan>(schema, std::move(build_batches));

    Schema join_schema({
        {"p_id", TypeKind::BigInt},
        {"p_name", TypeKind::VarChar},
        {"b_id", TypeKind::BigInt},
        {"b_name", TypeKind::VarChar}
    });

    auto cond = std::make_unique<BoundBinaryExpr>(
        BinaryOp::Equal,
        std::make_unique<BoundColumnRefExpr>("p_id", 0, TypeKind::BigInt),
        std::make_unique<BoundColumnRefExpr>("b_id", 2, TypeKind::BigInt),
        TypeKind::Boolean
    );

    QueryOptions opts;
    opts.memory_limit = 20 * 1024; // 20 KB
    opts.temp_dir = "tests/fixtures/temp_skew_dom";
    QueryExecutionContext ctx(opts);

    PhysicalHashJoin join_op(probe_op, build_op, JoinType::Inner, std::move(cond), join_schema, 64);
    join_op.init(&ctx);

    size_t total_joined_rows = 0;
    while (true) {
        auto batch = join_op.next();
        if (!batch) break;
        total_joined_rows += batch->row_count();
    }
    join_op.close();

    // 400x400 (dominant key 1) + 100 x 1 (unique keys) = 160,000 + 100 = 160,100 rows
    ASSERT_EQ(total_joined_rows, 160100);
}

// 3. Key Skew in Aggregation: all identical keys collapsing
void test_skew_identical_keys_aggregation() {
    Schema schema({
        {"k", TypeKind::BigInt},
        {"v", TypeKind::BigInt}
    });

    constexpr size_t TOTAL_ROWS = 5000;
    std::vector<std::vector<Value>> cols(2);
    for (size_t i = 0; i < TOTAL_ROWS; ++i) {
        cols[0].push_back(Value::make_bigint(999)); // All identical key
        cols[1].push_back(Value::make_bigint(10));
    }
    std::vector<std::unique_ptr<RecordBatch>> batches;
    batches.push_back(std::make_unique<RecordBatch>(schema, std::move(cols), TOTAL_ROWS));

    auto child_op = std::make_shared<ForensicsMockScan>(schema, std::move(batches));

    std::vector<std::unique_ptr<BoundExpr>> group_by;
    group_by.push_back(std::make_unique<BoundColumnRefExpr>("k", 0, TypeKind::BigInt));

    std::vector<std::unique_ptr<BoundAggregateExpr>> aggs;
    aggs.push_back(std::make_unique<BoundAggregateExpr>(
        AggregateFunc::Sum,
        std::make_unique<BoundColumnRefExpr>("v", 1, TypeKind::BigInt),
        TypeKind::BigInt,
        0
    ));
    aggs.push_back(std::make_unique<BoundAggregateExpr>(
        AggregateFunc::CountStar,
        nullptr,
        TypeKind::BigInt,
        1
    ));

    Schema out_schema({
        {"k", TypeKind::BigInt},
        {"sum_v", TypeKind::BigInt},
        {"cnt", TypeKind::BigInt}
    });

    QueryOptions opts;
    opts.memory_limit = 10 * 1024; // 10 KB
    opts.temp_dir = "tests/fixtures/temp_skew_agg";
    QueryExecutionContext ctx(opts);

    PhysicalHashAggregate agg_op(child_op, std::move(group_by), std::move(aggs), out_schema);
    agg_op.init(&ctx);

    auto res = agg_op.next();
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->row_count(), 1); // Exact 1 group!
    ASSERT_EQ(res->get_value(0, 0).get_bigint(), 999);
    ASSERT_EQ(res->get_value(1, 0).get_bigint(), 50000); // 5000 * 10
    ASSERT_EQ(res->get_value(2, 0).get_bigint(), 5000);

    ASSERT_TRUE(agg_op.next() == nullptr);
    agg_op.close();
}

// 4. Corrupt Spill File: Checksum Mismatch
void test_corrupt_spill_checksum() {
    Schema schema({
        {"a", TypeKind::BigInt},
        {"b", TypeKind::VarChar}
    });

    std::string test_file = "tests/fixtures/corrupt_chk.axmb";
    std::vector<std::vector<Value>> cols(2);
    cols[0].push_back(Value::make_bigint(12345));
    cols[1].push_back(Value::make_varchar("Hello world payload"));
    RecordBatch batch(schema, std::move(cols), 1);

    {
        std::ofstream os(test_file, std::ios::binary);
        BinarySerializer::write_batch(os, batch);
    }

    // Tamper with one byte in the payload
    {
        std::fstream fs(test_file, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(28, std::ios::beg);
        char bad = 'X';
        fs.write(&bad, 1);
    }

    // Reading should throw AxiomException with checksum error
    {
        std::ifstream is(test_file, std::ios::binary);
        bool caught = false;
        try {
            (void)BinarySerializer::read_batch(is, schema);
        } catch (const AxiomException& e) {
            caught = true;
            std::string msg = e.what();
            ASSERT_TRUE(msg.find("checksum") != std::string::npos || msg.find("corruption") != std::string::npos);
        }
        ASSERT_TRUE(caught);
    }

    std::filesystem::remove(test_file);
}

// 5. Corrupt Spill File: Invalid Magic
void test_corrupt_spill_magic() {
    Schema schema({{"x", TypeKind::BigInt}});
    std::string test_file = "tests/fixtures/bad_magic.axmb";

    {
        std::ofstream os(test_file, std::ios::binary);
        uint32_t bad_magic = 0xDEADBEEF;
        os.write(reinterpret_cast<const char*>(&bad_magic), sizeof(bad_magic));
        uint64_t dummy = 0;
        os.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    }

    {
        std::ifstream is(test_file, std::ios::binary);
        bool caught = false;
        try {
            (void)BinarySerializer::read_batch(is, schema);
        } catch (const AxiomException& e) {
            caught = true;
            std::string msg = e.what();
            ASSERT_TRUE(msg.find("magic") != std::string::npos);
        }
        ASSERT_TRUE(caught);
    }

    std::filesystem::remove(test_file);
}

// 6. Corrupt Spill File: Truncated File
void test_corrupt_spill_truncated() {
    Schema schema({{"x", TypeKind::BigInt}});
    std::string test_file = "tests/fixtures/truncated.axmb";

    {
        std::ofstream os(test_file, std::ios::binary);
        uint32_t magic = BinarySerializer::MAGIC;
        os.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        // File ends abruptly after 4 bytes
    }

    {
        std::ifstream is(test_file, std::ios::binary);
        bool caught = false;
        try {
            (void)BinarySerializer::read_batch(is, schema);
        } catch (const AxiomException& e) {
            caught = true;
            std::string msg = e.what();
            ASSERT_TRUE(msg.find("Truncated") != std::string::npos);
        }
        ASSERT_TRUE(caught);
    }

    std::filesystem::remove(test_file);
}

// 7. Spill File: Schema Column Mismatch
void test_corrupt_spill_col_mismatch() {
    Schema schema2({{"a", TypeKind::BigInt}, {"b", TypeKind::BigInt}});
    Schema schema3({{"a", TypeKind::BigInt}, {"b", TypeKind::BigInt}, {"c", TypeKind::BigInt}});
    std::string test_file = "tests/fixtures/col_mismatch.axmb";

    {
        std::vector<std::vector<Value>> cols(2);
        cols[0].push_back(Value::make_bigint(1));
        cols[1].push_back(Value::make_bigint(2));
        RecordBatch batch(schema2, std::move(cols), 1);

        std::ofstream os(test_file, std::ios::binary);
        BinarySerializer::write_batch(os, batch);
    }

    {
        std::ifstream is(test_file, std::ios::binary);
        bool caught = false;
        try {
            (void)BinarySerializer::read_batch(is, schema3);
        } catch (const AxiomException& e) {
            caught = true;
            std::string msg = e.what();
            ASSERT_TRUE(msg.find("mismatch") != std::string::npos);
        }
        ASSERT_TRUE(caught);
    }

    std::filesystem::remove(test_file);
}

// 8. Process Memory Info: Commit Charge Tracking
void test_process_memory_info_commit() {
    ProcessMemoryInfo info = get_process_memory_info();
    ASSERT_TRUE(info.current_rss > 0);
    ASSERT_TRUE(info.peak_rss > 0);
    ASSERT_TRUE(info.current_commit > 0);
    ASSERT_TRUE(info.peak_commit > 0);
    ASSERT_TRUE(info.peak_commit >= info.current_commit);
}

} // namespace

void run_m3_forensics_tests() {
    std::cout << "\n--- Milestone M3 Forensic & Hardening Tests ---" << std::endl;
    test_skew_identical_keys_join();
    test_skew_dominant_key_join();
    test_skew_identical_keys_aggregation();
    test_corrupt_spill_checksum();
    test_corrupt_spill_magic();
    test_corrupt_spill_truncated();
    test_corrupt_spill_col_mismatch();
    test_process_memory_info_commit();
}

