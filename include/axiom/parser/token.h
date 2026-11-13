#pragma once

#include "axiom/common/error.h"
#include <string>
#include <string_view>

namespace axiom {

enum class TokenType {
    // Keywords
    Select,
    From,
    Where,
    Join,
    Inner,
    On,
    Group,
    By,
    Having,
    Order,
    Asc,
    Desc,
    Limit,
    Offset,
    As,
    And,
    Or,
    Not,
    Is,
    Null,
    True,
    False,
    Count,
    Sum,
    Avg,
    Min,
    Max,
    Explain,

    // Identifiers & Literals
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,

    // Punctuation
    Comma,
    Dot,
    Semicolon,
    LeftParen,
    RightParen,

    // Special
    EndOfFile,
    Invalid
};

struct Token {
    TokenType type{TokenType::Invalid};
    std::string text;
    SourceLocation location{1, 1};

    [[nodiscard]] bool is(TokenType t) const noexcept { return type == t; }
    [[nodiscard]] std::string to_string() const;
};

std::string token_type_name(TokenType type);

} // namespace axiom
