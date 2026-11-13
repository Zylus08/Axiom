#!/usr/bin/env python3
"""
Synthetic analytical benchmark data generator for Project Axiom.
Generates lineitem and orders datasets in CSV format.
"""
import os
import random
import csv
import argparse

def generate_benchmark_data(num_orders=100000, num_items=50000, out_dir="benchmarks/data"):
    os.makedirs(out_dir, exist_ok=True)
    random.seed(42)

    items_file = os.path.join(out_dir, f"items_{num_items}.csv")
    orders_file = os.path.join(out_dir, f"orders_{num_orders}.csv")

    categories = ["Electronics", "Appliances", "Books", "Clothing", "Home", "Sports", "Toys", "Automotive"]

    print(f"Generating {num_items} items into {items_file}...")
    with open(items_file, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["item_id", "category", "base_price", "supplier_id"])
        for i in range(1, num_items + 1):
            cat = random.choice(categories)
            price = round(random.uniform(5.0, 1500.0), 2)
            supp = random.randint(1, 500)
            writer.writerow([i, cat, price, supp])

    print(f"Generating {num_orders} orders into {orders_file}...")
    statuses = ["COMPLETED", "PENDING", "SHIPPED", "CANCELLED"]
    with open(orders_file, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["order_id", "item_id", "quantity", "discount", "status"])
        for i in range(1, num_orders + 1):
            item_id = random.randint(1, num_items)
            qty = random.randint(1, 20)
            disc = round(random.choice([0.0, 0.05, 0.10, 0.15, 0.20]), 2)
            status = random.choice(statuses)
            writer.writerow([i, item_id, qty, disc, status])

    print("Data generation complete.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--orders", type=int, default=100000)
    parser.add_argument("--items", type=int, default=20000)
    parser.add_argument("--out", type=str, default="benchmarks/data")
    args = parser.parse_args()
    generate_benchmark_data(args.orders, args.items, args.out)
