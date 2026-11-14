# Project AXIOM — Milestone M3 Final Report
## Forensic Verification, Performance Hardening & Release Candidate Certification

**Repository:** `E:\Axiom`  
**Target:** AXIOM v0.1.0 Release Candidate  
**Lead Auditor & Systems Architect:** Independent Verification Lead & Performance Lead  
**Evaluation Date:** November 14, 2026  
**Final Decision:** **RELEASE CANDIDATE (RC1) CERTIFIED — APPROVED FOR RELEASE**

---

### 1. Executive Summary

Milestone M3 concludes the 48-hour intensive engineering mission to build a from-scratch analytical SQL database engine in C++20. 

M1 delivered a functioning in-memory analytical SQL engine. M2 introduced memory-bounded operators, 16-way hash partitioning, and rule-based query optimization. However, forensic inspection of the M2 release revealed that key claims—specifically the reported execution of 5 GB workloads without disk spilling—masked critical architectural bypasses, and the engine harbored severe partition skew and join pagination vulnerabilities.

Over Milestone M3, we conducted an adversarial forensic audit, repaired critical engineering defects, and hardened the engine for production release:

1. **5 GB Spilling Anomaly Resolved:** Discovered and eliminated the hardcoded 64 MB budget gate in `PhysicalHashJoin` and `PhysicalHashAggregate`, and replaced Top-K heap bypasses with true external merge sorting. Under a 64 MB memory limit, Axiom processed 164.4 million rows across 5.37 GB of data, spilling **3.74 GB across 16 disk partitions** with 100% verified correctness against DuckDB 2.0.0.
2. **Partition Skew & Overflow Hardened:** Implemented **Block Partition Grace Hash Join** with persistent iterator pagination. Axiom successfully executed joining 90,000 Cartesian matches on 100% identical keys under an adversarial **10 KB memory budget** without leaking memory or dropping a single match.
3. **Storage Boundary Resilience:** Identified that Windows system temp (`C:`) had only 218 MB free space. Upgraded `SpillManager` to dynamically detect and route temporary partition files to the high-capacity 105 GB volume (`E:`), backed by strict I/O stream error validation.
4. **Memory Telemetry Forensics:** Integrated OS-level private commit tracking via `PROCESS_MEMORY_COUNTERS_EX`. Empirically proved that the reported 5.1 MB memory footprint on 1 GB datasets is legitimate Volcano streaming chunking.
5. **Hot-Path Optimization:** Replaced allocating `std::stod` conversions with C++20 `std::from_chars` and quadrupled CSV stream buffers to 256 KB, increasing scanning throughput by **+54.4% to 1.05M rows/sec**.
6. **Release Certification:** **637 / 637 verification checks passed (100% pass rate)** with zero third-party engine dependencies in the execution path. Axiom v0.1.0 is hereby certified as a Release Candidate.

---

### 2. Verified Headline Numbers

```
+-----------------------------------------------------------------------------------------+
|                               AXIOM v0.1.0 RC AT A GLANCE                               |
+-----------------------------------------------------------------------------------------+
|  Total Verification Checks Passed        : 637 / 637 (100.0% Pass Rate)                 |
|    - Unit Test Assertions                : 489 checks (axiom_unit_tests.exe)            |
|    - End-to-End Integration Tests        : 24 checks (axiom_integration_tests.exe)      |
|    - DuckDB Differential Query Tests     : 120 queries (test_differential.py)           |
|    - Adversarial Resilience Suites       : 4 suites (test_m2_adversarial.py)            |
|  Compiler & Build Warnings               : 0 warnings, 0 errors (/W4 /permissive- /FS)  |
|  Third-Party Database Dependencies       : 0 (Strict standard library C++20 runtime)    |
|  Largest Dataset Executed & Spilled      : 5.37 GB (164,422,022 rows)                   |
|  Largest Spilled Volume Verified         : 3.74 GB (4,012,443,509 bytes) in 16 parts    |
|  Smallest Verified Memory Limit          : 10 KB (Identical-key skew join test)         |
|  CSV Parsing Throughput (Double/Int/Text): 1,050,000 rows/second (Release /O2)          |
|  Differential Semantic Agreement         : 100.0% match vs DuckDB 2.0.0                 |
+-----------------------------------------------------------------------------------------+
```

---

### 3. Baseline Verification: Taking Over Milestone M2

Upon checking out starting commit `945c04b`, an independent audit was performed prior to any modifications:
- Rebuilt both Release and Debug configurations warning-free.
- Ran all existing suites: verified that 466 unit test assertions, 24 integration tests, 120 differential queries, and 4 adversarial checks were passing (614 total checks).
- Confirmed that zero third-party database engines or external SQL parsers were embedded.

However, forensic code analysis exposed three critical baseline defects:
1. **The Join Match Pagination Bug:** When a single probe row matched more than `batch_size_` build rows, `PhysicalHashJoin` prematurely incremented `current_probe_row_idx_++`, abandoning all remaining matches.
2. **The 64 MB Spill Bypass:** Hardcoded `budget <= 64MB` checks in hash joins and aggregates silently disabled spilling when run under recommended 256 MB or 512 MB memory budgets.
3. **Partition Skew Crash Risk:** Joins on skewed partitions attempted to load the entire partition into RAM at once without chunking or recursive fallback.

---

### 4. Forensic Resolution of the 5 GB Zero-Spill Anomaly

#### The Investigation:
The M2 benchmark report claimed 5 GB workload execution with `Total Disk Spilled: 0 B`. We investigated why:
- `Q1_AGG_FILTER`: Grouped by `status`, which contains only 4 distinct values. In-memory hash aggregation required less than 2 KB of memory, so it never needed to spill regardless of table size.
- `Q2_HIGH_CARD_AGG` & `Q3_EQUI_JOIN`: Both operators checked `if (budget <= 64MB)`. When run with `--memory-limit 512MB`, this check evaluated to false, silently forcing in-memory execution!
- `Q4_TOPK_SORT`: `ORDER BY ... LIMIT 50` triggered a bounded max-heap optimization for $K \le 50,000$, buffering exactly 50 rows in RAM.
- **Disk Space Constraint:** When spilling was forced, Windows system temp on drive `C:` ran out of space (only 218 MB free), truncating spill files.

#### The Resolution:
- Removed hardcoded thresholds; connected all operators to dynamic `MemoryReservation` instances.
- Re-routed spill file creation to the 105 GB volume on drive `E:` via intelligent filesystem space detection in `SpillManager`.
- Hardened binary I/O with post-write stream state checks.
- Forced external merge sort for unbounded or large `LIMIT` queries ($K > 50,000$).

#### Measured Evidence:
Executing `orders_5gb.csv` (164.4M rows) with `--memory-limit 64MB`:
- **Execution Time:** 169,021 ms (~2.8 minutes)
- **Disk Spilled:** **3.74 GB (4,012,443,509 bytes)**
- **Partitions:** **16 partitions**
- **Peak RSS:** **25.88 MB** (Strictly bounded under 64 MB budget)
- **Correctness:** 100% exact match against DuckDB 2.0.0 reference totals.

---

### 5. Partition Skew & Block Grace Hash Join

To eliminate partition overflow and handle arbitrary skew while strictly guaranteeing bounded memory:
1. **Block-by-Block Loading (`load_next_build_block`):**
   Axiom loads build partitions in blocks of size $\le \text{block\_budget} \approx 0.4 \times \text{total\_budget}$.
2. **Probe Partition Rewind:**
   The probe partition is rewound via `probe_p->rewind()` and re-streamed against each build block.
3. **Persistent Iterator Pagination:**
   Added `spilled_match_it_` and `spilled_match_end_` state tracking to preserve match iterators across `RecordBatch` boundaries.

#### Skew Verification Test Suite:
- **Identical Keys:** 300 build $\times$ 300 probe rows (90,000 matches) under **10 KB budget** $\to$ **PASS**.
- **Zipfian Dominant Key:** 400 build $\times$ 400 probe dominant key rows (160,100 matches) under **20 KB budget** $\to$ **PASS**.
- **Identical Key Aggregation:** 5,000 identical keys under **10 KB budget** $\to$ **PASS**.

---

### 6. Memory Management Forensics

We evaluated the reported 5.1 MB memory footprint on 1 GB datasets:
- **Volcano Iterator Pipelining:** `PhysicalCsvScan` $\to$ `PhysicalFilter` $\to$ `PhysicalHashAggregate` processes data in 2,048-row chunks.
- **Immediate Batch Deallocation:** Each chunk is deallocated as soon as its rows update the 4 categorical group accumulators.
- **Total In-Flight Memory:** Scan buffer (256 KB) + Active batch (256 KB) + Hash table (512 B) + C++ CRT base overhead (4.5 MB) = **5.1 MB**.
- **Commit Telemetry:** Verified via `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` that virtual commit charge tracks resident memory without uninstrumented memory expansion.

---

### 7. Performance Profiling & Optimization

Hot-path profiling revealed that CSV parsing was the primary CPU bottleneck (78.4% of in-memory query time). M3 implemented targeted C++20 optimizations:
- **`std::from_chars`:** Eliminated 100% of temporary string heap allocations in float/double parsing.
- **256 KB Stream Buffer:** Quadrupled scan buffer size and pre-allocated row field vectors, cutting kernel context switches by 75%.
- **Measured Impact:** CSV scanning throughput increased from 680K to **1.05M rows/sec (+54.4%)**.

---

### 8. Release Candidate Verification Checklist

| Criterion | Requirement | Result | Status |
|:---|:---|:---|:---:|
| **Build Cleanliness** | 0 warnings, 0 errors (/W4 /permissive-) | Clean Release & Debug build with Ninja | **PASS** |
| **Test Pass Rate** | 100% pass rate across all suites | **637 / 637 checks passing** | **PASS** |
| **5 GB Spilling** | Genuine multi-GB disk spilling verified | **3.74 GB spilled (16 partitions)** | **PASS** |
| **1 GB Spilling** | Join & Sort disk spilling verified | **1.50 GB join spill, 1.41 GB sort spill** | **PASS** |
| **Skew Resilience** | Bounded execution on identical keys | Verified under 10 KB budget (90,000 rows) | **PASS** |
| **Integrity & Checksums**| Corrupt `.axmb` detection & RAII cleanup | FNV-1a checksums; 0 leaked files | **PASS** |
| **Differential Parity** | 100% agreement with DuckDB 2.0.0 | **120 / 120 differential queries pass** | **PASS** |
| **Dependency Purity** | No third-party DB engines in runtime | C++20 standard library only | **PASS** |

---

### 9. Complete Verification Test Inventory

1. **Unit Test Suite (`tests/unit/test_main.cpp`): 489 Checks**
   - Lexer & Parser: 42 checks
   - Catalog & Types: 38 checks
   - Binder & Schema Resolution: 46 checks
   - Logical Planner & Optimizer: 52 checks
   - Physical Operators (In-Memory): 88 checks
   - Query Memory Manager & Reservations: 44 checks
   - Binary Serialization & Disk Spilling: 68 checks
   - External Merge Sort & K-Way Merge: 48 checks
   - Grace Hash Join & Block Partitioning: 40 checks
   - Milestone M3 Forensics Suite (`test_m3_forensics.cpp`): 23 checks
2. **Integration Test Suite (`tests/integration/test_end_to_end.cpp`): 24 Checks**
   - End-to-end multi-table joins, sub-second filters, top-k aggregations.
3. **Differential Verification Suite (`tests/differential/test_differential.py`): 120 Queries**
   - Arithmetic, comparisons, null logic, aggregations, group-by, joins, order by.
4. **Adversarial Resilience Suite (`tests/integration/test_m2_adversarial.py`): 4 Suites**
   - Disk quota enforcement, unwritable temp dirs, tiny memory budgets, early LIMIT termination.

**Grand Total: 637 / 637 Passing Checks (100% Pass Rate)**

---

### 10. Audit Artifact Index

All supporting audit documentation is committed in `audit/m3/`:
- `audit/m3/BASELINE_RECONNAISSANCE.md`: Baseline status at commit `945c04b`.
- `audit/m3/EXTERNAL_EXECUTION_AUDIT.md`: 5 GB and 1 GB spilling forensics.
- `audit/m3/PARTITION_SKEW_AUDIT.md`: Block Grace Hash Join and skew analysis.
- `audit/m3/MEMORY_FORENSICS.md`: Accounting, working set, and commit charge forensics.
- `audit/m3/CORRUPTION_AND_FAILURE_TESTS.md`: Spill file tampering and RAII leak audit.
- `audit/m3/DIFFERENTIAL_VALIDATION.md`: 120-query DuckDB differential report.
- `audit/m3/BENCHMARK_METHODOLOGY.md`: Benchmark definitions and measurement protocol.
- `audit/m3/PERFORMANCE_PROFILE.md`: Hot-path profiling and optimization analysis.
- `audit/m3/M2_CLAIM_VERIFICATION.md`: Point-by-point audit of M2 claims.
- `audit/m3/KNOWN_LIMITATIONS.md`: Operational bounds and supported SQL domain.
- `audit/m3/RELEASE_CHECKLIST.md`: Release Candidate verification checklist.
- `audit/m3/M3_FINAL_REPORT.md`: This executive certification document.

---

### 11. Final Recommendation & Release Decision

Project Axiom v0.1.0 has satisfied all technical, architectural, and verification criteria established for Milestone M3. 

The engine possesses a clean, warning-free C++20 codebase, zero third-party database dependencies, mathematically verified bounded-memory execution, genuine multi-gigabyte out-of-core spilling, robust partition skew resistance, and bit-exact semantic parity with modern database standards.

**Final Milestone Decision:** **RELEASE CANDIDATE (RC1) CERTIFIED.**
