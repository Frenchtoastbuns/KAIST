# CAP-PDB parallelism map

**Recorded:** 2026-08-04

Most of the computation is parallelizable, but **not all in the same way**. The key distinction is:

* **Parallel within one subtree query**
* **Parallel across different subtree queries**
* **Sequential dependencies that cannot be removed**

## Parallelism by idea

| Component                   | Parallel within one query? | Parallel across queries?         | Main limitation              |
| --------------------------- | -------------------------- | -------------------------------- | ---------------------------- |
| Normal CAP                  | Yes, strongly              | Yes                              | PDB memory bandwidth         |
| Compact CAP                 | Yes, strongly              | Yes                              | More table reads and banks   |
| Fused dual                  | Partly                     | Yes, strongly                    | Dual updates are sequential  |
| Exact W1 oracle             | Yes                        | Yes                              | Reduction tree               |
| Exact W2 oracle             | Yes, massively             | Yes                              | 2,016 pair checks and memory |
| W3 specialized dual         | Partly                     | Yes                              | Larger combination search    |
| Normal→compact→dual cascade | Sequential per query       | Pipeline-parallel across queries | Stage waiting                |
| Split-R2 scheduler          | Yes through overlap        | Yes                              | Context buffers and queues   |

# 1. Normal CAP: highly parallelizable

Normal CAP splits the parity positions into approximately 13 groups.

For one subtree query, each group independently computes its cheapest relaxed completion:

```text
Group 0 ─┐
Group 1 ─┤
Group 2 ─┤
...      ├─ sum group minima → normal bound
Group 12 ┘
```

All 13 groups can be evaluated simultaneously.

Also parallelizable:

* different residual weights;
* different table-bank reads;
* multiple subtree queries;
* multiple surviving candidate scores.

The main problem is not mathematical dependency. It is having enough BRAM ports and duplicated banks to feed all lanes.

This is why normal CAP is the easiest hardware design.

# 2. Compact CAP: also highly parallelizable

Compact considers allocations such as:

[
(2,0),\quad(1,1),\quad(0,2).
]

For each allocation, all parity groups can operate in parallel:

```text
Allocation 2L/0R: all 13 groups
Allocation 1L/1R: all 13 groups
Allocation 0L/2R: all 13 groups
```

The allocations themselves can also be evaluated in parallel.

So compact has substantial parallelism. Its problem is **resource cost**:

* more table dimensions;
* more reads;
* more BRAM replication;
* larger reduction logic.

It is not slow because it is sequential. It is expensive because exploiting its parallelism requires a lot of hardware.

# 3. Fused dual: partially parallelizable

Within one dual update, the parity-group solves are independent:

```text
Dual update 1:
 Group 0 solver
 Group 1 solver
 ...
 Group 12 solver
```

All 13 group solvers can run simultaneously.

The selected witnesses can then be combined using a parallel reduction network to calculate disagreement and update the multipliers.

However:

```text
Update 1 → Update 2 → Update 3 → Update 4
```

These updates are sequential.

Update 2 depends on:

* witnesses from update 1;
* disagreement from update 1;
* new multipliers produced after update 1.

Therefore the four-update dependency cannot be fully parallelized **within one query**.

Fused control helps because many queries now require:

* zero updates;
* one update;
* occasionally two or more updates;

rather than always four.

## Across different queries

Different subtree queries are independent:

```text
Query A: update 3
Query B: update 1
Query C: compact check
Query D: scorer
```

They can occupy different engines simultaneously using tags.

So fused dual has weak-to-moderate parallelism within one query, but strong parallelism across queries.

# 4. Weight-1 exact oracle: extremely parallelizable

For residual weight 1, every remaining row can be scored independently:

```text
Row 0 score ─┐
Row 1 score ─┤
Row 2 score ─┤
...          ├─ minimum reduction
Row m−1 score┘
```

With (P) lanes, approximately (P) rows can be checked per cycle.

Its hardware structure is simple:

* row-effect read;
* XOR with current parity state;
* weighted mismatch calculation;
* minimum reduction.

The issue is that the W1-only oracle did not improve total work enough to justify a dedicated architecture.

So it is very parallelizable, but not sufficiently useful alone.

# 5. Weight-2 exact oracle: massively parallelizable

For (m=64), there are:

[
\binom{64}{2}=2016
]

possible pairs.

Every pair can theoretically be checked independently:

```text
Pair (0,1)
Pair (0,2)
Pair (0,3)
...
Pair (62,63)
```

This gives enormous data parallelism.

A practical engine could check:

* 4 pairs/cycle;
* 8 pairs/cycle;
* 16 pairs/cycle;
* potentially more.

But each lane needs:

* two row identities;
* pair parity signature or two row-effect reads;
* exact packed parity scoring;
* information-cost addition;
* comparison and witness tracking.

So W2 is easy to parallelize conceptually, but expensive in:

* memory bandwidth;
* pair-signature storage;
* scorer lanes;
* routing;
* reduction logic.

This is why the W1/W2 oracle only remains attractive if it sustains approximately four lanes at no more than around 16 cycles per pair check.

# 6. Dual weight-2 group solver: particularly suitable for parallelism

This is different from the exact global W2 oracle.

In dual W2, each parity group evaluates the same candidate pair under its own local parity effect and multiplier costs.

A shared pair generator can broadcast one pair to all groups:

```text
                       ┌─ Group 0
Pair (i,j) generator ──┼─ Group 1
                       ├─ Group 2
                       ...
                       └─ Group 12
```

With 13 group lanes, the group factor disappears from cycle complexity:

[
O\left(G\binom{m}{2}\right)
\rightarrow
O\left(\binom{m}{2}\right).
]

This is one of the most realistic forms of dual parallelism.

It costs more LUTs and multiplier memories, but the work maps naturally onto hardware.

# 7. Weight-3 dual: parallelizable, but less cleanly

For residual weight 3, candidate triples or dynamic-programming states can be distributed across lanes.

Parity groups can still run independently.

However, compared with W2:

* there are more combinations;
* state management is larger;
* witness tracking is more complex;
* memory access is less regular.

So it is parallelizable, but W2 is the cleaner hardware target.

# 8. The hierarchical cascade is not parallel within one query

For one subtree:

```text
Normal
   ↓ unresolved
Compact
   ↓ unresolved
Dual
   ↓ unresolved
Scoring
```

Those decisions are sequential because the system invokes the next stage only after the previous one fails.

Running normal, compact and dual simultaneously would remove the waiting, but it would defeat the purpose because you would always pay for all three engines.

Therefore the cascade should not use speculative full parallel execution.

## It can be pipeline-parallel across queries

```text
Cycle period     Normal       Compact       Dual         Scorer
----------------------------------------------------------------
Period 1         Query A
Period 2         Query B      Query A
Period 3         Query C      Query B       Query A
Period 4         Query D      Query C       Query B      Query A
```

So:

* latency for a hard individual query contains multiple stages;
* system throughput can still be high;
* easy queries leave after normal;
* only hard queries occupy compact or dual.

This is pipeline parallelism, not same-query parallelism.

# 9. Split-R2 is the most proven parallel architecture

The tagged split-R2 design overlaps:

* query generation;
* normal-bound evaluation;
* survivor handling;
* exact scoring;
* multiple active contexts.

It reduced context waiting substantially and delivered approximately 24.85% mean cycle reduction in the canonical replay.

This is currently the strongest demonstrated architecture because its parallelism has already been exercised in RTL and canonical simulation.

# What is genuinely parallel and what is fundamentally sequential?

## Strongly parallel

* parity groups inside normal CAP;
* parity groups inside compact CAP;
* compact allocations;
* exact W1 row checks;
* exact W2 pair checks;
* group-parallel dual W2 evaluation;
* independent candidate scorers;
* independent subtree queries.

## Partly parallel

* witness collection;
* disagreement reduction;
* multiplier updates;
* W3 specialized solving.

## Fundamentally sequential

* dual update (u+1) after update (u);
* normal→compact escalation for the same query;
* compact→dual escalation for the same query;
* incumbent-dependent decisions when a newer score changes (U).

# Best hardware mapping

The sensible heterogeneous architecture is:

```text
                         ┌─ Normal group lanes
Incoming tagged query ───┤
                         ├─ Threshold compact lanes
                         ├─ Fused dual W1/W2/W3 engines
                         └─ Exact scorer lanes
```

Different queries occupy different engines simultaneously.

Within each engine:

* normal and compact parallelize over groups;
* W1 parallelizes over rows;
* W2 parallelizes over pairs and groups;
* scoring parallelizes over candidates.

The only major serial chain left is the repeated dual updates for one query.

## Bottom line

The most parallelizable algorithmic parts are:

1. **Normal CAP parity groups**
2. **Compact allocations and parity groups**
3. **W2 pair processing**
4. **Different subtree queries**
5. **Exact scoring lanes**

The least parallelizable part is:

> Successive dual multiplier updates for the same subtree.

Fused dual is feasible because it reduces how often that serial chain is entered and how many updates are executed—not because the serial dependency disappeared.
