#pragma once

#include "axiom/binder/bound_expression.h"
#include "axiom/catalog/catalog.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace axiom {

enum class BoundTableType {
    Base,
    Join
};

class BoundTableRef {
public:
    virtual ~BoundTableRef() = default;
    [[nodiscard]] BoundTableType type() const noexcept { return type_; }
    [[nodiscard]] const Schema& output_schema() const noexcept { return output_schema_; }

protected:
    BoundTableRef(BoundTableType type, Schema schema)
        : type_(type), output_schema_(std::move(schema)) {}

    Schema output_schema_;

private:
    BoundTableType type_;
};

class BoundBaseTableRef : public BoundTableRef {
public:
    BoundBaseTableRef(TableDef table_def, std::string alias, Schema schema)
        : BoundTableRef(BoundTableType::Base, std::move(schema)),
          table_def_(std::move(table_def)),
          alias_(std::move(alias)) {}

    [[nodiscard]] const TableDef& table_def() const noexcept { return table_def_; }
    [[nodiscard]] const std::string& alias() const noexcept { return alias_; }

private:
    TableDef table_def_;
    std::string alias_;
};

class BoundJoinTableRef : public BoundTableRef {
public:
    BoundJoinTableRef(JoinType join_type, std::unique_ptr<BoundTableRef> left,
                      std::unique_ptr<BoundTableRef> right, std::unique_ptr<BoundExpr> on_condition,
                      Schema schema)
        : BoundTableRef(BoundTableType::Join, std::move(schema)),
          join_type_(join_type),
          left_(std::move(left)),
          right_(std::move(right)),
          on_condition_(std::move(on_condition)) {}

    [[nodiscard]] JoinType join_type() const noexcept { return join_type_; }
    [[nodiscard]] const BoundTableRef* left() const noexcept { return left_.get(); }
    [[nodiscard]] const BoundTableRef* right() const noexcept { return right_.get(); }
    [[nodiscard]] const BoundExpr* on_condition() const noexcept { return on_condition_.get(); }
    [[nodiscard]] std::unique_ptr<BoundTableRef>& left_mut() noexcept { return left_; }
    [[nodiscard]] std::unique_ptr<BoundTableRef>& right_mut() noexcept { return right_; }
    [[nodiscard]] std::unique_ptr<BoundExpr>& on_condition_mut() noexcept { return on_condition_; }

private:
    JoinType join_type_;
    std::unique_ptr<BoundTableRef> left_;
    std::unique_ptr<BoundTableRef> right_;
    std::unique_ptr<BoundExpr> on_condition_;
};

struct BoundSelectItem {
    std::unique_ptr<BoundExpr> expr;
    std::string name;
};

struct BoundOrderItem {
    std::unique_ptr<BoundExpr> expr;
    bool is_asc{true};
    bool nulls_first{false};
};

class BoundSelectStatement {
public:
    bool is_explain{false};
    std::unique_ptr<BoundTableRef> from_table;
    std::unique_ptr<BoundExpr> where_clause;
    std::vector<std::unique_ptr<BoundExpr>> group_by;
    std::vector<std::unique_ptr<BoundAggregateExpr>> aggregates;
    std::unique_ptr<BoundExpr> having_clause;
    std::vector<BoundSelectItem> select_list;
    std::vector<BoundOrderItem> order_by;
    std::optional<int64_t> limit;
    std::optional<int64_t> offset;
    Schema output_schema;
};

} // namespace axiom

