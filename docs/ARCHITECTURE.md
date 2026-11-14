# Project Axiom Architecture Specification (v0.1.0 Release)

## 1. System Overview

**Project Axiom** is an out-of-core, memory-bounded analytical SQL database engine built from scratch in modern C++20. It implements a complete, self-contained relational query processing pipeline:
- Lexical analysis with source location tracking
- Recursive-descent and Pratt-style expression parsing
- Catalog registration and streaming RFC-4180 CSV storage
- Semantic analysis, column resolution, and validation (Binder)
- Logical query plan construction and rule-based optimization (`LogicalOptimizer`)
- Centralized query memory management with process working set and private commit telemetry (`QueryMemoryManager`)
- Binary temporary disk spilling infrastructure (`BinarySerializer`, `SpillManager`)
- Out-of-core physical execution operators:
  - Block Grace Hash Join with adaptive disk partitioning and probe rewinding
  - External Hash Partitioned Aggregation
  - Hierarchical Multi-Pass Merge Sort with Top-K bounded max-heap optimization
- Command-line interface with interactive, batch, and profiling modes (`--profile`, `--explain-analyze`, `--memory-limit`, `--temp-dir`)

Axiom does not embed, fork, or delegate execution to SQLite, DuckDB, PostgreSQL, or any third-party parser, optimizer, or database engine.

```
                          SQL Query String
                                 │
                                 ▼
                         ┌───────────────┐
                         │     Lexer     │  (SourceLocation, Keywords, Identifiers)
                         └───────┬───────┘
                                 │ Tokens
                                 ▼
                         ┌───────────────┐
                         │    Parser     │  (AST, Pratt Expression Parser)
                         └───────┬───────┘
                                 │ Query AST
                                 ▼
                         ┌───────────────┐
                         │    Binder     │ ◄─── Catalog (Table & Schema Metadata)
                         └───────┬───────┘
                                 │ Bound AST (Types, Scopes, Resolved Columns)
                                 ▼
                         ┌───────────────┐
                         │Logical Planner│  (Initial Logical Operator Tree)
                         └───────┬───────┘
                                 │ Logical Plan
                                 ▼
                         ┌───────────────┐
                         │Logical Optimiz│  (Predicate Pushdown, Join Sides, Top-K)
                         └───────┬───────┘
                                 │ Optimized Logical Plan
                                 ▼
                         ┌───────────────┐
                         │Physical Plan. │  (Physical Operator Tree)
                         └───────┬───────┘
                                 │ Physical Operators (Init, Next, Close)
                                 ▼
                         ┌───────────────┐  ◄─── QueryMemoryManager (Budget, Commit & RSS)
                         │Execution Engine│ ◄─── SpillManager (AXMB Binary Spilling)
                         └───────┬───────┘  ◄─── CSV Storage (Chunked I/O)
                                 │ RecordBatches
                                 ▼
                            QueryResult (Data, Profile, & Resource Telemetry)
```

---

## 2. Core Architecture & Subsystems

### 2.1 Centralized Query Memory Management
`QueryMemoryManager` provides centralized memory budgeting and dual-layer resource telemetry across all physical operators:
- Operators create RAII `MemoryReservation` objects to account for internal analytical data structures (hash tables, row vectors, sort runs, active merge batches).
- Continuously samples OS physical memory via Windows `PROCESS_MEMORY_COUNTERS_EX`, reporting both **Process Working Set** (resident physical RAM) and **Private Commit Charge** (actual committed virtual memory).
- Configurable via CLI (`--memory-limit 256MB`) or programmatic `QueryOptions`.

### 2.2 Binary Temporary Storage Format (`AXMB` Version 1)
All temporary disk spilling utilizes the custom binary format `AXMB`:
- Magic bytes `0x424D5841` ('AXMB') and format version identifier.
- Schema column descriptors, row count headers, and per-column bit-packed NULL bitmaps.
- Contiguous typed value payloads without text conversion overhead.
- Tail 32-bit FNV-1a checksums for truncation and corruption detection.
- Malformed header defense: Enforces strict caps on deserialized row counts ($\le 10,000,000$) and payload byte buffers ($\le 256\text{ MB}$) to prevent out-of-memory aborts on tampered files.

### 2.3 Out-of-Core Physical Operators
1. **Block Grace Hash Join (`PhysicalHashJoin`):**
   - In-memory fast path when build table fits within memory reservation.
   - Grace hash partitioning into $B=16$ spilled disk partitions on build and probe keys when memory limits are reached.
   - Sequential partition pair processing: only 1 build partition and 1 probe batch in RAM at any time.
   - **Block Partition Handling:** If an individual partition exceeds operator memory (due to key skew, identical keys, or extreme memory limits), the partition is processed in chunked in-memory hash tables, rewinding the probe partition file for each chunk and paginating matches across batch boundaries without data loss or duplicate emission.
   - Immediate partition file unlinking upon completion.
2. **External Hash Aggregation (`PhysicalHashAggregate`):**
   - Scalar aggregations (`group_by.empty()`) execute in constant $O(1)$ memory without spilling.
   - Grouped aggregations hash-partition incoming rows into $P=16$ disk partitions by group key hash.
   - Partitions are aggregated sequentially, bounding resident memory to $1/P$ of unique groups.
3. **Hierarchical Merge Sort with Top-K Heap (`PhysicalSort`):**
   - Top-K Bounded Max-Heap: When `ORDER BY ... LIMIT K` ($K \le 50,000$) is executed, maintains a bounded max-heap of size $K$ in memory. Runs in $O(N \log K)$ time with zero disk spilling.
   - **Hierarchical Multi-Pass Merge:** Generates sorted runs bounded by operator budget ($40\%$ of total budget). If the number of runs exceeds dynamically calculated fan-in ($k \le 16$), merges runs in multi-pass hierarchical slices, unlinking input files immediately after each slice merge.
   - Memory-accounted merge cursors: Active cursors reserve memory in `QueryMemoryManager`, strictly capping resident working set during multi-way merges.

### 2.4 Temporary Storage & Volume Safety (`SpillManager`)
- Dynamic volume capacity check: Probes target filesystem free space, falling back to local workspace storage if default system temp volume has $< 1\text{ GB}$ available.
- Telemetry captures both **Peak Instantaneous Disk Spilled** (high-water mark of live temporary files) and **Cumulative Disk Written** (total bytes written across all runs and partitions).
- Enforces strict disk quotas with clean exception handling and immediate file deletion on query abort.

### 2.5 Rule-Based Logical Optimizer (`LogicalOptimizer`)
- **Predicate Pushdown:** Decomposes filter predicates into conjuncts and pushes child-specific filters through joins down to scan operators.
- **Top-K Pushdown:** Pushes `LogicalLimit` through `LogicalProjection` directly into `PhysicalSort`.
- **Join-Side Selection:** Arranges smaller relations on the build side of equijoins.
