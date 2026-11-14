# Project AXIOM — Milestone M3: Baseline Reconnaissance & Forensic Audit

**Repository:** E:\Axiom  
**Branch:** master  
**Starting Commit:** `945c04b` (feat(m2): deliver out-of-core execution, memory management, and query optimization)  
**Host Target:** Windows 11 x64, MSVC 19.43, Ninja 1.12.1, CMake 3.30.5  
**Auditor:** Principal Database Systems Engineer & Release Verification Lead  
**Date:** 2026-11-14  

---

## 1. Baseline Verification & Test Suite Accounting

A full independent re-execution of all verification suites against the clean M2 commit (`945c04b`) was conducted in both Debug and Release configurations:

| Test Suite / Binary | File Location | Executed Checks / Assertions | Result | Notes |
|:---|:---|:---:|:---:|:---|
| **Unit Tests** | `build/axiom_unit_tests.exe` | **466 checks** | PASS (0.05s) | Lexer, parser, binder, CSV reader, memory manager, serialization, external operators, types |
| **Integration Tests** | `build/axiom_integration_tests.exe` | **24 checks** | PASS (0.03s) | End-to-end SQL queries, joins, aggregates, sort, optimizer |
| **Differential Suite** | `tests/differential/test_differential.py` | **120 queries** | PASS (3.8s) | 100% equivalence vs DuckDB 2.0.0 on identical datasets |
| **Adversarial Suite** | `tests/integration/test_m2_adversarial.py` | **4 scenarios** | PASS (0.9s) | Unwritable temp dir, max disk quota, 10KB memory limit, early LIMIT cleanup |
| **Total Baseline Checks** | — | **614 / 614** | **100% PASS** | Zero failures, zero test skips |

---

## 2. Forensic Discovery: The 5 GB Zero-Spill Anomaly

In Milestone M2's `audit/m2/BENCHMARK_RESULTS.csv`, all 5 GB workloads reported `0.00 MB spilled` across all queries:

```csv
5gb,Q1_AGG_FILTER,Filtered Aggregation,5120.0,512MB,137963.73,5.2,0.00,0,N/A,N/A
5gb,Q2_HIGH_CARD_AGG,High-Card Agg,5120.0,512MB,171630.19,313.5,0.00,0,N/A,N/A
5gb,Q3_EQUI_JOIN,Equi-Join,5120.0,512MB,208320.60,159.2,0.00,0,N/A,N/A
5gb,Q4_TOPK_SORT,Top-K Heap Sort,5120.0,512MB,121354.35,5.0,0.00,0,N/A,N/A
```

Our forensic code inspection revealed the root causes:

1. **Hardcoded Memory Limit Thresholds in Physical Operators:**
   - In `src/execution/physical_hash_join.cpp` (lines 356-364):
     ```cpp
     if (is_equi_join_ && ctx_ && ctx_->memory_manager() && ctx_->memory_manager()->has_limit()) {
         size_t budget = ctx_->memory_manager()->max_budget();
         if (budget <= 64 * 1024 * 1024) {  // HARDCODED 64 MB CHECK!
             is_spilled_ = true;
             execute_grace_hash_partitioning();
             return;
         }
     }
     ```
   - In `src/execution/physical_hash_aggregate.cpp` (lines 407-416):
     ```cpp
     if (num_keys > 0 && ctx_ && ctx_->memory_manager() && ctx_->memory_manager()->has_limit()) {
         size_t budget = ctx_->memory_manager()->max_budget();
         if (budget <= 64 * 1024 * 1024) {  // HARDCODED 64 MB CHECK!
             is_spilled_ = true;
             execute_external_aggregation();
             return;
         }
     }
     ```
   Because M2 benchmarked 5 GB workloads with `--memory-limit 512MB` and 1 GB with `--memory-limit 256MB`, `budget <= 64MB` was **false**. Consequently, neither Grace Hash Join nor External Hash Aggregation was ever triggered at 1 GB or 5 GB!

2. **Top-K Bounded Heap Optimization:**
   - In `src/execution/physical_sort.cpp` (lines 212-255), any query with `LIMIT k` where `k <= 50,000` executes using an in-memory `std::priority_queue` max-heap holding at most $K$ rows. Q4 had `LIMIT 50`, so only 50 rows were kept in memory, bypassing the external sort pipeline entirely.

3. **Data Cardinality Fitting Into 512 MB:**
   - In `orders_5gb.csv`, Q2 grouped by `item_id` (cardinality 500,000 distinct items). 500,000 groups in `std::unordered_map` consumed ~313.5 MB of RAM, which fit below 512 MB.
   - In Q3, `items_5gb.csv` had 500,000 rows (12.5 MB CSV), so the build-side hash table consumed ~159.2 MB of RAM, which also fit below 512 MB.

**Conclusion:** The M2 benchmark did **not** exercise out-of-core execution at 5 GB scale. Milestone M3 must implement dynamic, reservation-driven spilling and execute genuine multi-gigabyte external join, sort, and aggregation workloads.

---

## 3. Forensic Discovery: Partition Overflow & Skew Risks

1. **Fixed 16-Way Partitioning:**
   - Both `PhysicalHashJoin` and `PhysicalHashAggregate` divide data into 16 partitions (`hash(key) % 16`).
   - If key skew is severe (e.g. 99% of rows have the same key or hash collision), a single partition will receive almost all rows.
   - When `load_next_partition()` reads that partition into memory (`partition_hash_table_` or `groups`), it allocates memory without bounds or overflow mitigation.
2. **Missing Recursive Repartitioning:**
   - If a partition pair exceeds available memory, there is no second-level partitioning (salted hash seeds) or block-nested-loop fallback.

---

## 4. Forensic Discovery: Memory Accounting & Instrumentation

1. **Operator Reservations Were Not Active:**
   - While `QueryMemoryManager` and `MemoryReservation` exist and passed unit tests, the physical operators (`PhysicalSort`, `PhysicalHashJoin`, `PhysicalHashAggregate`, `CsvReader`) never created or held `MemoryReservation` instances during real query execution.
   - As a result, `QueryTelemetry::peak_accounted_memory` always reported `0 B` for standard queries.
2. **Windows Working Set vs Private Commit:**
   - `get_process_memory_info()` in `src/common/memory_manager.cpp` only sampled `WorkingSetSize` via `PROCESS_MEMORY_COUNTERS`.
   - On Windows, `WorkingSetSize` measures resident physical pages and is subject to OS paging/trimming, which explains why streaming aggregations reported ~5.1 MB even when processing millions of rows.
   - It should also sample `PrivateUsage` (Commit Charge) via `PROCESS_MEMORY_COUNTERS_EX` to track genuine committed virtual memory.

---

## 5. Milestone M3 Technical Action Plan

Based on the verified reconnaissance, Milestone M3 will execute the following remediation and hardening plan:

1. **Dynamic Memory-Reservation-Driven Spilling:**
   - Eliminate hardcoded `64MB` checks.
   - Wire `MemoryReservation` directly into `PhysicalHashJoin`, `PhysicalHashAggregate`, and `PhysicalSort`.
   - Trigger external partitioning/spilling whenever memory reservation reaches the operator's budget limit, or when memory is constrained.
2. **Recursive Repartitioning & Partition Skew Handling:**
   - Implement partition size tracking and recursive repartitioning (with level-dependent hash seeds) or fallback handling for skewed keys.
   - Validate against adversarial distributions: all identical keys, Zipfian skew, dominant join key, massive duplicates.
3. **Genuine 5 GB External Spilling Verification:**
   - Formulate and benchmark deterministic 5 GB workloads that genuinely force external spill across sort, join, and aggregation.
   - Measure actual spilled bytes, partition counts, latency, and result checksums against DuckDB.
4. **Enhanced Memory Instrumentation:**
   - Update `ProcessMemoryInfo` to report both Working Set and Private Commit (`PrivateUsage`).
   - Connect operator-level memory accounting to `QueryMemoryManager`.
5. **Corruption & Resilience Testing:**
   - Add unit/adversarial tests for corrupt AXMB files (checksum failure, truncated files, corrupt magic bytes, schema mismatch).
   - Ensure clean RAII cleanup under all exception conditions.
6. **Performance Optimization:**
   - Profile execution and implement targeted C++20 hot-path optimizations (fast float parsing, string caching/views, batch reserve tuning).
7. **Release Certification:**
   - Conduct complete comparative benchmarks against DuckDB and SQLite.
   - Produce all required audit artifacts and release candidate v0.1.0.

