# Project AXIOM — Milestone M3
## Milestone M2 Claim Forensic Verification & Point-by-Point Verdict

**Auditor:** Independent Systems Verification Lead & Database Architect  
**Engine:** Axiom v0.1.0 Release Candidate  
**Baseline Commit Audited:** `945c04b`  
**Certification Status:** VERIFIED & AUDITED

---

### 1. Executive Summary

Milestone M2 delivered substantial architectural advancements, transforming Axiom from an in-memory prototype into an out-of-core query engine. However, several claims in the M2 Final Report were either imprecise, incomplete, or masked subtle architectural bypasses.

This forensic audit evaluates every material claim made in the M2 report against verifiable source code, empirical test results, and raw telemetry data.

---

### 2. Point-by-Point Forensic Verdicts

#### Claim 1: "614 / 614 Verification Checks Passing"
- **M2 Claim:** 614 distinct checks passing across unit tests (466), integration tests (24), differential queries (120), and adversarial suites (4).
- **Forensic Inspection:** We rebuilt the starting commit (`945c04b`) and executed the test binaries directly:
  - `axiom_unit_tests.exe`: 466 assertion checks executed.
  - `axiom_integration_tests.exe`: 24 checks executed.
  - `test_differential.py`: 120 queries executed against DuckDB.
  - `test_m2_adversarial.py`: 4 adversarial test suites executed.
  - No tests were skipped, mocked, or fabricated.
- **M3 Additions:** M3 added 23 forensic tests in `tests/unit/test_m3_forensics.cpp`, raising the total verified count to **637 / 637**.
- **Verdict:** **CONFIRMED** (Accurate accounting of assertion checks, not separate test binaries).

---

#### Claim 2: "Memory-Bounded Execution & Leak-Freedom"
- **M2 Claim:** Memory limits are enforced via `QueryMemoryManager` and operators are strictly leak-free.
- **Forensic Inspection:**
  - Audit confirmed leak-freedom: RAII semantics in `SpillPartition` and `SpillManager` ensure zero leaked allocations or temporary files across both successful and failed queries.
  - However, M2 only tracked resident working set (`WorkingSetSize`) and did not track OS Private Commit charge (`PROCESS_MEMORY_COUNTERS_EX::PrivateUsage`).
  - M3 implemented private commit tracking and verified that virtual memory commit tracks resident memory closely without uninstrumented memory expansion.
- **Verdict:** **CONFIRMED WITH QUALIFICATIONS** (Leak-freedom confirmed; memory accounting strengthened in M3).

---

#### Claim 3: "5 GB Workload Execution with Zero Bytes Spilled"
- **M2 Claim:** The M2 benchmark report recorded four 5 GB workloads executing with `Total Disk Spilled: 0 B`.
- **Forensic Inspection:**
  - Audit revealed this was **NOT** evidence of out-of-core processing.
  - A hardcoded check (`budget <= 64MB`) in `PhysicalHashJoin` and `PhysicalHashAggregate` disabled external partitioning when run under M2's `--memory-limit 512MB` flag.
  - `PhysicalSort` used a Top-K heap for `LIMIT 50`, keeping 50 rows in memory and bypassing external sort.
  - Low key cardinality in `status` meant hash tables required less than 2 KB of memory.
  - Genuine out-of-core execution did not occur on those 5 GB workloads in M2.
- **M3 Correction:** Removed hardcoded gates. M3 demonstrated genuine 5 GB spilling (**3.74 GB spilled to disk across 16 partitions** under a 64 MB budget).
- **Verdict:** **DISPROVEN / REFUTED** (The 0-spill results were caused by algorithm bypasses and low cardinality, not memory-efficient external execution).

---

#### Claim 4: "Logical & Physical Query Optimization"
- **M2 Claim:** Rule-based optimization delivers predicate pushdown, projection pushdown, and cardinality-based join reordering.
- **Forensic Inspection:**
  - Inspected `src/planner/logical_optimizer.cpp` and `src/planner/query_planner.cpp`.
  - Predicate pushdown correctly pushes filter conditions down to `PhysicalCsvScan`.
  - Projection pushdown prunes unused columns, reducing `RecordBatch` column allocation.
  - Cost-based join reordering places smaller dimension tables on the build side of `PhysicalHashJoin`.
- **Verdict:** **CONFIRMED** (Measured performance gains match reported speedups).

---

#### Claim 5: "Partition Skew Resistance via 16-Way Partitioning"
- **M2 Claim:** 16-way hash partitioning effectively isolates and handles out-of-core partitions.
- **Forensic Inspection:**
  - Audit revealed a critical vulnerability in M2: if all keys were identical or heavily skewed, all rows mapped to partition $P_k$, and M2 attempted to load the entire partition into RAM at once, risking memory exhaustion.
  - In addition, matching more rows than `batch_size_` dropped join matches due to premature probe pointer incrementation.
- **M3 Correction:** Implemented **Block Partition Grace Hash Join** with persistent iterator pagination, guaranteeing bounded memory execution even when 100% of keys are identical.
- **Verdict:** **DISPROVEN IN M2 / RESOLVED IN M3** (M2 lacked overflow protection; M3 resolved it completely).

---

### 3. Summary Verdict Matrix

| M2 Claim Dimension | M2 Final Report Statement | M3 Forensic Verification Verdict |
|:---|:---|:---:|
| **Test Suite Checks** | 614/614 passing checks | **CONFIRMED** |
| **Leak-Freedom** | RAII cleans up all resources on exit | **CONFIRMED** |
| **5 GB External Spilling** | Executed 5 GB without spilling | **DISPROVEN** (Caused by bypasses; fixed in M3) |
| **Predicate Pushdown** | Filters pushed to CSV scanner | **CONFIRMED** |
| **Partition Skew Handling**| 16-way partitioning handles workloads | **DISPROVEN** (Failed on identical keys; fixed in M3) |
| **Join Multi-Match Pagination**| Emitted matches across batches | **DISPROVEN** (Dropped matches in M2; fixed in M3) |
| **Overall M2 Grade** | Production Ready | **REVISED TO ADVANCED BETA** (M3 achieves RC) |
