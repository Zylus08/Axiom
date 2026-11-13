#include "axiom/binder/binder.h"
#include <algorithm>

namespace axiom {

Binder::Binder(const Catalog& catalog) : catalog_(catalog) {}

std::string Binder::to_lower(std::string_view str) {
    std::string s;
    s.reserve(str.size());
    for (char c : str) {
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::unique_ptr<BoundTableRef> Binder::bind_table_ref(const TableRef* table_ref, size_t& current_col_offset) {
    if (!table_ref) {
        return nullptr;
    }

    if (table_ref->type() == TableRefType::BaseTable) {
        const auto* base = static_cast<const BaseTableRef*>(table_ref);
        if (!catalog_.has_table(base->table_name())) {
            throw AxiomException(ErrorCategory::Binder, "Table not found: " + base->table_name(), base->location());
        }
        TableDef table_def = catalog_.get_table(base->table_name());
        std::string alias = base->alias_or_name();

        // Check for duplicate alias in scope
        for (const auto& existing : active_tables_) {
            if (to_lower(existing.alias) == to_lower(alias)) {
                throw AxiomException(ErrorCategory::Binder, "Duplicate table alias: " + alias, base->location());
            }
        }

        TableBinding binding;
        binding.alias = alias;
        binding.table_def = table_def;
        binding.column_offset = current_col_offset;
        active_tables_.push_back(binding);

        current_col_offset += table_def.schema.column_count();

        return std::make_unique<BoundBaseTableRef>(std::move(table_def), std::move(alias), binding.table_def.schema);
    }

    if (table_ref->type() == TableRefType::Join) {
        const auto* join = static_cast<const JoinTableRef*>(table_ref);
        auto bound_left = bind_table_ref(join->left(), current_col_offset);
        auto bound_right = bind_table_ref(join->right(), current_col_offset);

        // Bind join ON condition with both left and right tables in scope
        auto on_cond = bind_expression(join->on_condition());
        if (on_cond->return_type() != TypeKind::Boolean && on_cond->return_type() != TypeKind::Null) {
            throw AxiomException(ErrorCategory::Binder, "JOIN ON condition must evaluate to BOOLEAN", join->location());
        }

        // Construct combined schema
        std::vector<ColumnDesc> combined_cols;
        for (const auto& col : bound_left->output_schema().columns()) {
            combined_cols.push_back(col);
        }
        for (const auto& col : bound_right->output_schema().columns()) {
            combined_cols.push_back(col);
        }
        Schema combined_schema(std::move(combined_cols));

        return std::make_unique<BoundJoinTableRef>(join->join_type(), std::move(bound_left), std::move(bound_right),
                                                   std::move(on_cond), std::move(combined_schema));
    }

    throw AxiomException(ErrorCategory::Binder, "Unsupported TableRef type");
}

std::unique_ptr<BoundExpr> Binder::bind_expression(const Expr* expr) {
    if (!expr) return nullptr;

    switch (expr->type()) {
        case ExprType::Literal: {
            const auto* lit = static_cast<const LiteralExpr*>(expr);
            return std::make_unique<BoundLiteralExpr>(lit->value());
        }
        case ExprType::ColumnRef:
            return bind_column_ref(static_cast<const ColumnRefExpr*>(expr));
        case ExprType::Unary:
            return bind_unary(static_cast<const UnaryExpr*>(expr));
        case ExprType::Binary:
            return bind_binary(static_cast<const BinaryExpr*>(expr));
        case ExprType::Aggregate:
            return bind_aggregate(static_cast<const AggregateExpr*>(expr));
    }

    throw AxiomException(ErrorCategory::Binder, "Unknown expression type", expr->location());
}

std::unique_ptr<BoundExpr> Binder::bind_column_ref(const ColumnRefExpr* col_ref) {
    if (col_ref->table_name().has_value()) {
        // Qualified column reference
        std::string req_table = to_lower(*col_ref->table_name());
        const TableBinding* match_table = nullptr;
        for (const auto& tbl : active_tables_) {
            if (to_lower(tbl.alias) == req_table || to_lower(tbl.table_def.name) == req_table) {
                match_table = &tbl;
                break;
            }
        }

        if (!match_table) {
            throw AxiomException(ErrorCategory::Binder, "Unknown table or alias: " + *col_ref->table_name(), col_ref->location());
        }

        auto col_idx_opt = match_table->table_def.schema.find_column_index(col_ref->column_name());
        if (!col_idx_opt.has_value()) {
            throw AxiomException(ErrorCategory::Binder, "Column '" + col_ref->column_name() + "' not found in table '" + *col_ref->table_name() + "'", col_ref->location());
        }

        size_t local_idx = *col_idx_opt;
        const auto& col_desc = match_table->table_def.schema.column(local_idx);
        size_t global_idx = match_table->column_offset + local_idx;
        return std::make_unique<BoundColumnRefExpr>(col_desc.name, global_idx, col_desc.type, match_table->alias);
    }

    // Unqualified column reference
    const TableBinding* found_table = nullptr;
    size_t found_local_idx = 0;
    size_t match_count = 0;

    for (const auto& tbl : active_tables_) {
        auto col_idx_opt = tbl.table_def.schema.find_column_index(col_ref->column_name());
        if (col_idx_opt.has_value()) {
            found_table = &tbl;
            found_local_idx = *col_idx_opt;
            match_count++;
        }
    }

    if (match_count > 1) {
        throw AxiomException(ErrorCategory::Binder, "Column '" + col_ref->column_name() + "' is ambiguous (found in multiple tables)", col_ref->location());
    }

    if (match_count == 1) {
        const auto& col_desc = found_table->table_def.schema.column(found_local_idx);
        size_t global_idx = found_table->column_offset + found_local_idx;
        return std::make_unique<BoundColumnRefExpr>(col_desc.name, global_idx, col_desc.type, found_table->alias);
    }

    // Check if it matches an alias in the SELECT list (for ORDER BY)
    for (const auto& [alias, bound_expr] : select_aliases_) {
        if (to_lower(alias) == to_lower(col_ref->column_name())) {
            return bound_expr->clone();
        }
    }

    throw AxiomException(ErrorCategory::Binder, "Column '" + col_ref->column_name() + "' not found", col_ref->location());
}

std::unique_ptr<BoundExpr> Binder::bind_unary(const UnaryExpr* unary) {
    auto operand = bind_expression(unary->operand());

    switch (unary->op()) {
        case UnaryOp::Plus:
        case UnaryOp::Minus: {
            if (operand->return_type() != TypeKind::BigInt &&
                operand->return_type() != TypeKind::Double &&
                operand->return_type() != TypeKind::Null) {
                throw AxiomException(ErrorCategory::Binder, "Unary operator requires numeric type", unary->location());
            }
            return std::make_unique<BoundUnaryExpr>(unary->op(), std::move(operand), operand->return_type());
        }
        case UnaryOp::Not: {
            if (operand->return_type() != TypeKind::Boolean &&
                operand->return_type() != TypeKind::Null) {
                throw AxiomException(ErrorCategory::Binder, "NOT operator requires boolean type", unary->location());
            }
            return std::make_unique<BoundUnaryExpr>(unary->op(), std::move(operand), TypeKind::Boolean);
        }
        case UnaryOp::IsNull:
        case UnaryOp::IsNotNull: {
            return std::make_unique<BoundUnaryExpr>(unary->op(), std::move(operand), TypeKind::Boolean);
        }
    }

    throw AxiomException(ErrorCategory::Binder, "Unknown unary operator", unary->location());
}

std::unique_ptr<BoundExpr> Binder::bind_binary(const BinaryExpr* binary) {
    auto left = bind_expression(binary->left());
    auto right = bind_expression(binary->right());

    switch (binary->op()) {
        case BinaryOp::Add:
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
        case BinaryOp::Modulo: {
            // Numeric operations
            TypeKind ltype = left->return_type();
            TypeKind rtype = right->return_type();

            if (binary->op() == BinaryOp::Modulo) {
                if (ltype != TypeKind::BigInt || rtype != TypeKind::BigInt) {
                    throw AxiomException(ErrorCategory::Binder, "Modulo operator '%' requires integer operands", binary->location());
                }
                return std::make_unique<BoundBinaryExpr>(binary->op(), std::move(left), std::move(right), TypeKind::BigInt);
            }

            if ((ltype != TypeKind::BigInt && ltype != TypeKind::Double && ltype != TypeKind::Null) ||
                (rtype != TypeKind::BigInt && rtype != TypeKind::Double && rtype != TypeKind::Null)) {
                throw AxiomException(ErrorCategory::Binder, "Arithmetic operator requires numeric types", binary->location());
            }

            TypeKind result_type = TypeKind::BigInt;
            if (ltype == TypeKind::Double || rtype == TypeKind::Double) {
                result_type = TypeKind::Double;
                if (ltype == TypeKind::BigInt) {
                    left = std::make_unique<BoundCastExpr>(std::move(left), TypeKind::Double);
                }
                if (rtype == TypeKind::BigInt) {
                    right = std::make_unique<BoundCastExpr>(std::move(right), TypeKind::Double);
                }
            }

            return std::make_unique<BoundBinaryExpr>(binary->op(), std::move(left), std::move(right), result_type);
        }

        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
        case BinaryOp::LessThan:
        case BinaryOp::LessEqual:
        case BinaryOp::GreaterThan:
        case BinaryOp::GreaterEqual: {
            TypeKind ltype = left->return_type();
            TypeKind rtype = right->return_type();

            // Numeric comparison with coercion
            if ((ltype == TypeKind::BigInt && rtype == TypeKind::Double) ||
                (ltype == TypeKind::Double && rtype == TypeKind::BigInt)) {
                if (ltype == TypeKind::BigInt) {
                    left = std::make_unique<BoundCastExpr>(std::move(left), TypeKind::Double);
                }
                if (rtype == TypeKind::BigInt) {
                    right = std::make_unique<BoundCastExpr>(std::move(right), TypeKind::Double);
                }
            } else if (ltype != rtype && ltype != TypeKind::Null && rtype != TypeKind::Null) {
                throw AxiomException(ErrorCategory::Binder, "Cannot compare incompatible types: " +
                                     type_kind_to_string(ltype) + " and " + type_kind_to_string(rtype), binary->location());
            }

            return std::make_unique<BoundBinaryExpr>(binary->op(), std::move(left), std::move(right), TypeKind::Boolean);
        }

        case BinaryOp::And:
        case BinaryOp::Or: {
            TypeKind ltype = left->return_type();
            TypeKind rtype = right->return_type();

            if ((ltype != TypeKind::Boolean && ltype != TypeKind::Null) ||
                (rtype != TypeKind::Boolean && rtype != TypeKind::Null)) {
                throw AxiomException(ErrorCategory::Binder, "Logical AND/OR requires boolean operands", binary->location());
            }

            return std::make_unique<BoundBinaryExpr>(binary->op(), std::move(left), std::move(right), TypeKind::Boolean);
        }
    }

    throw AxiomException(ErrorCategory::Binder, "Unknown binary operator", binary->location());
}

std::unique_ptr<BoundExpr> Binder::bind_aggregate(const AggregateExpr* agg) {
    if (in_where_clause_) {
        throw AxiomException(ErrorCategory::Binder, "Aggregate functions are not allowed in WHERE clause", agg->location());
    }
    if (in_aggregate_) {
        throw AxiomException(ErrorCategory::Binder, "Aggregate functions cannot be nested", agg->location());
    }

    is_aggregate_query_ = true;
    in_aggregate_ = true;

    std::unique_ptr<BoundExpr> bound_arg = nullptr;
    TypeKind ret_type = TypeKind::BigInt;

    if (agg->func() == AggregateFunc::CountStar) {
        ret_type = TypeKind::BigInt;
    } else {
        bound_arg = bind_expression(agg->argument());
        TypeKind arg_type = bound_arg->return_type();

        switch (agg->func()) {
            case AggregateFunc::Count:
                ret_type = TypeKind::BigInt;
                break;
            case AggregateFunc::Sum:
                if (arg_type != TypeKind::BigInt && arg_type != TypeKind::Double && arg_type != TypeKind::Null) {
                    throw AxiomException(ErrorCategory::Binder, "SUM requires numeric argument", agg->location());
                }
                ret_type = (arg_type == TypeKind::Double) ? TypeKind::Double : TypeKind::BigInt;
                break;
            case AggregateFunc::Avg:
                if (arg_type != TypeKind::BigInt && arg_type != TypeKind::Double && arg_type != TypeKind::Null) {
                    throw AxiomException(ErrorCategory::Binder, "AVG requires numeric argument", agg->location());
                }
                ret_type = TypeKind::Double;
                break;
            case AggregateFunc::Min:
            case AggregateFunc::Max:
                ret_type = arg_type;
                break;
            default:
                break;
        }
    }

    in_aggregate_ = false;

    // Check if identical aggregate is already in aggregates_
    for (size_t i = 0; i < aggregates_.size(); ++i) {
        const auto& existing = aggregates_[i];
        if (existing->func() == agg->func()) {
            if (bound_arg == nullptr && existing->argument() == nullptr) {
                return std::make_unique<BoundAggregateExpr>(agg->func(), nullptr, ret_type, i);
            }
            if (bound_arg != nullptr && existing->argument() != nullptr &&
                bound_arg->is_equal(*existing->argument())) {
                return std::make_unique<BoundAggregateExpr>(agg->func(), nullptr, ret_type, i);
            }
        }
    }

    size_t agg_index = aggregates_.size();
    auto result_agg = std::make_unique<BoundAggregateExpr>(agg->func(), nullptr, ret_type, agg_index);

    // Store in aggregates_ with its argument
    aggregates_.push_back(std::make_unique<BoundAggregateExpr>(agg->func(), std::move(bound_arg), ret_type, agg_index));

    return result_agg;
}

bool Binder::expr_matches_group_by(const BoundExpr* expr, const std::vector<std::unique_ptr<BoundExpr>>& group_by) {
    for (const auto& gb : group_by) {
        if (expr->is_equal(*gb)) {
            return true;
        }
    }
    return false;
}

void Binder::validate_group_by(const BoundExpr* expr, const std::vector<std::unique_ptr<BoundExpr>>& group_by, const SourceLocation& loc) {
    if (!expr) return;
    if (expr_matches_group_by(expr, group_by)) {
        return;
    }
    if (expr->type() == BoundExprType::Aggregate || expr->type() == BoundExprType::Literal) {
        return;
    }
    if (expr->type() == BoundExprType::ColumnRef) {
        throw AxiomException(ErrorCategory::Binder, "Column '" + expr->to_string() + "' must appear in GROUP BY clause or be used in an aggregate function", loc);
    }
    if (expr->type() == BoundExprType::Unary) {
        const auto* u = static_cast<const BoundUnaryExpr*>(expr);
        validate_group_by(u->operand(), group_by, loc);
        return;
    }
    if (expr->type() == BoundExprType::Binary) {
        const auto* b = static_cast<const BoundBinaryExpr*>(expr);
        validate_group_by(b->left(), group_by, loc);
        validate_group_by(b->right(), group_by, loc);
        return;
    }
    if (expr->type() == BoundExprType::Cast) {
        const auto* c = static_cast<const BoundCastExpr*>(expr);
        validate_group_by(c->child(), group_by, loc);
        return;
    }
}

std::unique_ptr<BoundSelectStatement> Binder::bind_select(const SelectStatement& stmt) {
    auto bound = std::make_unique<BoundSelectStatement>();
    bound->is_explain = stmt.is_explain;

    // 1. Bind FROM / JOIN
    size_t col_offset = 0;
    if (stmt.from_table) {
        bound->from_table = bind_table_ref(stmt.from_table.get(), col_offset);
    }

    // 2. Bind WHERE
    if (stmt.where_clause) {
        in_where_clause_ = true;
        bound->where_clause = bind_expression(stmt.where_clause.get());
        in_where_clause_ = false;

        if (bound->where_clause->return_type() != TypeKind::Boolean &&
            bound->where_clause->return_type() != TypeKind::Null) {
            throw AxiomException(ErrorCategory::Binder, "WHERE clause must evaluate to BOOLEAN", stmt.where_clause->location());
        }
    }

    // 3. Bind GROUP BY
    if (!stmt.group_by.empty()) {
        is_aggregate_query_ = true;
        for (const auto& gb_expr : stmt.group_by) {
            auto bound_gb = bind_expression(gb_expr.get());
            bound->group_by.push_back(std::move(bound_gb));
        }
    }

    // 4. Bind SELECT list (expanding * and tbl.*)
    std::vector<ColumnDesc> output_columns;

    for (const auto& item : stmt.select_list) {
        if (item.is_star) {
            if (item.star_table.has_value()) {
                // table.*
                std::string req = to_lower(*item.star_table);
                const TableBinding* match_tbl = nullptr;
                for (const auto& tbl : active_tables_) {
                    if (to_lower(tbl.alias) == req || to_lower(tbl.table_def.name) == req) {
                        match_tbl = &tbl;
                        break;
                    }
                }
                if (!match_tbl) {
                    throw AxiomException(ErrorCategory::Binder, "Unknown table '" + *item.star_table + "' in " + *item.star_table + ".*");
                }
                for (size_t c = 0; c < match_tbl->table_def.schema.column_count(); ++c) {
                    const auto& col_desc = match_tbl->table_def.schema.column(c);
                    size_t global_idx = match_tbl->column_offset + c;
                    auto bound_col = std::make_unique<BoundColumnRefExpr>(col_desc.name, global_idx, col_desc.type, match_tbl->alias);
                    output_columns.emplace_back(col_desc.name, col_desc.type, col_desc.nullable);
                    BoundSelectItem b_item;
                    b_item.name = col_desc.name;
                    b_item.expr = std::move(bound_col);
                    bound->select_list.push_back(std::move(b_item));
                }
            } else {
                // *
                for (const auto& tbl : active_tables_) {
                    for (size_t c = 0; c < tbl.table_def.schema.column_count(); ++c) {
                        const auto& col_desc = tbl.table_def.schema.column(c);
                        size_t global_idx = tbl.column_offset + c;
                        auto bound_col = std::make_unique<BoundColumnRefExpr>(col_desc.name, global_idx, col_desc.type, tbl.alias);
                        output_columns.emplace_back(col_desc.name, col_desc.type, col_desc.nullable);
                        BoundSelectItem b_item;
                        b_item.name = col_desc.name;
                        b_item.expr = std::move(bound_col);
                        bound->select_list.push_back(std::move(b_item));
                    }
                }
            }
        } else {
            auto bound_expr = bind_expression(item.expr.get());
            std::string out_name = item.alias.value_or(item.expr->to_string());
            output_columns.emplace_back(out_name, bound_expr->return_type(), true);

            BoundSelectItem b_item;
            b_item.name = out_name;
            b_item.expr = std::move(bound_expr);
            bound->select_list.push_back(std::move(b_item));
        }
    }

    bound->output_schema = Schema(std::move(output_columns));

    // Register select aliases for ORDER BY resolution
    for (const auto& s_item : bound->select_list) {
        select_aliases_.emplace_back(s_item.name, s_item.expr.get());
    }

    // 5. Bind HAVING
    if (stmt.having_clause) {
        is_aggregate_query_ = true;
        bound->having_clause = bind_expression(stmt.having_clause.get());
        if (bound->having_clause->return_type() != TypeKind::Boolean &&
            bound->having_clause->return_type() != TypeKind::Null) {
            throw AxiomException(ErrorCategory::Binder, "HAVING clause must evaluate to BOOLEAN", stmt.having_clause->location());
        }
    }

    // 6. Validate GROUP BY constraints on SELECT list and HAVING
    if (is_aggregate_query_) {
        for (const auto& item : bound->select_list) {
            validate_group_by(item.expr.get(), bound->group_by, {});
        }
        if (bound->having_clause) {
            validate_group_by(bound->having_clause.get(), bound->group_by, stmt.having_clause->location());
        }
    }

    // 7. Bind ORDER BY
    for (const auto& ord : stmt.order_by) {
        auto bound_ord_expr = bind_expression(ord.expr.get());
        if (is_aggregate_query_) {
            validate_group_by(bound_ord_expr.get(), bound->group_by, ord.expr->location());
        }
        bool nulls_first = ord.nulls_first.value_or(!ord.is_asc); // Standard: ASC -> nulls last (false), DESC -> nulls first (true)
        bound->order_by.push_back(BoundOrderItem{std::move(bound_ord_expr), ord.is_asc, nulls_first});
    }

    // 8. Bind LIMIT and OFFSET
    if (stmt.limit.has_value()) {
        if (*stmt.limit < 0) {
            throw AxiomException(ErrorCategory::Binder, "LIMIT must be non-negative");
        }
        bound->limit = stmt.limit;
    }
    if (stmt.offset.has_value()) {
        if (*stmt.offset < 0) {
            throw AxiomException(ErrorCategory::Binder, "OFFSET must be non-negative");
        }
        bound->offset = stmt.offset;
    }

    bound->aggregates = std::move(aggregates_);

    return bound;
}

} // namespace axiom
