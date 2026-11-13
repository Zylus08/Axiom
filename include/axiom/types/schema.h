#pragma once

#include "axiom/types/value.h"
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

namespace axiom {

struct ColumnDesc {
    std::string name;
    TypeKind type{TypeKind::Null};
    bool nullable{true};

    ColumnDesc() = default;
    ColumnDesc(std::string n, TypeKind t, bool null_ok = true)
        : name(std::move(n)), type(t), nullable(null_ok) {}

    [[nodiscard]] bool operator==(const ColumnDesc& other) const {
        return name == other.name && type == other.type && nullable == other.nullable;
    }
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<ColumnDesc> columns) : columns_(std::move(columns)) {}

    [[nodiscard]] const std::vector<ColumnDesc>& columns() const noexcept { return columns_; }
    [[nodiscard]] size_t column_count() const noexcept { return columns_.size(); }
    [[nodiscard]] bool empty() const noexcept { return columns_.empty(); }

    [[nodiscard]] const ColumnDesc& column(size_t index) const {
        if (index >= columns_.size()) {
            throw AxiomException(ErrorCategory::Internal, "Column index out of range: " + std::to_string(index));
        }
        return columns_[index];
    }

    [[nodiscard]] std::optional<size_t> find_column_index(std::string_view name) const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (equal_case_insensitive(columns_[i].name, name)) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return find_column_index(name).has_value();
    }

    void add_column(ColumnDesc col) {
        columns_.push_back(std::move(col));
    }

    [[nodiscard]] std::string to_string() const {
        std::string res = "(";
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) res += ", ";
            res += columns_[i].name + " " + type_kind_to_string(columns_[i].type);
        }
        res += ")";
        return res;
    }

private:
    std::vector<ColumnDesc> columns_;

    static bool equal_case_insensitive(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
};

} // namespace axiom
