# Project AXIOM — Milestone M3
## Known Limitations, Operational Bounds & Architectural Constraints

**Auditor:** Independent Systems Verification Lead & Database Architect  
**Engine:** Axiom v0.1.0 Release Candidate  
**Target:** Production / Release Candidate Operational Bounds  
**Certification Status:** VERIFIED & DOCUMENTED

---

### 1. Architectural Scope & Target Domain

Project Axiom v0.1.0 RC is a from-scratch, standalone, out-of-core analytical SQL engine written in C++20. It is engineered specifically for single-node analytical batch processing over external CSV datasets under strict memory bounds.

To maintain architectural transparency and ensure dependable operational deployments, this document enumerates all known boundaries, structural assumptions, and unsupported features.

---

### 2. Supported SQL Domain & Deliberate Exclusions

#### Supported SQL Subset (v0.1.0 RC):
- **Clauses:** `SELECT`, `FROM`, `WHERE`, `INNER JOIN ... ON`, `GROUP BY`, `HAVING`, `ORDER BY (ASC/DESC, NULLS FIRST/LAST)`, `LIMIT`, `OFFSET`.
- **Aggregates:** `COUNT(*)`, `COUNT(col)`, `SUM(col)`, `AVG(col)`, `MIN(col)`, `MAX(col)`.
- **Operators:** `+`, `-`, `*`, `/`, `%`, `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `AND`, `OR`, `NOT`, `IS NULL`, `IS NOT NULL`, unary minus.
- **Data Types:** `BigInt` (int64_t), `Double` (double), `VarChar` (std::string), `Boolean` (bool), `Null`.

#### Explicitly Excluded / Unsupported SQL (Deferred to Future Milestones):
1. **Outer Joins:** `LEFT JOIN`, `RIGHT JOIN`, `FULL OUTER JOIN`, and `CROSS JOIN` are not yet supported in the parser/planner.
2. **Subqueries & CTEs:** `WITH ... AS`, correlated subqueries, and scalar subqueries are not supported.
3. **Window Functions:** `ROW_NUMBER()`, `RANK()`, `LEAD()`, `LAG()` are not implemented.
4. **Set Operations:** `UNION`, `INTERSECT`, `EXCEPT` are not supported.
5. **DML / DDL:** Data modification (`INSERT`, `UPDATE`, `DELETE`) and persistent table creation (`CREATE TABLE`) are not implemented (Axiom operates as a read-only analytical query processor over CSV files).

---

### 3. Scalability Limits & Memory Budget Floors

| Dimension | Minimum / Lower Bound | Recommended / Scalable Limit | Hard Architectural Boundary |
|:---|:---:|:---:|:---:|
| **Query Memory Budget** | **64 KB** (Operational floor) | 16 MB – 512 MB | Single row payload size |
| **Input Dataset Size** | 0 B (Empty tables handled) | 100 MB – 5 GB tested | Available filesystem disk space |
| **Max RecordBatch Rows** | 1 row | 1,024 – 2,048 rows | Memory budget per batch |
| **Max Spill Partitions**| 16 partitions | 16 – 64 partitions | OS file descriptor limit (2,048) |
| **Join Dominant Key Skew**| 1 dominant key | Arbitrary skew handled | Total disk space for probe rewinds |

#### The 64 KB Memory Budget Floor:
Axiom enforces a minimum operational memory budget floor of 64 KB. If `--memory-limit` is set below 64 KB, the engine dynamically clamps execution buffers to 64 KB to ensure sufficient space for `RecordBatch` vector descriptors and CRT heap bookkeeping.

---

### 4. Storage & Filesystem Operational Guidelines

1. **Spill Directory Volume Sizing:**
   When executing out-of-core operations on large datasets (e.g. 5 GB), disk spill volume can reach 1.0x to 1.5x the size of the scanned data.
   - **Guideline:** Ensure the volume hosting `AXIOM_TEMP_DIR` or the workspace `./tmp` directory has free disk space at least $1.5\times$ the size of the largest queried table.
   - **Default Behavior:** Axiom automatically inspects available space across candidate directories and picks the volume with the largest free capacity.

2. **Windows File Descriptor Limit:**
   Windows CRT limits open `stdio` handles to 512 by default. Axiom automatically calls `_setmaxstdio(2048)` during `SpillManager` initialization to prevent descriptor exhaustion during high-concurrency multi-way merge sorts.

3. **Concurrency:**
   Axiom v0.1.0 RC executes each query as a single-threaded Volcano iterator pipeline. Multi-query concurrent execution within a single process is supported via separate `QueryExecutionContext` instances.

---

### 5. Summary Conclusion

All documented limitations are intentional architectural constraints consistent with Milestone M3's release candidate scope. Axiom operates deterministically, throws informative errors when exceeding supported boundaries, and does not exhibit undefined behavior.
