#include "axiom/parser/lexer.h"
#include <cctype>
#include <unordered_map>

namespace axiom {

std::string token_type_name(TokenType type) {
    switch (type) {
        case TokenType::Select: return "SELECT";
        case TokenType::From: return "FROM";
        case TokenType::Where: return "WHERE";
        case TokenType::Join: return "JOIN";
        case TokenType::Inner: return "INNER";
        case TokenType::On: return "ON";
        case TokenType::Group: return "GROUP";
        case TokenType::By: return "BY";
        case TokenType::Having: return "HAVING";
        case TokenType::Order: return "ORDER";
        case TokenType::Asc: return "ASC";
        case TokenType::Desc: return "DESC";
        case TokenType::Limit: return "LIMIT";
        case TokenType::Offset: return "OFFSET";
        case TokenType::As: return "AS";
        case TokenType::And: return "AND";
        case TokenType::Or: return "OR";
        case TokenType::Not: return "NOT";
        case TokenType::Is: return "IS";
        case TokenType::Null: return "NULL";
        case TokenType::True: return "TRUE";
        case TokenType::False: return "FALSE";
        case TokenType::Count: return "COUNT";
        case TokenType::Sum: return "SUM";
        case TokenType::Avg: return "AVG";
        case TokenType::Min: return "MIN";
        case TokenType::Max: return "MAX";
        case TokenType::Explain: return "EXPLAIN";
        case TokenType::Identifier: return "Identifier";
        case TokenType::IntegerLiteral: return "IntegerLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::Equal: return "=";
        case TokenType::NotEqual: return "!=";
        case TokenType::LessThan: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::GreaterThan: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::Comma: return ",";
        case TokenType::Dot: return ".";
        case TokenType::Semicolon: return ";";
        case TokenType::LeftParen: return "(";
        case TokenType::RightParen: return ")";
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Invalid: return "INVALID";
    }
    return "UNKNOWN";
}

std::string Token::to_string() const {
    return token_type_name(type) + "('" + text + "') at " + location.to_string();
}

Lexer::Lexer(std::string sql) : sql_(std::move(sql)) {}

char Lexer::advance() {
    if (is_at_end()) return '\0';
    char c = sql_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '-' && peek_next() == '-') {
            // Line comment
            advance();
            advance();
            while (!is_at_end() && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && peek_next() == '*') {
            // Block comment
            SourceLocation start_loc{line_, col_};
            advance();
            advance();
            bool closed = false;
            while (!is_at_end()) {
                if (peek() == '*' && peek_next() == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                throw AxiomException(ErrorCategory::Lexer, "Unterminated block comment", start_loc);
            }
        } else {
            break;
        }
    }
}

TokenType Lexer::lookup_keyword(std::string_view text) {
    std::string upper;
    upper.reserve(text.size());
    for (char c : text) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"SELECT", TokenType::Select},
        {"FROM", TokenType::From},
        {"WHERE", TokenType::Where},
        {"JOIN", TokenType::Join},
        {"INNER", TokenType::Inner},
        {"ON", TokenType::On},
        {"GROUP", TokenType::Group},
        {"BY", TokenType::By},
        {"HAVING", TokenType::Having},
        {"ORDER", TokenType::Order},
        {"ASC", TokenType::Asc},
        {"DESC", TokenType::Desc},
        {"LIMIT", TokenType::Limit},
        {"OFFSET", TokenType::Offset},
        {"AS", TokenType::As},
        {"AND", TokenType::And},
        {"OR", TokenType::Or},
        {"NOT", TokenType::Not},
        {"IS", TokenType::Is},
        {"NULL", TokenType::Null},
        {"TRUE", TokenType::True},
        {"FALSE", TokenType::False},
        {"COUNT", TokenType::Count},
        {"SUM", TokenType::Sum},
        {"AVG", TokenType::Avg},
        {"MIN", TokenType::Min},
        {"MAX", TokenType::Max},
        {"EXPLAIN", TokenType::Explain}
    };

    auto it = keywords.find(upper);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenType::Identifier;
}

Token Lexer::scan_identifier_or_keyword() {
    SourceLocation loc{line_, col_};
    std::string text;
    while (!is_at_end()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            text += advance();
        } else {
            break;
        }
    }
    TokenType type = lookup_keyword(text);
    return Token{type, std::move(text), loc};
}

Token Lexer::scan_quoted_identifier() {
    SourceLocation loc{line_, col_};
    advance(); // Consume opening '"'
    std::string text;
    while (!is_at_end()) {
        char c = peek();
        if (c == '"') {
            advance();
            if (peek() == '"') {
                // Escaped double quote
                text += advance();
            } else {
                return Token{TokenType::Identifier, std::move(text), loc};
            }
        } else {
            text += advance();
        }
    }
    throw AxiomException(ErrorCategory::Lexer, "Unterminated quoted identifier", loc);
}

Token Lexer::scan_string_literal() {
    SourceLocation loc{line_, col_};
    advance(); // Consume opening '\''
    std::string text;
    while (!is_at_end()) {
        char c = peek();
        if (c == '\'') {
            advance();
            if (peek() == '\'') {
                // Escaped single quote
                text += advance();
            } else {
                return Token{TokenType::StringLiteral, std::move(text), loc};
            }
        } else {
            text += advance();
        }
    }
    throw AxiomException(ErrorCategory::Lexer, "Unterminated string literal", loc);
}

Token Lexer::scan_number() {
    SourceLocation loc{line_, col_};
    std::string text;
    bool is_float = false;

    while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }

    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        is_float = true;
        text += advance(); // '.'
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }

    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        text += advance();
        if (peek() == '+' || peek() == '-') {
            text += advance();
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            throw AxiomException(ErrorCategory::Lexer, "Malformed exponent in floating point literal", loc);
        }
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }

    return Token{is_float ? TokenType::FloatLiteral : TokenType::IntegerLiteral, std::move(text), loc};
}

Token Lexer::scan_token() {
    skip_whitespace_and_comments();
    SourceLocation loc{line_, col_};

    if (is_at_end()) {
        return Token{TokenType::EndOfFile, "", loc};
    }

    char c = peek();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scan_identifier_or_keyword();
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scan_number();
    }

    if (c == '"') {
        return scan_quoted_identifier();
    }

    if (c == '\'') {
        return scan_string_literal();
    }

    advance();
    switch (c) {
        case '+': return Token{TokenType::Plus, "+", loc};
        case '-': return Token{TokenType::Minus, "-", loc};
        case '*': return Token{TokenType::Star, "*", loc};
        case '/': return Token{TokenType::Slash, "/", loc};
        case '%': return Token{TokenType::Percent, "%", loc};
        case ',': return Token{TokenType::Comma, ",", loc};
        case '.': return Token{TokenType::Dot, ".", loc};
        case ';': return Token{TokenType::Semicolon, ";", loc};
        case '(': return Token{TokenType::LeftParen, "(", loc};
        case ')': return Token{TokenType::RightParen, ")", loc};
        case '=': return Token{TokenType::Equal, "=", loc};
        case '!':
            if (peek() == '=') {
                advance();
                return Token{TokenType::NotEqual, "!=", loc};
            }
            throw AxiomException(ErrorCategory::Lexer, "Unexpected character '!'. Did you mean '!='?", loc);
        case '<':
            if (peek() == '=') {
                advance();
                return Token{TokenType::LessEqual, "<=", loc};
            }
            if (peek() == '>') {
                advance();
                return Token{TokenType::NotEqual, "<>", loc};
            }
            return Token{TokenType::LessThan, "<", loc};
        case '>':
            if (peek() == '=') {
                advance();
                return Token{TokenType::GreaterEqual, ">=", loc};
            }
            return Token{TokenType::GreaterThan, ">", loc};
        default:
            throw AxiomException(ErrorCategory::Lexer, std::string("Unexpected character: '") + c + "'", loc);
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = scan_token();
        TokenType type = tok.type;
        tokens.push_back(std::move(tok));
        if (type == TokenType::EndOfFile) {
            break;
        }
    }
    return tokens;
}

} // namespace axiom

