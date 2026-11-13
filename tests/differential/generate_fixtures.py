#!/usr/bin/env python3
import os
import csv

def generate_fixtures(out_dir="tests/fixtures"):
    os.makedirs(out_dir, exist_ok=True)

    # 1. items.csv
    items_path = os.path.join(out_dir, "items.csv")
    with open(items_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "category", "price", "stock", "in_stock"])
        rows = [
            [1, "electronics", 299.99, 15, "true"],
            [2, "electronics", 49.50, 100, "true"],
            [3, "electronics", 1200.00, 5, "true"],
            [4, "books", 19.99, 50, "true"],
            [5, "books", 9.99, 0, "false"],
            [6, "books", 29.95, 25, "true"],
            [7, "clothing", 45.00, 30, "true"],
            [8, "clothing", 85.50, 12, "true"],
            [9, "clothing", 15.00, 0, "false"],
            [10, "home", 150.00, 8, "true"],
            [11, "home", 25.50, 40, "true"],
            [12, "home", 350.00, 3, "true"],
            [13, "", 99.00, 10, "true"],        # NULL category
            [14, "special", "", 5, "true"],       # NULL price
            [15, "special", -10.50, -2, "false"], # Negative price and stock
            [16, "", "", "", "false"],            # Multiple NULLs
        ]
        for r in rows:
            writer.writerow(r)

    # 2. sales.csv
    sales_path = os.path.join(out_dir, "sales.csv")
    with open(sales_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["sale_id", "item_id", "quantity", "discount", "region"])
        rows = [
            [101, 1, 2, 0.05, "North"],
            [102, 1, 1, 0.00, "South"], # Duplicate item_id 1
            [103, 1, 3, 0.10, "East"],  # Duplicate item_id 1
            [104, 2, 5, 0.00, "West"],
            [105, 2, 2, 0.15, "North"],
            [106, 4, 10, 0.20, "South"],
            [107, 4, 1, 0.00, "East"],
            [108, 5, 4, 0.00, "West"],
            [109, 7, 2, 0.10, "North"],
            [110, 7, 1, 0.00, "South"],
            [111, 10, 1, 0.00, "West"],
            [112, 10, 2, 0.05, "East"],
            [113, 999, 1, 0.00, "North"], # Non-existent item_id
            [114, "", 3, 0.00, "South"],   # NULL item_id
            [115, 13, 2, 0.00, "East"],
            [116, 14, 1, 0.10, "West"],
        ]
        for r in rows:
            writer.writerow(r)

    # 3. empty_table.csv
    empty_path = os.path.join(out_dir, "empty_table.csv")
    with open(empty_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "val", "tag"])

    # 4. grades.csv
    grades_path = os.path.join(out_dir, "grades.csv")
    with open(grades_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["student_id", "course", "score"])
        rows = [
            [1, "Math", 95.0],
            [1, "Physics", 88.5],
            [1, "Chem", 91.0],
            [2, "Math", 78.0],
            [2, "Physics", 82.0],
            [2, "Chem", ""],     # NULL score
            [3, "Math", 95.0],   # Duplicate score
            [3, "Physics", 90.0],
            [3, "Chem", 85.0],
            [4, "Math", 60.0],
            [4, "Physics", ""],
            [4, "Chem", 70.0],
            [5, "Math", ""],
            [5, "Physics", ""],
            [5, "Chem", ""]      # All NULL scores
        ]
        for r in rows:
            writer.writerow(r)

    print(f"Fixtures generated successfully in {out_dir}")

if __name__ == "__main__":
    generate_fixtures()
