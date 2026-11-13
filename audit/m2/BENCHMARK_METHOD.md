# Project AXIOM — Milestone M2: Benchmark Methodology & Environment

**Milestone:** M2 (Performance Engineering & Out-of-Core Benchmarking)  
**Date:** 2026-11-14  

---

## 1. Hardware & Software Environment

- **Operating System:** Windows 11 Home x64 (Build 26100)
- **Processor:** 11th Gen Intel(R) Core(TM) i5-11300H @ 3.10GHz (4 physical cores, 8 logical processors)
- **RAM:** 8.00 GB Physical RAM (~1.2 GB available host free memory during testing)
- **Storage:** NVMe SSD (Drive E:, 119.5 GB free space)
- **C++ Compiler:** Microsoft Visual C++ 2022 (MSVC 19.43.34808, v143 toolset)
- **Build System:** CMake 3.30.5, Ninja 1.12.1, Target `Release` with `/O2 /Ob2 /DNDEBUG /std:c++20 /utf-8 /Z7`
- **External References:** DuckDB 2.0.0 (Python x64 wheel)

---

## 2. Dataset Synthesis & Scale Tiers

Synthetic analytical datasets were generated using `benchmarks/generators/generate_multi_gb.py` via deterministic pseudorandom streaming algorithms.

| Scale Tier | Orders Rows | Orders File Size | Items Rows | Items File Size | Total Dataset Size |
|:---|:---:|:---:|:---:|:---:|:---:|
| **100 MB** | 3,494,109 | 100.0 MB | 50,000 | 1.8 MB | **101.8 MB** |
| **1 GB** | 33,907,545 | 1,024.0 MB | 200,000 | 7.3 MB | **1,031.3 MB** |
| **5 GB** | 164,422,022 | 5,120.0 MB | 500,000 | 18.2 MB | **5,138.2 MB** |

---

## 3. Workload Definitions

1. **Q1 (Filtered Aggregation):**
   ```sql
   SELECT status, COUNT(*), SUM(quantity), AVG(discount)
   FROM orders
   WHERE quantity > 5
   GROUP BY status
   ```
2. **Q2 (High-Cardinality Group By):**
   ```sql
   SELECT item_id, COUNT(*), SUM(quantity)
   FROM orders
   GROUP BY item_id
   ORDER BY COUNT(*) DESC, item_id ASC
   LIMIT 20
   ```
3. **Q3 (Equi-Join with Dimension Table):**
   ```sql
   SELECT o.status, i.category, COUNT(*), SUM(o.quantity)
   FROM orders o
   JOIN items i ON o.item_id = i.item_id
   GROUP BY o.status, i.category
   ```
4. **Q4 (Top-K Bounded Heap Sort):**
   ```sql
   SELECT order_id, quantity, discount
   FROM orders
   ORDER BY quantity DESC, discount DESC
   LIMIT 50
   ```

---

## 4. Measurement & Accounting Protocol

- Each query is executed as a standalone process via `build/axiom.exe`.
- Process memory is measured via `GetProcessMemoryInfo` (retrieving `PeakWorkingSetSize` from the operating system kernel).
- Execution timing is measured using high-resolution monotonic clocks (`std::chrono::steady_clock` in C++ and `time.perf_counter` in Python).
- Spilled disk volume is tracked via atomic byte counters in `SpillManager`.
- Results are recorded directly into `audit/m2/BENCHMARK_RESULTS.csv`.

