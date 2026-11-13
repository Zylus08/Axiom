#include "tests/test_helpers.h"
#include "axiom/parser/lexer.h"
#include "axiom/parser/parser.h"

void run_parser_tests() {
    using namespace axiom;

    auto parse_sql = [](const std::string& sql) {
        Lexer lex(sql);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        return parser.parse_select();
    };

    // 1. Basic select
    {
        auto stmt = parse_sql("SELECT a, b AS my_b FROM tbl");
        ASSERT_EQ(stmt->select_list.size(), 2);
        ASSERT_FALSE(stmt->select_list[0].is_star);
        ASSERT_FALSE(stmt->select_list[0].alias.has_value());
        ASSERT_TRUE(stmt->select_list[1].alias.has_value());
        ASSERT_EQ(*stmt->select_list[1].alias, "my_b");
        ASSERT_TRUE(stmt->from_table != nullptr);
    }

    // 2. Star select
    {
        auto stmt = parse_sql("SELECT * FROM tbl");
        ASSERT_EQ(stmt->select_list.size(), 1);
        ASSERT_TRUE(stmt->select_list[0].is_star);
        ASSERT_FALSE(stmt->select_list[0].star_table.has_value());
    }
    {
        auto stmt = parse_sql("SELECT t.* FROM tbl t");
        ASSERT_EQ(stmt->select_list.size(), 1);
        ASSERT_TRUE(stmt->select_list[0].is_star);
        ASSERT_TRUE(stmt->select_list[0].star_table.has_value());
        ASSERT_EQ(*stmt->select_list[0].star_table, "t");
    }

    // 3. Precedence check: 1 + 2 * 3
    {
        auto stmt = parse_sql("SELECT 1 + 2 * 3");
        ASSERT_EQ(stmt->select_list.size(), 1);
        const auto* expr = stmt->select_list[0].expr.get();
        ASSERT_EQ(expr->type(), ExprType::Binary);
        const auto* b = static_cast<const BinaryExpr*>(expr);
        ASSERT_EQ(b->op(), BinaryOp::Add);
        ASSERT_EQ(b->right()->type(), ExprType::Binary);
        const auto* r = static_cast<const BinaryExpr*>(b->right());
        ASSERT_EQ(r->op(), BinaryOp::Multiply);
    }

    // 4. Parentheses override precedence: (1 + 2) * 3
    {
        auto stmt = parse_sql("SELECT (1 + 2) * 3");
        ASSERT_EQ(stmt->select_list.size(), 1);
        const auto* expr = stmt->select_list[0].expr.get();
        ASSERT_EQ(expr->type(), ExprType::Binary);
        const auto* b = static_cast<const BinaryExpr*>(expr);
        ASSERT_EQ(b->op(), BinaryOp::Multiply);
        ASSERT_EQ(b->left()->type(), ExprType::Binary);
        const auto* l = static_cast<const BinaryExpr*>(b->left());
        ASSERT_EQ(l->op(), BinaryOp::Add);
    }

    // 5. WHERE, GROUP BY, HAVING, ORDER BY, LIMIT, OFFSET
    {
        auto stmt = parse_sql("SELECT dept, COUNT(*), AVG(salary) FROM employees WHERE active = true GROUP BY dept HAVING AVG(salary) > 50000 ORDER BY dept DESC LIMIT 10 OFFSET 5");
        ASSERT_TRUE(stmt->where_clause != nullptr);
        ASSERT_EQ(stmt->group_by.size(), 1);
        ASSERT_TRUE(stmt->having_clause != nullptr);
        ASSERT_EQ(stmt->order_by.size(), 1);
        ASSERT_FALSE(stmt->order_by[0].is_asc);
        ASSERT_TRUE(stmt->limit.has_value());
        ASSERT_EQ(*stmt->limit, 10);
        ASSERT_TRUE(stmt->offset.has_value());
        ASSERT_EQ(*stmt->offset, 5);
    }

    // 6. INNER JOIN
    {
        auto stmt = parse_sql("SELECT o.id, c.name FROM orders o INNER JOIN customers c ON o.customer_id = c.id");
        ASSERT_TRUE(stmt->from_table != nullptr);
        ASSERT_EQ(stmt->from_table->type(), TableRefType::Join);
    }

    // 7. IS NULL / IS NOT NULL
    {
        auto stmt = parse_sql("SELECT a IS NULL, b IS NOT NULL FROM tbl");
        ASSERT_EQ(stmt->select_list.size(), 2);
        const auto* u1 = static_cast<const UnaryExpr*>(stmt->select_list[0].expr.get());
        ASSERT_EQ(u1->op(), UnaryOp::IsNull);
        const auto* u2 = static_cast<const UnaryExpr*>(stmt->select_list[1].expr.get());
        ASSERT_EQ(u2->op(), UnaryOp::IsNotNull);
    }

    // 8. Syntax errors fail gracefully
    ASSERT_THROWS(parse_sql(""), AxiomException);
    ASSERT_THROWS(parse_sql("SELECT"), AxiomException);
    ASSERT_THROWS(parse_sql("SELECT FROM"), AxiomException);
    ASSERT_THROWS(parse_sql("SELECT * FROM tbl GROUP BY"), AxiomException);
    ASSERT_THROWS(parse_sql("SELECT * FROM tbl LIMIT abc"), AxiomException);
}

