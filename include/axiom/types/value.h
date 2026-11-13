#pragma once

#include "axiom/common/error.h"
#include <string>
#include <variant>
#include <cstdint>
#include <optional>
#include <iostream>
#include <cmath>

namespace axiom {

enum class TypeKind {
    Null,
    Boolean,
    BigInt,
    Double,
    VarChar
};

std::string type_kind_to_string(TypeKind kind);
std::optional<TypeKind> parse_type_kind(std::string_view str);

class Value {
public:
    Value() : kind_(TypeKind::Null), data_(std::monostate{}) {}

    static Value make_null(TypeKind kind = TypeKind::Null) {
        Value v;
        v.kind_ = kind;
        v.data_ = std::monostate{};
        return v;
    }

    static Value make_bool(bool b) {
        Value v;
        v.kind_ = TypeKind::Boolean;
        v.data_ = b;
        return v;
    }

    static Value make_bigint(int64_t val) {
        Value v;
        v.kind_ = TypeKind::BigInt;
        v.data_ = val;
        return v;
    }

    static Value make_double(double val) {
        Value v;
        v.kind_ = TypeKind::Double;
        v.data_ = val;
        return v;
    }

    static Value make_varchar(std::string val) {
        Value v;
        v.kind_ = TypeKind::VarChar;
        v.data_ = std::move(val);
        return v;
    }

    [[nodiscard]] TypeKind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data_); }

    [[nodiscard]] bool get_bool() const {
        check_not_null();
        return std::get<bool>(data_);
    }

    [[nodiscard]] int64_t get_bigint() const {
        check_not_null();
        return std::get<int64_t>(data_);
    }

    [[nodiscard]] double get_double() const {
        check_not_null();
        if (kind_ == TypeKind::BigInt) {
            return static_cast<double>(std::get<int64_t>(data_));
        }
        return std::get<double>(data_);
    }

    [[nodiscard]] const std::string& get_varchar() const {
        check_not_null();
        return std::get<std::string>(data_);
    }

    [[nodiscard]] std::string to_string() const;

    // Coercion / Casting
    [[nodiscard]] Value cast_to(TypeKind target_type) const;

    // Comparisons
    // Returns std::nullopt if either value is NULL (SQL 3-valued comparison)
    [[nodiscard]] std::optional<int> compare(const Value& other) const;

    // SQL equality (=) returning Value (BOOLEAN or NULL)
    [[nodiscard]] Value sql_equals(const Value& other) const;
    [[nodiscard]] Value sql_not_equals(const Value& other) const;
    [[nodiscard]] Value sql_less_than(const Value& other) const;
    [[nodiscard]] Value sql_less_than_or_equal(const Value& other) const;
    [[nodiscard]] Value sql_greater_than(const Value& other) const;
    [[nodiscard]] Value sql_greater_than_or_equal(const Value& other) const;

    // Three-valued Boolean operations
    [[nodiscard]] Value sql_and(const Value& other) const;
    [[nodiscard]] Value sql_or(const Value& other) const;
    [[nodiscard]] Value sql_not() const;

    // IS NULL / IS NOT NULL (always return non-null BOOLEAN)
    [[nodiscard]] Value sql_is_null() const { return Value::make_bool(is_null()); }
    [[nodiscard]] Value sql_is_not_null() const { return Value::make_bool(!is_null()); }

    // Arithmetic
    [[nodiscard]] Value add(const Value& other) const;
    [[nodiscard]] Value subtract(const Value& other) const;
    [[nodiscard]] Value multiply(const Value& other) const;
    [[nodiscard]] Value divide(const Value& other) const;
    [[nodiscard]] Value modulo(const Value& other) const;
    [[nodiscard]] Value negate() const;

    // Deterministic sort comparison with explicit NULL ordering
    // nulls_first: true -> NULL < non-NULL; false -> non-NULL < NULL (NULLS LAST)
    [[nodiscard]] bool sort_less_than(const Value& other, bool nulls_first = false) const;

    // Hash code for hashing keys (GROUP BY, Hash Join)
    [[nodiscard]] size_t hash() const;

    // Exact equality for hash table grouping (treating NULL == NULL for GROUP BY keys)
    [[nodiscard]] bool exact_equal(const Value& other) const;

private:
    void check_not_null() const {
        if (is_null()) {
            throw AxiomException(ErrorCategory::Execution, "Attempted to extract value from SQL NULL");
        }
    }

    TypeKind kind_{TypeKind::Null};
    std::variant<std::monostate, bool, int64_t, double, std::string> data_{std::monostate{}};
};

} // namespace axiom
