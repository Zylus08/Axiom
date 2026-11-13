# Axiom SQL Support Matrix (Milestone M1)

## 1. Supported SQL Dialect

Axiom M1 supports an analytical subset of SQL-92 / SQL:1999 standard queries with strict three-valued logic and type checking.

### 1.1 Statements & Clauses

| Clause / Feature | Supported | Description |
| :--- | :---: | :--- |
| `SELECT` | Yes | Column list, expressions, aliases (`AS alias`), `*`, `table.*` |
| `FROM` | Yes | Base table references, table aliases (`FROM table [AS] alias`) |
| `INNER JOIN ... ON` | Yes | Multi-table inner joins with arbitrary condition and equijoins |
| `WHERE` | Yes | Boolean filtering predicates |
| `GROUP BY` | Yes | Grouping by single or multiple keys, with grouping on NULL keys |
| `HAVING` | Yes | Filter over aggregated groups and group keys |
| `ORDER BY` | Yes | Sort by expressions or select aliases, `ASC` / `DESC`, deterministic |
| `LIMIT` | Yes | Maximum rows returned, short-circuits operator pull |
| `OFFSET` | Yes | Skips leading rows |
| `EXPLAIN` | Yes | Visualizes logical and physical plan execution trees |

---

### 1.2 Data Types

| Type Name | Internal Representation | Format / Aliases |
| :--- | :--- | :--- |
| `BIGINT` | Signed 64-bit integer (`int64_t`) | `BIGINT`, `INT`, `INTEGER`, `INT64` |
| `DOUBLE` | 64-bit IEEE-754 float (`double`) | `DOUBLE`, `FLOAT`, `REAL`, `FLOAT64` |
| `VARCHAR` | UTF-8 String (`std::string`) | `VARCHAR`, `TEXT`, `STRING`, `CHAR` |
| `BOOLEAN` | Boolean (`bool`) | `BOOLEAN`, `BOOL` (`true`, `false`) |
| `NULL` | Tagged null indicator | `NULL` |

---

### 1.3 Expressions and Operators

#### Arithmetic Operators
- `+` (Addition)
- `-` (Subtraction, Unary Negation)
- `*` (Multiplication)
- `/` (Division: integer division if both operands `BIGINT`, float division if either operand is `DOUBLE`)
- `%` (Integer Modulo)

#### Comparison Operators
- `=` (Equal)
- `!=` or `<>` (Not Equal)
- `<` (Less Than)
- `<=` (Less Than or Equal)
- `>` (Greater Than)
- `>=` (Greater Than or Equal)
- `IS NULL` (Returns `true` if operand is NULL)
- `IS NOT NULL` (Returns `true` if operand is non-NULL)

#### Logical Operators (Three-Valued Logic)
- `AND`: `false AND NULL -> false`, `true AND NULL -> NULL`
- `OR`: `true OR NULL -> true`, `false OR NULL -> NULL`
- `NOT`: `NOT NULL -> NULL`

---

### 1.4 Aggregate Functions

| Aggregate | Description | Empty Table (No Group By) |
| :--- | :--- | :---: |
| `COUNT(*)` | Counts total rows in group, including NULLs | `0` |
| `COUNT(expr)` | Counts non-NULL values in group | `0` |
| `SUM(expr)` | Computes arithmetic sum of non-NULL values | `NULL` |
| `AVG(expr)` | Computes arithmetic mean of non-NULL values | `NULL` |
| `MIN(expr)` | Finds minimum non-NULL value | `NULL` |
| `MAX(expr)` | Finds maximum non-NULL value | `NULL` |

---

## 2. Unsupported Syntax & Future Scope (Post-M1)

The following features are intentionally outside the M1 scope and will be rejected with clear syntactic or semantic diagnostic errors:
- `LEFT OUTER JOIN`, `RIGHT OUTER JOIN`, `FULL OUTER JOIN`
- Subqueries in `FROM`, `WHERE`, or `SELECT` (e.g. `IN (SELECT ...)`, scalar subqueries)
- `DISTINCT` in `SELECT` or aggregate functions (e.g. `COUNT(DISTINCT x)`)
- Set operations: `UNION`, `INTERSECT`, `EXCEPT`
- Window functions: `OVER (PARTITION BY ...)`
- `CASE WHEN ... THEN ... ELSE END` expressions
- `LIKE` / Regular expressions
- `INSERT`, `UPDATE`, `DELETE`, `CREATE TABLE` (Axiom M1 is an analytical engine querying CSV datasets)

