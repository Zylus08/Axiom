#include "tests/test_helpers.h"
#include "axiom/storage/csv_reader.h"
#include <fstream>
#include <filesystem>

void run_csv_reader_tests() {
    using namespace axiom;

    std::string test_dir = "tests/fixtures";
    std::filesystem::create_directories(test_dir);

    // 1. Standard CSV with header, quotes, escaped quotes, embedded commas
    std::string csv1 = test_dir + "/test1.csv";
    {
        std::ofstream out(csv1, std::ios::binary);
        out << "id,name,salary,active\n"
            << "1,\"John, Doe\",50000.5,true\n"
            << "2,\"Jane \"\"The Boss\"\" Smith\",75000.0,false\n"
            << "3,\"Line1\nLine2\",12000.25,true\n"
            << "4,,NULL,false\n";
    }

    Schema schema1({
        {"id", TypeKind::BigInt},
        {"name", TypeKind::VarChar},
        {"salary", TypeKind::Double},
        {"active", TypeKind::Boolean}
    });

    CsvOptions opts1;
    opts1.has_header = true;
    opts1.null_string = "NULL";
    opts1.treat_empty_as_null = true;

    CsvReader reader1(csv1, schema1, opts1);
    auto batch1 = reader1.read_batch(10);
    ASSERT_TRUE(batch1 != nullptr);
    ASSERT_EQ(batch1->row_count(), 4);

    // Row 1: id=1, name="John, Doe", salary=50000.5, active=true
    ASSERT_EQ(batch1->get_value(0, 0).get_bigint(), 1);
    ASSERT_EQ(batch1->get_value(1, 0).get_varchar(), "John, Doe");
    ASSERT_TRUE(std::abs(batch1->get_value(2, 0).get_double() - 50000.5) < 1e-6);
    ASSERT_TRUE(batch1->get_value(3, 0).get_bool());

    // Row 2: escaped quote
    ASSERT_EQ(batch1->get_value(1, 1).get_varchar(), "Jane \"The Boss\" Smith");
    ASSERT_FALSE(batch1->get_value(3, 1).get_bool());

    // Row 3: embedded newline
    ASSERT_EQ(batch1->get_value(1, 2).get_varchar(), "Line1\nLine2");

    // Row 4: empty field and "NULL" string parsed as NULL
    ASSERT_TRUE(batch1->get_value(1, 3).is_null()); // empty name
    ASSERT_TRUE(batch1->get_value(2, 3).is_null()); // "NULL" salary
    ASSERT_FALSE(batch1->get_value(3, 3).get_bool());

    // Next read should return nullptr (EOF)
    auto eof_batch = reader1.read_batch(10);
    ASSERT_TRUE(eof_batch == nullptr);

    // 2. Chunked reading
    reader1.rewind();
    auto chunk1 = reader1.read_batch(2);
    ASSERT_TRUE(chunk1 != nullptr);
    ASSERT_EQ(chunk1->row_count(), 2);
    auto chunk2 = reader1.read_batch(2);
    ASSERT_TRUE(chunk2 != nullptr);
    ASSERT_EQ(chunk2->row_count(), 2);
    auto chunk3 = reader1.read_batch(2);
    ASSERT_TRUE(chunk3 == nullptr);

    // 3. File not found error
    ASSERT_THROWS(CsvReader("non_existent_file.csv", schema1, opts1), AxiomException);

    // 4. Column mismatch error
    std::string csv_bad = test_dir + "/bad_cols.csv";
    {
        std::ofstream out(csv_bad);
        out << "id,name\n"
            << "1,Alice,ExtraColumn\n";
    }
    Schema bad_schema({{"id", TypeKind::BigInt}, {"name", TypeKind::VarChar}});
    CsvReader bad_reader(csv_bad, bad_schema, opts1);
    ASSERT_THROWS(bad_reader.read_batch(10), AxiomException);
}
