#pragma once

#include "axiom/parser/token.h"
#include "axiom/parser/ast.h"
#include <vector>
#include <memory>

namespace axiom {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    [[nodiscard]] std::unique_ptr<SelectStatement> parse_select();

private:
    std::vector<Token> tokens_;
    size_t current_{0};

    [[nodiscard]] bool is_at_end() const noexcept { return peek().type == TokenType::EndOfFile; }
    [[nodiscard]] const Token& peek() const noexcept { return tokens_[current_]; }
    [[nodiscard]] const Token& previous() const noexcept { return tokens_[current_ - 1]; }
    const Token& advance();
    bool check(TokenType type) const noexcept;
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);

    // Expression parsing (Pratt / Precedence Climbing)
    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_or();
    std::unique_ptr<Expr> parse_and();
    std::unique_ptr<Expr> parse_not();
    std::unique_ptr<Expr> parse_comparison();
    std::unique_ptr<Expr> parse_additive();
    std::unique_ptr<Expr> parse_multiplicative();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_aggregate(TokenType type);

    // Statement / Clause parsing
    std::vector<SelectItem> parse_select_list();
    std::unique_ptr<TableRef> parse_table_ref();
    std::unique_ptr<TableRef> parse_base_table_ref();
    std::vector<OrderItem> parse_order_by_list();
};

} // namespace axiom

