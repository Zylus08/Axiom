# Project AXIOM — Milestone M2: Adversarial Robustness Test Results

**Milestone:** M2 (Failure Injection, Resource Bounds, and Leaked File Audits)  
**Date:** 2026-11-14  
**Harness:** `tests/integration/test_m2_adversarial.py`  

---

## 1. Test Matrix & Results

| Test ID | Test Scenario | Expected Behavior | Actual Behavior | Result |
|:---|:---|:---|:---|:---:|
| **ADV-01** | Unwritable / Invalid Temp Directory (`Z:/non_existent/`) | Reject with clean `Storage Error`, no crash | Clean error output: `Storage Error at line 1, column 1: Failed to create temporary directory` | **PASS** |
| **ADV-02** | Exceeded Spill Disk Quota (`--max-temp-disk 100B`) | Throw quota exceeded error and clean up all spilled files | Clean error: `Temporary disk quota exceeded`, 0 leaked files in temp directory | **PASS** |
| **ADV-03** | Extreme Memory Constraint (`--memory-limit 10KB`) | Complete multi-way join and sort correctly with zero leaked files | Completed with 100% correct output rows, 0 leaked files | **PASS** |
| **ADV-04** | Early LIMIT Termination on Spilled Sort | Evict remaining batches and delete all active partition files | Early termination clean exit, 0 leaked files | **PASS** |

**Summary:** 4/4 Adversarial Tests Passed (100%).

