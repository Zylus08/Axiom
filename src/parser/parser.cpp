#include "axiom/parser/parser.h"

namespace axiom {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::check(TokenType type) const noexcept {
    if (is_at_end()) return type == TokenType::EndOfFile;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    throw AxiomException(ErrorCategory::Parser, message + ", found " + peek().to_string(), peek().location);
}

std::unique_ptr<SelectStatement> Parser::parse_select() {
    auto stmt = std::make_unique<SelectStatement>();

    if (match(TokenType::Explain)) {
        stmt->is_explain = true;
    }

    consume(TokenType::Select, "Expected 'SELECT'");

    stmt->select_list = parse_select_list();

    if (match(TokenType::From)) {
        stmt->from_table = parse_table_ref();
    }

    if (match(TokenType::Where)) {
        stmt->where_clause = parse_expression();
    }

    if (match(TokenType::Group)) {
        consume(TokenType::By, "Expected 'BY' after 'GROUP'");
        do {
            stmt->group_by.push_back(parse_expression());
        } while (match(TokenType::Comma));
    }

    if (match(TokenType::Having)) {
        stmt->having_clause = parse_expression();
    }

    if (match(TokenType::Order)) {
        consume(TokenType::By, "Expected 'BY' after 'ORDER'");
        stmt->order_by = parse_order_by_list();
    }

    if (match(TokenType::Limit)) {
        const Token& tok = consume(TokenType::IntegerLiteral, "Expected integer count after 'LIMIT'");
        try {
            stmt->limit = std::stoll(tok.text);
        } catch (...) {
            throw AxiomException(ErrorCategory::Parser, "Invalid LIMIT value: " + tok.text, tok.location);
        }

        if (match(TokenType::Offset)) {
            const Token& off_tok = consume(TokenType::IntegerLiteral, "Expected integer offset after 'OFFSET'");
            try {
                stmt->offset = std::stoll(off_tok.text);
            } catch (...) {
                throw AxiomException(ErrorCategory::Parser, "Invalid OFFSET value: " + off_tok.text, off_tok.location);
            }
        }
    } else if (match(TokenType::Offset)) {
        const Token& off_tok = consume(TokenType::IntegerLiteral, "Expected integer offset after 'OFFSET'");
        try {
            stmt->offset = std::stoll(off_tok.text);
        } catch (...) {
            throw AxiomException(ErrorCategory::Parser, "Invalid OFFSET value: " + off_tok.text, off_tok.location);
        }
    }

    // Optional trailing semicolon
    match(TokenType::Semicolon);

    if (!is_at_end()) {
        throw AxiomException(ErrorCategory::Parser, "Unexpected token after statement: " + peek().to_string(), peek().location);
    }

    return stmt;
}

std::vector<SelectItem> Parser::parse_select_list() {
    std::vector<SelectItem> items;

    do {
        SelectItem item;
        // Check for *
        if (match(TokenType::Star)) {
            item.is_star = true;
            items.push_back(std::move(item));
            continue;
        }

        // Check for table.*
        if (check(TokenType::Identifier) && current_ + 1 < tokens_.size() &&
            tokens_[current_ + 1].type == TokenType::Dot &&
            current_ + 2 < tokens_.size() && tokens_[current_ + 2].type == TokenType::Star) {
            std::string tbl = advance().text; // table
            advance(); // .
            advance(); // *
            item.is_star = true;
            item.star_table = std::move(tbl);
            items.push_back(std::move(item));
            continue;
        }

        item.expr = parse_expression();

        if (match(TokenType::As)) {
            const Token& alias_tok = consume(TokenType::Identifier, "Expected alias identifier after 'AS'");
            item.alias = alias_tok.text;
        } else if (check(TokenType::Identifier) && !check(TokenType::From)) {
            item.alias = advance().text;
        }

        items.push_back(std::move(item));
    } while (match(TokenType::Comma));

    return items;
}

std::unique_ptr<TableRef> Parser::parse_table_ref() {
    std::unique_ptr<TableRef> current = parse_base_table_ref();

    while (check(TokenType::Join) || (check(TokenType::Inner) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::Join)) {
        SourceLocation loc = peek().location;
        if (match(TokenType::Inner)) {
            consume(TokenType::Join, "Expected 'JOIN' after 'INNER'");
        } else {
            advance(); // Consume 'JOIN'
        }

        std::unique_ptr<TableRef> right = parse_base_table_ref();
        consume(TokenType::On, "Expected 'ON' condition for JOIN");
        std::unique_ptr<Expr> on_cond = parse_expression();

        current = std::make_unique<JoinTableRef>(JoinType::Inner, std::move(current), std::move(right), std::move(on_cond), loc);
    }

    return current;
}

std::unique_ptr<TableRef> Parser::parse_base_table_ref() {
    const Token& name_tok = consume(TokenType::Identifier, "Expected table name in FROM clause");
    std::optional<std::string> alias;

    if (match(TokenType::As)) {
        const Token& alias_tok = consume(TokenType::Identifier, "Expected alias identifier after 'AS'");
        alias = alias_tok.text;
    } else if (check(TokenType::Identifier) &&
               !check(TokenType::Join) &&
               !check(TokenType::Inner) &&
               !check(TokenType::On) &&
               !check(TokenType::Where) &&
               !check(TokenType::Group) &&
               !check(TokenType::Having) &&
               !check(TokenType::Order) &&
               !check(TokenType::Limit) &&
               !check(TokenType::Offset)) {
        alias = advance().text;
    }

    return std::make_unique<BaseTableRef>(name_tok.text, std::move(alias), name_tok.location);
}

std::vector<OrderItem> Parser::parse_order_by_list() {
    std::vector<OrderItem> items;
    do {
        OrderItem item;
        item.expr = parse_expression();
        if (match(TokenType::Asc)) {
            item.is_asc = true;
        } else if (match(TokenType::Desc)) {
            item.is_asc = false;
        } else {
            item.is_asc = true;
        }
        items.push_back(std::move(item));
    } while (match(TokenType::Comma));
    return items;
}

std::unique_ptr<Expr> Parser::parse_expression() {
    return parse_or();
}

std::unique_ptr<Expr> Parser::parse_or() {
    auto expr = parse_and();
    while (match(TokenType::Or)) {
        SourceLocation loc = previous().location;
        auto right = parse_and();
        expr = std::make_unique<BinaryExpr>(BinaryOp::Or, std::move(expr), std::move(right), loc);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_and() {
    auto expr = parse_not();
    while (match(TokenType::And)) {
        SourceLocation loc = previous().location;
        auto right = parse_not();
        expr = std::make_unique<BinaryExpr>(BinaryOp::And, std::move(expr), std::move(right), loc);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_not() {
    if (match(TokenType::Not)) {
        SourceLocation loc = previous().location;
        auto operand = parse_not();
        return std::make_unique<UnaryExpr>(UnaryOp::Not, std::move(operand), loc);
    }
    return parse_comparison();
}

std::unique_ptr<Expr> Parser::parse_comparison() {
    auto expr = parse_additive();

    while (true) {
        if (match({TokenType::Equal, TokenType::NotEqual, TokenType::LessThan,
                   TokenType::LessEqual, TokenType::GreaterThan, TokenType::GreaterEqual})) {
            TokenType op_type = previous().type;
            SourceLocation loc = previous().location;
            auto right = parse_additive();
            BinaryOp bop;
            switch (op_type) {
                case TokenType::Equal: bop = BinaryOp::Equal; break;
                case TokenType::NotEqual: bop = BinaryOp::NotEqual; break;
                case TokenType::LessThan: bop = BinaryOp::LessThan; break;
                case TokenType::LessEqual: bop = BinaryOp::LessEqual; break;
                case TokenType::GreaterThan: bop = BinaryOp::GreaterThan; break;
                case TokenType::GreaterEqual: bop = BinaryOp::GreaterEqual; break;
                default: bop = BinaryOp::Equal; break;
            }
            expr = std::make_unique<BinaryExpr>(bop, std::move(expr), std::move(right), loc);
        } else if (match(TokenType::Is)) {
            SourceLocation loc = previous().location;
            if (match(TokenType::Not)) {
                consume(TokenType::Null, "Expected 'NULL' after 'IS NOT'");
                expr = std::make_unique<UnaryExpr>(UnaryOp::IsNotNull, std::move(expr), loc);
            } else {
                consume(TokenType::Null, "Expected 'NULL' after 'IS'");
                expr = std::make_unique<UnaryExpr>(UnaryOp::IsNull, std::move(expr), loc);
            }
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_additive() {
    auto expr = parse_multiplicative();

    while (match({TokenType::Plus, TokenType::Minus})) {
        TokenType op_type = previous().type;
        SourceLocation loc = previous().location;
        auto right = parse_multiplicative();
        BinaryOp bop = (op_type == TokenType::Plus) ? BinaryOp::Add : BinaryOp::Subtract;
        expr = std::make_unique<BinaryExpr>(bop, std::move(expr), std::move(right), loc);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_multiplicative() {
    auto expr = parse_unary();

    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        TokenType op_type = previous().type;
        SourceLocation loc = previous().location;
        auto right = parse_unary();
        BinaryOp bop;
        if (op_type == TokenType::Star) bop = BinaryOp::Multiply;
        else if (op_type == TokenType::Slash) bop = BinaryOp::Divide;
        else bop = BinaryOp::Modulo;
        expr = std::make_unique<BinaryExpr>(bop, std::move(expr), std::move(right), loc);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (match(TokenType::Minus)) {
        SourceLocation loc = previous().location;
        auto operand = parse_unary();
        return std::make_unique<UnaryExpr>(UnaryOp::Minus, std::move(operand), loc);
    }
    if (match(TokenType::Plus)) {
        SourceLocation loc = previous().location;
        auto operand = parse_unary();
        return std::make_unique<UnaryExpr>(UnaryOp::Plus, std::move(operand), loc);
    }
    return parse_primary();
}

std::unique_ptr<Expr> Parser::parse_aggregate(TokenType type) {
    SourceLocation loc = previous().location;
    consume(TokenType::LeftParen, "Expected '(' after aggregate function");

    if (type == TokenType::Count && match(TokenType::Star)) {
        consume(TokenType::RightParen, "Expected ')' after 'COUNT(*)'");
        return std::make_unique<AggregateExpr>(AggregateFunc::CountStar, nullptr, loc);
    }

    auto arg = parse_expression();
    consume(TokenType::RightParen, "Expected ')' after aggregate function argument");

    AggregateFunc func;
    switch (type) {
        case TokenType::Count: func = AggregateFunc::Count; break;
        case TokenType::Sum: func = AggregateFunc::Sum; break;
        case TokenType::Avg: func = AggregateFunc::Avg; break;
        case TokenType::Min: func = AggregateFunc::Min; break;
        case TokenType::Max: func = AggregateFunc::Max; break;
        default: func = AggregateFunc::Count; break;
    }

    return std::make_unique<AggregateExpr>(func, std::move(arg), loc);
}

std::unique_ptr<Expr> Parser::parse_primary() {
    SourceLocation loc = peek().location;

    if (match(TokenType::IntegerLiteral)) {
        try {
            int64_t val = std::stoll(previous().text);
            return std::make_unique<LiteralExpr>(Value::make_bigint(val), loc);
        } catch (...) {
            throw AxiomException(ErrorCategory::Parser, "Integer literal out of range: " + previous().text, loc);
        }
    }

    if (match(TokenType::FloatLiteral)) {
        try {
            double val = std::stod(previous().text);
            return std::make_unique<LiteralExpr>(Value::make_double(val), loc);
        } catch (...) {
            throw AxiomException(ErrorCategory::Parser, "Float literal out of range: " + previous().text, loc);
        }
    }

    if (match(TokenType::StringLiteral)) {
        return std::make_unique<LiteralExpr>(Value::make_varchar(previous().text), loc);
    }

    if (match(TokenType::True)) {
        return std::make_unique<LiteralExpr>(Value::make_bool(true), loc);
    }

    if (match(TokenType::False)) {
        return std::make_unique<LiteralExpr>(Value::make_bool(false), loc);
    }

    if (match(TokenType::Null)) {
        return std::make_unique<LiteralExpr>(Value::make_null(), loc);
    }

    if (match({TokenType::Count, TokenType::Sum, TokenType::Avg, TokenType::Min, TokenType::Max})) {
        return parse_aggregate(previous().type);
    }

    if (match(TokenType::Identifier)) {
        std::string name = previous().text;
        if (match(TokenType::Dot)) {
            // Qualified: table.column
            const Token& col_tok = consume(TokenType::Identifier, "Expected column name after '.'");
            return std::make_unique<ColumnRefExpr>(name, col_tok.text, loc);
        }
        return std::make_unique<ColumnRefExpr>(std::nullopt, std::move(name), loc);
    }

    if (match(TokenType::LeftParen)) {
        auto expr = parse_expression();
        consume(TokenType::RightParen, "Expected ')' to close parenthesized expression");
        return expr;
    }

    throw AxiomException(ErrorCategory::Parser, "Unexpected token in expression: " + peek().to_string(), loc);
}

} // namespace axiom
