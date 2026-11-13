# Project Axiom Limitations & M2 Roadmap (Milestone M1)

## 1. Milestone M1 Limitations

Milestone M1 prioritizes an end-to-end, correct, self-contained analytical query engine. The following architectural limitations are explicitly documented:

### 1.1 Memory Boundedness (Spill to Disk)
- **Hash Join Build Side:** The right table of an `INNER JOIN` is materialized in memory in a hash table during the build phase.
- **Aggregation Group State:** Hash aggregation accumulates distinct group keys and their running accumulators in an in-memory hash map.
- **Sorting Buffer:** `ORDER BY` collects all input rows before performing in-memory `std::stable_sort`.
- *Note:* While the scan and filter pipeline streams in 1,024-row batches without buffering full input files, operators that block on pipeline breakers (sort, build join, group aggregate) remain memory-resident in M1. No claim of memory-bounded execution is made for M1.

### 1.2 SQL Dialect Boundaries
- **Outer Joins:** `LEFT JOIN`, `RIGHT JOIN`, and `FULL OUTER JOIN` are unsupported.
- **Subqueries:** Scalar subqueries, correlated subqueries, and `IN (SELECT ...)` are unsupported.
- **Set Operations:** `UNION`, `INTERSECT`, `EXCEPT` are unsupported.
- **Window Functions:** `OVER (PARTITION BY ... ORDER BY ...)` is unsupported.
- **DISTINCT Aggregates:** `COUNT(DISTINCT col)` is unsupported.
- **Write Operations:** No DDL or DML (`CREATE TABLE`, `INSERT`, `UPDATE`, `DELETE`). Axiom M1 is a pure analytical query engine over external CSV files.

---

## 2. Integration Points for Milestone M2

The Axiom architecture was structured to cleanly accommodate M2 requirements:
1. **Memory Accounting Interface:**
   - Operators can wrap memory allocations through an abstract `MemoryManager` / `MemoryPool`.
2. **External Merge Sort:**
   - Replace in-memory `collect_and_sort()` with an external merge sort operator that flushes sorted runs of `RecordBatch` to temporary files on disk when a memory limit is reached.
3. **Grace Hash Join:**
   - Partition build and probe tuples into hash-bucketed spill files on disk when memory thresholds are exceeded.
4. **Partitioned Hash Aggregation:**
   - Spill aggregation buckets when grouping large high-cardinality datasets.

