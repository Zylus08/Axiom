#!/usr/bin/env python3
"""
Adversarial & Resource Robustness Test Suite for Project Axiom Milestone M2.
Tests extreme memory pressure, quota exhaustion, unwritable directories,
early termination cleanup, and verifies zero leaked spill files.
"""
import os
import sys
import subprocess
import glob
import tempfile

AXIOM_BIN = "build/axiom.exe"
FIXTURES_DIR = "tests/fixtures"

def test_unwritable_temp_dir():
    print("[ADVERSARIAL 1] Unwritable temp dir handling...")
    # On Windows, a non-existent or invalid drive path simulates unwritable dir
    invalid_dir = "Z:/non_existent_drive/invalid_temp"
    cmd = [
        AXIOM_BIN,
        "-t", f"items={FIXTURES_DIR}/items.csv",
        "-e", "SELECT category, COUNT(*) FROM items GROUP BY category",
        "--memory-limit", "1KB",
        "--temp-dir", invalid_dir
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0 and "Storage Error" in res.stderr:
        print("  PASS: Engine cleanly rejected unwritable temp directory with Storage Error.")
        return True
    else:
        print(f"  FAIL: Expected Storage Error, got exit={res.returncode}, stderr={res.stderr}")
        return False

def test_exceeded_disk_quota():
    print("[ADVERSARIAL 2] Exceeded max disk spill quota...")
    temp_dir = tempfile.mkdtemp(prefix="axiom_quota_test_")
    cmd = [
        AXIOM_BIN,
        "-t", f"items={FIXTURES_DIR}/items.csv",
        "-e", "SELECT id, category, price FROM items ORDER BY price DESC",
        "--batch-size", "2",
        "--memory-limit", "1KB",
        "--max-temp-disk", "100B", # Only 100 bytes allowed
        "--temp-dir", temp_dir
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    # Check for leaked files in temp_dir
    leaked = glob.glob(os.path.join(temp_dir, "*.axmb"))
    try:
        os.rmdir(temp_dir)
    except Exception:
        pass

    if res.returncode != 0 and "quota exceeded" in res.stderr.lower():
        if len(leaked) == 0:
            print("  PASS: Engine cleanly threw quota exceeded error and cleaned up all temporary files.")
            return True
        else:
            print(f"  FAIL: Leaked {len(leaked)} spill files after quota error!")
            return False
    else:
        print(f"  FAIL: Expected quota error, got exit={res.returncode}, stderr={res.stderr}")
        return False

def test_tiny_memory_budget():
    print("[ADVERSARIAL 3] Tiny memory limit (10KB) with multi-way join and sort...")
    temp_dir = tempfile.mkdtemp(prefix="axiom_tiny_mem_")
    cmd = [
        AXIOM_BIN,
        "-t", f"items={FIXTURES_DIR}/items.csv",
        "-t", f"sales={FIXTURES_DIR}/sales.csv",
        "-e", "SELECT i.category, COUNT(*), SUM(s.quantity) FROM items i JOIN sales s ON i.id = s.item_id GROUP BY i.category ORDER BY COUNT(*) DESC",
        "--memory-limit", "10KB",
        "--temp-dir", temp_dir,
        "--profile"
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    # Check for zero leaked files
    leaked = glob.glob(os.path.join(temp_dir, "*.axmb"))
    try:
        os.rmdir(temp_dir)
    except Exception:
        pass

    if res.returncode == 0:
        if len(leaked) == 0:
            print("  PASS: Query executed correctly under 10KB budget with zero leaked files.")
            return True
        else:
            print(f"  FAIL: Leaked {len(leaked)} temporary files!")
            return False
    else:
        print(f"  FAIL: Query failed: {res.stderr}")
        return False

def test_early_limit_termination():
    print("[ADVERSARIAL 4] Early LIMIT termination resource cleanup...")
    temp_dir = tempfile.mkdtemp(prefix="axiom_early_limit_")
    cmd = [
        AXIOM_BIN,
        "-t", f"items={FIXTURES_DIR}/items.csv",
        "-e", "SELECT id, category, price FROM items ORDER BY price DESC LIMIT 2",
        "--batch-size", "2",
        "--memory-limit", "1KB",
        "--temp-dir", temp_dir
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    leaked = glob.glob(os.path.join(temp_dir, "*.axmb"))
    try:
        os.rmdir(temp_dir)
    except Exception:
        pass

    if res.returncode == 0:
        if len(leaked) == 0:
            print("  PASS: Early LIMIT termination released and deleted all spill runs.")
            return True
        else:
            print(f"  FAIL: Leaked {len(leaked)} spill runs!")
            return False
    else:
        print(f"  FAIL: {res.stderr}")
        return False

def main():
    print("=== Running Axiom Milestone M2 Adversarial Test Suite ===\n")
    tests = [
        test_unwritable_temp_dir,
        test_exceeded_disk_quota,
        test_tiny_memory_budget,
        test_early_limit_termination
    ]
    passed = 0
    for t in tests:
        if t():
            passed += 1

    print(f"\nAdversarial Tests Finished: {passed}/{len(tests)} PASSED.")
    if passed == len(tests):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()

