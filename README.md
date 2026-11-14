<div align="center">

# AXIOM

### An analytical SQL engine, built from first principles in C++20.

**Custom parser · Query optimizer · Batch execution · Out-of-core joins, aggregation & sorting**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)](CMakeLists.txt)
[![Release](https://img.shields.io/badge/release-v0.1.0-2f855a)](https://github.com/Zylus08/Axiom/releases/tag/v0.1.0)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

[Architecture](docs/ARCHITECTURE.md) · [SQL support](docs/SQL_SUPPORT.md) · [Benchmarks](audit/rc1_1/BENCHMARK_CORRECTIONS.md) · [Release audit](audit/rc1_1/FINAL_RELEASE_DECISION.md)

</div>

---

## Why AXIOM?

What happens between writing `SELECT ... GROUP BY ...` and receiving the result?

AXIOM explores that question by implementing an analytical query engine **without embedding SQLite, DuckDB, PostgreSQL, a third-party SQL parser, or a database execution framework**. It reads external CSV files, understands a documented SQL subset, creates query plans, and runs batch-oriented physical operators. When a join, aggregation, or sort exceeds its configured operator-memory budget, AXIOM can spill intermediate data to disk.

This is a **read-only, experimental analytical engine**, not a transactional DBMS or a drop-in replacement for mature databases. The focus is correctness, resource-aware algorithms, inspectable architecture, and reproducible measurement.

## What it implements

| Subsystem | Implementation |
|---|---|
| **SQL front end** | Lexer with source locations; recursive-descent statement parser and Pratt-style expressions; AST and semantic binder |
| **Types & semantics** | `BIGINT`, `DOUBLE`, `VARCHAR`, `BOOLEAN`, `NULL`; SQL three-valued logic; type checking and aggregate validation |
| **Planning** | Logical/physical plans; conjunctive predicate pushdown, Top-K pushdown, and join-side selection |
| **Execution** | Batch-oriented scans, filters, projections, inner joins, hash aggregates, ordering, and limits |
| **Joins** | In-memory hash join and disk-partitioned block Grace Hash Join with bounded build blocks and probe rewinding for skewed partitions |
| **Aggregation** | `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`; in-memory and partitioned external hash aggregation |
| **Sorting** | Top-K heap optimization; external sorted runs and hierarchical multi-pass merge with bounded fan-in |
| **Spill storage** | AXMB v1 binary temporary batches, NULL bitmaps, non-cryptographic checksums, disk quotas, and RAII cleanup |
| **Diagnostics** | `EXPLAIN`, `EXPLAIN ANALYZE`, query profiles, memory metrics, and disk-spill telemetry |

### Architecture

```text
                              SQL query
                                  │
                          Lexer → Parser
                                  │
                           AST → Binder
                                  │
                           Logical plan
                                  │
                          Rule optimizer
                                  │
                           Physical plan
                                  │
          ┌───────────────────────┴───────────────────────┐
          │          Batch-oriented execution             │
          │  CSV scan → filter → join → aggregate → sort   │
          └───────────────────────┬───────────────────────┘
                                  │
                 ┌────────────────┴────────────────┐
                 │ Query memory & spill management │
                 │ Reservations · AXMB · temp disk │
                 └────────────────┬────────────────┘
                                  │
                                Results
```

The execution model is currently **single-threaded**. Memory limits apply to engine-managed query/operator allocations; they are **not** OS-enforced limits on the entire process working set.

## Quick start

### Requirements

The tested development environment is **Windows 11 x64**, **Visual Studio 2022 C++ Build Tools (MSVC v143)**, **CMake**, and **Ninja**. Python 3.10+ is useful for differential tests and benchmark scripts, not for AXIOM's C++ query runtime.

```powershell
# Clone
git clone https://github.com/Zylus08/Axiom.git
cd Axiom

# Build (script configures the MSVC developer environment)
.\scripts\build.ps1 -Config Release

# Build and run registered CTest tests
.\scripts\build.ps1 -Config Release -Test

# Inspect CLI options
.\build\axiom.exe --help
```

The supplied `scripts/build.ps1` uses Visual Studio's developer command script and generates a Ninja build under `build/`. If your toolchain lives elsewhere, consult the script before running it.

### Execute your first query

Create `people.csv`:

```csv
id,name,age,city
1,Ada,28,London
2,Grace,34,New York
3,Linus,31,Helsinki
4,Margaret,34,London
```

Register it as a table and supply explicit types:

```powershell
.\build\axiom.exe `
  -t "people=people.csv:id:bigint,name:varchar,age:bigint,city:varchar" `
  -e "SELECT city, COUNT(*) AS people FROM people WHERE age >= 30 GROUP BY city ORDER BY people DESC" `
  --format table
```

Or let AXIOM infer a basic schema from CSV input:

```powershell
.\build\axiom.exe -t "people=people.csv" -e "SELECT name, age FROM people ORDER BY age DESC"
```

**Schema-inference caution:** The current CLI inspects the initial data row for types and performs simple comma splitting during header/type inference. For production-like CSV fixtures, especially quoted header fields or mixed-type columns, prefer **explicit schemas**.

### Memory-constrained analytical query

```powershell
.\build\axiom.exe `
  -t "orders=benchmarks/data/orders_1gb.csv" `
  -e "SELECT status, COUNT(*), SUM(quantity) FROM orders GROUP BY status" `
  --memory-limit 64MB `
  --temp-dir ".\tmp" `
  --max-temp-disk 10GB `
  --profile
```

The large benchmark CSV above is generated locally; it is not expected to be in a fresh checkout.

For other modes:

```powershell
# Query from a SQL file
.\build\axiom.exe -t "people=people.csv" -f query.sql

# Show query plan
.\build\axiom.exe -t "people=people.csv" -e "SELECT * FROM people WHERE age > 30" --explain

# Machine-readable results
.\build\axiom.exe -t "people=people.csv" -e "SELECT * FROM people" --format json

# Interactive shell (register tables first)
.\build\axiom.exe -t "people=people.csv"
```

CLI options include `-e/--query`, `-f/--file`, `-t/--table`, `--batch-size`, `--memory-limit`, `--temp-dir`, `--max-temp-disk`, `--profile`, `--explain`, `--explain-analyze`, and `--format table|csv|json`. Run `--help` for the authoritative syntax.

## SQL support

```sql
SELECT o.item_id,
       COUNT(*) AS order_count,
       SUM(o.quantity) AS units
FROM orders o
WHERE o.quantity > 0
GROUP BY o.item_id
HAVING COUNT(*) > 1
ORDER BY units DESC
LIMIT 10;
```

Supported features include `SELECT`, `FROM`, `INNER JOIN ... ON`, `WHERE`, `GROUP BY`, `HAVING`, `ORDER BY` (including NULL ordering), `LIMIT`, `OFFSET`, arithmetic/comparison/boolean expressions, aliases, and the five core aggregate functions.

Unsupported in v0.1.0: outer joins, subqueries/CTEs, window functions, set operations, `DISTINCT` aggregates, and data mutations such as `INSERT`, `UPDATE`, or `DELETE`. See the [SQL support matrix](docs/SQL_SUPPORT.md) and [limitations](docs/LIMITATIONS.md).

## Benchmarks & verification

These figures are **reported results from the v0.1.0 milestone and RC1.1 audit artifacts**, not universal performance guarantees. Hardware, query shapes, data distribution, cache state, threading, and CSV ingestion method affect comparisons.

| Reported result | v0.1.0 evidence |
|---|---:|
| Largest input processed | **5.37 GB / 164,422,022 rows** |
| Large-scale external aggregation | **3.74 GB cumulative spilled data** reported |
| RC1.1 external-sort working set (before → after) | **150.86 MB → 17.68 MB** |
| External-sort improvement | **88.3% lower peak working set** in the measured case |
| Reported CSV parsing throughput | **~1.05 million rows/s** in the cited Release workload |
| Reported final verification | **644/644** across unit, integration, differential, adversarial, and CLI smoke checks |
| Differential comparisons | **120/120** reported against DuckDB 2.0.0 |

The final-report and repository audit summaries do not all use identical check totals or peak-memory numbers; consult the executable test logs and original benchmark files for the exact run being cited. Results above are presented as **reported**, not independently reproduced here.

**Comparative performance:** AXIOM is slower than DuckDB on multiple measured analytical workloads. AXIOM/SQLite timings also depend heavily on whether CSV-import costs are included. The project does **not** claim to outperform mature SQL engines generally.

**Memory terminology matters:** Query memory reservations, peak OS working set, and private committed memory are different measures. A `--memory-limit 16MB` value is not a guarantee that process RSS stays under 16 MB. The RC1.1 audit specifically repaired an external-sort merge that had loaded hundreds of unaccounted batches at once.

For reproducibility and methodology, see [the benchmark corrections](audit/rc1_1/BENCHMARK_CORRECTIONS.md), [memory forensics](audit/rc1_1/MEMORY_DISCREPANCY.md), and [regression results](audit/rc1_1/REGRESSION_RESULTS.md).

### Run the verification suites

```powershell
# C++ build / CTest
.\scripts\build.ps1 -Config Release -Test

# Standalone C++ test executables
.\build\axiom_unit_tests.exe
.\build\axiom_integration_tests.exe

# Adversarial cases
python tests/integration/test_m2_adversarial.py

# Independent SQL differential comparison (requires Python DuckDB dependencies)
python tests/differential/test_differential.py
```

### Generate benchmark data

```powershell
python benchmarks/generators/generate_multi_gb.py --tier all
python benchmarks/run_m2_benchmarks.py
```

The full scale suite can generate several gigabytes of data and temporary spill files. Check available disk space and the benchmark scripts' options before running it.

## Repository layout

```text
Axiom/
├── include/axiom/       Public engine interfaces and subsystem headers
├── src/                 Parser, binder, planner, operators, storage, CLI
├── tests/               C++ unit/integration and Python differential tests
├── benchmarks/          Data generators, workloads, benchmark harnesses
├── audit/               Milestone and release-candidate evidence
│   ├── m2/
│   ├── m3/
│   └── rc1_1/
├── docs/                Architecture, SQL support, performance, limitations
├── scripts/             PowerShell build/test automation
├── CMakeLists.txt
└── LICENSE
```

## Design boundaries

AXIOM intentionally omits transactions, persistent table storage, indexing, networking, and full SQL compatibility. It currently runs query operators on one thread, and raw CSV parsing is a major performance cost. Hash-partitioned algorithms and binary temporary spill files are designed for analytical workloads, not adversarial multi-tenant hosting or untrusted network exposure.

**Potential directions:** parallel execution, native columnar storage, stronger cost-based planning, additional SQL semantics, and wider platform validation. None of these are promised in v0.1.0.

## License

Licensed under the [Apache License 2.0](LICENSE).

---

<div align="center">

**From SQL text to query results—without borrowing a database engine.**

Built and maintained by [Saksham Mishra](https://github.com/Zylus08).

</div>
