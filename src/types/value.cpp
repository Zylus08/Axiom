#include "axiom/types/value.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <functional>

namespace axiom {

std::string type_kind_to_string(TypeKind kind) {
    switch (kind) {
        case TypeKind::Null: return "NULL";
        case TypeKind::Boolean: return "BOOLEAN";
        case TypeKind::BigInt: return "BIGINT";
        case TypeKind::Double: return "DOUBLE";
        case TypeKind::VarChar: return "VARCHAR";
    }
    return "UNKNOWN";
}

std::optional<TypeKind> parse_type_kind(std::string_view str) {
    std::string s;
    for (char c : str) {
        s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (s == "INT" || s == "INTEGER" || s == "BIGINT" || s == "INT64" || s == "INT8") {
        return TypeKind::BigInt;
    }
    if (s == "DOUBLE" || s == "FLOAT" || s == "REAL" || s == "FLOAT64" || s == "FLOAT8") {
        return TypeKind::Double;
    }
    if (s == "VARCHAR" || s == "TEXT" || s == "STRING" || s == "CHAR") {
        return TypeKind::VarChar;
    }
    if (s == "BOOL" || s == "BOOLEAN") {
        return TypeKind::Boolean;
    }
    if (s == "NULL") {
        return TypeKind::Null;
    }
    return std::nullopt;
}

std::string Value::to_string() const {
    if (is_null()) {
        return "NULL";
    }
    switch (kind_) {
        case TypeKind::Boolean:
            return std::get<bool>(data_) ? "true" : "false";
        case TypeKind::BigInt:
            return std::to_string(std::get<int64_t>(data_));
        case TypeKind::Double: {
            double d = std::get<double>(data_);
            std::ostringstream oss;
            oss << std::setprecision(12) << d;
            std::string s = oss.str();
            // Ensure float formatting is readable
            return s;
        }
        case TypeKind::VarChar:
            return std::get<std::string>(data_);
        case TypeKind::Null:
            return "NULL";
    }
    return "";
}

Value Value::cast_to(TypeKind target_type) const {
    if (is_null()) {
        return Value::make_null(target_type);
    }
    if (kind_ == target_type) {
        return *this;
    }

    switch (target_type) {
        case TypeKind::Boolean: {
            if (kind_ == TypeKind::BigInt) {
                return Value::make_bool(std::get<int64_t>(data_) != 0);
            }
            if (kind_ == TypeKind::Double) {
                return Value::make_bool(std::get<double>(data_) != 0.0);
            }
            if (kind_ == TypeKind::VarChar) {
                const auto& s = std::get<std::string>(data_);
                std::string lower;
                for (char c : s) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == "true" || lower == "t" || lower == "1") return Value::make_bool(true);
                if (lower == "false" || lower == "f" || lower == "0") return Value::make_bool(false);
                throw AxiomException(ErrorCategory::Execution, "Cannot cast string '" + s + "' to BOOLEAN");
            }
            break;
        }
        case TypeKind::BigInt: {
            if (kind_ == TypeKind::Boolean) {
                return Value::make_bigint(std::get<bool>(data_) ? 1 : 0);
            }
            if (kind_ == TypeKind::Double) {
                return Value::make_bigint(static_cast<int64_t>(std::get<double>(data_)));
            }
            if (kind_ == TypeKind::VarChar) {
                try {
                    size_t idx = 0;
                    const auto& s = std::get<std::string>(data_);
                    int64_t v = std::stoll(s, &idx);
                    return Value::make_bigint(v);
                } catch (...) {
                    throw AxiomException(ErrorCategory::Execution, "Cannot cast string '" + std::get<std::string>(data_) + "' to BIGINT");
                }
            }
            break;
        }
        case TypeKind::Double: {
            if (kind_ == TypeKind::BigInt) {
                return Value::make_double(static_cast<double>(std::get<int64_t>(data_)));
            }
            if (kind_ == TypeKind::Boolean) {
                return Value::make_double(std::get<bool>(data_) ? 1.0 : 0.0);
            }
            if (kind_ == TypeKind::VarChar) {
                try {
                    size_t idx = 0;
                    const auto& s = std::get<std::string>(data_);
                    double v = std::stod(s, &idx);
                    return Value::make_double(v);
                } catch (...) {
                    throw AxiomException(ErrorCategory::Execution, "Cannot cast string '" + std::get<std::string>(data_) + "' to DOUBLE");
                }
            }
            break;
        }
        case TypeKind::VarChar:
            return Value::make_varchar(to_string());
        case TypeKind::Null:
            return Value::make_null(TypeKind::Null);
    }
    throw AxiomException(ErrorCategory::Execution, "Unsupported cast from " + type_kind_to_string(kind_) + " to " + type_kind_to_string(target_type));
}

std::optional<int> Value::compare(const Value& other) const {
    if (is_null() || other.is_null()) {
        return std::nullopt;
    }

    if (kind_ == other.kind_) {
        switch (kind_) {
            case TypeKind::Boolean: {
                bool a = std::get<bool>(data_);
                bool b = std::get<bool>(other.data_);
                if (a == b) return 0;
                return a ? 1 : -1;
            }
            case TypeKind::BigInt: {
                int64_t a = std::get<int64_t>(data_);
                int64_t b = std::get<int64_t>(other.data_);
                if (a == b) return 0;
                return a < b ? -1 : 1;
            }
            case TypeKind::Double: {
                double a = std::get<double>(data_);
                double b = std::get<double>(other.data_);
                if (a == b) return 0;
                return a < b ? -1 : 1;
            }
            case TypeKind::VarChar: {
                const auto& a = std::get<std::string>(data_);
                const auto& b = std::get<std::string>(other.data_);
                int cmp = a.compare(b);
                if (cmp == 0) return 0;
                return cmp < 0 ? -1 : 1;
            }
            default:
                return 0;
        }
    }

    // Coercion between BigInt and Double
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::Double) {
        double a = static_cast<double>(std::get<int64_t>(data_));
        double b = std::get<double>(other.data_);
        if (a == b) return 0;
        return a < b ? -1 : 1;
    }
    if (kind_ == TypeKind::Double && other.kind_ == TypeKind::BigInt) {
        double a = std::get<double>(data_);
        double b = static_cast<double>(std::get<int64_t>(other.data_));
        if (a == b) return 0;
        return a < b ? -1 : 1;
    }

    throw AxiomException(ErrorCategory::Execution, "Cannot compare incompatible types: " + type_kind_to_string(kind_) + " and " + type_kind_to_string(other.kind_));
}

Value Value::sql_equals(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp == 0);
}

Value Value::sql_not_equals(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp != 0);
}

Value Value::sql_less_than(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp < 0);
}

Value Value::sql_less_than_or_equal(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp <= 0);
}

Value Value::sql_greater_than(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp > 0);
}

Value Value::sql_greater_than_or_equal(const Value& other) const {
    auto cmp = compare(other);
    if (!cmp.has_value()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(*cmp >= 0);
}

Value Value::sql_and(const Value& other) const {
    // 3-valued AND:
    // FALSE AND anything = FALSE
    // TRUE AND TRUE = TRUE
    // TRUE AND NULL = NULL
    // NULL AND NULL = NULL
    bool this_null = is_null();
    bool other_null = other.is_null();

    if (!this_null && !std::get<bool>(data_)) return Value::make_bool(false);
    if (!other_null && !std::get<bool>(other.data_)) return Value::make_bool(false);

    if (this_null || other_null) return Value::make_null(TypeKind::Boolean);

    return Value::make_bool(std::get<bool>(data_) && std::get<bool>(other.data_));
}

Value Value::sql_or(const Value& other) const {
    // 3-valued OR:
    // TRUE OR anything = TRUE
    // FALSE OR FALSE = FALSE
    // FALSE OR NULL = NULL
    // NULL OR NULL = NULL
    bool this_null = is_null();
    bool other_null = other.is_null();

    if (!this_null && std::get<bool>(data_)) return Value::make_bool(true);
    if (!other_null && std::get<bool>(other.data_)) return Value::make_bool(true);

    if (this_null || other_null) return Value::make_null(TypeKind::Boolean);

    return Value::make_bool(std::get<bool>(data_) || std::get<bool>(other.data_));
}

Value Value::sql_not() const {
    if (is_null()) return Value::make_null(TypeKind::Boolean);
    return Value::make_bool(!std::get<bool>(data_));
}

Value Value::add(const Value& other) const {
    if (is_null() || other.is_null()) {
        TypeKind res_kind = (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) ? TypeKind::Double : TypeKind::BigInt;
        return Value::make_null(res_kind);
    }
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::BigInt) {
        return Value::make_bigint(std::get<int64_t>(data_) + std::get<int64_t>(other.data_));
    }
    if (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) {
        return Value::make_double(get_double() + other.get_double());
    }
    throw AxiomException(ErrorCategory::Execution, "Cannot add " + type_kind_to_string(kind_) + " and " + type_kind_to_string(other.kind_));
}

Value Value::subtract(const Value& other) const {
    if (is_null() || other.is_null()) {
        TypeKind res_kind = (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) ? TypeKind::Double : TypeKind::BigInt;
        return Value::make_null(res_kind);
    }
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::BigInt) {
        return Value::make_bigint(std::get<int64_t>(data_) - std::get<int64_t>(other.data_));
    }
    if (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) {
        return Value::make_double(get_double() - other.get_double());
    }
    throw AxiomException(ErrorCategory::Execution, "Cannot subtract " + type_kind_to_string(kind_) + " and " + type_kind_to_string(other.kind_));
}

Value Value::multiply(const Value& other) const {
    if (is_null() || other.is_null()) {
        TypeKind res_kind = (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) ? TypeKind::Double : TypeKind::BigInt;
        return Value::make_null(res_kind);
    }
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::BigInt) {
        return Value::make_bigint(std::get<int64_t>(data_) * std::get<int64_t>(other.data_));
    }
    if (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) {
        return Value::make_double(get_double() * other.get_double());
    }
    throw AxiomException(ErrorCategory::Execution, "Cannot multiply " + type_kind_to_string(kind_) + " and " + type_kind_to_string(other.kind_));
}

Value Value::divide(const Value& other) const {
    if (is_null() || other.is_null()) {
        TypeKind res_kind = (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) ? TypeKind::Double : TypeKind::BigInt;
        return Value::make_null(res_kind);
    }
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::BigInt) {
        int64_t divisor = std::get<int64_t>(other.data_);
        if (divisor == 0) {
            throw AxiomException(ErrorCategory::Execution, "Division by zero");
        }
        return Value::make_bigint(std::get<int64_t>(data_) / divisor);
    }
    if (kind_ == TypeKind::Double || other.kind_ == TypeKind::Double) {
        double divisor = other.get_double();
        if (divisor == 0.0) {
            throw AxiomException(ErrorCategory::Execution, "Division by zero");
        }
        return Value::make_double(get_double() / divisor);
    }
    throw AxiomException(ErrorCategory::Execution, "Cannot divide " + type_kind_to_string(kind_) + " and " + type_kind_to_string(other.kind_));
}

Value Value::modulo(const Value& other) const {
    if (is_null() || other.is_null()) {
        return Value::make_null(TypeKind::BigInt);
    }
    if (kind_ == TypeKind::BigInt && other.kind_ == TypeKind::BigInt) {
        int64_t divisor = std::get<int64_t>(other.data_);
        if (divisor == 0) {
            throw AxiomException(ErrorCategory::Execution, "Division by zero");
        }
        return Value::make_bigint(std::get<int64_t>(data_) % divisor);
    }
    throw AxiomException(ErrorCategory::Execution, "Modulo operator requires integer operands");
}

Value Value::negate() const {
    if (is_null()) {
        return *this;
    }
    if (kind_ == TypeKind::BigInt) {
        return Value::make_bigint(-std::get<int64_t>(data_));
    }
    if (kind_ == TypeKind::Double) {
        return Value::make_double(-std::get<double>(data_));
    }
    throw AxiomException(ErrorCategory::Execution, "Cannot negate non-numeric type " + type_kind_to_string(kind_));
}

bool Value::sort_less_than(const Value& other, bool nulls_first) const {
    bool this_null = is_null();
    bool other_null = other.is_null();
    if (this_null && other_null) return false;
    if (this_null) return nulls_first;
    if (other_null) return !nulls_first;

    auto cmp = compare(other);
    if (!cmp.has_value()) return false;
    return *cmp < 0;
}

size_t Value::hash() const {
    if (is_null()) {
        return 0xdeadbeef;
    }
    switch (kind_) {
        case TypeKind::Boolean:
            return std::hash<bool>{}(std::get<bool>(data_));
        case TypeKind::BigInt:
            return std::hash<int64_t>{}(std::get<int64_t>(data_));
        case TypeKind::Double: {
            double d = std::get<double>(data_);
            if (d == 0.0) d = 0.0; // normalize -0.0
            return std::hash<double>{}(d);
        }
        case TypeKind::VarChar:
            return std::hash<std::string>{}(std::get<std::string>(data_));
        default:
            return 0;
    }
}

bool Value::exact_equal(const Value& other) const {
    if (is_null() && other.is_null()) return true;
    if (is_null() || other.is_null()) return false;
    auto cmp = compare(other);
    return cmp.has_value() && *cmp == 0;
}

} // namespace axiom
