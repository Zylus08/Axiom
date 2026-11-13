#pragma once

#include "axiom/types/value.h"
#include "axiom/parser/token.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace axiom {

enum class ExprType {
    Literal,
    ColumnRef,
    Unary,
    Binary,
    Aggregate
};

class Expr {
public:
    virtual ~Expr() = default;
    [[nodiscard]] ExprType type() const noexcept { return type_; }
    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }
    [[nodiscard]] virtual std::string to_string() const = 0;

protected:
    Expr(ExprType type, SourceLocation loc) : type_(type), location_(loc) {}

private:
    ExprType type_;
    SourceLocation location_;
};

class LiteralExpr : public Expr {
public:
    LiteralExpr(Value val, SourceLocation loc)
        : Expr(ExprType::Literal, loc), value_(std::move(val)) {}

    [[nodiscard]] const Value& value() const noexcept { return value_; }
    [[nodiscard]] std::string to_string() const override { return value_.to_string(); }

private:
    Value value_;
};

class ColumnRefExpr : public Expr {
public:
    ColumnRefExpr(std::optional<std::string> table, std::string col, SourceLocation loc)
        : Expr(ExprType::ColumnRef, loc), table_name_(std::move(table)), column_name_(std::move(col)) {}

    [[nodiscard]] const std::optional<std::string>& table_name() const noexcept { return table_name_; }
    [[nodiscard]] const std::string& column_name() const noexcept { return column_name_; }

    [[nodiscard]] std::string to_string() const override {
        if (table_name_.has_value()) {
            return *table_name_ + "." + column_name_;
        }
        return column_name_;
    }

private:
    std::optional<std::string> table_name_;
    std::string column_name_;
};

enum class UnaryOp {
    Plus,
    Minus,
    Not,
    IsNull,
    IsNotNull
};

class UnaryExpr : public Expr {
public:
    UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand, SourceLocation loc)
        : Expr(ExprType::Unary, loc), op_(op), operand_(std::move(operand)) {}

    [[nodiscard]] UnaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr* operand() const noexcept { return operand_.get(); }
    [[nodiscard]] std::unique_ptr<Expr>& operand_mut() noexcept { return operand_; }

    [[nodiscard]] std::string to_string() const override;

private:
    UnaryOp op_;
    std::unique_ptr<Expr> operand_;
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    And,
    Or
};

class BinaryExpr : public Expr {
public:
    BinaryExpr(BinaryOp op, std::unique_ptr<Expr> left, std::unique_ptr<Expr> right, SourceLocation loc)
        : Expr(ExprType::Binary, loc), op_(op), left_(std::move(left)), right_(std::move(right)) {}

    [[nodiscard]] BinaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr* left() const noexcept { return left_.get(); }
    [[nodiscard]] const Expr* right() const noexcept { return right_.get(); }
    [[nodiscard]] std::unique_ptr<Expr>& left_mut() noexcept { return left_; }
    [[nodiscard]] std::unique_ptr<Expr>& right_mut() noexcept { return right_; }

    [[nodiscard]] std::string to_string() const override;

private:
    BinaryOp op_;
    std::unique_ptr<Expr> left_;
    std::unique_ptr<Expr> right_;
};

enum class AggregateFunc {
    CountStar,
    Count,
    Sum,
    Avg,
    Min,
    Max
};

class AggregateExpr : public Expr {
public:
    AggregateExpr(AggregateFunc func, std::unique_ptr<Expr> arg, SourceLocation loc)
        : Expr(ExprType::Aggregate, loc), func_(func), argument_(std::move(arg)) {}

    [[nodiscard]] AggregateFunc func() const noexcept { return func_; }
    [[nodiscard]] const Expr* argument() const noexcept { return argument_.get(); }
    [[nodiscard]] std::unique_ptr<Expr>& argument_mut() noexcept { return argument_; }

    [[nodiscard]] std::string to_string() const override;

private:
    AggregateFunc func_;
    std::unique_ptr<Expr> argument_;
};

struct SelectItem {
    std::unique_ptr<Expr> expr;
    std::optional<std::string> alias;
    bool is_star{false};
    std::optional<std::string> star_table;
};

enum class TableRefType {
    BaseTable,
    Join
};

class TableRef {
public:
    virtual ~TableRef() = default;
    [[nodiscard]] TableRefType type() const noexcept { return type_; }
    [[nodiscard]] virtual std::string to_string() const = 0;

protected:
    explicit TableRef(TableRefType type) : type_(type) {}

private:
    TableRefType type_;
};

class BaseTableRef : public TableRef {
public:
    BaseTableRef(std::string table_name, std::optional<std::string> alias, SourceLocation loc)
        : TableRef(TableRefType::BaseTable), table_name_(std::move(table_name)), alias_(std::move(alias)), location_(loc) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }
    [[nodiscard]] const std::optional<std::string>& alias() const noexcept { return alias_; }
    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }

    [[nodiscard]] std::string alias_or_name() const noexcept {
        return alias_.value_or(table_name_);
    }

    [[nodiscard]] std::string to_string() const override {
        if (alias_.has_value()) return table_name_ + " AS " + *alias_;
        return table_name_;
    }

private:
    std::string table_name_;
    std::optional<std::string> alias_;
    SourceLocation location_;
};

enum class JoinType {
    Inner
};

class JoinTableRef : public TableRef {
public:
    JoinTableRef(JoinType join_type, std::unique_ptr<TableRef> left, std::unique_ptr<TableRef> right,
                 std::unique_ptr<Expr> on_condition, SourceLocation loc)
        : TableRef(TableRefType::Join), join_type_(join_type), left_(std::move(left)),
          right_(std::move(right)), on_condition_(std::move(on_condition)), location_(loc) {}

    [[nodiscard]] JoinType join_type() const noexcept { return join_type_; }
    [[nodiscard]] const TableRef* left() const noexcept { return left_.get(); }
    [[nodiscard]] const TableRef* right() const noexcept { return right_.get(); }
    [[nodiscard]] const Expr* on_condition() const noexcept { return on_condition_.get(); }
    [[nodiscard]] std::unique_ptr<TableRef>& left_mut() noexcept { return left_; }
    [[nodiscard]] std::unique_ptr<TableRef>& right_mut() noexcept { return right_; }
    [[nodiscard]] std::unique_ptr<Expr>& on_condition_mut() noexcept { return on_condition_; }
    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }

    [[nodiscard]] std::string to_string() const override;

private:
    JoinType join_type_;
    std::unique_ptr<TableRef> left_;
    std::unique_ptr<TableRef> right_;
    std::unique_ptr<Expr> on_condition_;
    SourceLocation location_;
};

struct OrderItem {
    std::unique_ptr<Expr> expr;
    bool is_asc{true};
    std::optional<bool> nulls_first{std::nullopt}; // If nullopt, defaults: ASC -> nulls last, DESC -> nulls first
};

class SelectStatement {
public:
    bool is_explain{false};
    std::vector<SelectItem> select_list;
    std::unique_ptr<TableRef> from_table;
    std::unique_ptr<Expr> where_clause;
    std::vector<std::unique_ptr<Expr>> group_by;
    std::unique_ptr<Expr> having_clause;
    std::vector<OrderItem> order_by;
    std::optional<int64_t> limit;
    std::optional<int64_t> offset;

    [[nodiscard]] std::string to_string() const;
};

} // namespace axiom

