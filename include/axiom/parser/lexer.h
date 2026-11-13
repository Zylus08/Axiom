#pragma once

#include "axiom/parser/token.h"
#include <string>
#include <vector>
#include <string_view>

namespace axiom {

class Lexer {
public:
    explicit Lexer(std::string sql);

    [[nodiscard]] std::vector<Token> tokenize();

private:
    std::string sql_;
    size_t pos_{0};
    size_t line_{1};
    size_t col_{1};

    [[nodiscard]] bool is_at_end() const noexcept { return pos_ >= sql_.size(); }
    [[nodiscard]] char peek() const noexcept { return is_at_end() ? '\0' : sql_[pos_]; }
    [[nodiscard]] char peek_next() const noexcept { return (pos_ + 1 >= sql_.size()) ? '\0' : sql_[pos_ + 1]; }
    char advance();

    void skip_whitespace_and_comments();
    Token scan_token();
    Token scan_identifier_or_keyword();
    Token scan_quoted_identifier();
    Token scan_string_literal();
    Token scan_number();

    static TokenType lookup_keyword(std::string_view text);
};

} // namespace axiom

