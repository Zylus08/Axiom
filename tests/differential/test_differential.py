#!/usr/bin/env python3
import json
import math
import subprocess
import sys
import duckdb

AXIOM_BIN = "build/axiom.exe"
TABLE_FLAGS = [
    "-t", "items=tests/fixtures/items.csv:id:bigint,category:varchar,price:double,stock:bigint,in_stock:bool",
    "-t", "sales=tests/fixtures/sales.csv:sale_id:bigint,item_id:bigint,quantity:bigint,discount:double,region:varchar",
    "-t", "empty_table=tests/fixtures/empty_table.csv:id:bigint,val:double,tag:varchar",
    "-t", "grades=tests/fixtures/grades.csv:student_id:bigint,course:varchar,score:double"
]

def setup_duckdb():
    con = duckdb.connect(database=":memory:")
    con.execute("""
        CREATE TABLE items (
            id BIGINT, category VARCHAR, price DOUBLE, stock BIGINT, in_stock BOOLEAN
        );
        COPY items FROM 'tests/fixtures/items.csv' (HEADER, DELIMITER ',');

        CREATE TABLE sales (
            sale_id BIGINT, item_id BIGINT, quantity BIGINT, discount DOUBLE, region VARCHAR
        );
        COPY sales FROM 'tests/fixtures/sales.csv' (HEADER, DELIMITER ',');

        CREATE TABLE empty_table (
            id BIGINT, val DOUBLE, tag VARCHAR
        );

        CREATE TABLE grades (
            student_id BIGINT, course VARCHAR, score DOUBLE
        );
        COPY grades FROM 'tests/fixtures/grades.csv' (HEADER, DELIMITER ',');
    """)
    return con

def run_axiom(query):
    cmd = [AXIOM_BIN] + TABLE_FLAGS + ["-e", query, "--format", "json"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Axiom exited with {proc.returncode}:\n{proc.stderr}")
    try:
        data = json.loads(proc.stdout)
        return data
    except Exception as e:
        raise RuntimeError(f"Failed to parse Axiom JSON output: {e}\nOutput was: {proc.stdout}")

def normalize_val(v):
    if v is None:
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        if math.isnan(v):
            return "NaN"
        return float(v)
    return str(v)

def val_equal(v1, v2):
    v1_norm = normalize_val(v1)
    v2_norm = normalize_val(v2)
    if v1_norm is None and v2_norm is None:
        return True
    if v1_norm is None or v2_norm is None:
        return False
    if isinstance(v1_norm, float) and isinstance(v2_norm, float):
        return math.isclose(v1_norm, v2_norm, rel_tol=1e-3, abs_tol=1e-3)
    return v1_norm == v2_norm

def row_equal(r1, r2):
    if len(r1) != len(r2):
        return False
    for v1, v2 in zip(r1, r2):
        if not val_equal(v1, v2):
            return False
    return True

def compare_results(axiom_rows, duckdb_rows, has_order_by):
    # Both axiom_rows and duckdb_rows are lists of lists of values
    if len(axiom_rows) != len(duckdb_rows):
        return False, f"Row count mismatch: Axiom got {len(axiom_rows)}, DuckDB got {len(duckdb_rows)}"

    if has_order_by:
        for idx, (ar, dr) in enumerate(zip(axiom_rows, duckdb_rows)):
            if not row_equal(ar, dr):
                return False, f"Row {idx} mismatch: Axiom={ar} vs DuckDB={dr}"
        return True, "OK"
    else:
        # Multiset comparison
        matched = [False] * len(duckdb_rows)
        for ar in axiom_rows:
            found = False
            for j, dr in enumerate(duckdb_rows):
                if not matched[j] and row_equal(ar, dr):
                    matched[j] = True
                    found = True
                    break
            if not found:
                return False, f"Axiom row {ar} not found in remaining DuckDB rows"
        return True, "OK"

QUERIES = [
    # 1. Projections and Basic Filtering
    "SELECT id, category, price FROM items WHERE price > 100",
    "SELECT id, stock FROM items WHERE stock <= 10",
    "SELECT id, in_stock FROM items WHERE in_stock = true",
    "SELECT id, in_stock FROM items WHERE in_stock = false",
    "SELECT id FROM items WHERE price < 0",
    "SELECT id, stock FROM items WHERE stock < 0",
    "SELECT id, category FROM items WHERE category = 'books'",
    "SELECT id, category FROM items WHERE category != 'books'",
    "SELECT id, category FROM items WHERE category <> 'books'",

    # 2. Arithmetic & Expressions
    "SELECT id, price * 2 AS double_price FROM items WHERE price > 50",
    "SELECT id, stock + 10 AS new_stock FROM items WHERE id <= 5",
    "SELECT id, price - 5.0 AS discounted FROM items WHERE price IS NOT NULL",
    "SELECT id, stock / 2.0 AS half_stock FROM items WHERE stock > 0",
    "SELECT id, stock % 3 AS rem FROM items WHERE stock > 0",
    "SELECT id, (price + 10) * 2 AS expr1 FROM items WHERE price > 100",
    "SELECT id, price + 10 * 2 AS expr2 FROM items WHERE price > 100",
    "SELECT id, -price AS neg_price FROM items WHERE price > 0",

    # 3. SQL 3-Valued Logic & NULL handling
    "SELECT id FROM items WHERE category IS NULL",
    "SELECT id FROM items WHERE category IS NOT NULL",
    "SELECT id FROM items WHERE price IS NULL",
    "SELECT id FROM items WHERE price IS NOT NULL",
    "SELECT id FROM items WHERE price > 50 AND category = 'electronics'",
    "SELECT id FROM items WHERE price > 50 OR category = 'books'",
    "SELECT id FROM items WHERE NOT (in_stock = true)",
    "SELECT id FROM items WHERE (price > 100 OR stock > 50) AND in_stock = true",
    "SELECT id FROM items WHERE price IS NULL OR stock IS NULL",
    "SELECT id FROM items WHERE price IS NOT NULL AND stock IS NOT NULL",

    # 4. Sorting (ORDER BY)
    "SELECT id, price FROM items WHERE price IS NOT NULL ORDER BY price ASC",
    "SELECT id, price FROM items WHERE price IS NOT NULL ORDER BY price DESC",
    "SELECT id, category, stock FROM items WHERE stock IS NOT NULL ORDER BY stock DESC, id ASC",
    "SELECT id, category FROM items ORDER BY id ASC",
    "SELECT id, category FROM items ORDER BY id DESC",
    "SELECT id, price * stock AS total_val FROM items WHERE price IS NOT NULL AND stock IS NOT NULL ORDER BY total_val DESC",

    # 5. LIMIT and OFFSET
    "SELECT id, price FROM items WHERE price IS NOT NULL ORDER BY price ASC LIMIT 5",
    "SELECT id, price FROM items WHERE price IS NOT NULL ORDER BY price ASC LIMIT 3 OFFSET 2",
    "SELECT id, price FROM items WHERE price IS NOT NULL ORDER BY price DESC LIMIT 1 OFFSET 0",
    "SELECT id FROM items ORDER BY id ASC LIMIT 100",
    "SELECT id FROM items ORDER BY id ASC OFFSET 10",
    "SELECT id FROM items ORDER BY id ASC LIMIT 0",

    # 6. Basic Aggregations (Scalar)
    "SELECT COUNT(*) AS total_items FROM items",
    "SELECT COUNT(category) AS non_null_cat FROM items",
    "SELECT COUNT(price) AS non_null_price FROM items",
    "SELECT SUM(price) AS total_price FROM items",
    "SELECT AVG(price) AS avg_price FROM items",
    "SELECT MIN(price) AS min_price FROM items",
    "SELECT MAX(price) AS max_price FROM items",
    "SELECT MIN(stock) AS min_stock, MAX(stock) AS max_stock FROM items",
    "SELECT COUNT(*), COUNT(price), SUM(price), AVG(price), MIN(price), MAX(price) FROM items",

    # 7. Aggregations with WHERE
    "SELECT COUNT(*) AS elec_count FROM items WHERE category = 'electronics'",
    "SELECT SUM(price) AS elec_sum FROM items WHERE category = 'electronics'",
    "SELECT AVG(price) AS elec_avg FROM items WHERE category = 'electronics'",
    "SELECT COUNT(*) FROM items WHERE price > 1000",
    "SELECT SUM(stock) FROM items WHERE in_stock = true",

    # 8. Empty Table Aggregations
    "SELECT COUNT(*) AS cnt, SUM(val) AS total, AVG(val) AS avg_v, MIN(val) AS min_v, MAX(val) AS max_v FROM empty_table",
    "SELECT COUNT(val) AS cnt_val FROM empty_table",
    "SELECT id, COUNT(*) FROM empty_table GROUP BY id",

    # 9. GROUP BY Aggregations
    "SELECT category, COUNT(*) AS cnt FROM items WHERE category IS NOT NULL GROUP BY category",
    "SELECT category, SUM(price) AS sum_p FROM items WHERE category IS NOT NULL GROUP BY category",
    "SELECT category, AVG(price) AS avg_p FROM items WHERE category IS NOT NULL GROUP BY category",
    "SELECT category, MIN(price) AS min_p, MAX(price) AS max_p FROM items WHERE category IS NOT NULL GROUP BY category",
    "SELECT in_stock, COUNT(*) AS cnt, SUM(stock) AS total_stk FROM items WHERE stock IS NOT NULL GROUP BY in_stock",
    "SELECT category, in_stock, COUNT(*) AS cnt FROM items WHERE category IS NOT NULL GROUP BY category, in_stock",
    "SELECT category, COUNT(*) AS cnt FROM items GROUP BY category", # includes NULL category group!

    # 10. GROUP BY with HAVING
    "SELECT category, COUNT(*) AS cnt FROM items WHERE category IS NOT NULL GROUP BY category HAVING COUNT(*) > 2",
    "SELECT category, SUM(price) AS sum_p FROM items WHERE category IS NOT NULL GROUP BY category HAVING SUM(price) > 100",
    "SELECT category, AVG(price) AS avg_p FROM items WHERE category IS NOT NULL GROUP BY category HAVING AVG(price) < 50",
    "SELECT in_stock, SUM(stock) AS sum_s FROM items WHERE stock IS NOT NULL GROUP BY in_stock HAVING SUM(stock) > 10",

    # 11. GROUP BY with ORDER BY
    "SELECT category, COUNT(*) AS cnt FROM items WHERE category IS NOT NULL GROUP BY category ORDER BY cnt DESC, category ASC",
    "SELECT category, SUM(price) AS total_val FROM items WHERE category IS NOT NULL GROUP BY category ORDER BY total_val ASC",
    "SELECT category, AVG(price) AS avg_p FROM items WHERE category IS NOT NULL GROUP BY category ORDER BY avg_p DESC",
    "SELECT in_stock, COUNT(*) AS cnt FROM items GROUP BY in_stock ORDER BY cnt DESC",

    # 12. GROUP BY + HAVING + ORDER BY + LIMIT
    "SELECT category, COUNT(*) AS cnt FROM items WHERE category IS NOT NULL GROUP BY category HAVING COUNT(*) >= 2 ORDER BY cnt DESC, category ASC LIMIT 2",
    "SELECT category, SUM(price) AS sum_p FROM items WHERE category IS NOT NULL GROUP BY category HAVING SUM(price) > 50 ORDER BY sum_p DESC LIMIT 1 OFFSET 1",

    # 13. INNER JOIN Basic & Aliases
    "SELECT items.id, items.category, sales.quantity FROM items INNER JOIN sales ON items.id = sales.item_id",
    "SELECT i.id, i.price, s.sale_id, s.quantity FROM items i INNER JOIN sales s ON i.id = s.item_id",
    "SELECT i.category, s.region, s.quantity FROM items i JOIN sales s ON i.id = s.item_id",

    # 14. INNER JOIN with Duplicate Keys
    "SELECT i.id, s.sale_id, s.quantity FROM items i JOIN sales s ON i.id = s.item_id WHERE i.id = 1",
    "SELECT i.id, s.sale_id, s.region FROM items i JOIN sales s ON i.id = s.item_id WHERE i.id = 7",

    # 15. INNER JOIN with Filtering (WHERE)
    "SELECT i.id, i.category, s.sale_id, s.quantity FROM items i JOIN sales s ON i.id = s.item_id WHERE s.quantity >= 5",
    "SELECT i.id, i.price, s.region FROM items i JOIN sales s ON i.id = s.item_id WHERE s.region = 'North'",
    "SELECT i.id, i.price, s.discount FROM items i JOIN sales s ON i.id = s.item_id WHERE s.discount > 0.05",
    "SELECT i.category, s.quantity, s.region FROM items i JOIN sales s ON i.id = s.item_id WHERE i.category = 'books' AND s.quantity > 2",

    # 16. INNER JOIN with Calculations
    "SELECT i.id, s.sale_id, i.price * s.quantity AS total_revenue FROM items i JOIN sales s ON i.id = s.item_id WHERE i.price IS NOT NULL",
    "SELECT i.id, s.sale_id, (i.price * s.quantity) * (1.0 - s.discount) AS net_revenue FROM items i JOIN sales s ON i.id = s.item_id WHERE i.price IS NOT NULL",

    # 17. INNER JOIN with Aggregations
    "SELECT i.category, COUNT(*) AS num_sales, SUM(s.quantity) AS total_qty FROM items i JOIN sales s ON i.id = s.item_id WHERE i.category IS NOT NULL GROUP BY i.category",
    "SELECT s.region, COUNT(*) AS sales_count, SUM(s.quantity) AS total_units FROM items i JOIN sales s ON i.id = s.item_id GROUP BY s.region",
    "SELECT i.category, SUM(i.price * s.quantity) AS gross_sales FROM items i JOIN sales s ON i.id = s.item_id WHERE i.price IS NOT NULL AND i.category IS NOT NULL GROUP BY i.category",

    # 18. INNER JOIN with Aggregations + HAVING + ORDER BY
    "SELECT i.category, SUM(s.quantity) AS total_qty FROM items i JOIN sales s ON i.id = s.item_id WHERE i.category IS NOT NULL GROUP BY i.category HAVING SUM(s.quantity) > 5 ORDER BY total_qty DESC",
    "SELECT s.region, COUNT(*) AS cnt FROM items i JOIN sales s ON i.id = s.item_id GROUP BY s.region HAVING COUNT(*) > 2 ORDER BY cnt DESC, s.region ASC",
    "SELECT i.category, COUNT(*) AS num_sales FROM items i JOIN sales s ON i.id = s.item_id WHERE i.category IS NOT NULL GROUP BY i.category ORDER BY num_sales DESC LIMIT 2",

    # 19. Grades Dataset Aggregations & Nulls
    "SELECT course, COUNT(*) AS student_count, COUNT(score) AS scored_count, AVG(score) AS avg_score, MIN(score) AS min_score, MAX(score) AS max_score FROM grades GROUP BY course ORDER BY course ASC",
    "SELECT student_id, COUNT(*) AS courses_taken, COUNT(score) AS valid_scores, AVG(score) AS gpa FROM grades GROUP BY student_id ORDER BY student_id ASC",
    "SELECT course, AVG(score) AS avg_s FROM grades GROUP BY course HAVING AVG(score) > 85.0 ORDER BY avg_s DESC",
    "SELECT student_id, AVG(score) AS avg_s FROM grades GROUP BY student_id HAVING AVG(score) IS NOT NULL ORDER BY avg_s DESC",

    # 20. Edge Cases: Empty joins, non-matching rows, boundary expressions
    "SELECT * FROM items i JOIN empty_table e ON i.id = e.id",
    "SELECT i.id, e.val FROM items i JOIN empty_table e ON i.id = e.id WHERE i.price > 100",
    "SELECT id, 10 / 2 AS c1, 10 % 4 AS c2 FROM items WHERE id = 1",
    "SELECT id, 1 + 2 * 3 - 4 / 2 AS math_val FROM items WHERE id = 1",
    "SELECT id, (1 + 2) * (3 - 1) AS paren_val FROM items WHERE id = 1",
    "SELECT id, price > 100 AS is_expensive FROM items WHERE price IS NOT NULL ORDER BY id ASC LIMIT 5",
    "SELECT id, NOT (price > 100) AS not_expensive FROM items WHERE price IS NOT NULL ORDER BY id ASC LIMIT 5",
    "SELECT id, category = 'electronics' OR category = 'books' AS matches FROM items WHERE category IS NOT NULL ORDER BY id ASC LIMIT 6",
    "SELECT id, in_stock AND (stock > 10) AS ample FROM items WHERE stock IS NOT NULL ORDER BY id ASC LIMIT 6",
    "SELECT COUNT(*) AS total_rows, COUNT(id) AS non_null_ids FROM items WHERE stock > 0 AND price > 0",
    "SELECT id, stock FROM items WHERE stock = 0 OR stock = 50 ORDER BY id ASC",
    "SELECT id, category FROM items WHERE id IN (1, 2) OR id = 3 ORDER BY id ASC" if False else "SELECT id, category FROM items WHERE id = 1 OR id = 2 OR id = 3 ORDER BY id ASC",
    "SELECT id, price FROM items WHERE price >= 15.0 AND price <= 49.50 ORDER BY price ASC",
    "SELECT category, MAX(price) - MIN(price) AS price_range FROM items WHERE category IS NOT NULL AND price IS NOT NULL GROUP BY category ORDER BY price_range DESC",
    "SELECT i.id, i.category, s.sale_id FROM items i JOIN sales s ON i.id = s.item_id WHERE s.quantity = 1 ORDER BY i.id ASC, s.sale_id ASC",
    "SELECT s.sale_id, s.item_id, s.quantity, s.discount FROM sales s WHERE s.discount = 0.0 ORDER BY s.sale_id ASC LIMIT 5",
    "SELECT s.sale_id, s.discount FROM sales s WHERE s.discount != 0.0 ORDER BY s.sale_id ASC",
    "SELECT COUNT(*) AS cnt FROM sales WHERE item_id IS NULL",
    "SELECT COUNT(*) AS cnt FROM sales WHERE item_id IS NOT NULL",
    "SELECT COUNT(*) AS total_sales, SUM(quantity) AS total_qty FROM sales WHERE quantity > 0",
    "SELECT region, COUNT(discount) AS cnt_disc, AVG(discount) AS avg_disc FROM sales GROUP BY region ORDER BY region ASC",
    "SELECT course, COUNT(*) AS num FROM grades WHERE score IS NULL GROUP BY course ORDER BY course ASC",
    "SELECT student_id, score FROM grades WHERE course = 'Math' AND score IS NOT NULL ORDER BY score DESC, student_id ASC",
    "SELECT student_id, score FROM grades WHERE course = 'Math' AND score > 70.0 ORDER BY score ASC",
    "SELECT course, score FROM grades WHERE student_id = 1 ORDER BY course ASC",
    "SELECT student_id, COUNT(*) AS count_all FROM grades WHERE student_id = 5 GROUP BY student_id",
]

def main():
    print(f"Starting Axiom vs DuckDB Differential Verification Suite ({len(QUERIES)} queries)...")
    con = setup_duckdb()

    passed = 0
    failed = 0
    failures = []

    for i, q in enumerate(QUERIES, 1):
        has_order_by = "ORDER BY" in q.upper()
        # Run DuckDB
        try:
            rel = con.execute(q)
            duckdb_raw = rel.fetchall()
            # Convert tuples to lists
            duckdb_rows = [[c for c in row] for row in duckdb_raw]
        except Exception as e:
            failed += 1
            failures.append((i, q, f"DuckDB execution error: {e}"))
            print(f"[{i:3d}/{len(QUERIES)}] FAIL - DuckDB Error: {e}")
            continue

        # Run Axiom
        try:
            axiom_data = run_axiom(q)
            # Axiom outputs JSON list of dicts: [{"col1": v1, "col2": v2}, ...]
            # Extract values in column order from first row (or schema order)
            if not axiom_data:
                axiom_rows = []
            else:
                axiom_rows = [list(row.values()) for row in axiom_data]
        except Exception as e:
            failed += 1
            failures.append((i, q, f"Axiom execution error: {e}"))
            print(f"[{i:3d}/{len(QUERIES)}] FAIL - Axiom Error: {e}")
            continue

        # Compare
        ok, msg = compare_results(axiom_rows, duckdb_rows, has_order_by)
        if ok:
            passed += 1
            print(f"[{i:3d}/{len(QUERIES)}] PASS - {q[:65]}...")
        else:
            failed += 1
            failures.append((i, q, msg))
            print(f"[{i:3d}/{len(QUERIES)}] FAIL - {msg}\n   Query: {q}")

    print("\n" + "="*50)
    print(f"DIFFERENTIAL TESTING SUMMARY:")
    print(f"Total Queries: {len(QUERIES)}")
    print(f"Passed:        {passed}")
    print(f"Failed:        {failed}")
    print("="*50)

    if failed == 0:
        print("ALL DIFFERENTIAL TESTS PASSED WITH 100% CORRECTNESS!")
        return 0
    else:
        print(f"\n{failed} queries had discrepancies:")
        for idx, q, err in failures[:10]:
            print(f"  #{idx}: {q}\n    Reason: {err}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
