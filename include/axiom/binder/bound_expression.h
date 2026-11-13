#pragma once

#include "axiom/types/schema.h"
#include "axiom/parser/ast.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace axiom {

enum class BoundExprType {
    Literal,
    ColumnRef,
    Unary,
    Binary,
    Aggregate,
    Cast
};

class BoundExpr {
public:
    virtual ~BoundExpr() = default;
    [[nodiscard]] BoundExprType type() const noexcept { return type_; }
    [[nodiscard]] TypeKind return_type() const noexcept { return return_type_; }
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual bool is_equal(const BoundExpr& other) const = 0;
    [[nodiscard]] virtual std::unique_ptr<BoundExpr> clone() const = 0;

protected:
    BoundExpr(BoundExprType type, TypeKind ret_type)
        : type_(type), return_type_(ret_type) {}

private:
    BoundExprType type_;
    TypeKind return_type_;
};

class BoundLiteralExpr : public BoundExpr {
public:
    explicit BoundLiteralExpr(Value val)
        : BoundExpr(BoundExprType::Literal, val.kind()), value_(std::move(val)) {}

    [[nodiscard]] const Value& value() const noexcept { return value_; }
    [[nodiscard]] std::string to_string() const override { return value_.to_string(); }
    [[nodiscard]] bool is_equal(const BoundExpr& other) const override {
        if (other.type() != BoundExprType::Literal) return false;
        const auto& lit = static_cast<const BoundLiteralExpr&>(other);
        return value_.exact_equal(lit.value_);
    }
    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundLiteralExpr>(value_);
    }

private:
    Value value_;
};

class BoundColumnRefExpr : public BoundExpr {
public:
    BoundColumnRefExpr(std::string name, size_t col_index, TypeKind type, std::optional<std::string> table_alias = std::nullopt)
        : BoundExpr(BoundExprType::ColumnRef, type),
          name_(std::move(name)),
          col_index_(col_index),
          table_alias_(std::move(table_alias)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] size_t col_index() const noexcept { return col_index_; }
    [[nodiscard]] const std::optional<std::string>& table_alias() const noexcept { return table_alias_; }

    [[nodiscard]] std::string to_string() const override {
        if (table_alias_.has_value()) {
            return *table_alias_ + "." + name_;
        }
        return name_;
    }

    [[nodiscard]] bool is_equal(const BoundExpr& other) const override {
        if (other.type() != BoundExprType::ColumnRef) return false;
        const auto& c = static_cast<const BoundColumnRefExpr&>(other);
        return col_index_ == c.col_index_ && table_alias_ == c.table_alias_;
    }

    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundColumnRefExpr>(name_, col_index_, return_type(), table_alias_);
    }

private:
    std::string name_;
    size_t col_index_;
    std::optional<std::string> table_alias_;
};

class BoundUnaryExpr : public BoundExpr {
public:
    BoundUnaryExpr(UnaryOp op, std::unique_ptr<BoundExpr> operand, TypeKind ret_type)
        : BoundExpr(BoundExprType::Unary, ret_type), op_(op), operand_(std::move(operand)) {}

    [[nodiscard]] UnaryOp op() const noexcept { return op_; }
    [[nodiscard]] const BoundExpr* operand() const noexcept { return operand_.get(); }
    [[nodiscard]] std::unique_ptr<BoundExpr>& operand_mut() noexcept { return operand_; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] bool is_equal(const BoundExpr& other) const override;
    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundUnaryExpr>(op_, operand_->clone(), return_type());
    }

private:
    UnaryOp op_;
    std::unique_ptr<BoundExpr> operand_;
};

class BoundBinaryExpr : public BoundExpr {
public:
    BoundBinaryExpr(BinaryOp op, std::unique_ptr<BoundExpr> left, std::unique_ptr<BoundExpr> right, TypeKind ret_type)
        : BoundExpr(BoundExprType::Binary, ret_type), op_(op), left_(std::move(left)), right_(std::move(right)) {}

    [[nodiscard]] BinaryOp op() const noexcept { return op_; }
    [[nodiscard]] const BoundExpr* left() const noexcept { return left_.get(); }
    [[nodiscard]] const BoundExpr* right() const noexcept { return right_.get(); }
    [[nodiscard]] std::unique_ptr<BoundExpr>& left_mut() noexcept { return left_; }
    [[nodiscard]] std::unique_ptr<BoundExpr>& right_mut() noexcept { return right_; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] bool is_equal(const BoundExpr& other) const override;
    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundBinaryExpr>(op_, left_->clone(), right_->clone(), return_type());
    }

private:
    BinaryOp op_;
    std::unique_ptr<BoundExpr> left_;
    std::unique_ptr<BoundExpr> right_;
};

class BoundCastExpr : public BoundExpr {
public:
    BoundCastExpr(std::unique_ptr<BoundExpr> child, TypeKind target_type)
        : BoundExpr(BoundExprType::Cast, target_type), child_(std::move(child)) {}

    [[nodiscard]] const BoundExpr* child() const noexcept { return child_.get(); }
    [[nodiscard]] std::unique_ptr<BoundExpr>& child_mut() noexcept { return child_; }

    [[nodiscard]] std::string to_string() const override {
        return "CAST(" + child_->to_string() + " AS " + type_kind_to_string(return_type()) + ")";
    }

    [[nodiscard]] bool is_equal(const BoundExpr& other) const override {
        if (other.type() != BoundExprType::Cast) return false;
        const auto& c = static_cast<const BoundCastExpr&>(other);
        return return_type() == c.return_type() && child_->is_equal(*c.child_);
    }

    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundCastExpr>(child_->clone(), return_type());
    }

private:
    std::unique_ptr<BoundExpr> child_;
};

class BoundAggregateExpr : public BoundExpr {
public:
    BoundAggregateExpr(AggregateFunc func, std::unique_ptr<BoundExpr> arg, TypeKind ret_type, size_t agg_index)
        : BoundExpr(BoundExprType::Aggregate, ret_type), func_(func), argument_(std::move(arg)), agg_index_(agg_index) {}

    [[nodiscard]] AggregateFunc func() const noexcept { return func_; }
    [[nodiscard]] const BoundExpr* argument() const noexcept { return argument_.get(); }
    [[nodiscard]] std::unique_ptr<BoundExpr>& argument_mut() noexcept { return argument_; }
    [[nodiscard]] size_t agg_index() const noexcept { return agg_index_; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] bool is_equal(const BoundExpr& other) const override;
    [[nodiscard]] std::unique_ptr<BoundExpr> clone() const override {
        return std::make_unique<BoundAggregateExpr>(func_, argument_ ? argument_->clone() : nullptr, return_type(), agg_index_);
    }

private:
    AggregateFunc func_;
    std::unique_ptr<BoundExpr> argument_;
    size_t agg_index_{0};
};

} // namespace axiom
