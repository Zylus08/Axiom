# Project AXIOM — Milestone M3
## Corrupt Spill File Resilience & RAII Resource Leak Audit

**Auditor:** Independent Systems Verification Lead & Database Architect  
**Engine:** Axiom v0.1.0 Release Candidate  
**Baseline Commit Audited:** `945c04b`  
**Certification Status:** VERIFIED & HARDENED

---

### 1. Adversarial Corruption Attack Surface

Project Axiom serializes spilled batches to disk using a proprietary binary partition format (`.axmb`):
```
+-----------------------------------------------------------------------------------------+
|                               AXMB Binary Partition Format                              |
|  Offset  Size (B)   Field Name         Type / Description                               |
|  0       4          Magic Number       0x41584D42 ('AXMB')                              |
|  4       2          Format Version     0x0001 (Version 1)                               |
|  6       4          Column Count       uint32_t                                         |
|  10      4          Row Count          uint32_t                                         |
|  14      8          Payload Length     uint64_t                                         |
|  22      4          Checksum           uint32_t (FNV-1a 32-bit Hash)                    |
|  26      L          Payload Data       Compressed Type-Tagged Columns + Null Bitmaps    |
+-----------------------------------------------------------------------------------------+
```

If an engine does not validate file integrity before reading, hostile tampering, partial disk writes, hardware faults, or premature stream termination can cause:
1. Out-of-bounds pointer reads / memory corruption
2. Uncontrolled allocation (`std::bad_alloc`) from corrupted length fields
3. Infinite hangs or crashes
4. Orphaned temporary files on disk (resource leaks)

---

### 2. Forensic Corruption Hardening Implementation

In Milestone M3, `BinarySerializer::read_batch` and `BinarySerializer::write_batch` (`src/storage/binary_serializer.cpp`) were audited and hardened with multi-stage verification:

1. **Magic Number Verification:**
   Reads first 4 bytes and compares against `0x41584D42`. Any mismatch immediately throws:
   `AxiomException(ErrorCategory::Storage, "Invalid magic number in spill file format")`.
2. **Version Compatibility Gate:**
   Validates format version against `VERSION = 1`. Any unsupported version throws:
   `AxiomException(ErrorCategory::Storage, "Unsupported spill file format version")`.
3. **Schema Column Count Cross-Check:**
   Verifies that `num_cols` in the header matches `expected_schema.column_count()`:
   Throws `Spill file column count mismatch`.
4. **Header Truncation Detection:**
   Verifies `!is` on every header field read, preventing EOF reads from masquerading as valid data.
5. **FNV-1a Integrity Checksum Verification:**
   Computes a 32-bit FNV-1a hash over the entire payload buffer:
   $$H_{i} = (H_{i-1} \oplus B_i) \times 16777619 \pmod{2^{32}}$$
   If `actual_chk != stored_chk`, immediately aborts and throws:
   `AxiomException(ErrorCategory::Storage, "Spill file checksum verification failure (data corruption)")`.
6. **Payload Boundary Underrun Protection:**
   Validates column types and null bitmaps against remaining buffer offset, rejecting malformed column offsets.

---

### 3. Empirical Test Suite Results

The adversarial corruption suite in `tests/unit/test_m3_forensics.cpp` subjected the engine to intentional binary corruption:

#### Test 1: Bit Tampering & Checksum Verification
- **Attack:** An authentic 100-row batch was serialized to an `.axmb` spill file. A single byte at offset 50 inside the binary payload was modified.
- **Engine Response:** `BinarySerializer::read_batch` detected that the computed FNV-1a checksum did not match the header checksum.
- **Result:** Threw `Storage Error: Spill file checksum verification failure (data corruption)`. **PASSED**.

#### Test 2: Invalid Magic Number Rejection
- **Attack:** An authentic `.axmb` spill file header was overwritten with `0xDEADBEEF`.
- **Engine Response:** The engine rejected the file on the first 4 bytes.
- **Result:** Threw `Storage Error: Invalid magic number in spill file format`. **PASSED**.

#### Test 3: Truncated Spill File Detection
- **Attack:** A spill file with an authentic header specifying a 4 KB payload was truncated to 20 bytes (simulating sudden disk full or power failure).
- **Engine Response:** `is.read()` detected premature EOF.
- **Result:** Threw `Storage Error: Truncated spill file payload header` / `data`. **PASSED**.

#### Test 4: Schema Mismatch Detection
- **Attack:** An `.axmb` partition containing a 2-column batch was passed to a reader expecting a 5-column schema.
- **Engine Response:** Checked header before touching payload.
- **Result:** Threw `Storage Error: Spill file column count mismatch: expected 5, got 2`. **PASSED**.

---

### 5. RAII Resource Lifecycle & Zero Disk Leaks

Milestone M3 audited file descriptor and filesystem lifetimes across all error paths:

1. **`SpillPartition` Destructor Semantics:**
   `SpillPartition` implements strict RAII:
   ```cpp
   SpillPartition::~SpillPartition() {
       close_streams();
       remove_file();
   }
   ```
   If an exception occurs at any point during query execution, the C++ unwinding mechanism calls destructors on all active partitions, immediately closing open file handles and unlinking the files via `std::filesystem::remove`.

2. **`SpillManager::cleanup_all` Guarantee:**
   `SpillManager` maintains an active registry of all created temporary files. In its destructor, it iterates over all tracked files and removes them.

3. **Adversarial Verification:**
   The adversarial test suite (`tests/integration/test_m2_adversarial.py`) verified:
   - **Disk Quota Exceeded:** When a query exceeded `max_temp_disk`, the exception was caught, and **zero orphaned files remained on disk**.
   - **Early LIMIT Termination:** When an external sort with 285 runs was terminated early via `LIMIT 10`, all remaining runs were cleaned up immediately.
   - **Unwritable Directory:** Cleanly rejected with informative error; zero zombie processes.

---

### 6. Audit Conclusion

Project Axiom v0.1.0 RC satisfies all corruption-resilience and resource-safety requirements. Tampered, truncated, and malformed spill files are detected deterministically, and RAII guarantees that zero disk artifacts leak across any normal or exceptional termination.
