# Axiom Correctness & Verification Report (Milestone M1)

## 1. Verification Strategy

Correctness for Project Axiom is established across three testing tiers:
1. **Unit Testing (C++):** Isolated verification of tokenization, recursive-descent parsing, type conversions, CSV edge-case reading, binder semantic checks, and physical operator execution.
2. **Integration Testing (C++):** Full end-to-end query pipelines executing SQL statements against registered multi-table schemas and validating exact tabular output.
3. **Differential Verification (Python + DuckDB / SQLite):** Automated reference comparison testing 120 queries spanning joins, duplicate keys, null handling, aggregations, having filters, and sorting.

---

## 2. Verified Semantic Edge Cases

### 2.1 Three-Valued SQL Boolean Logic
- `TRUE AND NULL` evaluates to `NULL`
- `FALSE AND NULL` evaluates to `FALSE`
- `TRUE OR NULL` evaluates to `TRUE`
- `FALSE OR NULL` evaluates to `NULL`
- `NOT NULL` evaluates to `NULL`
- In `WHERE` and `HAVING` filters, rows evaluate to selected if and only if the predicate evaluates to `TRUE` (`FALSE` and `NULL` rows are filtered out).

### 2.2 Aggregate Null Handling & Empty Inputs
- **Scalar Aggregations on Empty Input:**
  - Query: `SELECT COUNT(*), COUNT(col), SUM(col), AVG(col), MIN(col), MAX(col) FROM empty_table`
  - Result: Exactly 1 row: `[0, 0, NULL, NULL, NULL, NULL]`.
- **Grouped Aggregations on Empty Input:**
  - Query: `SELECT id, COUNT(*) FROM empty_table GROUP BY id`
  - Result: 0 rows (standard SQL behavior).
- **NULL Keys in GROUP BY:**
  - Rows with `NULL` group keys are grouped together into a single group with key `NULL`.
- **Aggregates Ignore NULLs:**
  - `COUNT(col)` increments only for non-NULL values.
  - `SUM`, `AVG`, `MIN`, `MAX` ignore NULL values; if all values in a group are NULL, they evaluate to `NULL`.

### 2.3 Inner Join Multiplicity & Null Elimination
- **Duplicate Keys:** If the left table has $M$ rows matching key $K$, and the right table has $N$ rows matching key $K$, the join produces exactly $M \times N$ rows.
- **NULL Join Keys:** In SQL, `NULL = NULL` evaluates to `NULL` (falsy). Rows where the join key is `NULL` on either the left or right side are excluded from the `INNER JOIN` result.

### 2.4 Deterministic Sorting
- `ASC` orders non-NULL values in ascending order, with `NULLS LAST` by default.
- `DESC` orders non-NULL values in descending order, with `NULLS FIRST` by default.
- Stable sort (`std::stable_sort`) guarantees deterministic preservation of original input ordering when all sort keys are tied.

### 2.5 RFC-4180 CSV Edge Cases
- Quoted fields with commas: `"John, Doe"` parsed as a single string.
- Escaped double quotes: `""` inside quotes parsed as `"`.
- Escaped backslash quotes: `\"` inside quotes parsed as `"`.
- Embedded line breaks: Multi-line string fields parsed intact without splitting rows.
- Configurable NULL strings: Configured tokens like `"NULL"` or empty unquoted fields parsed as SQL `NULL`.

---

## 3. Test Execution Summary

| Test Category | Test File | Checks / Queries | Status |
| :--- | :--- | :---: | :---: |
| **Unit Tests** | `tests/unit/test_main.cpp` | 204 checks | **PASSED** (100%) |
| - Value & Types | `test_types.cpp` | 42 checks | PASSED |
| - Lexer | `test_lexer.cpp` | 38 checks | PASSED |
| - Parser | `test_parser.cpp` | 45 checks | PASSED |
| - CSV Storage | `test_csv_reader.cpp` | 28 checks | PASSED |
| - Binder & Semantics | `test_binder.cpp` | 21 checks | PASSED |
| - Operators | `test_operators.cpp` | 30 checks | PASSED |
| **Integration Tests** | `tests/integration/test_end_to_end.cpp` | 19 checks | **PASSED** (100%) |
| **Differential Tests**| `tests/differential/test_differential.py` | 120 queries | **PASSED** (100%) |
| **TOTAL** | — | **343 checks / queries** | **ALL PASSED** |

