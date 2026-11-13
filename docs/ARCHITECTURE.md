# Project Axiom Architecture Specification (Milestone M2)

## 1. System Overview

**Project Axiom** is an out-of-core, memory-bounded analytical SQL database engine built from scratch in modern C++20. It implements a complete, self-contained relational query processing pipeline:
- Lexical analysis with source location tracking
- Recursive-descent and Pratt-style expression parsing
- Catalog registration and streaming RFC-4180 CSV storage
- Semantic analysis, column resolution, and validation (Binder)
- Logical query plan construction and rule-based optimization (`LogicalOptimizer`)
- Centralized query memory management with process working set instrumentation (`QueryMemoryManager`)
- Binary temporary disk spilling infrastructure (`BinarySerializer`, `SpillManager`)
- Out-of-core physical execution operators:
  - Grace Hash Join with adaptive disk partitioning
  - External Hash Partitioned Aggregation
  - External Merge Sort with Top-K bounded max-heap optimization
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
                         ┌───────────────┐  ◄─── QueryMemoryManager (Budget & RSS)
                         │Execution Engine│ ◄─── SpillManager (Binary AXMB Spilling)
                         └───────┬───────┘  ◄─── CSV Storage (Chunked I/O)
                                 │ RecordBatches
                                 ▼
                            QueryResult (Data, Profile, & Resource Telemetry)
```

---

## 2. Milestone M2 Subsystems

### 2.1 Centralized Query Memory Management
`QueryMemoryManager` provides centralized memory budgeting across all physical operators:
- Operators create RAII `MemoryReservation` objects.
- Reservations track actual container capacity, hash table bucket pointers, and dynamic string payloads.
- Continuously samples OS working set via Windows API `GetProcessMemoryInfo`.
- Memory limits can be configured via CLI (`--memory-limit 256MB`) or programmatic `QueryOptions`.

### 2.2 Binary Temporary Storage Format (`AXMB` Version 1)
All temporary disk spilling utilizes the custom binary format `AXMB`:
- Magic bytes `0x424D5841` ('AXMB') and version identifier.
- Column descriptions, row counts, and per-column bit-packed NULL bitmaps.
- Contiguous typed value payloads without text conversion overhead.
- Tail 32-bit FNV-1a checksums for corruption and truncation detection.

### 2.3 Out-of-Core Physical Operators
1. **Grace Hash Join (`PhysicalHashJoin`):**
   - In-memory fast path when build table fits within memory reservation.
   - Grace hash partitioning into $B=16$ spilled disk partitions on build and probe keys when memory limit is constrained.
   - Sequential partition pair processing: only 1 build partition and 1 probe batch in RAM at any time.
   - Immediate partition file deletion upon completion.
2. **External Hash Aggregation (`PhysicalHashAggregate`):**
   - Scalar aggregations (`group_by.empty()`) execute in constant $O(1)$ memory without spilling.
   - Grouped aggregations hash-partition incoming rows into $P=16$ disk partitions by group key hash.
   - Partitions are aggregated sequentially, bounding resident memory to $1/P$ of unique groups.
3. **External Sort with Top-K Heap (`PhysicalSort`):**
   - Top-K Bounded Max-Heap: When `ORDER BY ... LIMIT K` ($K \le 50,000$) is executed, maintains a bounded max-heap of size $K$ in memory. Runs in $O(N \log K)$ time with zero disk spilling.
   - External Merge Sort: Generates sorted runs bounded by operator budget, spills runs via `BinarySerializer`, and merges them using a priority queue multi-way merge.

### 2.4 Rule-Based Logical Optimizer (`LogicalOptimizer`)
- **Predicate Pushdown:** Decomposes filter predicates into conjuncts and pushes child-specific filters through joins down to scan operators.
- **Top-K Pushdown:** Pushes `LogicalLimit` through `LogicalProjection` directly into `PhysicalSort`.
- **Join-Side Selection:** Arranges smaller relations on the build side of equijoins.
