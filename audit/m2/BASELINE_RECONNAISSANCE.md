# Project Axiom — Milestone M2 Baseline Reconnaissance Report

**Date:** 2026-11-13  
**Repository:** `E:\Axiom`  
**Branch:** `master`  
**Baseline Git Commit:** `7b4d5b51bc4e34ffcd6d362c0c7f297455eab122` (Parent M1 commit: `b7d31627f72eb70de4cc3e1714039cd741b1d3a0`)  
**Auditor:** Principal Database Systems Engineer & Performance Architect

---

## 1. Baseline Verification & Git State

Before any M2 modifications, the repository was inspected and audited directly on the Windows 11 host:
- Git status was clean on branch `master`.
- The reported M1 commit `b7d31627f72eb70de4cc3e1714039cd741b1d3a0` is the direct parent of current HEAD (`7b4d5b51bc4e34ffcd6d362c0c7f297455eab122`), which contains cosmetic newline terminations.
- Clean compilation was independently verified for both **Release** (`/O2 /Ob2 /NDEBUG /W4 /permissive- /Z7`) and **Debug** (`/Od /RTC1 /W4 /permissive- /Z7`) using MSVC 19.43.34808 and Ninja 1.12.1.
- Crucial environmental finding: The build host has ~1.2 GB of available free RAM. Concurrency of Ninja was configured to `--parallel 2` in `scripts/build.ps1` to prevent MSVC compiler out-of-heap-space errors (`C1060`) caused by 16 simultaneous C++20 standard library compilation instances.

---

## 2. Verification Suites & Baseline Assertions

The reported M1 test counts were verified and categorized into distinct test types:

| Suite | File | Checks / Assertions | Result | Notes |
| :--- | :--- | :---: | :---: | :--- |
| **Unit Tests** | `tests/unit/test_main.cpp` | **204** assertions | **PASS** | Evaluates types (42), lexer (38), parser (45), CSV (28), binder (21), and operators (30). |
| **Integration Tests** | `tests/integration/test_end_to_end.cpp` | **24** assertions | **PASS** | 6 multi-table scenarios testing scans, inner joins, groups, having, limits, and plan explain. |
| **Differential Verification** | `tests/differential/test_differential.py` | **120** distinct queries | **PASS** | Order-independent multiset and ordered comparisons against DuckDB 2.0.0 and SQLite 3.45.3. |
| **Total Baseline Checks** | | **348** checks | **100% PASS** | Zero failures or ignored assertions. |

---

## 3. M1 Measured Performance Baseline

The baseline analytical benchmark workload (`benchmarks/workloads/benchmark_suite.py`) was executed on the 100,000-row `orders` dataset joined against the 20,000-row `items` dataset:

| Query ID | Workload Type | Baseline Latency (Avg) | Baseline Throughput | Output Rows |
| :--- | :--- | :---: | :---: | :---: |
| **Q1** | Table Scan with Filter | 91.2 ms | 1,096,127 rows/s | 7,454 |
| **Q2** | Scalar Aggregation (`COUNT`, `SUM`) | 81.0 ms | 1,235,245 rows/s | 1 |
| **Q3** | Hash Group-By Aggregation | 92.2 ms | 1,084,453 rows/s | 4 |
| **Q4** | Hash Inner Join (100k x 20k) | 195.6 ms | 511,190 rows/s | 50,030 |
| **Q5** | Join + Group-By + Aggregation | 142.4 ms | 702,039 rows/s | 8 |
| **Q6** | Sort + Top-50 Limit | 138.0 ms | 724,450 rows/s | 50 |

---

## 4. Current Memory Architecture & In-Core Limitations

An in-depth review of `RecordBatch`, physical operator lifecycles, and query execution revealed the following memory characteristics:

1. **`RecordBatch` Layout:**
   - Columnar: Each batch holds `std::vector<std::vector<Value>>` where `columns_[col_idx][row_idx]` is a `Value`.
   - `Value` is a tagged `std::variant<std::monostate, bool, int64_t, double, std::string>`.
   - Short strings (SSO) are internal to `std::string`, but long strings allocate dynamically on the process heap without tracking.
2. **`PhysicalHashJoin` Memory Consumption:**
   - The entire build-side table is ingested and stored in a `std::unordered_multimap<Value, std::vector<Value>>`.
   - Dynamic hash table node overhead: `std::unordered_multimap` allocates a node per entry plus bucket array pointers.
   - For a multi-gigabyte build table, this causes unbounded memory growth and process aborts under tight memory budgets.
3. **`PhysicalHashAggregate` Memory Consumption:**
   - Groups are held in an in-memory `std::unordered_map<std::vector<Value>, std::vector<AggregateAccumulator>>`.
   - For high-cardinality queries (e.g. 10M distinct keys), memory grows linearly with distinct keys.
4. **`PhysicalSort` Memory Consumption:**
   - Materializes every row from the child into `std::vector<std::vector<Value>> sorted_rows_` before sorting with `std::stable_sort`.
   - Completely unsuited for datasets larger than available RAM.
5. **Streaming Components:**
   - `PhysicalScan` streams batches in bounded row chunks (default 1,024 rows) using a 64 KB I/O buffer.
   - `PhysicalFilter` and `PhysicalProjection` operate batch-by-batch without unbounded buffering.

---

## 5. Architectural Requirements for Milestone M2

To achieve out-of-core scaling, deterministic memory boundedness, and performance optimization in M2:

1. **Global Memory Subsystem (`QueryMemoryManager`):**
   - Centralized memory budget with hierarchical reservations (`MemoryReservation`).
   - Tracking of both container capacities (vectors, hash maps) and variable-length payload bytes (strings).
   - Process-level OS telemetry (`GetProcessMemoryInfo` on Windows) to monitor process RSS alongside accounted memory.
2. **Disk Spilling Infrastructure (`SpillManager` & `SpillPartition`):**
   - High-throughput binary serialization format for `RecordBatch` avoiding raw pointer or platform-specific serialization.
   - Schema validation, magic numbers, checksums/truncation detection, and buffered sequential I/O.
   - RAII spill file lifecycle management with guaranteed cleanup on success, exceptions, and early query termination.
3. **Adaptive External Hash Join:**
   - In-memory fast path when build side fits within memory reservation.
   - Multi-pass Grace Hash Join when reservation is exceeded: hash-partitions build and probe streams into temporary spilled partitions, then processes partitions individually.
   - Skew handling: Fallback or deterministic sub-partitioning for oversized partitions.
4. **External Hash Aggregation:**
   - In-memory aggregation while within memory threshold.
   - When memory is exhausted, partitions and spills partial aggregate groups to disk, followed by partition-local merging.
5. **External Merge Sort:**
   - In-memory sorting of runs bounded by memory budget.
   - Spills sorted runs to temporary binary files.
   - Multi-way merge stream using a priority queue (min-heap) over active run readers.
   - Top-K optimization for `ORDER BY ... LIMIT N`.
6. **Query Optimization:**
   - Predicate pushdown through projections and joins into scans.
   - Projection column pruning to avoid parsing/copying unused CSV columns.
   - Join order and build/probe side selection based on sampled input size.
   - `EXPLAIN ANALYZE` / profiling instrumentation with execution timings and spill telemetry.
7. **Multi-GB Workloads & Differential Verification:**
   - Generate multi-GB synthetic datasets (100MB, 1GB, 5GB).
   - Validate that external operators spill to disk under constrained memory budgets (e.g., 256MB / 512MB) and return identical results to reference engines.

