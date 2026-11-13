# Milestone M1 Audit & Acceptance Verification Report

## 1. Executive Summary

Milestone M1 of **Project Axiom** — building a functioning analytical SQL engine from scratch in C++20 — is **COMPLETE**.

The engine successfully compiles under modern C++20 on Windows (MSVC 2022 BuildTools), loads external CSV files, parses supported SQL syntax with a Pratt expression parser, performs semantic binding, builds logical and physical query plans, executes batch-oriented operators (scans, filters, hash joins, hash aggregations, stable sorts, and limits), and outputs formatted query results.

---

## 2. Acceptance Gate Verification Matrix

| # | Acceptance Criterion | Status | Verification Evidence |
| :-: | :--- | :---: | :--- |
| **1** | Engine compiles successfully | **PASS** | Builds cleanly in both `Release` and `Debug` on MSVC 19.43 with `/W4 /permissive- /Z7` |
| **2** | SQL queries execute through real pipeline | **PASS** | Validated end-to-end execution through Lexer -> Parser -> Binder -> Logical Planner -> Physical Planner -> Execution Engine |
| **3** | INNER JOIN with duplicate keys | **PASS** | Verified correct multiplicity ($M \times N$ rows) in `test_operators.cpp` and differential tests #77, #78, #85 |
| **4** | GROUP BY and all specified aggregates | **PASS** | Verified `COUNT(*)`, `COUNT(col)`, `SUM`, `AVG`, `MIN`, `MAX` with single and composite keys |
| **5** | NULL behavior validated | **PASS** | 3-valued boolean logic, NULL key grouping, NULL aggregate skipping, empty table scalar aggregates verified |
| **6** | CSV loading handles edge cases | **PASS** | Escaped quotes `""` and `\"`, commas inside quotes, embedded newlines, chunked reading verified in `test_csv_reader.cpp` |
| **7** | CLI runs working examples | **PASS** | `axiom.exe` runs table, csv, json formats, file execution, and interactive shell |
| **8** | Automated tests pass | **PASS** | 204 unit checks and 19 integration checks passed (100% pass rate) |
| **9** | Differential verification passes | **PASS** | 120 differential test queries compared against DuckDB and SQLite passed with 100% accuracy |
| **10**| Documentation accurately reflects implementation | **PASS** | Comprehensive docs created in `docs/` and `audit/` |

**Final Milestone Decision:** **`PASS`**

---

## 3. Test Statistics & Verified Configurations

### 3.1 Build Configurations
- **Release:** MSVC 19.43 (`/O2 /Ob2 /NDEBUG /W4 /permissive- /utf-8 /Z7 /std:c++20`), Ninja 1.12.1.
- **Debug:** MSVC 19.43 (`/Od /RTC1 /W4 /permissive- /utf-8 /Z7 /std:c++20`), Ninja 1.12.1.

### 3.2 Test Counts
- **Unit Tests:** 204 checks passed, 0 failed.
- **Integration Tests:** 19 checks passed, 0 failed.
- **Differential Verification:** 120 queries executed against DuckDB / SQLite, 120 passed (100% agreement).
- **Total Verification Assertions:** **343 checks / queries**.

---

## 4. Defect Log & Resolutions

1. **Defect:** Parser semicolon token was counted as trailing token in early lexer tests.
   - *Resolution:* Added `match(TokenType::Semicolon)` in parser and updated token expectations in unit tests.
2. **Defect:** Windows text mode translated `\n` to `\r\n` in CSV test fixture creation.
   - *Resolution:* Switched fixture file generation to binary mode (`std::ios::binary`).
3. **Defect:** Binder alias lookup only supported column references, failing when `ORDER BY` referenced aggregate aliases (e.g. `COUNT(*) AS cnt ... ORDER BY cnt`).
   - *Resolution:* Updated `Binder::bind_column_ref` to clone any bound expression matching a select alias, which the logical planner then rewrites to the aggregate output column.
4. **Defect:** MSVC parallel build resource contention when compiling 16 jobs concurrently with `/FS`.
   - *Resolution:* Switched debug format to `/Z7` and limited Ninja concurrency with `-j 4`.

