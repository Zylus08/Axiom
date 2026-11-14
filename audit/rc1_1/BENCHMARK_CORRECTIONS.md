# Project AXIOM — RC1.1 Release Integrity Audit
## Comprehensive Benchmark Corrections & Ratio Recalculation

**Auditor:** Independent Release Auditor & Performance Architect  
**Audit Scope:** Empirical Accuracy, Mathematical Ratios & Fair Methodology  
**Status:** ALL RATIOS RECALCULATED, METHODOLOGY LIMITATIONS DOCUMENTED, INVERSIONS CORRECTED

---

### 1. Executive Summary: Identification of Reporting Inversions

In Milestone M3, the benchmark comparison report ([`audit/m3/BENCHMARK_COMPARISON.md`](file:///E:/Axiom/audit/m3/BENCHMARK_COMPARISON.md)) contained a critical mathematical inversion:
The report displayed ratios where Axiom's latency was divided by the reference engine's latency, but labeled the result as **"Axiom is X times faster"**!

For example:
- **100 MB Filtered Aggregation:**
  - Axiom Latency: **3,308.63 ms**
  - SQLite Latency: **480.20 ms**
  - Ratio: $480.20 / 3308.63 = 0.145$
  - Flawed M3 Report Label: `0.15x (6.9x faster)`!
  - **Corrected Reality:** Axiom took $3308.63 / 480.20 = \mathbf{6.89\times\text{ longer}}$ than SQLite. SQLite was 6.89x faster than Axiom!

This section recalculates every ratio directly from the raw measurements in [`audit/m3/BENCHMARK_RAW_RESULTS.csv`](file:///E:/Axiom/audit/m3/BENCHMARK_RAW_RESULTS.csv) and establishes a rigorous, defensible comparison methodology.

---

### 2. Recalculated Latency Ratios Across All Workloads

All ratios below are computed using standard database benchmarking definitions:
$$\text{Slowdown Factor} = \frac{\text{Axiom Latency}}{\text{Reference Latency}}$$
$$\text{Relative Throughput} = \frac{\text{Reference Latency}}{\text{Axiom Latency}}$$

| Scale | Workload ID & Name | Axiom (ms) | DuckDB (ms) | SQLite (ms) | Axiom vs DuckDB | Axiom vs SQLite | Corrected Empirical Verdict |
|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---|
| **100 MB** | **W1: Filtered Aggregation** | 3,308.63 | 280.45 | 480.20 | **11.80x slower** | **6.89x slower** | DuckDB & SQLite executed faster on in-memory/preloaded data. |
| **100 MB** | **W2: High-Card Group By** | 5,529.17 | 720.15 | 1,450.30 | **7.68x slower** | **3.81x slower** | Axiom spilled 81.3 MB to disk under 16 MB budget. |
| **100 MB** | **W3: Equi-Join (Grace Join)** | 9,608.67 | 450.80 | 980.50 | **21.31x slower** | **9.80x slower** | Axiom executed 16-way Grace partitioning & disk rewinding. |
| **100 MB** | **W4: External Merge Sort** | 12,806.51 | 520.10 | 1,120.40 | **24.62x slower** | **11.43x slower** | Axiom executed multi-pass hierarchical merge sort to disk. |
| **1 GB** | **W1: Filtered Aggregation** | 25,410.22 | 2,850.30 | N/A* | **8.91x slower** | N/A* | DuckDB parallel multi-threading vs Axiom single thread. |
| **1 GB** | **W2: High-Card Group By** | 30,547.93 | 4,920.80 | N/A* | **6.21x slower** | N/A* | Axiom spilled 789.1 MB to disk under 32 MB budget. |
| **1 GB** | **W3: Equi-Join (Grace Join)** | 64,173.03 | 4,210.40 | N/A* | **15.24x slower** | N/A* | Axiom spilled 1.50 GB across 48 partitions. |
| **1 GB** | **W4: External Merge Sort** | 42,078.95 | 3,980.50 | N/A* | **10.57x slower** | N/A* | Axiom executed out-of-core sort with 650 runs. |
| **5 GB** | **W1: Filtered Aggregation** | 128,400.15 | DNF / OOM | N/A* | N/A | N/A* | Axiom completed in 5.2 MB streaming footprint. |
| **5 GB** | **W2: High-Card Group By** | 169,021.61 | DNF / OOM | N/A* | N/A | N/A* | Axiom completed under 64 MB budget, spilling 3.74 GB. |

*\*Note: SQLite 1 GB and 5 GB benchmarks were not feasible due to extreme B-tree insertion latency (> 45 minutes to import).*

---

### 3. Separation of CPU Parsing Throughput vs SQL Execution Throughput

Axiom operates as a standalone CLI query processor that reads raw CSV files directly from disk on every invocation. The total reported execution time must be decomposed into its two constituent phases:

$$\text{Total Time} = \text{CSV Scan \& Parsing Time} + \text{Query Execution Time}$$

#### Breakdown for 100 MB Filtered Aggregation (3,494,109 rows):
- **Raw CSV File Size:** 104.8 MB
- **Total Execution Time:** 3,308.63 ms
- **CSV Read & Parse Time (`PhysicalCsvScan`):** **2,593.20 ms (78.4% of total runtime)**
- **Pure Query Processing Time (Filter + Aggregate):** **715.43 ms (21.6% of total runtime)**

#### Throughput Metrics:
1. **Raw CSV Parsing Throughput:**
   $$\text{Parsing Speed} = \frac{3,494,109\text{ rows}}{2.593\text{ seconds}} \approx \mathbf{1,347,000\text{ rows/second}} \quad (40.4\text{ MB/sec text parse})$$
2. **End-to-End Query Execution Throughput:**
   $$\text{End-to-End Speed} = \frac{3,494,109\text{ rows}}{3.308\text{ seconds}} \approx \mathbf{1,056,000\text{ rows/second}} \quad (31.7\text{ MB/sec pipeline})$$

---

### 4. Methodological Limitations: Cold CSV Scan vs Preloaded In-Memory Tables

The comparison against SQLite 3 in Milestone M3 had a fundamental methodological flaw:
- In the benchmark script, SQLite was initialized by **pre-loading the entire CSV into an in-memory SQLite table** prior to running query timers:
  ```python
  # SQLite was preloaded!
  cur.executemany("INSERT INTO orders VALUES (?,?,?,?,?,?)", csv_reader)
  # Only this line was timed:
  t0 = time.perf_counter()
  sqlite_con.cursor().execute(sql).fetchall()
  t1 = time.perf_counter()
  ```
- Conversely, Axiom was invoked cold via CLI, meaning Axiom had to spawn a fresh process, load CRT DLLs, parse 104.8 MB of CSV text, build `RecordBatch` streams, and execute the query within that same timed window!

When SQLite's pre-load insertion time is included (approximately **14,200 ms** for 3.5 million rows), Axiom's cold execution time of **3,308 ms** is actually **4.3x faster end-to-end than SQLite's data load + query time**. However, on pure query execution over preloaded data, SQLite's optimized C B-tree is faster than Axiom's cold CSV pipeline.

---

### 5. Proper Context Regarding DuckDB's 5 GB Memory Behavior

In the Milestone M3 report, DuckDB's failure on the 5 GB dataset was characterized as evidence of Axiom's superiority. This interpretation is scientifically flawed:

1. **Test Configuration:**
   DuckDB was invoked in Python using `duckdb.connect(":memory:")` and `read_csv_auto()`, without specifying a memory limit (`SET max_memory='...'`) or disk spill directory (`SET temp_directory='...'`).
2. **Host Constraints:**
   The test machine possessed 8 GB of physical RAM, with only ~1.8 GB free. Attempting to ingest a 5.37 GB CSV into an unconstrained in-memory DuckDB instance predictably exceeded physical RAM.
3. **Engine Evaluation:**
   DuckDB is a world-class analytical query engine capable of out-of-core execution when explicitly configured with `SET max_memory='...'` and a valid `temp_directory`. The out-of-memory error was an artifact of running an unconstrained in-memory database on a memory-constrained host, not an inherent architectural limitation of DuckDB.

Axiom's accomplishment on 5 GB is not that it is "better than DuckDB," but rather that **Axiom's internal memory management strictly and autonomously honored its 64 MB budget**, successfully spilling 3.74 GB to disk and completing the analytical workload in 25.88 MB of resident memory without crashing the host.

---

### 6. Audit Conclusion

All benchmark conclusions have been corrected:
1. Axiom is $6\times$ to $24\times$ slower than DuckDB in single-node RAM due to DuckDB's 16-thread parallelism, SIMD columnar execution, and morsel-driven push scheduling.
2. Axiom is slower than SQLite on preloaded in-memory tables, but faster than SQLite end-to-end when raw CSV ingestion is included.
3. Axiom provides strictly verified memory boundedness on multi-gigabyte datasets where unconstrained in-memory engines exhaust system RAM.
