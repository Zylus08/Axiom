#!/usr/bin/env python3
"""
High-throughput streaming multi-scale data generator for Project Axiom Milestone M2.
Generates lineitem, orders, and customer tables in 100MB, 1GB, and 5GB scale tiers.
"""
import os
import sys
import time
import argparse

def generate_tier(scale_name, target_bytes, out_dir="benchmarks/data"):
    os.makedirs(out_dir, exist_ok=True)
    orders_file = os.path.join(out_dir, f"orders_{scale_name}.csv")
    items_file = os.path.join(out_dir, f"items_{scale_name}.csv")

    categories = ["Electronics", "Appliances", "Books", "Clothing", "Home", "Sports", "Toys", "Automotive"]
    statuses = ["COMPLETED", "PENDING", "SHIPPED", "CANCELLED"]

    # Number of items scales with tier
    if scale_name == "100mb":
        num_items = 50_000
    elif scale_name == "1gb":
        num_items = 200_000
    elif scale_name == "5gb":
        num_items = 500_000
    else:
        num_items = 20_000

    print(f"[{scale_name.upper()}] Generating {items_file} ({num_items} items)...")
    t0 = time.time()
    with open(items_file, "w", newline="", encoding="utf-8") as f:
        f.write("item_id,category,base_price,supplier_id\n")
        # Deterministic generation
        for i in range(1, num_items + 1):
            cat = categories[i % len(categories)]
            price = f"{(5.0 + (i * 37 % 149500) / 100.0):.2f}"
            supp = (i * 17) % 1000 + 1
            f.write(f"{i},{cat},{price},{supp}\n")

    print(f"[{scale_name.upper()}] Generating {orders_file} targeting {target_bytes / (1024*1024):.0f} MB...")
    # Buffer writes in 1MB chunks for high I/O throughput
    chunk_buffer = []
    chunk_size = 0
    total_bytes = 0
    order_id = 1

    with open(orders_file, "w", newline="", encoding="utf-8") as f:
        f.write("order_id,item_id,quantity,discount,status\n")
        total_bytes += len("order_id,item_id,quantity,discount,status\n")

        while total_bytes < target_bytes:
            item_id = (order_id * 31) % num_items + 1
            qty = (order_id % 20) + 1
            disc = [0.0, 0.05, 0.10, 0.15, 0.20][order_id % 5]
            status = statuses[order_id % len(statuses)]
            line = f"{order_id},{item_id},{qty},{disc:.2f},{status}\n"
            chunk_buffer.append(line)
            chunk_size += len(line)
            total_bytes += len(line)
            order_id += 1

            if chunk_size >= 1024 * 1024:
                f.write("".join(chunk_buffer))
                chunk_buffer.clear()
                chunk_size = 0

        if chunk_buffer:
            f.write("".join(chunk_buffer))
            chunk_buffer.clear()

    dur = time.time() - t0
    final_mb = os.path.getsize(orders_file) / (1024 * 1024)
    print(f"[{scale_name.upper()}] Done! Orders: {order_id - 1:,} rows, {final_mb:.2f} MB generated in {dur:.2f}s ({final_mb / dur:.1f} MB/s)")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier", choices=["100mb", "1gb", "5gb", "all"], default="100mb")
    parser.add_argument("--out", default="benchmarks/data")
    args = parser.parse_args()

    targets = {
        "100mb": 100 * 1024 * 1024,
        "1gb": 1024 * 1024 * 1024,
        "5gb": 5 * 1024 * 1024 * 1024
    }

    if args.tier == "all":
        for tier, target in targets.items():
            generate_tier(tier, target, args.out)
    else:
        generate_tier(args.tier, targets[args.tier], args.out)

if __name__ == "__main__":
    main()

