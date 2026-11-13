# Project AXIOM — Milestone M2: Spill Lifecycle & Disk Subsystem Audit

**Milestone:** M2 (Temporary Spill Format, Partition Lifecycle, and Cleanup Verification)  
**Date:** 2026-11-14  

---

## 1. Binary Temporary Format (`AXMB` Version 1)

Disk spilling uses the custom binary format `AXMB` (`include/axiom/storage/binary_serializer.h`).

### 1.1 File Header Specification
- **Magic:** `0x424D5841` ('AXMB' in little-endian ASCII)
- **Version:** `0x0001` (Version 1)
- **Num Columns:** `uint32_t`
- **Num Rows:** `uint32_t`

### 1.2 Column Layout & Encoding
For each column:
1. **Type Identifier:** `uint8_t` (0: BigInt, 1: Double, 2: VarChar, 3: Boolean, 4: Null)
2. **Null Bitmap:**
   - 1 bit per row (1 = NULL, 0 = Valid value).
   - Packed into contiguous `uint64_t` words: $\lceil N_{\text{rows}} / 64 \rceil \times 8$ bytes.
3. **Payload Stream:**
   - `BigInt`: Contiguous `int64_t` values (8 bytes each).
   - `Double`: Contiguous IEEE 754 `double` values (8 bytes each).
   - `Boolean`: Contiguous `uint8_t` values (1 byte each).
   - `VarChar`: Length-prefixed string array: `uint32_t length` followed by raw UTF-8 string bytes.
4. **Tail Integrity Checksum:**
   - 32-bit FNV-1a checksum calculated over the entire batch payload.
   - Verified on deserialization to detect truncation or partial writes.

---

## 2. Partition Lifecycle & State Machine

```
   [create_partition()]
            |
            v
      +------------+
      |  WRITING   | <---+ (write_batch())
      +------------+     |
            |            |
            | finish_write()
            v
      +------------+
      |   CLOSED   |
      +------------+
            |
            | prepare_read()
            v
      +------------+
      |  READING   | <---+ (read_next_batch())
      +------------+     |
            | (EOF / remove_file())
            v
      +------------+
      |  DELETED   |
      +------------+
```

### 2.1 File Descriptor Mitigation
On Windows x64, MSVC CRT limits open file handles to 512 by default.
1. `SpillManager` calls `_setmaxstdio(2048)` upon initialization.
2. `SpillPartition::finish_write()` immediately flushes and closes `std::ofstream` upon completion of a spill run.
3. `SpillPartition::read_next_batch()` closes `std::ifstream` immediately when reaching EOF.
4. `PhysicalHashJoin` deletes each build partition file as soon as it is loaded into the in-memory hash table, halving peak disk space usage.

---

## 3. Leaked File Verification & Audit Evidence

### 3.1 Normal Out-of-Core Execution Test
- **Query:** `Q3_EQUI_JOIN` on 100 MB dataset with `--memory-limit 32MB`.
- **Partitions Created:** 48 partitions (158.6 MB spilled).
- **Residual Files in Temp Directory after Query Completion:** **0 files** (verified via directory scan).

### 3.2 Adversarial Termination Tests
Executed via `tests/integration/test_m2_adversarial.py`:
1. **Unwritable Directory:** Fails cleanly with `Storage Error`, 0 leaked files.
2. **Exceeded Disk Quota:** Manager stops further writes with `Temporary disk quota exceeded`, invokes RAII cleanup, 0 leaked files.
3. **Tiny Memory Budget (10 KB):** Generates partitions, completes join, deletes all files, 0 leaked files.
4. **Early `LIMIT` Termination:** Sort child cancels reading, RAII destructor deletes all active run files, 0 leaked files.

All 4 adversarial tests passed with 100% verification.

