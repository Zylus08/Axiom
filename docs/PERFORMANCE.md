# Project Axiom Performance Analysis (Milestone M1)

## 1. Benchmark Methodology

Benchmarks were executed on a realistic 100,000-row `orders` dataset joined against a 20,000-row `items` dataset.
- Environment: Windows 11 (x64), Visual Studio 2022 C++20 (MSVC 19.43), Release mode (`/O2`, `/Ob2`, `/NDEBUG`).
- Batch size: 1,024 rows.
- Timing: High-precision wall-clock measurements across multiple iterations with cold/warm execution separation.

---

## 2. Measured Benchmark Results

| Query ID | Workload Name | Latency (Avg) | Throughput | Rows Emitted |
| :--- | :--- | :---: | :---: | :---: |
| **Q1** | Table Scan with Filter | 110.1 ms | **908,320 rows/sec** | 7,454 |
| **Q2** | Simple Aggregation (`COUNT`, `SUM`) | 93.4 ms | **1,071,146 rows/sec** | 1 |
| **Q3** | Hash Group-By Aggregation | 137.0 ms | **730,096 rows/sec** | 4 |
| **Q4** | Hash Inner Join (100k x 20k) | 244.0 ms | **409,858 rows/sec** | 50,030 |
| **Q5** | Join + Group-By + Aggregation | 156.0 ms | **641,023 rows/sec** | 8 |
| **Q6** | Sort + Limit (Top-50) | 163.3 ms | **612,397 rows/sec** | 50 |

---

## 3. Performance Architecture Highlights

1. **Streaming Chunked CSV Parsing:**
   `CsvReader` buffers I/O in 64 KB chunks and parses tokens directly without full-file allocation. Integer conversion utilizes `<charconv>` `std::from_chars`.
2. **Batch-Oriented Pipeline:**
   Operators process chunks of 1,024 rows. Slicing and filtering avoid unnecessary value deep-copies.
3. **Pre-evaluated Expression Vectors:**
   `PhysicalSort` and `PhysicalHashAggregate` evaluate expressions once per batch rather than evaluating expressions repetitively inside comparators or hashing routines.
4. **Fast Hash Tables for Equijoins:**
   `PhysicalHashJoin` detects equijoin equality keys in `ON` conditions, shifts indices, and builds an in-memory hash table on the smaller build side before streaming the probe side.

---

## 4. Path to Milestone M2 (Memory-Bounded Scaling)

In Milestone M1, hash table build states, sort states, and group aggregation states remain memory-resident during their lifetime.
For Milestone M2:
- Introduce memory budget accounting per operator.
- Implement external sorting (multi-way merge sort with spill partitions on disk).
- Implement partitioned grace hash join with partition spilling for datasets exceeding available RAM.

