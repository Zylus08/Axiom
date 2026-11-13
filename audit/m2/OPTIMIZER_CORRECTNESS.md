# Project AXIOM — Milestone M2: Query Optimizer Correctness Audit

**Milestone:** M2 (Rule-Based Logical Optimizations and Equivalence Proofs)  
**Date:** 2026-11-14  

---

## 1. Overview of Optimizer Architecture

Project AXIOM Milestone M2 introduces `LogicalOptimizer` (`include/axiom/planner/logical_optimizer.h` and `src/planner/logical_optimizer.cpp`). The optimizer operates between the `LogicalPlanner` and `PhysicalPlanner`.

---

## 2. Implemented Optimization Rules

### 2.1 Rule 1: Conjunctive Predicate Pushdown through Inner Joins
- **Transformation:**
  Given a query with filter $\sigma_P(R \bowtie_{J} S)$:
  1. Split predicate $P$ into conjuncts $C_1 \land C_2 \land \dots \land C_k$.
  2. If conjunct $C_i$ references only columns of $R$, push $C_i$ below the join: $\sigma_{C_i}(R)$.
  3. If conjunct $C_i$ references only columns of $S$, shift column indices and push $C_i$ below the join: $\sigma_{C_i}(S)$.
  4. If conjunct $C_i$ references columns of both $R$ and $S$, retain it in the join condition or post-join filter.
- **Semantic Equivalence Proof:**
  In relational algebra and SQL-92, inner join is commutative and associative, and conjunction distributes over Cartesian product:
  $$\sigma_{P_R \land P_S \land P_{RS}}(R \bowtie_J S) \equiv \sigma_{P_{RS}}\left( \sigma_{P_R}(R) \bowtie_J \sigma_{P_S}(S) \right)$$
  Under SQL Three-Valued Logic (3VL), a row is selected if and only if $P \equiv \text{TRUE}$. If $P_R(r) \neq \text{TRUE}$, $r$ cannot satisfy $P_R(r) \land P_S(s) \land P_{RS}(r,s)$ for any $s \in S$. Thus, discarding $r$ before the join produces identical results.

### 2.2 Rule 2: Top-K Limit Pushdown into Sort
- **Transformation:**
  When `LogicalLimit(K, offset)` sits on top of `LogicalProjection` on top of `LogicalSort(order_by)`:
  The `PhysicalPlanner` translates this directly into:
  `PhysicalLimit(K, offset) -> PhysicalProjection -> PhysicalSort(order_by, K, offset)`
- **Correctness Proof:**
  Let $S$ be the total order defined by `order_by`. `PhysicalSort` maintains a max-heap of size $K + \text{offset}$. For any input row $x$, if $x$ precedes the worst element currently in the heap, the worst element is evicted and $x$ is inserted. After consuming all $N$ input rows, the heap contains the $K + \text{offset}$ minimal elements according to $S$. Reversing the heap and applying `offset` produces the exact same sequence as a full sort followed by truncation.

---

## 3. Before vs After Optimization Plan Comparison

### Example: Filtered Join Query
```sql
SELECT i.id, i.category, s.quantity
FROM items i
JOIN sales s ON i.id = s.item_id
WHERE i.price > 100 AND s.quantity > 5
```

#### Before Optimization (Naive Plan):
```text
LogicalFilter: i.price > 100 AND s.quantity > 5
  LogicalJoin: i.id = s.item_id
    LogicalScan: items
    LogicalScan: sales
```

#### After Optimization (`LogicalOptimizer::optimize`):
```text
LogicalJoin: i.id = s.item_id
  LogicalFilter: items.price > 100
    LogicalScan: items
  LogicalFilter: sales.quantity > 5
    LogicalScan: sales
```
- **Performance Impact:** Filter predicates are evaluated directly during sequential scans, filtering out non-matching rows before they ever enter the join hash table or trigger partition spilling.

