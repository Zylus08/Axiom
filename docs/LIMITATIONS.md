# Project Axiom Limitations & Roadmap (Milestone M2)

## 1. Milestone M2 Status & Resolved Capabilities

Milestone M2 successfully delivered out-of-core, memory-bounded query execution:
- **Centralized Memory Budgeting:** Enforced via `QueryMemoryManager` and RAII `MemoryReservation`.
- **Grace Hash Join:** Partitions build and probe tables into $B=16$ disk files via `SpillManager` when memory limits are reached.
- **External Merge Sort:** Spills sorted runs using `BinarySerializer` AXMB v1 and performs priority-queue multi-way merge.
- **Top-K Bounded Heap Optimization:** Evaluates `ORDER BY ... LIMIT K` in $O(N \log K)$ time and $O(K)$ space with zero disk spilling.
- **External Hash Aggregation:** Hash-partitions large groups into $P=16$ intermediate partitions.
- **Multi-GB Scalability:** Verified correct execution on datasets up to 5 GB (164.4 million rows) under strict 512 MB memory limit.

---

## 2. Disclosed Technical Limitations in M2

### 2.1 Execution & Runtime
1. **Single-Threaded Execution:** Operators execute sequentially on a single core. Multi-threaded intra-operator parallelism is deferred to Milestone M3.
2. **Text CSV Input Overhead:** Raw text parsing and ASCII-to-binary conversions remain the primary execution bottleneck during table scans. Columnar binary formats are planned for subsequent milestones.
3. **Non-Equijoins:** Fall back to in-memory block nested-loop joins, as arbitrary inequality conditions cannot be hash-partitioned.
4. **Key Skew in Hash Join:** If a single join key has millions of duplicates that exceed the total operator memory budget, hash partitioning cannot split those identical keys across buckets.

### 2.2 SQL Dialect Boundaries
1. **Outer Joins:** `LEFT JOIN`, `RIGHT JOIN`, `FULL OUTER JOIN` are unsupported.
2. **Subqueries & CTEs:** `IN (SELECT ...)`, correlated subqueries, and `WITH ...` CTEs are unsupported.
3. **Window Functions:** `OVER (PARTITION BY ... ORDER BY ...)` is unsupported.
4. **Set Operations:** `UNION`, `INTERSECT`, `EXCEPT` are unsupported.
5. **DISTINCT Aggregates:** `COUNT(DISTINCT col)` is unsupported.
6. **Mutations:** DML / DDL statements (`INSERT`, `UPDATE`, `DELETE`) are unsupported. Axiom is a read-only analytical query engine.

---

## 3. Integration Points for Milestone M3
1. **Morsel-Driven Parallelism:** Chunked task scheduler for multi-threaded table scans, parallel hash join build, and parallel aggregation.
2. **Columnar Binary Storage (Native Engine Format):** High-throughput persistent format to bypass CSV parsing overhead.
3. **Adaptive Skew Handling:** Dynamic re-partitioning and block nested loop fallback for oversized single-key partitions.
