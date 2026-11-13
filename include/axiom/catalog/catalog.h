#pragma once

#include "axiom/types/schema.h"
#include "axiom/storage/csv_reader.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace axiom {

struct TableDef {
    std::string name;
    Schema schema;
    std::string file_path;
    CsvOptions options;
};

class Catalog {
public:
    Catalog() = default;

    void register_table(std::string name, Schema schema, std::string file_path, CsvOptions options = {}) {
        std::string key = normalize_name(name);
        if (tables_.find(key) != tables_.end()) {
            throw AxiomException(ErrorCategory::Binder, "Table already registered: " + name);
        }
        TableDef def{std::move(name), std::move(schema), std::move(file_path), std::move(options)};
        tables_.emplace(std::move(key), std::move(def));
    }

    [[nodiscard]] bool has_table(std::string_view name) const {
        return tables_.find(normalize_name(name)) != tables_.end();
    }

    [[nodiscard]] const TableDef& get_table(std::string_view name) const {
        auto it = tables_.find(normalize_name(name));
        if (it == tables_.end()) {
            throw AxiomException(ErrorCategory::Binder, "Table not found: " + std::string(name));
        }
        return it->second;
    }

    [[nodiscard]] std::vector<std::string> table_names() const {
        std::vector<std::string> names;
        names.reserve(tables_.size());
        for (const auto& [k, v] : tables_) {
            names.push_back(v.name);
        }
        return names;
    }

private:
    std::unordered_map<std::string, TableDef> tables_;

    static std::string normalize_name(std::string_view name) {
        std::string res;
        res.reserve(name.size());
        for (char c : name) {
            res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return res;
    }
};

} // namespace axiom
