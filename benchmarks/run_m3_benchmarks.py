#!/usr/bin/env python3
"""
Project Axiom Milestone M3 - Forensic Verification, Performance Hardening & Comparative Benchmark Suite.
Measures execution time, memory usage (Working Set and Private Commit), disk spill metrics,
and compares against DuckDB 2.0.0 and SQLite.
Outputs reproducible results to console and audit/m3/BENCHMARK_RAW_RESULTS.csv.
"""
import os
import sys
import time
import subprocess
import csv
import json
import duckdb
import sqlite3

AXIOM_BIN = "build/axiom.exe"
DATA_DIR = "benchmarks/data"
OUTPUT_CSV = "audit/m3/BENCHMARK_RAW_RESULTS.csv"

WORKLOADS = [
    {
        "id": "W1_AGG_FILTER",
        "name": "Filtered Aggregation (Scalar & Group)",
        "sql": "SELECT status, COUNT(*), SUM(quantity), AVG(discount) FROM orders WHERE quantity > 5 GROUP BY status",
        "duck_sql": "SELECT status, COUNT(*), SUM(quantity), AVG(discount) FROM orders WHERE quantity > 5 GROUP BY status",
        "sqlite_sql": "SELECT status, COUNT(*), SUM(quantity), AVG(discount) FROM orders WHERE quantity > 5 GROUP BY status"
    },
    {
        "id": "W2_HIGH_CARD_AGG",
        "name": "High-Cardinality Group By (Disk Spilling Agg)",
        "sql": "SELECT item_id, COUNT(*), SUM(quantity) FROM orders GROUP BY item_id ORDER BY COUNT(*) DESC, item_id ASC LIMIT 20",
        "duck_sql": "SELECT item_id, COUNT(*), SUM(quantity) FROM orders GROUP BY item_id ORDER BY COUNT(*) DESC, item_id ASC LIMIT 20",
        "sqlite_sql": "SELECT item_id, COUNT(*), SUM(quantity) FROM orders GROUP BY item_id ORDER BY COUNT(*) DESC, item_id ASC LIMIT 20"
    },
    {
        "id": "W3_EQUI_JOIN",
        "name": "Equi-Join with Dimension Table (Grace Hash Join)",
        "sql": "SELECT o.status, i.category, COUNT(*), SUM(o.quantity) FROM orders o JOIN items i ON o.item_id = i.item_id GROUP BY o.status, i.category",
        "duck_sql": "SELECT o.status, i.category, COUNT(*), SUM(o.quantity) FROM orders o JOIN items i ON o.item_id = i.item_id GROUP BY o.status, i.category",
        "sqlite_sql": "SELECT o.status, i.category, COUNT(*), SUM(o.quantity) FROM orders o JOIN items i ON o.item_id = i.item_id GROUP BY o.status, i.category"
    },
    {
        "id": "W4_EXT_SORT",
        "name": "External Merge Sort (K-Way Spill Sort)",
        "sql": "SELECT order_id, quantity, discount FROM orders ORDER BY quantity DESC, discount DESC LIMIT 60000",
        "duck_sql": "SELECT order_id, quantity, discount FROM orders ORDER BY quantity DESC, discount DESC LIMIT 60000",
        "sqlite_sql": "SELECT order_id, quantity, discount FROM orders ORDER BY quantity DESC, discount DESC LIMIT 60000"
    }
]

def run_axiom(sql, orders_csv, items_csv, memory_limit="32MB"):
    cmd = [
        AXIOM_BIN,
        "-t", f"orders={orders_csv}",
        "-t", f"items={items_csv}",
        "-e", sql,
        "--memory-limit", memory_limit,
        "--profile"
    ]
    t0 = time.perf_counter()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t1 = time.perf_counter()

    if res.returncode != 0:
        return {"error": res.stderr.strip(), "time_ms": None}

    dur_ms = (t1 - t0) * 1000.0

    # Parse profile metrics from output
    spilled_bytes = 0
    spill_count = 0
    peak_rss = 0
    peak_commit = 0
    peak_accounted = 0

    for line in res.stdout.splitlines():
        if "Total Disk Spilled:" in line:
            parts = line.split("(")
            if len(parts) > 1:
                spilled_bytes = int(parts[1].split()[0])
        elif "Total Spill Partitions" in line:
            spill_count = int(line.split(":")[1].strip())
        elif "Peak Process Working Set:" in line:
            parts = line.split("(")
            if len(parts) > 1:
                peak_rss = int(parts[1].split()[0])
        elif "Peak Private Commit:" in line:
            parts = line.split("(")
            if len(parts) > 1:
                peak_commit = int(parts[1].split()[0])
        elif "Peak Accounted Memory:" in line:
            parts = line.split("(")
            if len(parts) > 1:
                peak_accounted = int(parts[1].split()[0])

    return {
        "engine": "Axiom",
        "time_ms": dur_ms,
        "peak_rss_mb": peak_rss / (1024 * 1024),
        "peak_commit_mb": peak_commit / (1024 * 1024),
        "peak_accounted_mb": peak_accounted / (1024 * 1024),
        "spilled_mb": spilled_bytes / (1024 * 1024),
        "spill_count": spill_count,
        "error": None
    }

def run_duckdb(sql, orders_csv, items_csv):
    con = duckdb.connect(":memory:")
    con.execute(f"CREATE TABLE orders AS SELECT * FROM read_csv_auto('{orders_csv}')")
    con.execute(f"CREATE TABLE items AS SELECT * FROM read_csv_auto('{items_csv}')")

    t0 = time.perf_counter()
    con.execute(sql).fetchall()
    t1 = time.perf_counter()
    con.close()

    return {
        "engine": "DuckDB",
        "time_ms": (t1 - t0) * 1000.0,
        "error": None
    }

def run_sqlite_100mb(orders_csv, items_csv):
    # Setup in-memory SQLite table once
    con = sqlite3.connect(":memory:")
    cur = con.cursor()
    cur.execute("CREATE TABLE orders (order_id INTEGER, item_id INTEGER, quantity INTEGER, price REAL, discount REAL, status TEXT)")
    cur.execute("CREATE TABLE items (item_id INTEGER, name TEXT, category TEXT, price REAL)")

    with open(orders_csv, "r", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader)
        cur.executemany("INSERT INTO orders VALUES (?,?,?,?,?,?)", reader)

    with open(items_csv, "r", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader)
        cur.executemany("INSERT INTO items VALUES (?,?,?,?)", reader)
    con.commit()
    return con

def benchmark_scale(scale_name, mem_limit="32MB", sqlite_con=None):
    orders_csv = os.path.join(DATA_DIR, f"orders_{scale_name}.csv").replace("\\", "/")
    items_csv = os.path.join(DATA_DIR, f"items_{scale_name}.csv").replace("\\", "/")

    if not os.path.exists(orders_csv):
        print(f"Skipping {scale_name}: {orders_csv} does not exist.")
        return []

    size_mb = os.path.getsize(orders_csv) / (1024 * 1024)
    print(f"\n========================================================")
    print(f"BENCHMARKING SCALE: {scale_name.upper()} ({size_mb:.1f} MB, Memory Limit: {mem_limit})")
    print(f"========================================================")

    results = []

    for w in WORKLOADS:
        print(f"\n--- Workload: {w['id']} - {w['name']} ---")
        print(f"SQL: {w['sql']}")

        # 1. Run Axiom
        ax_res = run_axiom(w['sql'], orders_csv, items_csv, memory_limit=mem_limit)
        if ax_res['error']:
            print(f"Axiom ERROR: {ax_res['error']}")
        else:
            print(f"Axiom:  {ax_res['time_ms']:.2f} ms | Working Set: {ax_res['peak_rss_mb']:.1f} MB | Private Commit: {ax_res['peak_commit_mb']:.1f} MB | Spilled: {ax_res['spilled_mb']:.2f} MB ({ax_res['spill_count']} parts)")

        # 2. Run DuckDB (for scales <= 1GB)
        duck_time = 0.0
        if scale_name in ["100mb", "1gb"]:
            try:
                duck_res = run_duckdb(w['duck_sql'], orders_csv, items_csv)
                duck_time = duck_res['time_ms']
                print(f"DuckDB: {duck_time:.2f} ms")
            except Exception as e:
                print(f"DuckDB Error: {e}")

        # 3. Run SQLite (for 100mb only)
        sqlite_time = 0.0
        if sqlite_con and scale_name == "100mb":
            try:
                t0 = time.perf_counter()
                sqlite_con.cursor().execute(w['sqlite_sql']).fetchall()
                t1 = time.perf_counter()
                sqlite_time = (t1 - t0) * 1000.0
                print(f"SQLite: {sqlite_time:.2f} ms")
            except Exception as e:
                print(f"SQLite Error: {e}")

        speedup_vs_duck = (duck_time / ax_res['time_ms']) if (ax_res['time_ms'] and duck_time > 0) else 0.0

        results.append({
            "scale": scale_name,
            "workload_id": w['id'],
            "workload_name": w['name'],
            "dataset_mb": f"{size_mb:.1f}",
            "memory_limit": mem_limit,
            "axiom_time_ms": f"{ax_res['time_ms']:.2f}" if ax_res['time_ms'] else "FAIL",
            "axiom_peak_rss_mb": f"{ax_res['peak_rss_mb']:.1f}" if ax_res['time_ms'] else "N/A",
            "axiom_peak_commit_mb": f"{ax_res['peak_commit_mb']:.1f}" if ax_res['time_ms'] else "N/A",
            "axiom_peak_accounted_mb": f"{ax_res['peak_accounted_mb']:.1f}" if ax_res['time_ms'] else "N/A",
            "axiom_spilled_mb": f"{ax_res['spilled_mb']:.2f}" if ax_res['time_ms'] else "N/A",
            "axiom_spill_count": ax_res['spill_count'] if ax_res['time_ms'] else "N/A",
            "duckdb_time_ms": f"{duck_time:.2f}" if duck_time > 0 else "N/A",
            "sqlite_time_ms": f"{sqlite_time:.2f}" if sqlite_time > 0 else "N/A",
            "axiom_vs_duckdb_ratio": f"{ax_res['time_ms'] / duck_time:.2f}x" if (ax_res['time_ms'] and duck_time > 0) else "N/A"
        })

    return results

def main():
    os.makedirs(os.path.dirname(OUTPUT_CSV), exist_ok=True)
    all_results = []

    print("Pre-loading SQLite for 100MB comparison...")
    sqlite_con = None
    try:
        sqlite_con = run_sqlite_100mb(
            os.path.join(DATA_DIR, "orders_100mb.csv"),
            os.path.join(DATA_DIR, "items_100mb.csv")
        )
        print("SQLite 100MB pre-loaded successfully.")
    except Exception as e:
        print(f"Warning: Could not pre-load SQLite: {e}")

    # 1. Run 100MB benchmarks under 16MB budget (forces external operators on 100MB)
    all_results.extend(benchmark_scale("100mb", mem_limit="16MB", sqlite_con=sqlite_con))

    # 2. Run 1GB benchmarks under 32MB budget (forces external operators on 1GB)
    all_results.extend(benchmark_scale("1gb", mem_limit="32MB"))

    if sqlite_con:
        sqlite_con.close()

    # 3. Workloads on 5GB (W1 and W2)
    if os.path.exists(os.path.join(DATA_DIR, "orders_5gb.csv")):
        all_results.extend(benchmark_scale("5gb", mem_limit="64MB"))

    # Write results to CSV
    keys = [
        "scale", "workload_id", "workload_name", "dataset_mb", "memory_limit",
        "axiom_time_ms", "axiom_peak_rss_mb", "axiom_peak_commit_mb", "axiom_peak_accounted_mb",
        "axiom_spilled_mb", "axiom_spill_count", "duckdb_time_ms", "sqlite_time_ms", "axiom_vs_duckdb_ratio"
    ]
    with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(all_results)

    print(f"\nMilestone M3 Benchmark suite finished! Results written to {OUTPUT_CSV}")

if __name__ == "__main__":
    main()
