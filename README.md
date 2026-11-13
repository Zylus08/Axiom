# Project Axiom

An analytical SQL database engine built from scratch in modern C++20.

Milestone M1 delivers a functioning, self-contained analytical SQL engine capable of loading external CSV datasets, parsing SQL queries with a recursive-descent and Pratt expression parser, performing semantic binding and type validation, compiling logical and physical query plans, executing batch-oriented operators (CSV scan, filter, projection, hash join, hash aggregation, sort, and limit), and returning formatted results.

Axiom contains **zero** third-party database engines (no SQLite, DuckDB, PostgreSQL) and **zero** third-party parsers. All query processing is implemented from first principles.

---

## Architecture Overview

```
SQL Query -> Lexer -> Parser -> Binder -> Logical Planner -> Physical Planner -> Vectorized Engine -> QueryResult
```

- **Batch Execution:** Volcano-style iterator model over `RecordBatch` columnar chunks (default 1,024 rows).
- **Type System:** `BIGINT`, `DOUBLE`, `VARCHAR`, `BOOLEAN`, and `NULL` with strict SQL 3-valued boolean logic.
- **Join Operator:** Hash inner join supporting equijoins, arbitrary join predicates, correct duplicate key multiplicity, and NULL key elimination.
- **Aggregate Operator:** Hash aggregation supporting `COUNT(*)`, `COUNT(col)`, `SUM`, `AVG`, `MIN`, `MAX`, composite keys, NULL key grouping, and standard empty table semantics.
- **CSV Storage:** RFC-4180 streaming reader supporting chunked I/O, quoted strings, escaped quotes, embedded commas, and configurable NULL representations.

---

## Directory Structure

```text
axiom/
  CMakeLists.txt
  README.md
  LICENSE
  include/axiom/
    common/         # Error, RecordBatch, QueryEngine
    types/          # Value, Schema, TypeKind
    parser/         # Lexer, Parser, AST, Tokens
    catalog/        # Catalog, TableDef
    binder/         # Binder, BoundAST
    planner/        # LogicalPlan, LogicalPlanner
    execution/      # PhysicalOperators, ExpressionEvaluator, PhysicalPlanner
    storage/        # CsvReader
  src/              # Implementation files
  tests/
    unit/           # C++ Unit test suite (204 checks)
    integration/    # C++ End-to-end integration tests (19 checks)
    differential/   # Python DuckDB/SQLite differential harness (120 queries)
    fixtures/       # Deterministic CSV fixtures
  benchmarks/
    workloads/      # Analytical workload benchmark suite
    generators/     # Synthetic CSV generator
  scripts/          # PowerShell build & test helpers
  docs/             # Architectural and technical documentation
  audit/            # M1 Audit and Acceptance Report
```

---

## Building Axiom on Windows

### Prerequisites
- Visual Studio 2022 C++ Build Tools (with C++20 support)
- CMake 3.20+ and Ninja (included with VS BuildTools)
- Python 3.10+ (for differential tests and benchmarks)

### Quick Build with PowerShell
```powershell
# Build in Release mode and run C++ tests
.\scripts\build.ps1 -Config Release -Test

# Clean rebuild in Debug mode
.\scripts\build.ps1 -Config Debug -Clean -Test
```

### Standard CMake Commands
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

---

## Using the CLI (`axiom`)

```powershell
# Display help and version
.\build\axiom.exe --help
.\build\axiom.exe --version

# Run SQL query over CSV tables
.\build\axiom.exe `
  -t customers=tests/fixtures/customers.csv:id:bigint,name:varchar,country:varchar,active:bool `
  -t orders=tests/fixtures/orders.csv:order_id:bigint,customer_id:bigint,amount:double,status:varchar `
  -e "SELECT c.country, COUNT(*) AS num_orders, SUM(o.amount) AS total FROM customers c JOIN orders o ON c.id = o.customer_id GROUP BY c.country ORDER BY total DESC" `
  --format table

# Output formats: table, csv, json
.\build\axiom.exe -t customers=tests/fixtures/customers.csv -e "SELECT * FROM customers WHERE active = true" --format json

# Visualizing query plans with EXPLAIN
.\build\axiom.exe -t customers=tests/fixtures/customers.csv -e "SELECT country, COUNT(*) FROM customers GROUP BY country" --explain

# Interactive REPL shell
.\build\axiom.exe -t customers=tests/fixtures/customers.csv
```

---

## Testing & Verification

```powershell
# 1. Run Unit Tests (204 checks)
.\build\axiom_unit_tests.exe

# 2. Run Integration Tests (19 checks)
.\build\axiom_integration_tests.exe

# 3. Run Differential Verification against DuckDB (120 queries)
python tests/differential/test_differential.py

# 4. Run Analytical Benchmarks (100k rows)
python benchmarks/workloads/benchmark_suite.py
```

---

## Documentation

- [Architecture Specification](docs/ARCHITECTURE.md)
- [SQL Support Matrix](docs/SQL_SUPPORT.md)
- [Correctness & Verification](docs/CORRECTNESS.md)
- [Performance Analysis](docs/PERFORMANCE.md)
- [Known Limitations & M2 Roadmap](docs/LIMITATIONS.md)
- [Milestone M1 Acceptance Report](audit/M1_REPORT.md)
