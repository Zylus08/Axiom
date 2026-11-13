#pragma once

#include <string>
#include <stdexcept>
#include <string_view>
#include <sstream>

namespace axiom {

enum class ErrorCategory {
    Lexer,
    Parser,
    Binder,
    Planner,
    Execution,
    Storage,
    Internal
};

struct SourceLocation {
    size_t line{1};
    size_t column{1};

    [[nodiscard]] std::string to_string() const {
        return "line " + std::to_string(line) + ", column " + std::to_string(column);
    }
};

class AxiomException : public std::runtime_error {
public:
    AxiomException(ErrorCategory category, std::string message, SourceLocation loc = {})
        : std::runtime_error(format_message(category, message, loc)),
          category_(category),
          raw_message_(std::move(message)),
          location_(loc) {}

    [[nodiscard]] ErrorCategory category() const noexcept { return category_; }
    [[nodiscard]] const std::string& raw_message() const noexcept { return raw_message_; }
    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }

    static std::string category_name(ErrorCategory cat) {
        switch (cat) {
            case ErrorCategory::Lexer: return "Lexer Error";
            case ErrorCategory::Parser: return "Syntax Error";
            case ErrorCategory::Binder: return "Semantic Error";
            case ErrorCategory::Planner: return "Planning Error";
            case ErrorCategory::Execution: return "Execution Error";
            case ErrorCategory::Storage: return "Storage Error";
            case ErrorCategory::Internal: return "Internal Error";
        }
        return "Error";
    }

private:
    ErrorCategory category_;
    std::string raw_message_;
    SourceLocation location_;

    static std::string format_message(ErrorCategory cat, const std::string& msg, const SourceLocation& loc) {
        std::ostringstream oss;
        oss << category_name(cat);
        if (loc.line > 0 && loc.column > 0) {
            oss << " at " << loc.to_string();
        }
        oss << ": " << msg;
        return oss.str();
    }
};

} // namespace axiom
