# Project AXIOM — Milestone M3
## Benchmark Methodology, Environment & Comparative Framework

**Auditor:** Independent Systems Verification Lead & Performance Architect  
**Engine:** Axiom v0.1.0 Release Candidate  
**Reference Engines:** DuckDB v2.0.0 & SQLite 3  
**Certification Status:** VERIFIED & DOCUMENTED

---

### 1. Hardware & Operating Environment Specification

All benchmarks were conducted on a standardized reference hardware platform to ensure rigorous reproducibility:

- **Host Machine:** Windows 11 Home x64 (Build 26100)
- **CPU Architecture:** x86_64, 8 physical / 16 logical cores
- **Physical Memory:** 8.00 GB DDR4 (Available physical RAM during test runs: ~1.8 GB – 2.2 GB)
- **Primary Storage (Axiom Workspace & Spill Volume):** Fast NVMe SSD (Drive `E:`, 105 GB free space)
- **System Drive:** Drive `C:`, 218 MB free space (bypassed via smart storage routing)
- **Toolchain:** MSVC 19.43.34808 (Visual Studio 2022 Dev Command Prompt v17.13.5), Ninja 1.12.1, CMake 3.30.5
- **Compilation Flags:** `/std:c++20 /O2 /permissive- /utf-8 /FS` (Release mode)
- **Python Harness:** Python 3.12 with DuckDB 2.0.0 and SQLite 3.45.3

---

### 2. Benchmark Workloads & Query Definitions

The benchmark suite defines 4 standardized analytical SQL workloads representing distinct query processing patterns:

#### Workload 1: Filtered Aggregation (Scalar & Low-Cardinality Group-By)
- **SQL:**
  ```sql
  SELECT status, COUNT(*), SUM(quantity), AVG(discount) 
  FROM orders 
  WHERE quantity > 5 
  GROUP BY status
  ```
- **Operator Profile:** `PhysicalCsvScan` $\to$ `PhysicalFilter` $\to$ `PhysicalHashAggregate`.
- **Target Characteristic:** Evaluates pure Volcano streaming throughput and filter pushdown speed. Pipelined execution maintains $O(1)$ memory state regardless of row count.

#### Workload 2: High-Cardinality Aggregation (Disk Spilling Aggregation)
- **SQL:**
  ```sql
  SELECT item_id, COUNT(*), SUM(quantity) 
  FROM orders 
  GROUP BY item_id 
  ORDER BY COUNT(*) DESC, item_id ASC 
  LIMIT 20
  ```
- **Operator Profile:** `PhysicalCsvScan` $\to$ `PhysicalHashAggregate` (16-way hash partitioning & spill) $\to$ `PhysicalSort` (Top-K heap).
- **Target Characteristic:** Evaluates out-of-core partition generation, `.axmb` binary serialization, and intermediate hash table accumulation under strict memory budgets.

#### Workload 3: Equi-Join with Dimension Table (Grace Hash Join)
- **SQL:**
  ```sql
  SELECT o.status, i.category, COUNT(*), SUM(o.quantity) 
  FROM orders o 
  JOIN items i ON o.item_id = i.item_id 
  GROUP BY o.status, i.category
  ```
- **Operator Profile:** Dual `PhysicalCsvScan` $\to$ `PhysicalHashJoin` (16-way Grace partition spilling & block-by-block probe rewinding) $\to$ `PhysicalHashAggregate`.
- **Target Characteristic:** Stresses multi-relation partition synchronization, probe rewinding, and memory-bounded block grace matching.

#### Workload 4: External Merge Sort (Multi-Run K-Way Merge)
- **SQL:**
  ```sql
  SELECT order_id, quantity, discount 
  FROM orders 
  ORDER BY quantity DESC, discount DESC 
  LIMIT 60000
  ```
- **Operator Profile:** `PhysicalCsvScan` $\to$ `PhysicalSort` (Run generation, `.axmb` spill, and K-way priority queue merge).
- **Target Characteristic:** Exceeds the 50,000 Top-K heap threshold (`LIMIT 60000`), forcing generation of hundreds of sorted runs on disk and multi-cursor merge.

---

### 3. Dataset Scales & Physical Dimensions

Datasets were generated using deterministic pseudo-random generators (`benchmarks/generators/generate_benchmark_data.py`):

| Scale | Table | Rows | File Size | Physical Schema |
|:---:|:---:|:---:|:---:|:---|
| **100 MB** | `orders` | 3,494,109 | 104.8 MB | `order_id (BIGINT), item_id (BIGINT), quantity (BIGINT), price (DOUBLE), discount (DOUBLE), status (VARCHAR)` |
| | `items` | 50,000 | 1.2 MB | `item_id (BIGINT), name (VARCHAR), category (VARCHAR), price (DOUBLE)` |
| **1 GB** | `orders` | 33,907,545 | 1,073.7 MB | Same as above |
| | `items` | 200,000 | 5.0 MB | Same as above |
| **5 GB** | `orders` | 164,422,022 | 5,368.7 MB | Same as above |
| | `items` | 500,000 | 12.5 MB | Same as above |

---

### 4. Memory Limiting & Measurement Protocol

1. **Memory Budget Injection:**
   Axiom queries were executed via CLI with explicit `--memory-limit` parameters (16 MB for 100 MB scale, 32 MB for 1 GB scale, 64 MB for 5 GB scale).
2. **Process Telemetry:**
   Logged via `--profile`, capturing:
   - Wall-clock query latency (`Total Execution Time: X.XX ms`)
   - Peak Accounted Memory (`QueryMemoryManager`)
   - Peak Process Working Set (`K32GetProcessMemoryInfo::WorkingSetSize`)
   - Peak Private Virtual Commit (`PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` & `PeakPagefileUsage`)
   - Total Disk Spilled bytes and active partition counts
3. **Reference Engine Protocol:**
   - **DuckDB:** Executed via Python `duckdb.connect(":memory:")` using `read_csv_auto()`. For 100 MB and 1 GB scales, execution was timed after loading data to measure analytical query execution.
   - **SQLite:** Executed via Python `sqlite3.connect(":memory:")` on the 100 MB scale. Loading 1 GB or 5 GB CSVs into SQLite was omitted due to excessive B-tree insertion latency (> 45 minutes).
