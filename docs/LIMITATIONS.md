# Project Axiom Limitations & Architectural Boundaries (v0.1.0 Release)

## 1. System Status & Verified Capabilities in v0.1.0

Project Axiom v0.1.0 delivers a fully autonomous, from-scratch analytical SQL database engine in modern C++20 with verified out-of-core, memory-bounded execution:

- **Centralized Memory Budgeting & OS Telemetry:** Enforced via `QueryMemoryManager` and RAII `MemoryReservation`. Tracks internal analytical state alongside operating system working set and private committed memory (`PROCESS_MEMORY_COUNTERS_EX`).
- **Block Partition Grace Hash Join:** Partitions relations into disk files via `SpillManager`. Supports arbitrary partition overflow, dominant-key skew, and identical join keys via chunked in-memory hash tables, probe stream rewinding, and persistent iterator pagination.
- **Hierarchical Multi-Pass Merge Sort:** Enforces dynamic fan-in bounding ($k \le 16$) with intermediate run merging, active batch reservation, and immediate unlinking of merged slice runs. Eliminates memory ballooning during multi-way merges.
- **Top-K Bounded Heap Optimization:** Evaluates `ORDER BY ... LIMIT K` ($K \le 50,000$) in $O(N \log K)$ time and $O(K)$ space with zero disk spilling.
- **External Hash Aggregation:** Hash-partitions high-cardinality groups across $P=16$ disk partitions with sequential partition aggregation.
- **AXMB v1 Binary Spill Architecture:** Direct typed serialization with NULL bitmaps, tail 32-bit FNV-1a checksums for truncation/corruption detection, and a 256 MB per-batch payload cap to guard against malformed headers.
- **Spill Volume Selection & Space Guardrails:** Automatically selects storage volumes with sufficient free capacity (precedence: `--temp-dir` > `AXIOM_TEMP_DIR` > system temp > `./tmp`). Enforces strict spill byte quotas with clean rollback and file deletion.
- **Multi-GB Scalability:** Empirically verified on datasets up to 5.37 GB (164.4 million rows) under strict memory limits.

---

## 2. Disclosed Technical Boundaries in v0.1.0

### 2.1 Execution & Storage Runtime
1. **Single-Threaded Execution:** Operators execute sequentially on a single core. Multi-threaded intra-operator parallelism is deferred to v0.2.0.
2. **Text CSV Input Overhead:** Raw text parsing and ASCII-to-binary conversions remain the primary execution bottleneck during table scans (accounting for 75–85% of cold execution time). A native columnar binary format is planned for v0.2.0.
3. **Non-Equijoins:** Fall back to in-memory block nested-loop joins, as arbitrary inequality conditions cannot be hash-partitioned.
4. **Memory Distinction:** User-specified `--memory-limit` bounds internal query-operator state (hash tables, sort runs, merge buffers). Operating system process working set includes runtime overhead (MSVC CRT heap, dynamic libraries, thread stacks, and OS file buffers), typically adding 4–10 MB above operator state.

### 2.2 SQL Dialect Boundaries
1. **Outer Joins:** `LEFT JOIN`, `RIGHT JOIN`, `FULL OUTER JOIN` are unsupported.
2. **Subqueries & CTEs:** `IN (SELECT ...)`, correlated subqueries, and `WITH ...` CTEs are unsupported.
3. **Window Functions:** `OVER (PARTITION BY ... ORDER BY ...)` is unsupported.
4. **Set Operations:** `UNION`, `INTERSECT`, `EXCEPT` are unsupported.
5. **DISTINCT Aggregates:** `COUNT(DISTINCT col)` is unsupported.
6. **Mutations:** DML / DDL statements (`INSERT`, `UPDATE`, `DELETE`) are unsupported. Axiom is a read-only analytical query engine.

---

## 3. Engineering Roadmap (v0.2.0)
1. **Morsel-Driven Parallelism:** Chunked task scheduler for multi-threaded table scans, parallel hash join build, and parallel aggregation.
2. **Native Columnar Binary Format (AXMC):** High-throughput persistent columnar format with dictionary and bit-packing compression to eliminate CSV parsing bottlenecks.
3. **Subquery Unnesting & Correlated Expressions:** Decorrelation and semi-join execution paths.
