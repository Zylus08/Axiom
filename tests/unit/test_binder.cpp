#include "tests/test_helpers.h"
#include "axiom/catalog/catalog.h"
#include "axiom/parser/lexer.h"
#include "axiom/parser/parser.h"
#include "axiom/binder/binder.h"

void run_binder_tests() {
    using namespace axiom;

    Catalog catalog;
    Schema t1_schema({
        {"id", TypeKind::BigInt},
        {"val", TypeKind::Double},
        {"name", TypeKind::VarChar}
    });
    Schema t2_schema({
        {"id", TypeKind::BigInt},
        {"t1_id", TypeKind::BigInt},
        {"score", TypeKind::Double}
    });

    catalog.register_table("t1", t1_schema, "t1.csv");
    catalog.register_table("t2", t2_schema, "t2.csv");

    auto bind_sql = [&](const std::string& sql) {
        Lexer lex(sql);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto ast = parser.parse_select();
        Binder binder(catalog);
        return binder.bind_select(*ast);
    };

    // 1. Valid binding
    {
        auto bound = bind_sql("SELECT t1.id, name, score FROM t1 JOIN t2 ON t1.id = t2.t1_id WHERE score > 50.0");
        ASSERT_EQ(bound->select_list.size(), 3);
        ASSERT_TRUE(bound->where_clause != nullptr);
    }

    // 2. Ambiguous column error
    // Both t1 and t2 have 'id'
    ASSERT_THROWS(bind_sql("SELECT id FROM t1 JOIN t2 ON t1.id = t2.t1_id"), AxiomException);

    // 3. Column not found error
    ASSERT_THROWS(bind_sql("SELECT non_existent_col FROM t1"), AxiomException);

    // 4. Unknown table error
    ASSERT_THROWS(bind_sql("SELECT * FROM non_existent_table"), AxiomException);

    // 5. Aggregate in WHERE clause rejected
    ASSERT_THROWS(bind_sql("SELECT id FROM t1 WHERE SUM(val) > 10"), AxiomException);

    // 6. Nested aggregates rejected
    ASSERT_THROWS(bind_sql("SELECT SUM(AVG(val)) FROM t1"), AxiomException);

    // 7. Non-aggregate column in SELECT without GROUP BY rejected
    ASSERT_THROWS(bind_sql("SELECT id, SUM(val) FROM t1"), AxiomException);

    // 8. Non-aggregate column not in GROUP BY rejected
    ASSERT_THROWS(bind_sql("SELECT id, name, SUM(val) FROM t1 GROUP BY id"), AxiomException);

    // 9. Valid GROUP BY query
    {
        auto bound = bind_sql("SELECT id, SUM(val) AS total FROM t1 GROUP BY id");
        ASSERT_EQ(bound->group_by.size(), 1);
        ASSERT_EQ(bound->aggregates.size(), 1);
    }
}
