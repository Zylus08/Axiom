# Project Axiom Architecture Specification (Milestone M1)

## 1. System Overview

**Project Axiom** is an analytical SQL database engine built from scratch in modern C++20. It implements a complete, self-contained relational query processing pipeline:
- Lexical analysis with source location tracking
- Recursive-descent and Pratt-style expression parsing
- Catalog registration and streaming RFC-4180 CSV storage
- Semantic analysis, column resolution, and validation (Binder)
- Logical query plan construction and expression rewriting
- Vectorized physical query execution using iterator-batch operators
- Command-line interface with interactive and batch execution modes

Axiom does not embed, fork, or delegate execution to SQLite, DuckDB, PostgreSQL, or any third-party parser or execution framework.

```
                          SQL Query String
                                 │
                                 ▼
                         ┌───────────────┐
                         │     Lexer     │  (SourceLocation, Keywords, Identifiers)
                         └───────┬───────┘
                                 │ Tokens
                                 ▼
                         ┌───────────────┐
                         │    Parser     │  (AST, Pratt Expression Parser)
                         └───────┬───────┘
                                 │ Query AST
                                 ▼
                         ┌───────────────┐
                         │    Binder     │ ◄─── Catalog (Table & Schema Metadata)
                         └───────┬───────┘
                                 │ Bound AST (Types, Scopes, Resolved Columns)
                                 ▼
                         ┌───────────────┐
                         │Logical Planner│  (Logical Operator Tree)
                         └───────┬───────┘
                                 │ Logical Plan
                                 ▼
                         ┌───────────────┐
                         │Physical Plan. │  (Physical Operator Tree)
                         └───────┬───────┘
                                 │ Physical Operators (Init, Next, Close)
                                 ▼
                         ┌───────────────┐
                         │Execution Engine│ ◄─── CSV Storage (Chunked I/O)
                         └───────┬───────┘
                                 │ RecordBatches
                                 ▼
                           QueryResult
                       (Table / CSV / JSON)
```

---

## 2. Component Architecture

### 2.1 Type System & Value Representation (`axiom::Value`)

The engine provides a tagged union `Value` supporting five fundamental types:
1. `BIGINT`: 64-bit signed integer (`int64_t`)
2. `DOUBLE`: 64-bit IEEE-754 double precision floating point (`double`)
3. `VARCHAR`: Dynamic UTF-8 string (`std::string`)
4. `BOOLEAN`: Boolean (`bool`)
5. `NULL`: SQL 3-valued unknown representation

#### SQL Semantics
- **Three-Valued Logic:**
  - `TRUE AND NULL -> NULL`, `FALSE AND NULL -> FALSE`
  - `TRUE OR NULL -> TRUE`, `FALSE OR NULL -> NULL`
  - `NOT NULL -> NULL`
- **Arithmetic:** Division by zero and modulo by zero trigger actionable exceptions (`ErrorCategory::Execution`). Implicit type promotion coerces `BIGINT` to `DOUBLE` when operands have mixed numeric types.
- **Comparison & Ordering:** Strict SQL `NULL` propagation (comparisons with `NULL` return `NULL`). Deterministic sorting comparator with explicit `NULLS FIRST` / `NULLS LAST` logic and tie-breaking.
- **Grouping:** Exact equality (`exact_equal`) and hashing (`hash()`) treat `NULL == NULL` for grouping keys, compliant with the SQL standard.

---

### 2.2 Storage & Chunked CSV Reading (`axiom::CsvReader`)

The CSV reader provides high-performance, streaming ingestion without materializing the entire file in RAM:
- **RFC-4180 Compliance:** Handles quoted fields, embedded linebreaks (`\n` and `\r\n`), embedded delimiters (commas inside quotes), and escaped quotes (`""` and `\"`).
- **Chunked Reading:** Configurable internal 64KB I/O buffer with `read_batch(max_rows)` producing bounded `RecordBatch` instances.
- **NULL Handling:** Distinguishes unquoted empty values, explicit configured NULL strings (e.g. `"NULL"`), and empty strings.
- **Fast Numeric Parsing:** Uses `<charconv>` `std::from_chars` for high-throughput integer conversion.

---

### 2.3 Lexer and Parser

- **Lexer (`axiom::Lexer`):** Scans SQL text into a stream of tokens, tracking 1-based line and column coordinates. Supports line comments (`-- ...`) and block comments (`/* ... */`).
- **Parser (`axiom::Parser`):**
  - **Pratt Precedence Climbing:** Explicit precedence hierarchy for operators:
    `OR` < `AND` < `NOT` < Comparisons (`=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `IS NULL`, `IS NOT NULL`) < Additive (`+`, `-`) < Multiplicative (`*`, `/`, `%`) < Unary (`+`, `-`).
  - **Statement Syntax:** Full parsing for `SELECT`, `FROM`, `INNER JOIN ... ON`, `WHERE`, `GROUP BY`, `HAVING`, `ORDER BY`, `LIMIT`, and `OFFSET`.
  - **Diagnostics:** Produces descriptive syntax errors with source location information.

---

### 2.4 Binder & Semantic Analysis (`axiom::Binder`)

The binder resolves names against the catalog and validates semantic constraints:
- **Table Scoping:** Tracks active tables in `FROM` and `JOIN` clauses, assigning global column offsets for composite tuples.
- **Ambiguity Detection:** Detects and reports ambiguous column references across joined tables.
- **Alias Propagation:** Resolves column aliases defined in `SELECT` for use in `ORDER BY` clauses.
- **GROUP BY Enforcement:** Validates that every non-aggregate expression in `SELECT`, `HAVING`, and `ORDER BY` matches a `GROUP BY` key.
- **Aggregate Placement:** Rejects aggregates placed in `WHERE` clauses or nested inside other aggregates.
- **Star Expansion:** Expands `*` and `table.*` into concrete bound column references.

---

### 2.5 Logical Planning & Rewriting

Translates bound ASTs into a logical operator tree:
- `LogicalScan`: References base table definition and schema.
- `LogicalJoin`: Inner join with join predicate.
- `LogicalFilter`: Boolean selection condition.
- `LogicalAggregate`: Grouping keys and unique aggregate functions.
- `LogicalSort`: Order items with ASC/DESC and NULL ordering flags.
- `LogicalProjection`: Output expressions and column aliases.
- `LogicalLimit`: Row count and offset limits.

#### Aggregate Expression Rewriting
Downstream operators (HAVING, Sort, Projection) are rewritten:
- Expressions matching group keys are mapped to key column indices `0 .. k-1`.
- Aggregate expressions are mapped to aggregate result columns `k .. k+m-1`.

---

### 2.6 Vectorized Physical Execution

The execution engine uses a batch-oriented iterator model (`Volcano` model operating over `RecordBatch` vectors of configurable size, default 1,024 rows):
- **Lifecycle:** Operators expose `init()`, `next() -> std::unique_ptr<RecordBatch>`, and `close()`.
- **`PhysicalScan`:** Streams batches from disk using `CsvReader`.
- **`PhysicalFilter`:** Evaluates predicates in batch, producing a selection vector and sliced batch.
- **`PhysicalProjection`:** Evaluates projected expressions column-by-column.
- **`PhysicalHashJoin`:**
  - Build phase: Hashes build table on join key into an in-memory hash table.
  - Probe phase: Probes hash table batch-by-batch from left child, correctly handling duplicate keys (join multiplicity) and skipping NULL keys.
- **`PhysicalHashAggregate`:**
  - Hashes composite group keys and maintains running accumulators for `COUNT(*)`, `COUNT(col)`, `SUM`, `AVG`, `MIN`, `MAX`.
  - Complies with SQL standard for empty tables: returns 0 rows for grouped queries, and 1 row (`COUNT=0`, others `NULL`) for scalar queries.
- **`PhysicalSort`:** Collects child batches, pre-evaluates sort keys into columnar arrays, and executes a deterministic `std::stable_sort`.
- **`PhysicalLimit`:** Tracks offset and emitted rows, short-circuiting child operator execution once the limit is met.

---

## 3. Memory Safety & Engineering Standards

- **Strict RAII:** All dynamic allocations are managed via `std::unique_ptr` and `std::shared_ptr`. Zero uncontrolled raw owning pointers.
- **Warning Clean:** Compiles warning-free on MSVC 2022 with `/W4`, `/permissive-`, and `/Z7`.
- **Exception Safety:** Structured exceptions inherit from `AxiomException` with categorized error codes and source locations.

