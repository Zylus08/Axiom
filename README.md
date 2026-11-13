# Project Axiom — Milestone M2

An out-of-core, memory-bounded analytical SQL database engine built from scratch in modern C++20.

Project Axiom implements a complete, self-contained relational query processing pipeline capable of executing multi-gigabyte analytical workloads under strict memory limits:
- **Lexical & Syntactic Analysis:** Tokenizer with source location tracking, recursive-descent and Pratt-style expression parser.
- **Semantic Analysis & Binding:** Catalog schema registration, column resolution, and expression type checking.
- **Query Optimization:** Rule-based optimizer (`LogicalOptimizer`) supporting conjunctive predicate pushdown, Top-K pushdown, and equijoin side selection.
- **Centralized Memory Management:** `QueryMemoryManager` enforcing operator memory reservations, dynamic container capacity tracking, and Windows OS working set monitoring.
- **Binary Spill Infrastructure:** High-throughput `BinarySerializer` (`AXMB` v1) with bit-packed NULL vectors and 32-bit integrity checksums, managed by RAII `SpillManager`.
- **Out-of-Core Physical Operators:**
  - **Grace Hash Join:** $B=16$ partition hash spilling on build and probe streams with sequential partition pair processing.
  - **External Hash Aggregation:** $P=16$ partition hash spilling for high-cardinality groups; constant $O(1)$ streaming for scalar aggregates.
  - **External Merge Sort:** Bounded run generation, priority-queue multi-way merge, and Top-K bounded max-heap optimization.
- **CLI & Telemetry:** `--memory-limit <size>`, `--temp-dir <path>`, `--max-temp-disk <size>`, `--profile`, and `EXPLAIN ANALYZE`.

Axiom contains **zero** third-party database engines (no SQLite, DuckDB, PostgreSQL) and **zero** third-party parsers or query planning libraries.

---

## Architecture Overview

```text
SQL Query -> Lexer -> Parser -> Binder -> Logical Planner -> Logical Optimizer -> Physical Planner -> Vectorized Engine (with MemoryManager & SpillManager) -> QueryResult
```

---

## Directory Structure

```text
axiom/
  CMakeLists.txt
  README.md
  LICENSE
  include/axiom/
    common/         # MemoryManager, QueryContext, RecordBatch, QueryEngine
    types/          # Value, Schema, TypeKind
    parser/         # Lexer, Parser, AST, Tokens
    catalog/        # Catalog, TableDef
    binder/         # Binder, BoundAST
    planner/        # LogicalPlan, LogicalPlanner, LogicalOptimizer
    execution/      # PhysicalOperators, ExpressionEvaluator, PhysicalPlanner
    storage/        # CsvReader, BinarySerializer, SpillManager
  src/              # Implementation files
  tests/
    unit/           # C++ Unit test suite (466 checks)
    integration/    # C++ Integration and Adversarial tests (28 checks)
    differential/   # Python DuckDB differential harness (120 queries)
    fixtures/       # Deterministic test fixtures
  benchmarks/
    generators/     # Multi-scale streaming generator (100MB, 1GB, 5GB)
    data/           # Benchmark datasets
    run_m2_benchmarks.py # Multi-scale benchmark harness
  audit/
    m2/             # Complete audit reports, ADRs, and benchmark results
  scripts/          # PowerShell build & test automation
  docs/             # Architectural, performance, and specification docs
```

---

## Building and Testing

### Prerequisites
- Windows 11 x64
- Visual Studio 2022 (MSVC v143 with C++20 support)
- CMake 3.20+ and Ninja
- Python 3.10+ (for differential testing and benchmark generation)

### Build
```powershell
.\scripts\build.ps1 -Config Release
```

### Run Full Test Suite
```powershell
# 1. C++ Unit Tests (466 checks)
.\build\axiom_unit_tests.exe

# 2. C++ Integration Tests (24 checks)
.\build\axiom_integration_tests.exe

# 3. Python Adversarial Robustness Suite (4 scenarios)
python tests/integration/test_m2_adversarial.py

# 4. DuckDB Differential Verification Suite (120 queries)
python tests/differential/test_differential.py
```

### Run Benchmarks
```powershell
# Generate datasets (100MB, 1GB, 5GB)
python benchmarks/generators/generate_multi_gb.py --tier all

# Execute benchmark suite comparing Axiom under memory constraints against DuckDB
python benchmarks/run_m2_benchmarks.py
```

---

## CLI Usage Examples

```powershell
# Execute query with 256MB memory limit and execution profiling
.\build\axiom.exe -t orders=benchmarks/data/orders_1gb.csv -e "SELECT status, COUNT(*), SUM(quantity) FROM orders WHERE quantity > 5 GROUP BY status" --memory-limit 256MB --profile

# Execute EXPLAIN ANALYZE on a join query
.\build\axiom.exe -t orders=orders.csv -t items=items.csv -e "EXPLAIN ANALYZE SELECT o.status, i.category, COUNT(*) FROM orders o JOIN items i ON o.item_id = i.item_id GROUP BY o.status, i.category" --memory-limit 32MB
```

---

## Verification & Acceptance Summary

- **Total Automated Verification Checks:** **614 / 614 PASSED (100%)**
  - Unit Assertions: 466
  - Integration Assertions: 24
  - Differential Queries: 120
  - Adversarial Scenarios: 4
- **Multi-GB Scalability:** Verified up to 5 GB (164,422,022 rows) bounded under 512 MB RAM.
- **Milestone Decision:** **PASS** (Fully documented in `audit/m2/M2_REPORT.md`).
