# Project AXIOM — Milestone M2: Limitations, Edge Cases & Technical Debt

**Milestone:** M2 (Out-of-Core Execution & Performance Engineering)  
**Date:** 2026-11-14  

---

## 1. Disclosed Limitations

In accordance with Project AXIOM's non-negotiable engineering principles, the following limitations are explicitly documented:

### 1. Single-Threaded Execution
- All operators currently execute on a single thread. While this simplifies determinism and memory accounting, query latency on multi-core systems is bounded by single-core instruction throughput. Intra-operator parallelism and work-stealing task schedulers are deferred to Milestone M3.

### 2. Text CSV Input Parsing Bottleneck
- Scanning large CSV files remains the primary execution bottleneck. While `BinarySerializer` provides fast I/O for temporary spill partitions, source table reads must parse raw CSV text, perform UTF-8 delimiter scanning, and convert ASCII to binary types on every query.

### 3. Non-Equijoins
- Grace Hash Join requires equality predicates (`A.key = B.key`). Queries with non-equijoin conditions (`A.val < B.val`) fall back to in-memory block nested loop join and cannot be hash partitioned.

### 4. Severe Key Skew in Grace Hash Join
- If a single join key has millions of duplicate entries such that the rows for that single key exceed the entire memory budget, hash partitioning cannot split those identical keys across partitions. A block-based fallback or nested loop over spilled partition chunks is required.

### 5. Allocator Fragmentation vs Process RSS
- While AXIOM accounts for vector capacity, string dynamic allocations, and hash map bucket overhead, the C runtime heap (`msvcr*.dll`) does not immediately return free pages to the Windows kernel. As a result, operating system Working Set memory may exceed accounted memory by 10–25 MB due to heap fragmentation.

### 6. Unsupported SQL Features
- Window functions (`OVER (PARTITION BY ...)`).
- Correlated subqueries and CTEs (`WITH ...`).
- Outer joins (`LEFT JOIN`, `RIGHT JOIN`, `FULL OUTER JOIN`).
- Set operations (`UNION`, `INTERSECT`, `EXCEPT`).

