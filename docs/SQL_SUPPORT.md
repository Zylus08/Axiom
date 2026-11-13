# Axiom SQL Support Matrix (Milestone M2)

## 1. Supported SQL Dialect

Axiom supports an analytical subset of SQL-92 / SQL:1999 standard queries with strict three-valued logic, explicit type coercion, and out-of-core execution under memory bounds.

### 1.1 Statements & Clauses

| Clause / Feature | Supported | Description |
| :--- | :---: | :--- |
| `SELECT` | Yes | Column list, expressions, aliases (`AS alias`), `*`, `table.*` |
| `FROM` | Yes | Base table references, table aliases (`FROM table [AS] alias`) |
| `INNER JOIN ... ON` | Yes | Inner equijoins with Grace Hash Join spilling; non-equijoins with nested loop |
| `WHERE` | Yes | Boolean filtering predicates with rule-based pushdown to scans |
| `GROUP BY` | Yes | Single or multi-key grouping with hash-partitioned external spilling |
| `HAVING` | Yes | Filter over aggregated groups and group keys |
| `ORDER BY` | Yes | `ASC` / `DESC`, `NULLS FIRST` / `NULLS LAST`, external merge sort |
| `LIMIT` | Yes | Row count cap; pushdown into `PhysicalSort` via Top-K bounded max-heap |
| `OFFSET` | Yes | Skips leading rows |
| `EXPLAIN` | Yes | Visualizes logical and physical plan execution trees |
| `EXPLAIN ANALYZE` | Yes | Executes query and displays detailed operator telemetry and profile |

---

### 1.2 Data Types

| Type Name | Internal Representation | Supported Syntax / Aliases |
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

#### Logical Operators (Three-Valued Logic)
- `AND`
- `OR`
- `NOT`
- `IS NULL`
- `IS NOT NULL`

---

### 1.4 Aggregate Functions

| Aggregate | Description | Empty Table Return |
| :--- | :--- | :---: |
| `COUNT(*)` | Counts all rows in group | `0` |
| `COUNT(expr)` | Counts non-null evaluations of `expr` | `0` |
| `SUM(expr)` | Sum of values (bigint or double) | `NULL` |
| `AVG(expr)` | Arithmetic mean (returns double) | `NULL` |
| `MIN(expr)` | Minimum value (preserves type) | `NULL` |
| `MAX(expr)` | Maximum value (preserves type) | `NULL` |

---

### 1.5 CLI Execution Options

| Flag | Argument | Description |
| :--- | :--- | :--- |
| `-e, --query` | `<sql>` | Execute SQL query string |
| `-f, --file` | `<path>` | Execute SQL query from file |
| `-t, --table` | `<name>=<csv>[:schema]` | Register CSV table with auto-inferred or explicit schema |
| `--memory-limit` | `<size>` | Set query memory budget (e.g. `256MB`, `1GB`) |
| `--temp-dir` | `<path>` | Set directory for temporary spill partitions |
| `--max-temp-disk`| `<size>` | Set disk quota for temporary spill files |
| `--profile` | None | Output query execution time, peak memory, and spill stats |
| `--explain-analyze`| None | Execute query and output plan tree and execution telemetry |
| `--format` | `<fmt>` | Output formatting: `table` (default), `csv`, `json` |
