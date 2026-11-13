#!/usr/bin/env python3
"""
Analytical benchmark runner for Project Axiom.
Measures latency and throughput on realistic queries.
"""
import time
import subprocess
import json
import os
import sys

AXIOM_BIN = "build/axiom.exe"
DATA_DIR = "benchmarks/data"
ITEMS_CSV = os.path.join(DATA_DIR, "items_20000.csv")
ORDERS_CSV = os.path.join(DATA_DIR, "orders_100000.csv")

WORKLOADS = [
    {
        "name": "Q1: Table Scan with Filter",
        "sql": "SELECT order_id, quantity FROM orders WHERE quantity >= 15 AND status = 'COMPLETED'",
        "description": "High-selectivity scan and filter over 100k rows"
    },
    {
        "name": "Q2: Simple Aggregation",
        "sql": "SELECT COUNT(*) AS total_orders, SUM(quantity) AS total_units FROM orders",
        "description": "Scalar aggregation (COUNT + SUM) over 100k rows"
    },
    {
        "name": "Q3: Hash Group-By Aggregation",
        "sql": "SELECT status, COUNT(*) AS cnt, SUM(quantity) AS total_units FROM orders GROUP BY status",
        "description": "Low-cardinality hash aggregation over 100k rows"
    },
    {
        "name": "Q4: Hash Inner Join",
        "sql": "SELECT o.order_id, i.category, o.quantity FROM orders o INNER JOIN items i ON o.item_id = i.item_id WHERE o.quantity > 10",
        "description": "Hash inner join between 100k orders and 20k items"
    },
    {
        "name": "Q5: Join + Group-By + Aggregation",
        "sql": "SELECT i.category, COUNT(*) AS num_orders, SUM(o.quantity) AS total_qty FROM orders o JOIN items i ON o.item_id = i.item_id GROUP BY i.category",
        "description": "Analytical Join with Category Group-By Aggregation"
    },
    {
        "name": "Q6: Sort + Limit",
        "sql": "SELECT order_id, quantity FROM orders ORDER BY quantity DESC LIMIT 50",
        "description": "Order by and top-N limit"
    }
]

def run_benchmark():
    if not os.path.exists(ORDERS_CSV) or not os.path.exists(ITEMS_CSV):
        print("Benchmark data not found. Generating 100k dataset...")
        from benchmarks.generators.generate_benchmark_data import generate_benchmark_data
        generate_benchmark_data(num_orders=100000, num_items=20000, out_dir=DATA_DIR)

    table_flags = [
        "-t", f"items={ITEMS_CSV}:item_id:bigint,category:varchar,base_price:double,supplier_id:bigint",
        "-t", f"orders={ORDERS_CSV}:order_id:bigint,item_id:bigint,quantity:bigint,discount:double,status:varchar"
    ]

    print("="*75)
    print("PROJECT AXIOM M1 — ANALYTICAL WORKLOAD BENCHMARKS")
    print(f"Dataset: Orders (100,000 rows), Items (20,000 rows)")
    print("="*75)

    results = []

    for w in WORKLOADS:
        name = w["name"]
        sql = w["sql"]
        desc = w["description"]

        cmd = [AXIOM_BIN] + table_flags + ["-e", sql, "--format", "json"]

        # Warm-up run
        subprocess.run(cmd, capture_output=True, check=True)

        # Timed runs (3 iterations)
        times = []
        for _ in range(3):
            t0 = time.perf_counter()
            proc = subprocess.run(cmd, capture_output=True, text=True)
            t1 = time.perf_counter()
            if proc.returncode != 0:
                print(f"Error in {name}: {proc.stderr}")
                sys.exit(1)
            times.append((t1 - t0) * 1000.0)

        data = json.loads(proc.stdout)
        out_rows = len(data)
        min_ms = min(times)
        avg_ms = sum(times) / len(times)
        throughput_rows_sec = (100000.0 / (avg_ms / 1000.0))

        results.append({
            "name": name,
            "desc": desc,
            "min_ms": min_ms,
            "avg_ms": avg_ms,
            "throughput": throughput_rows_sec,
            "out_rows": out_rows
        })

        print(f"{name:35s} | Latency: {avg_ms:6.1f} ms | Throughput: {throughput_rows_sec:9,.0f} rows/s | Output: {out_rows} rows")

    print("="*75)
    return results

if __name__ == "__main__":
    run_benchmark()

