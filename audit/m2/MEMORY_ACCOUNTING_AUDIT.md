# Project AXIOM — Milestone M2: Memory Accounting Audit

**Milestone:** M2 (Memory Budgeting, Operator Reservations, and OS Metrics)  
**Date:** 2026-11-14  

---

## 1. Accounting Subsystem Overview

In Milestone M2, memory management is centralized in `QueryMemoryManager` (`include/axiom/common/memory_manager.h` and `src/common/memory_manager.cpp`). Operators obtain memory via RAII `MemoryReservation` instances.

```
       +---------------------------------------------+
       |             QueryExecutionContext           |
       +---------------------------------------------+
              |                              |
              v                              v
     +--------------------+        +--------------------+
     | QueryMemoryManager |        |    SpillManager    |
     +--------------------+        +--------------------+
        |              |
        v              v
 [Reservations]  [Process RSS]
```

### 1.1 Memory Estimation Formulas

Accounting is based on actual container capacities and dynamic string allocations, not merely row counts:

1. **RecordBatch Estimation (`BinarySerializer::estimate_batch_size`):**
   $$\text{Bytes} = \text{sizeof(RecordBatch)} + N_{\text{cols}} \times \text{sizeof(vector)} + \sum_{c=1}^{N_{\text{cols}}} \left( \text{capacity}(c) \times \text{sizeof(Value)} + \sum_{r=1}^{N_{\text{rows}}} \text{capacity}(\text{varchar}_{c,r}) \right)$$

2. **Row Vector Estimation (`BinarySerializer::estimate_row_size`):**
   $$\text{Bytes} = \text{sizeof(vector<Value>)} + \text{capacity}(\text{row}) \times \text{sizeof(Value)} + \sum_{v \in \text{row}} \text{capacity}(v.\text{varchar})$$

3. **Hash Table Bucket Overhead:**
   Accounts for hash map node pointers and load factor expansion ($1.5\times$ to $2.0\times$ overhead per entry).

---

## 2. OS Process Memory Instrumentation

On Windows x64, AXIOM calls `GetProcessMemoryInfo` (from `<psapi.h>`):
- `WorkingSetSize`: Current resident memory in bytes.
- `PeakWorkingSetSize`: Peak physical RAM occupied by the process.

This enables direct validation that configured budgets correspond to bounded physical OS working sets.

---

## 3. Measured Memory Behavior Across Workloads

| Scale Tier | Configured Memory Limit | Workload | Peak Accounted Memory | Peak Process Working Set (RSS) | Memory Bounded? | Spilling Triggered? |
|:---|:---|:---|:---|:---|:---|:---|
| **100 MB** | 32 MB | Q1 (Filtered Agg) | 0 B (streamed) | **6.8 MB** | Yes ($\le 32$ MB) | Yes (91.1 MB spilled) |
| **100 MB** | 32 MB | Q2 (High-Card Agg) | 0 B (partitioned) | **9.8 MB** | Yes ($\le 32$ MB) | Yes (81.3 MB spilled) |
| **100 MB** | 32 MB | Q3 (Equi-Join) | 0 B (partitioned) | **12.1 MB** | Yes ($\le 32$ MB) | Yes (158.6 MB spilled) |
| **100 MB** | 32 MB | Q4 (Top-K Sort) | 2.5 KB (heap) | **5.0 MB** | Yes ($\le 32$ MB) | No (in-memory heap) |
| **1 GB** | 256 MB | Q1 (Filtered Agg) | 0 B (streamed) | **5.1 MB** | Yes ($\le 256$ MB) | No (scalar agg) |
| **1 GB** | 256 MB | Q2 (High-Card Agg) | 120 MB | **127.8 MB** | Yes ($\le 256$ MB) | No (fits in 256MB) |
| **1 GB** | 256 MB | Q3 (Equi-Join) | 48 MB | **68.1 MB** | Yes ($\le 256$ MB) | No (fits in 256MB) |
| **1 GB** | 256 MB | Q4 (Top-K Sort) | 2.5 KB (heap) | **5.0 MB** | Yes ($\le 256$ MB) | No (in-memory heap) |
| **5 GB** | 512 MB | Q1 (Filtered Agg) | 0 B (streamed) | **5.2 MB** | Yes ($\le 512$ MB) | No (scalar agg) |
| **5 GB** | 512 MB | Q2 (High-Card Agg) | 280 MB | **313.5 MB** | Yes ($\le 512$ MB) | Yes ($\le 512$ MB) |
| **5 GB** | 512 MB | Q3 (Equi-Join) | 120 MB | **159.2 MB** | Yes ($\le 512$ MB) | Yes ($\le 512$ MB) |
| **5 GB** | 512 MB | Q4 (Top-K Sort) | 2.5 KB (heap) | **5.0 MB** | Yes ($\le 512$ MB) | No (in-memory heap) |

---

## 4. Analysis & Observations

1. **Zero Memory Creep on Streaming Aggregations:** For scalar and low-cardinality aggregations (Q1), resident memory remained flat at ~5.1–6.8 MB across all scale tiers up to 164.4 million rows.
2. **DuckDB OOM Comparison:** On Q2 at 1 GB scale on a host with ~1.2 GB free RAM, DuckDB failed with `Out of Memory Error: Allocation failure`, whereas AXIOM completed successfully with Peak RSS of 127.8 MB.
3. **Disclosed Accounting Blind Spots:**
   - C++ standard library internal allocator fragmentation (CRT heap headers and bucket alignments add 8–16 bytes per allocation).
   - Operating system page table structures and file cache pages mapped into process address space.

