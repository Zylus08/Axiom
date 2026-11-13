# Project AXIOM — Milestone M2: Differential Verification Results

**Milestone:** M2 (Differential Verification against DuckDB 2.0.0)  
**Date:** 2026-11-14  
**Harness:** `tests/differential/test_differential.py`  
**Reference Engine:** DuckDB 2.0.0 (x86_64 Windows Python binding)  

---

## 1. Summary of Results

| Query Category | Total Queries | Passed | Failed | Pass Rate |
|:---|:---:|:---:|:---:|:---:|
| **Basic Filters & Comparisons** | 12 | 12 | 0 | **100.0%** |
| **Arithmetic & Expressions** | 15 | 15 | 0 | **100.0%** |
| **Three-Valued Logic (IS NULL, AND/OR/NOT)** | 10 | 10 | 0 | **100.0%** |
| **Sorting (ORDER BY ASC/DESC, NULLS FIRST/LAST)** | 9 | 9 | 0 | **100.0%** |
| **LIMIT & OFFSET** | 3 | 3 | 0 | **100.0%** |
| **Aggregates (COUNT, SUM, AVG, MIN, MAX)** | 15 | 15 | 0 | **100.0%** |
| **Group By & Having** | 17 | 17 | 0 | **100.0%** |
| **Inner Equijoins & Multiplicity** | 17 | 17 | 0 | **100.0%** |
| **Empty Input Semantics** | 6 | 6 | 0 | **100.0%** |
| **Edge Cases & Complex Expressions** | 16 | 16 | 0 | **100.0%** |
| **TOTAL** | **120** | **120** | **0** | **100.0%** |

---

## 2. Key Semantic Invariants Verified

1. **Three-Valued Logic (3VL):**
   - `NULL AND false` evaluates to `false`.
   - `NULL OR true` evaluates to `true`.
   - `WHERE NULL` filters out the row without runtime error.
2. **Aggregate Handling of NULL:**
   - `COUNT(*)` counts all rows including NULLs.
   - `COUNT(col)` ignores NULL values.
   - `SUM(col)`, `AVG(col)`, `MIN(col)`, `MAX(col)` ignore NULL values. If all inputs are NULL, return NULL.
3. **Empty Input Semantics:**
   - Scalar aggregate over empty table produces 1 row: `COUNT(*) = 0`, `SUM = NULL`.
   - Grouped aggregate (`GROUP BY`) over empty table produces 0 rows.
4. **Deterministic ORDER BY & Ties:**
   - `NULLS FIRST` vs `NULLS LAST` follows documented SQL standard rules.
   - Secondary unique tie-breaker columns produce identical row sequences across DuckDB and Axiom.
5. **SQL-92 Integer Division:**
   - Standard SQL-92 integer division truncates: `10 / 4 = 2`. Floating division: `10 / 4.0 = 2.5`. Both engines produce identical results.

