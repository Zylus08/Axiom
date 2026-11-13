#include "tests/test_helpers.h"
#include "axiom/parser/lexer.h"

void run_lexer_tests() {
    using namespace axiom;

    // 1. Basic keywords and identifiers
    {
        Lexer lex("SELECT id, name FROM users WHERE age >= 21;");
        auto tokens = lex.tokenize();
        ASSERT_EQ(tokens.size(), 12);
        ASSERT_TRUE(tokens[0].is(TokenType::Select));
        ASSERT_TRUE(tokens[1].is(TokenType::Identifier));
        ASSERT_EQ(tokens[1].text, "id");
        ASSERT_TRUE(tokens[2].is(TokenType::Comma));
        ASSERT_TRUE(tokens[3].is(TokenType::Identifier));
        ASSERT_EQ(tokens[3].text, "name");
        ASSERT_TRUE(tokens[4].is(TokenType::From));
        ASSERT_TRUE(tokens[5].is(TokenType::Identifier));
        ASSERT_EQ(tokens[5].text, "users");
        ASSERT_TRUE(tokens[6].is(TokenType::Where));
        ASSERT_TRUE(tokens[7].is(TokenType::Identifier));
        ASSERT_EQ(tokens[7].text, "age");
        ASSERT_TRUE(tokens[8].is(TokenType::GreaterEqual));
        ASSERT_TRUE(tokens[9].is(TokenType::IntegerLiteral));
        ASSERT_EQ(tokens[9].text, "21");
        ASSERT_TRUE(tokens[10].is(TokenType::Semicolon));
        ASSERT_TRUE(tokens[11].is(TokenType::EndOfFile));
    }

    // 2. Case insensitivity of keywords
    {
        Lexer lex("select FROM where Group By HAVING ORDER BY LIMIT OFFSET");
        auto tokens = lex.tokenize();
        ASSERT_TRUE(tokens[0].is(TokenType::Select));
        ASSERT_TRUE(tokens[1].is(TokenType::From));
        ASSERT_TRUE(tokens[2].is(TokenType::Where));
        ASSERT_TRUE(tokens[3].is(TokenType::Group));
        ASSERT_TRUE(tokens[4].is(TokenType::By));
        ASSERT_TRUE(tokens[5].is(TokenType::Having));
        ASSERT_TRUE(tokens[6].is(TokenType::Order));
        ASSERT_TRUE(tokens[7].is(TokenType::By));
        ASSERT_TRUE(tokens[8].is(TokenType::Limit));
        ASSERT_TRUE(tokens[9].is(TokenType::Offset));
    }

    // 3. String literals and quotes escaping
    {
        Lexer lex("'hello world' 'it''s awesome'");
        auto tokens = lex.tokenize();
        ASSERT_TRUE(tokens[0].is(TokenType::StringLiteral));
        ASSERT_EQ(tokens[0].text, "hello world");
        ASSERT_TRUE(tokens[1].is(TokenType::StringLiteral));
        ASSERT_EQ(tokens[1].text, "it's awesome");
    }

    // 4. Quoted identifiers
    {
        Lexer lex("\"col with spaces\" \"table\"\"name\"");
        auto tokens = lex.tokenize();
        ASSERT_TRUE(tokens[0].is(TokenType::Identifier));
        ASSERT_EQ(tokens[0].text, "col with spaces");
        ASSERT_TRUE(tokens[1].is(TokenType::Identifier));
        ASSERT_EQ(tokens[1].text, "table\"name");
    }

    // 5. Numeric literals
    {
        Lexer lex("123 45.67 1e5 2.5E-3");
        auto tokens = lex.tokenize();
        ASSERT_TRUE(tokens[0].is(TokenType::IntegerLiteral));
        ASSERT_EQ(tokens[0].text, "123");
        ASSERT_TRUE(tokens[1].is(TokenType::FloatLiteral));
        ASSERT_EQ(tokens[1].text, "45.67");
        ASSERT_TRUE(tokens[2].is(TokenType::FloatLiteral));
        ASSERT_EQ(tokens[2].text, "1e5");
        ASSERT_TRUE(tokens[3].is(TokenType::FloatLiteral));
        ASSERT_EQ(tokens[3].text, "2.5E-3");
    }

    // 6. Comments
    {
        Lexer lex("SELECT -- this is a line comment\n 42 /* block comment */ + 8");
        auto tokens = lex.tokenize();
        ASSERT_TRUE(tokens[0].is(TokenType::Select));
        ASSERT_TRUE(tokens[1].is(TokenType::IntegerLiteral));
        ASSERT_EQ(tokens[1].text, "42");
        ASSERT_TRUE(tokens[2].is(TokenType::Plus));
        ASSERT_TRUE(tokens[3].is(TokenType::IntegerLiteral));
        ASSERT_EQ(tokens[3].text, "8");
    }

    // 7. Error handling
    {
        Lexer lex("'unclosed string");
        ASSERT_THROWS(lex.tokenize(), AxiomException);
    }
    {
        Lexer lex("\"unclosed identifier");
        ASSERT_THROWS(lex.tokenize(), AxiomException);
    }
    {
        Lexer lex("/* unclosed comment");
        ASSERT_THROWS(lex.tokenize(), AxiomException);
    }
}
