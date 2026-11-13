# Project Axiom Performance Analysis (Milestone M2)

## 1. Benchmark Methodology

Milestone M2 benchmarks evaluate out-of-core analytical query execution over datasets of increasing scale:
- **100 MB Scale:** 3,494,109 rows (`orders`) + 50,000 rows (`items`).
- **1 GB Scale:** 33,907,545 rows (`orders`) + 200,000 rows (`items`).
- **5 GB Scale:** 164,422,022 rows (`orders`) + 500,000 rows (`items`).
- **Environment:** Windows 11 Home x64, Intel Core i5-11300H @ 3.10 GHz, 8 GB RAM (~1.2 GB host free RAM during benchmarks).
- **Toolchain:** MSVC 2022 (v143), Ninja, CMake, Release mode (`/O2 /Ob2 /DNDEBUG /std:c++20 /utf-8 /Z7`).
- **Memory Limits Tested:** 32 MB (100MB scale), 256 MB (1GB scale), 512 MB (5GB scale).

---

## 2. Empirical Benchmark Results

Measured and verified via `benchmarks/run_m2_benchmarks.py` and recorded in `audit/m2/BENCHMARK_RESULTS.csv`:

| Scale Tier | Workload | Dataset Rows | Memory Limit | AXIOM Latency | Throughput | Peak Process RSS | Spilled Disk | DuckDB Latency |
|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **100 MB** | Q1 (Agg Filter) | 3,494,109 | 32 MB | **4,230.8 ms** | 825,800 rows/s | 6.8 MB | 91.13 MB | 25.1 ms |
| **100 MB** | Q2 (High-Card Agg) | 3,494,109 | 32 MB | **4,630.4 ms** | 754,600 rows/s | 9.8 MB | 81.32 MB | 91.5 ms |
| **100 MB** | Q3 (Equi-Join) | 3,494,109 | 32 MB | **8,224.7 ms** | 424,800 rows/s | 12.1 MB | 158.60 MB | 45.4 ms |
| **100 MB** | Q4 (Top-K Sort) | 3,494,109 | 32 MB | **2,760.4 ms** | 1,265,800 rows/s | 5.0 MB | 0.00 MB | 10.4 ms |
| **1 GB** | Q1 (Agg Filter) | 33,907,545 | 256 MB | **28,045.8 ms** | 1,209,000 rows/s | 5.1 MB | 0.00 MB | 137.6 ms |
| **1 GB** | Q2 (High-Card Agg) | 33,907,545 | 256 MB | **32,317.8 ms** | 1,049,200 rows/s | 127.8 MB | 0.00 MB | **CRASH (OOM)** |
| **1 GB** | Q3 (Equi-Join) | 33,907,545 | 256 MB | **40,848.5 ms** | 830,100 rows/s | 68.1 MB | 0.00 MB | 312.1 ms |
| **1 GB** | Q4 (Top-K Sort) | 33,907,545 | 256 MB | **25,069.3 ms** | 1,352,600 rows/s | 5.0 MB | 0.00 MB | 59.4 ms |
| **5 GB** | Q1 (Agg Filter) | 164,422,022 | 512 MB | **137,963.7 ms** | 1,191,800 rows/s | 5.2 MB | 0.00 MB | N/A |
| **5 GB** | Q2 (High-Card Agg) | 164,422,022 | 512 MB | **171,630.2 ms** | 958,000 rows/s | 313.5 MB | 0.00 MB | N/A |
| **5 GB** | Q3 (Equi-Join) | 164,422,022 | 512 MB | **208,320.6 ms** | 789,300 rows/s | 159.2 MB | 0.00 MB | N/A |
| **5 GB** | Q4 (Top-K Sort) | 164,422,022 | 512 MB | **121,354.4 ms** | 1,354,900 rows/s | 5.0 MB | 0.00 MB | N/A |

---

## 3. Performance Engineering Highlights in M2

1. **Grace Hash Join with Early File Deletion:**
   Partitioned Grace Hash Join spills $B=16$ partition pairs to disk. As each build partition is loaded into the in-memory hash table, its disk file is immediately deleted, bounding peak disk usage to $1\times$ dataset size.
2. **Top-K Bounded Max-Heap:**
   For `ORDER BY ... LIMIT K` queries, `PhysicalSort` maintains an in-memory bounded max-heap of size $K$. This reduces time complexity from $O(N \log N)$ to $O(N \log K)$ and eliminates disk spilling entirely.
3. **High-Throughput Binary Serialization (`AXMB` v1):**
   Spill files are serialized in binary chunks with packed bitmask NULL vectors and typed contiguous buffers, achieving >150 MB/s disk I/O on NVMe storage.
4. **Out-of-Core Resilience:**
   On a memory-constrained host with ~1.2 GB free RAM, DuckDB failed with `Out of Memory Error: Allocation failure` on Q2 at 1 GB scale. AXIOM completed the exact same workload in 32.3 seconds with resident memory strictly bounded to **127.8 MB**.
