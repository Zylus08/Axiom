#include "tests/test_helpers.h"
#include "axiom/common/query_engine.h"
#include <fstream>
#include <filesystem>

void run_integration_tests() {
    using namespace axiom;

    std::string fixtures = "tests/fixtures";
    std::filesystem::create_directories(fixtures);

    // Create customers CSV
    std::string cust_csv = fixtures + "/customers.csv";
    {
        std::ofstream out(cust_csv, std::ios::binary);
        out << "id,name,country,active\n"
            << "1,Alice,USA,true\n"
            << "2,Bob,UK,true\n"
            << "3,Charlie,USA,false\n"
            << "4,David,Canada,true\n"
            << "5,Eve,Canada,false\n";
    }

    // Create orders CSV
    std::string orders_csv = fixtures + "/orders.csv";
    {
        std::ofstream out(orders_csv, std::ios::binary);
        out << "order_id,customer_id,amount,status\n"
            << "101,1,150.50,completed\n"
            << "102,1,50.00,completed\n"
            << "103,2,300.00,pending\n"
            << "104,2,200.00,completed\n"
            << "105,4,75.25,completed\n"
            << "106,4,125.75,pending\n"
            << "107,99,999.00,completed\n"; // customer 99 does not exist in customers
    }

    QueryEngine engine;

    Schema cust_schema({
        {"id", TypeKind::BigInt},
        {"name", TypeKind::VarChar},
        {"country", TypeKind::VarChar},
        {"active", TypeKind::Boolean}
    });

    Schema orders_schema({
        {"order_id", TypeKind::BigInt},
        {"customer_id", TypeKind::BigInt},
        {"amount", TypeKind::Double},
        {"status", TypeKind::VarChar}
    });

    CsvOptions opts;
    opts.has_header = true;
    engine.catalog().register_table("customers", cust_schema, cust_csv, opts);
    engine.catalog().register_table("orders", orders_schema, orders_csv, opts);

    // 1. Simple Scan and Filter
    {
        auto res = engine.execute("SELECT id, name FROM customers WHERE country = 'USA' AND active = true");
        ASSERT_EQ(res.total_rows, 1);
        ASSERT_EQ(res.batches[0]->get_value(0, 0).get_bigint(), 1);
        ASSERT_EQ(res.batches[0]->get_value(1, 0).get_varchar(), "Alice");
    }

    // 2. INNER JOIN
    {
        auto res = engine.execute("SELECT c.name, o.amount FROM customers c INNER JOIN orders o ON c.id = o.customer_id WHERE o.status = 'completed' ORDER BY o.amount DESC");
        ASSERT_EQ(res.total_rows, 4);
        // Amounts: 200.00 (Bob), 150.50 (Alice), 75.25 (David), 50.00 (Alice)
        ASSERT_EQ(res.batches[0]->get_value(0, 0).get_varchar(), "Bob");
        ASSERT_TRUE(std::abs(res.batches[0]->get_value(1, 0).get_double() - 200.00) < 1e-4);
        ASSERT_EQ(res.batches[0]->get_value(0, 1).get_varchar(), "Alice");
        ASSERT_TRUE(std::abs(res.batches[0]->get_value(1, 1).get_double() - 150.50) < 1e-4);
    }

    // 3. GROUP BY and Aggregations
    {
        auto res = engine.execute("SELECT country, COUNT(*) AS cust_count FROM customers GROUP BY country ORDER BY cust_count DESC, country ASC");
        ASSERT_EQ(res.total_rows, 3);
        // Canada: 2, USA: 2, UK: 1
        ASSERT_EQ(res.batches[0]->get_value(0, 0).get_varchar(), "Canada");
        ASSERT_EQ(res.batches[0]->get_value(1, 0).get_bigint(), 2);
        ASSERT_EQ(res.batches[0]->get_value(0, 1).get_varchar(), "USA");
        ASSERT_EQ(res.batches[0]->get_value(1, 1).get_bigint(), 2);
        ASSERT_EQ(res.batches[0]->get_value(0, 2).get_varchar(), "UK");
        ASSERT_EQ(res.batches[0]->get_value(1, 2).get_bigint(), 1);
    }

    // 4. GROUP BY with HAVING
    {
        auto res = engine.execute("SELECT c.country, SUM(o.amount) AS total_spent FROM customers c JOIN orders o ON c.id = o.customer_id GROUP BY c.country HAVING SUM(o.amount) > 300.0");
        ASSERT_EQ(res.total_rows, 1);
        ASSERT_EQ(res.batches[0]->get_value(0, 0).get_varchar(), "UK");
        ASSERT_TRUE(std::abs(res.batches[0]->get_value(1, 0).get_double() - 500.0) < 1e-4);
    }

    // 5. LIMIT and OFFSET
    {
        auto res = engine.execute("SELECT id, name FROM customers ORDER BY id ASC LIMIT 2 OFFSET 1");
        ASSERT_EQ(res.total_rows, 2);
        ASSERT_EQ(res.batches[0]->get_value(0, 0).get_bigint(), 2); // Bob
        ASSERT_EQ(res.batches[0]->get_value(0, 1).get_bigint(), 3); // Charlie
    }

    // 6. EXPLAIN
    {
        auto res = engine.execute("EXPLAIN SELECT c.name, SUM(o.amount) FROM customers c JOIN orders o ON c.id = o.customer_id GROUP BY c.name");
        ASSERT_TRUE(res.is_explain);
        ASSERT_TRUE(res.explain_text.find("LogicalJoin") != std::string::npos);
        ASSERT_TRUE(res.explain_text.find("PhysicalHashJoin") != std::string::npos);
    }
}

int main() {
    std::cout << "Running Axiom Integration Tests...\n";
    try {
        run_integration_tests();
    } catch (const axiom::AxiomException& e) {
        std::cerr << "Unhandled AxiomException: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled std::exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!\n";
        return 1;
    }
    std::cout << "\n=========================================\n";
    std::cout << "Integration Tests Finished: " << axiom::testing::g_test_count << " checks run.\n";
    if (axiom::testing::g_fail_count == 0) {
        std::cout << "ALL INTEGRATION TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << "FAILURES: " << axiom::testing::g_fail_count << " checks failed!\n";
        return 1;
    }
}
