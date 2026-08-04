# Complete Technical History of the OSD / CAP-PDB Decoder Project

**Project period covered:** 16–30 July 2026
**Primary research target:** exact acceleration of fixed-order ordered-statistics decoding (OSD)
**Current strongest practical result:** compact two-block CAP-PDB
**Current strongest pruning result:** cardinality-specialized, sparse/dense, early-stop four-update dual CAP-PDB
**Status:** algorithmically validated; FPGA/RTL validation not yet completed

---

## 1. Executive summary

This project began as a broad search for an algorithmic or hardware improvement to ordered-statistics decoding. Many plausible directions were investigated: packed parity scoring, projected lower bounds, best-first discovery, ORBGRAND pre-search, adaptive routing, pattern databases, relaxation witnesses, conflict learning, pairwise parity coupling, global-consistency dual decomposition, static compiled multiplier banks, memoization, and specialized exact subproblem solvers.

The research eventually converged on **CAP-PDB**, short for **cardinality-synchronized additive parity pattern database**. CAP-PDB is an exact branch-and-bound accelerator for a conventional fixed-order OSD list. It does not change which TEPs belong to the target list and does not approximate the metric. Instead, it computes an admissible lower bound on every unexplored subtree. If that lower bound is strictly greater than the best exact candidate already found, the entire subtree is safely discarded.

The central mathematical idea is to divide the parity coordinates of the systematic OSD generator matrix into small disjoint groups. Each group independently solves a relaxed fixed-cardinality completion problem using dynamic programming. Although the groups may choose different relaxed information-row identities, they must all choose the same number of future flips. Summing the group minima is admissible because each independent minimum is no greater than the cost induced by any real shared completion. Synchronizing all groups on the same remaining cardinality makes the bound stronger than a completely independent relaxation.

The project then developed two stronger descendants:

1. **Compact two-block CAP-PDB:** divides the remaining information ranks into two rank blocks and coordinates how the remaining TEP cardinality is allocated between them. This produces a pointwise stronger bound than normal CAP-PDB while retaining a compact table-based implementation.
2. **Dual two-block CAP-PDB:** formulates the missing cross-group agreement as a Lagrangian dual problem. Query-specific subgradient updates encourage the parity groups to agree on the same information-row selection. This dramatically strengthens pruning, but its original dense dynamic program was computationally prohibitive.

At the canonical difficult test point—eBCH(128,64), OSD-4, 0 dB, 1,000 identical frames—the controlled progression was:

| Method | Mean unique TEPs evaluated per frame | Reduction versus classical OSD |
|---|---:|---:|
| Classical exhaustive fixed-order OSD | 679,121.000 | 0% |
| Normal CAP-PDB | 354,339.465 | 47.824% |
| Compact two-block CAP-PDB | 286,642.377 | 57.792% |
| Four-update dual CAP-PDB | 119,889.055 | 82.346% |

The original dense dual required approximately **2.180 billion DP-cell transitions per frame**, so its pruning advantage did not initially translate into a practical engine. A sequence of exact solver improvements then reduced this cost:

| Dual implementation | Mean counted bound work/frame | Relative to original dense dual | Pruning/output change |
|---|---:|---:|---|
| Original dense four-update dual | 2.180B DP cells | 1.00× | Reference |
| Exact sparse/dense solver | 145.70M operations | 14.96× less work | None |
| Sparse/dense plus per-update early stopping | 112.57M operations | 19.36× less work | None |
| Cardinality-specialized weights 1–3 plus the above | **45.15M operations** | **48.27× less work** | None |

For the final cardinality-specialized dual, the per-frame operation distribution across the same 1,000 frames was:

| Statistic | Early-stop sparse dual | Final specialized dual |
|---|---:|---:|
| Mean | 112.57M | **45.15M** |
| Median | 120.82M | **48.70M** |
| p95 | 135.01M | **55.80M** |
| p99 | 137.82M | **57.16M** |
| Maximum | 139.26M | **58.13M** |

The optimized dual preserved:

- exactly 119,889.055 unique TEPs/frame;
- exactly the same decoded codeword, best metric, and tie status on every matched frame;
- every per-frame pruning and dual-update decision;
- zero violations in 1,454 structural subtree checks;
- zero violations in 4,362 early-stop equivalence checks;
- ASan, UBSan, and the coding-library regression suite;
- no additional persistent table memory over the reconstructed dual.

The project’s honest current conclusion is:

- **Normal CAP-PDB** establishes the core exact cardinality-synchronized parity abstraction.
- **Compact CAP-PDB** is the most credible practical/table-oriented algorithm.
- **Optimized dual CAP-PDB** is the strongest exact pruning result and a promising hard-subtree accelerator, but still requires hardware architecture and synthesis evidence before it can be called the overall practical winner.
- The next decisive research experiment is **trace-driven RTL simulation and FPGA synthesis**, not another CPU-runtime comparison.

---

## 2. The exact problem being solved

### 2.1 Conventional fixed-order OSD

After reliability ordering, basis selection, Gaussian elimination, and systematicization, the generator matrix is written as

\[
G'=[I_K\mid P].
\]

Here:

- \(N\) is the codeword length;
- \(K\) is the information dimension;
- \(I_K\) is the \(K\times K\) identity matrix;
- \(P\) is the systematic parity submatrix;
- the first \(K\) positions form the most reliable independent basis after permutation and elimination.

Let:

- \(z\in\{0,1\}^N\) be the channel hard-decision vector;
- \(a_j=|r_j|\ge0\) be the reliability magnitude at coordinate \(j\);
- \(c^{(0)}\) be the order-zero re-encoding obtained from the hard decisions on the systematic information basis.

A test-error pattern (TEP) is a binary vector or equivalently a set

\[
S\subseteq\{0,\ldots,K-1\}.
\]

An order-\(p\) OSD tests

\[
\mathcal E_p=\{S:|S|\le p\},
\]

whose size is

\[
M(K,p)=\sum_{w=0}^{p}\binom{K}{w}.
\]

For \(K=64,p=4\),

\[
M(64,4)=679{,}121.
\]

For each TEP \(S\), the candidate is

\[
c(S)=c^{(0)}\oplus\bigoplus_{i\in S}G'_i.
\]

The exact weighted Hamming distance is

\[
D(S)=\sum_{j=0}^{N-1}a_j\mathbf 1[c_j(S)\ne z_j].
\]

Because the systematic information part is the identity, this can be separated into information and parity costs:

\[
D(S)=
\sum_{i\in S}a_i+
\sum_{j=0}^{N-K-1}a_{K+j}
\left(
s_j\oplus\bigoplus_{i\in S}P_{i,j}
\right),
\]

where \(s\) is the order-zero parity-mismatch state.

Classical fixed-order OSD explicitly visits every TEP in \(\mathcal E_p\), forms or incrementally updates its parity effect, computes its exact metric, and retains the best candidate.

### 2.2 The actual exactness target

Every exact method in this project targets:

\[
S^*\in\arg\min_{S\in\mathcal E_p}D(S).
\]

This is **exact equivalence to exhaustive fixed-order OSD**, not necessarily maximum-likelihood decoding over the entire codebook.

The distinction is fundamental:

- If order-\(p\) OSD excludes the full-codebook ML codeword, CAP-PDB excludes it too.
- CAP-PDB accelerates the same finite candidate list; it does not enlarge the list.
- “Exact” therefore always means exact relative to the chosen OSD order, reliability ordering, systematic basis, metric, and tie convention.

---

## 3. The first successful foundation: CLM / selected-parity certification

Before CAP-PDB, the project developed an exact discover–certify architecture called **CLM-OSD** in the reports. The name referred to an implementation built around certified lower metrics and lazy materialization. This phase matters because it established several reusable principles later retained in CAP-PDB.

### 3.1 Selected-parity lower bound

Choose a small subset \(Q\) of parity coordinates. Define:

\[
L_Q(S)=
\sum_{i\in S}a_i+
\sum_{j\in Q}a_{K+j}
\left(
s_j\oplus\bigoplus_{i\in S}P_{i,j}
\right).
\]

All omitted parity terms are non-negative, so:

\[
L_Q(S)\le D(S).
\]

If \(U\) is the exact metric of the current incumbent, a candidate or subtree can be rejected only if:

\[
L_Q(S)>U.
\]

The inequality must be strict. If \(L_Q(S)=U\), the candidate must survive because it could tie the incumbent. This is how the implementation preserves best-metric uniqueness and tied-candidate behavior.

### 3.2 Discover–certify separation

The early architecture separated performance from proof:

1. **Bounded discovery:** a small best-first search, initially with budgets such as 64 candidates, tries to find a strong exact incumbent quickly.
2. **Queue-free certification:** a complete DFS over the remaining fixed-order TEP domain either scores a candidate exactly or removes it using an admissible bound.
3. **Packed exact scoring:** parity-row effects are represented as packed XOR signatures; byte-indexed weight tables compute the full parity metric.
4. **Lazy materialization:** the full \(N\)-bit codeword is constructed only when an exact score improves the incumbent.
5. **Tie fallback:** equal-best candidates trigger the conventional path where needed to reproduce the reference codeword tie convention exactly.

Discovery is not the proof of exactness unless it completes the list. The complete certification traversal is what guarantees that every omitted candidate has been safely ruled out.

### 3.3 CLM exactness theorem

For a fixed reliability ordering, systematic basis, fixed TEP list, non-negative weights, non-overflowing integer accumulation or guarded floating-point comparison, and the same tie convention, CLM returns:

- the same minimum weighted distance as exhaustive fixed-order OSD;
- the same uniqueness/tie status;
- the same decoded vector when the deterministic tie fallback is enabled.

The proof has three parts:

1. the projected metric is no greater than the full metric;
2. strict pruning cannot remove an improving or tying candidate;
3. every surviving TEP is scored exactly and every non-surviving TEP is covered by the certificate traversal.

### 3.4 Finite-precision conditions

For integer fixed-point weights, the argument is bit-exact provided the accumulator does not overflow.

For floating-point weights, regrouping additions is not automatically bit-exact. The validated guarded floating variant:

- reconstructed mismatch states using packed XOR;
- accumulated surviving candidates in the same canonical coordinate order as the reference;
- pruned only with a conservative roundoff margin.

With total reliability magnitude \(T=\sum_i a_i\), unit roundoff \(u\), and length \(N\), the implementation used a conservative margin \(16NuT\). This was stronger than the basic two-sum error allowance used in the derivation.

### 3.5 What CLM established—and what it did not

CLM established that:

- exact fixed-order OSD can be reorganized around admissible certification;
- packed parity scoring can avoid repeated full-codeword materialization;
- a fast discovery phase can be separated from the correctness proof;
- non-negative projected parity costs provide exact pruning.

However, a small selected-parity projection ignores most parity coordinates. Its certificate is therefore loose. CAP-PDB was developed to use **all parity coordinates** through multiple compact abstractions while keeping the lower bound admissible.

---

## 4. Normal CAP-PDB: the core retained contribution

CAP-PDB stands for **cardinality-synchronized additive parity pattern database**.

### 4.1 Parity grouping

Partition the \(R=N-K\) parity coordinates into disjoint groups:

\[
\mathcal B_0,\ldots,\mathcal B_{J-1},
\qquad
J=\left\lceil\frac{R}{B}\right\rceil.
\]

The frozen software formulation generally used group width \(B=5\), so each full group has:

\[
2^B=32
\]

possible parity-mismatch states.

For group \(g\):

- \(p_{g,i}\) is information row \(i\)'s projected XOR effect in that group;
- \(u_g\) is the current mismatch state;
- \(\phi_g(u)\) is the exact reliability cost of mismatch state \(u\).

### 4.2 Suffix dynamic program

At a DFS node, suppose information ranks below \(r\) have already been fixed. For exactly \(q\) future flips, define:

\[
M_g(r,q,u)=
\min_{\substack{T\subseteq\{r,\ldots,K-1\}\\|T|=q}}
\phi_g\left(
u\oplus\bigoplus_{i\in T}p_{g,i}
\right).
\]

Terminal conditions:

\[
M_g(K,0,u)=\phi_g(u),
\]

\[
M_g(K,q,u)=+\infty,\qquad q>0.
\]

Recurrence:

\[
M_g(r,q,u)=
\min\left\{
M_g(r+1,q,u),
M_g(r+1,q-1,u\oplus p_{g,r})
\right\}.
\]

This table gives the cheapest parity cost that group \(g\) could possibly achieve using exactly \(q\) rows from the remaining suffix.

### 4.3 Cardinality synchronization

Each group independently minimizes over the identities of future rows, but all groups are forced to use the same number \(q\) of future flips.

This is the defining relaxation:

- Group 0 may believe rows \(\{1,7\}\) are optimal.
- Group 1 may believe rows \(\{3,9\}\) are optimal.
- These row identities need not agree.
- But both groups must select exactly two rows.

Without synchronization, each group could choose whatever cardinality minimized its local parity cost, producing a much weaker lower bound. Synchronization preserves a global fact that all real TEP completions must satisfy: every parity group is induced by the same number of future information flips.

### 4.4 Information-cost relaxation

Information ranks are ordered by non-decreasing reliability cost. The cheapest possible information cost for any size-\(q\) completion from suffix \(r\) is:

\[
I(r,q)=\sum_{t=r}^{r+q-1}a_t.
\]

For a DFS node with already selected set \(A\), committed information cost \(C_A\), current group states \(u_g(A)\), and remaining budget \(b\), define:

\[
H_q(A,r)=
C_A+I(r,q)+\sum_{g=0}^{J-1}M_g(r,q,u_g(A)).
\]

The strict-descendant subtree bound is:

\[
H(A,r,b)=\min_{1\le q\le b}H_q(A,r).
\]

### 4.5 Theorem: CAP-PDB admissibility

For every real completion \(T\subseteq\{r,\ldots,K-1\}\) of size \(q\):

\[
H_q(A,r)\le D(A\cup T).
\]

Proof:

1. The \(q\) cheapest remaining information reliabilities cannot cost more than the specific real set \(T\):

   \[
   I(r,q)\le\sum_{i\in T}a_i.
   \]

2. The real set \(T\) is a feasible choice inside every group DP, so:

   \[
   M_g(r,q,u_g(A))
   \le
   \phi_g\left(
   u_g(A)\oplus\bigoplus_{i\in T}p_{g,i}
   \right).
   \]

3. The parity groups are disjoint. Summing their real costs reconstructs the complete parity contribution of \(D(A\cup T)\).

4. Adding the information inequality proves the lower-bound property.

Therefore a subtree may be discarded only when:

\[
H(A,r,b)>U,
\]

where \(U\) is the exact incumbent metric.

### 4.6 Theorem: complete wrapper equivalence

The complete CAP-PDB wrapper used exact branches:

- conventional exhaustive OSD outside its supported regime;
- order-zero exact exit where valid;
- projected discovery to establish an incumbent;
- CAP-PDB certification for remaining candidates;
- strict pruning;
- exact scoring for all survivors;
- conventional tie fallback.

Since every branch is either exhaustive or certified by an admissible bound, the wrapper returns the same best metric and uniqueness state as conventional fixed-order OSD. The tie fallback reproduces the same output codeword where the reference has a deterministic tied-vector convention.

### 4.7 Complexity and storage

With parity group width \(B\), order \(p\), and \(J=\lceil(N-K)/B\rceil\):

\[
\text{DP cells}=J(K+1)(p+1)2^B.
\]

Build time and storage are:

\[
\mathcal O\left(JK(p+1)2^B\right).
\]

For BCH(127,36), OSD-4:

- \(R=91\);
- \(B=5\);
- \(J=19\);
- 32 states/group;
- 112,480 DP cells;
- 454,574 bytes for the C++ int32 PDB;
- approximately 461,110 bytes including the discovery object in the final \(K=36\) runs;
- at most 512,980 bytes across the original \(K\le48\) validation grid.

With seven-bit reliability magnitudes, a five-coordinate parity group costs at most \(5\cdot127=635\), requiring 10 bits per packed hardware entry. The raw packed table would be:

\[
112{,}480\cdot10=1{,}124{,}800\text{ bits}
\]

or approximately 137.3 KiB before banking and metadata.

The parity groups are independent during construction, so their DPs can be built in parallel. Within a group, states and cardinalities can also be parallelized; the suffix recurrence remains sequential unless pipelined or unrolled.

---

## 5. The normal CAP-PDB systems wrapper

The July 21 frozen CAP-PDB wrapper was not simply “always build the PDB.” It used multiple exact policies to avoid setup cost in regimes where exhaustive OSD is already cheap.

### 5.1 Frozen support envelope

The original latency-oriented envelope was:

- OSD order \(p=4\);
- \(32\le K\le48\).

Outside this envelope, the wrapper ran conventional exhaustive OSD.

### 5.2 Order-zero-distance guard

The wrapper computed normalized order-zero distance:

\[
\rho=\frac{d_0}{\sum_j a_j}.
\]

The frozen July 21 rule used an integer equivalent of:

\[
\rho>0.080
\]

to choose conventional OSD. The logic used integer cross-multiplication, not floating point.

This was a performance routing decision only. Both branches were exact.

### 5.3 Bounded discovery

A 16-candidate projected discovery front end attempted to find a strong incumbent before paying for the full PDB construction and certification.

### 5.4 Tie handling

If the final best and second-best metrics were equal, conventional OSD was rerun to reproduce the reference’s exact tied-vector output and uniqueness flag.

### 5.5 Static small-list bypass

Related CLM and multi-regime versions also used a static threshold near \(2^{14}=16,384\) TEPs. Lists smaller than this were routed to conventional OSD because fixed table setup could dominate direct enumeration.

This boundary was later confirmed on eBCH(64,30), OSD-3:

- list size: 4,526 candidates;
- small-list guard fired on 600/600 frames;
- CAP-PDB correctly fell back to the conventional list.

---

## 6. Relaxation witnesses

The PDB contains not only minimum costs but also paths achieving those relaxed minima. Backtracking a group DP yields a relaxed witness:

\[
T^*_{g,q}\in\arg\min_T M_g(0,q,s_g).
\]

The witness is valid as a proposal even though different groups solve different relaxations. Every witness is rescored using the complete exact metric \(D(T^*_{g,q})\). Therefore:

- a good witness can improve the incumbent early;
- a bad witness merely fails to improve it;
- no witness is allowed to prune directly;
- exactness is unchanged.

### 6.1 Original witness gating

One frozen witness policy invoked witnesses only when the normalized discovery incumbent satisfied:

\[
\rho_s\ge0.100.
\]

On the 400-frame-per-SNR BCH(127,36) untouched AWGN holdout, witnesses were invoked on 6.56% of frames. These rare difficult frames dominated the tail.

### 6.2 Initial witness result

Relative to the same packed PDB certifier without witnesses:

| Eb/N0 | Latency speedup | Candidate reduction |
|---:|---:|---:|
| 3 dB | 1.127× | 34.39% |
| 4 dB | 1.203× | 55.84% |
| 6 dB | 1.095× | 80.87% |
| 8 dB | 1.059× | 98.69% |
| Pooled | 1.146× | 42.57% |

The initial result suggested meaningful hard-frame value.

### 6.3 Fresh-seed and cross-family ablation

The later, stricter ablation weakened the claim:

- fresh BCH/AWGN candidate reduction: 38.18%;
- four-family pooled candidate reduction: 38.25%;
- mean-latency ratio of PDB without witnesses to PDB with witnesses: approximately 1.01×;
- confidence interval included or nearly included no mean improvement;
- p95 improved by about 1.14×;
- p99 improved by approximately 1.16–1.20×.

By family:

| Family | No-witness / witness mean latency | Candidate reduction |
|---|---:|---:|
| BCH(127,36) | 1.012× | 37.46% |
| eBCH(128,36) | 1.017× | 42.72% |
| Polar-derived (128,36) | 0.953× | 16.31% |
| Random systematic (127,36) | 1.053× | 45.86% |

The correct conclusion is:

- witnesses are a valid exact incumbent-generation technique;
- they substantially reduce candidate evaluation;
- their strongest reproducible value is tail/hard-frame reduction;
- they are not a universal mean-throughput contribution;
- they should not be the paper headline.

---

## 7. Multi-regime exact routing

Because multiple exact engines have different cost profiles, a predictor can choose among them without affecting decoding correctness.

If every engine \(A_i\) returns the same exact fixed-list optimum, then for any routing function \(R(x)\):

\[
A_{R(x)}(y)=\arg\min_{S\in\mathcal E_p}D(S).
\]

A bad routing decision can make the decoder slower, but cannot make it wrong.

### 7.1 Three-regime rule

One frozen BCH(127,36) AWGN policy used:

- \(\rho_0\le0.020245\): packed PDB certification;
- \(0.020245<\rho_0\le0.055855\): packed selected-parity certification;
- \(\rho_0>0.055855\): witness-PDB certification.

Results:

| Policy | Pooled speedup vs exhaustive | p95 ratio | p99 ratio |
|---|---:|---:|---:|
| Per-frame oracle | 8.006× | 0.330 | 0.336 |
| Frozen three-regime rule | 7.312× | 0.354 | 0.542 |
| Always witness-PDB | 6.854× | 0.354 | 0.542 |
| Always packed PDB | 6.000× | 0.476 | 0.660 |

For BCH(127,64), OSD-4:

| Policy | Pooled speedup vs exhaustive |
|---|---:|
| Per-frame oracle | 105.732× |
| Frozen three-regime rule | 86.860× |
| Always witness-PDB | 78.997× |
| Always packed PDB | 44.761× |

### 7.2 Cross-family and channel results

Without retuning, the three-regime rule produced a pooled 7.116× speedup across BCH, extended BCH, polar-derived, and random systematic \(K=36\) codes, with 0/1,600 slower frames in that test.

A channel-mixture router produced:

| Channel | Routed speedup | p95 ratio |
|---|---:|---:|
| Matched AWGN | 6.499× | 0.391 |
| Markov burst with mismatched AWGN metric | 2.849× | 0.474 |
| Rayleigh with mismatched CSI metric | 3.574× | 0.356 |
| Matched Rayleigh | 7.133× | 0.384 |
| Pooled | 4.285× | 0.382 |

### 7.3 Why routing became supporting work

Routing is useful engineering, but the algorithmic novelty is weaker:

- exact algorithm portfolios are a known general idea;
- thresholds are platform- and implementation-dependent;
- the predictor is not the correctness mechanism;
- the core paper is stronger when centered on the CAP-PDB certificate.

Routing remains useful for a future hardware controller that chooses conventional, compact, or dual CAP depending on query difficulty.

---

## 8. CLM–ORBGRAND hybrid: successful in a narrow regime, rejected as the headline

A 256-query automatic one-line ORBGRAND prefix was tested before CLM:

1. ORBGRAND attempts a limited set of guesses.
2. If it returns early, the decoder accepts that candidate.
3. Otherwise the exact CLM engine runs.

### 8.1 Why the hybrid was not mathematically exact relative to CLM

A syndrome-valid ORBGRAND candidate found within 256 guesses is not necessarily the minimum-metric candidate in CLM’s fixed OSD-\(p\) list. Therefore early ORB return is an approximation relative to CLM, even if no mismatches are observed in a finite experiment.

The hybrid’s fallback branch is exact; its early-return branch is empirical.

### 8.2 Results

On 24,000 untouched AWGN frames:

- 0 observed decoded, metric, or uniqueness mismatches versus CLM;
- pooled CLM/hybrid speedup: 2.448×;
- paired geometric speedup: 8.410×.

Across 48,000 fading, burst, and mismatch frames, all observed outputs still matched, but:

- Markov burst mismatch pooled speedup was approximately 1.000×;
- Rayleigh CSI mismatch pooled speedup was 0.996×;
- fallback was nearly universal in Rayleigh conditions;
- 60–70% of frames could be slightly slower because they paid the prefix and then CLM.

### 8.3 Decision

The hybrid was rejected as the main novelty because:

- exact equivalence was not proved for its early-return branch;
- ORB/OSD hybrid principles were already adjacent prior art;
- its benefit was strongly regime-dependent;
- CAP-PDB later produced a cleaner exact theorem.

ORBGRAND remains potentially useful as an **incumbent generator followed by CAP certification**, but not as an uncertified early-return headline.

---

## 9. Failed and negative CAP-related theories

The project deliberately used kill gates. A mathematically valid mechanism was not retained merely because it reduced candidates.

### 9.1 Inter-frame SIMD OSD

Hypothesis: process multiple independent frames in SIMD lanes to improve throughput.

Observed relative performance: 0.566×.

Decision: killed. The layout and control divergence cost outweighed vector parallelism.

### 9.2 Exact HARQ warm start

Hypothesis: reuse prior-frame or prior-transmission exact OSD state across 719 tested transitions.

Result: exact but approximately 1.0×.

Decision: killed. Reuse did not provide a meaningful performance advantage.

### 9.3 Tie-aware basis selection

Hypothesis: choose among equivalent reliability/basis decisions to reduce future OSD search.

Result: only about 1–5% proxy reduction.

Decision: killed as too small and insufficiently direct.

### 9.4 Uniform cost-partitioned parity PDB

Hypothesis: split information reliability cost uniformly across parity abstractions to make their sum stronger.

Result:

- slightly stronger pruning;
- approximately 4–10% slower than simpler additive CAP-PDB.

Decision: killed. The apparent early gain came from experiments where the expensive table was rarely built; component attribution exposed the misleading result.

### 9.5 Row-coupled zero-one cost partition

Hypothesis: assign each information-row cost to selected parity abstractions to create a stronger cost-partitioned bound.

Result:

- weaker bound in practice;
- approximately 20–30% slower.

Decision: killed.

### 9.6 Static and learned high-rate dispatch complexity

More complex machine-learning predictors were screened to catch rare high-rate slow frames. On a fixed calibration split, routing the 100 highest-risk frames achieved at most:

- 9% precision;
- 33.3% recall;
- pooled speedup reduced to at most 5.83×.

Decision: rejected. A simple exact static rule was more defensible and reliable.

### 9.7 Universal per-frame speedup theory

This claim was decisively rejected.

Examples:

- Markov burst mismatch included a condition-geometric speedup of only 0.922×;
- mismatched Rayleigh included 0.968×;
- 489/9,600 primary channel frames were slower;
- easy or small-list frames can pay table construction and bound-check overhead without saving enough enumeration.

The corrected claim is pooled/mean and tail benefit in a declared regime, not “always faster.”

---

## 10. Conflict coupling and conflict learning

The ordinary CAP relaxation allows different parity groups to choose incompatible information-row identities. This suggested learning or explicitly encoding cross-group compatibility.

### 10.1 Fused cross-group conflict/compatibility certificate

The fused method added exact compatibility information between parity abstractions.

Results on BCH(127,36), OSD-4:

| Eb/N0 | Current candidates | Fused candidates | Candidate reduction | Current/fused speed |
|---:|---:|---:|---:|---:|
| 3 dB | 8,880.72 | 6,714.17 | 24.4% | 0.693× |
| 4 dB | 2,357.13 | 1,476.68 | 37.4% | 0.595× |
| 6 dB | 142.39 | 84.17 | 40.9% | 0.591× |
| 8 dB | 1.32 | 1.32 | 0.0% | 1.136× |

Pooled/hard behavior:

- fused engine was 1.520× slower on the screening pool;
- 1.320× slower on the hardest 10%;
- p95 increased 33.9%;
- p99 increased 24.6%;
- zero exactness disagreements.

Cheaper fingerprint/quotient approximations also reduced candidates but remained slower.

Decision: killed as a main algorithm. It was mathematically sound but a negative efficiency result.

### 10.2 Dynamic exact conflict learning

Hypothesis: derive reusable no-good certificates during decoding and apply them to later subtrees.

The learned certificate was exact and tie-safe, but reusable conflicts were sparse.

At a derivation budget of 32:

| Eb/N0 | Current candidates | Conflict candidates | Candidates saved |
|---:|---:|---:|---:|
| 3 dB | 8,880.72 | 8,869.64 | 11.08 |
| 4 dB | 2,357.13 | 2,332.71 | 24.43 |
| 6 dB | 142.39 | 138.97 | 3.41 |
| 8 dB | 1.32 | 1.32 | 0 |

Across derivation budgets 8–64:

- best pooled current/conflict ratio: 0.984×;
- best hard-10% ratio: 0.954×;
- substantive policies worsened p95 and p99;
- 1,600 unique direct frames produced zero disagreements.

Decision: killed. Correctness passed; performance and reusable-conflict density failed.

### 10.3 Selective parity-group coupling oracle

To give pairwise coupling its best possible chance, the experiment used an oracle that could choose from prebuilt pair PDBs while excluding predictor and table-construction cost.

Primary sweep:

| Pair group pool | Candidate reduction | Pooled speed | Hardest-10% speed |
|---:|---:|---:|---:|
| 2 groups | 30.63% | 1.176× | 1.152× |
| 4 groups | 43.79% | 1.228× | 1.197× |
| 8 groups | 47.84% | 1.266× | 1.242× |
| All 19 groups | 48.08% | 1.268× | 1.243× |

Untouched-seed top-eight result:

- 35.77% candidate reduction;
- 1.263× pooled speed;
- 1.172× hardest-10% speed;
- 1.152× p95;
- 1.114× p99.

Even the free-build counterfactual failed the predefined hard-frame and tail gates.

Decision: killed as a full research direction. Pair coupling was useful but not strong enough to justify its complexity.

---

## 11. Perfect-subtree oracle: the key diagnostic success

After conflict and coupling mechanisms failed, the project asked a more fundamental question:

> If an ideal exact subtree bound were free, how much pruning and latency headroom would remain?

The oracle exhaustively computed the true best descendant cost for each queried subtree, then replayed decoding as if that perfect bound were free.

### 11.1 Results

Across BCH(127,36), OSD-4, seven-bit fixed point, AWGN at 3/4/6/8 dB, 900 unique frames:

| Statistic | Perfect-subtree oracle vs current witness-PDB |
|---|---:|
| Pooled speedup | 2.313× |
| Hardest-10% speedup | 4.954× |
| p95 speedup | 3.900× |
| p99 speedup | 5.064× |
| Candidate reduction | 98.052% |
| Output/metric/uniqueness mismatches | 0 |

By SNR:

| Eb/N0 | Candidate ratio | Latency ratio |
|---:|---:|---:|
| 3 dB | 86.56× | 3.361× |
| 4 dB | 32.14× | 2.133× |
| 6 dB | 2.54× | 1.096× |
| 8 dB | 1.00× | 1.066× |

### 11.2 Interpretation

The oracle was not an algorithm and was impossible to deploy as implemented. Its value was diagnostic:

- ordinary CAP-PDB left substantial exact pruning headroom;
- the headroom was concentrated in low-SNR/hard subtrees;
- candidate count could fall by nearly two orders of magnitude under an ideal bound;
- the failure of conflict and pair-coupling methods did not mean the bound-strength direction was exhausted.

This result motivated the search for a stronger **global-consistency relaxation**, leading to the dual bound.

---

## 12. Root-level dual global-consistency screen

### 12.1 Idea

Each parity group independently solves a fixed-cardinality completion problem. The missing information is agreement on which information rows are selected.

Introduce row-specific Lagrange multipliers. Each group solves its own DP with modified row costs. If the multipliers satisfy a zero-sum constraint across groups, summing the group optima and scaling appropriately remains an admissible lower bound.

Subgradient updates use group disagreement:

\[
d_{g,i}=Gx_{g,i}-\sum_hx_{h,i},
\]

where \(x_{g,i}\) indicates whether group \(g\)'s relaxed optimum selected row \(i\), and \(G\) is the number of groups.

The direction sums to zero across groups, preserving the multiplier constraint.

### 12.2 Root-screen results

Test:

- BCH(127,36);
- OSD-4;
- seven-bit signed soft inputs;
- AWGN at 3, 4, 6, and 8 dB;
- 256 frames/SNR, 1,024 total;
- exhaustive OSD supplied exact best non-empty TEP cost.

One update:

| Eb/N0 | Order-zero truly optimal | Certified | Mean bound time |
|---:|---:|---:|---:|
| 3 dB | 171/256 | 3/256 | 69.6 µs |
| 4 dB | 202/256 | 12/256 | 72.9 µs |
| 6 dB | 248/256 | 161/256 | 71.3 µs |
| 8 dB | 256/256 | 254/256 | 71.4 µs |

Four updates:

| Eb/N0 | Order-zero truly optimal | Certified | Mean bound time |
|---:|---:|---:|---:|
| 3 dB | 171/256 | 4/256 | 235.8 µs |
| 4 dB | 202/256 | 12/256 | 237.0 µs |
| 6 dB | 248/256 | 165/256 | 233.9 µs |
| 8 dB | 256/256 | 254/256 | 231.8 µs |

There were zero unsafe bound overruns.

### 12.3 Initial decision

Always-on root dual was killed:

- one update consumed about 85% of the complete witness-PDB decoder’s measured mean latency;
- four updates were about 2.8× slower than the complete decoder;
- the dual was weakest on the difficult low-SNR frames where additional strength mattered most.

The broader idea was not abandoned. The next hypothesis was to invoke it inside selected subtrees, where the incumbent and residual problem might make stronger certification worthwhile.

---

## 13. Reconstructed CAP family: normal, compact, and dual

The July 29 source reconstruction recovered three directly comparable CAP formulations in one canonical harness.

### 13.1 Normal CAP-PDB

This is the cardinality-synchronized additive bound from Section 4:

\[
C_A+I(r,q)+\sum_gM_g(r,q,u_g).
\]

### 13.2 Compact two-block CAP-PDB

The remaining information ranks are divided at:

\[
\text{split}=\left\lceil\frac{3K}{4}\right\rceil.
\]

For every:

- parity group \(g\);
- suffix \(s\);
- exact first-block weight \(a\);

a 64-bit word records all group XOR effects reachable by exactly \(a\) flips in the first rank block \([s,\text{split})\).

For total remaining weight \(w\), the query enumerates:

\[
a+b=w.
\]

For a chosen allocation \((a,b)\):

- every group uses the same allocation;
- each group may choose different row identities/effects within that allocation;
- the first-block reachable-effect bitset is convolved with the ordinary suffix PDB for the second block;
- the information-cost relaxation uses the same allocation.

The final compact bound minimizes over allocations and weights.

#### Why compact dominates normal CAP

Normal CAP allows each parity group to choose any size-\(w\) set across the whole suffix.

Compact CAP additionally conditions every group on the same number \(a\) of selections from block 1 and \(b\) from block 2. Every real completion has one such allocation. Conditioning on it shrinks each group’s relaxed feasible set. Minimizing over all valid allocations preserves admissibility.

Therefore:

\[
H_{\text{compact}}\ge H_{\text{normal}}
\]

pointwise, while both remain no greater than the true descendant cost.

Structural validation found zero cases where compact fell below normal.

#### Reconstructed compact fingerprints

For BCH(127,36), OSD-4:

- added persistent storage: exactly 28,120 bytes;
- derivation:

  \[
  19\cdot37\cdot5\cdot8=28{,}120;
  \]

- maximum root-like table lookups: 8,512;
- derivation:

  \[
  19\cdot32\cdot\sum_{w=1}^4(w+1)=8{,}512.
  \]

### 13.3 Four-update dual two-block CAP-PDB

The dual query lets every group solve its own residual row-selection DP with group-specific row multipliers.

Key construction:

- terminal parity costs are scaled by the number of groups;
- initial row multipliers equal information reliabilities;
- the multiplier sum across groups equals the scaled true information cost;
- every update uses a zero-sum disagreement direction;
- the best lower bound seen across four updates is retained;
- the decoder uses the maximum of compact and dual;
- pruning remains strict.

The reconstructed root-like work fingerprint was:

\[
4\cdot19\cdot36\cdot32\cdot
\sum_{w=1}^4(w+1)
=1{,}225{,}728
\]

DP cells for a root-like query.

The dual is stronger because it partially restores the row-identity agreement discarded by independent CAP groups.

---

## 14. Canonical 1,000-frame audit

Configuration:

- code: eBCH(128,64,22);
- OSD order: 4;
- BPSK over code-rate-normalized AWGN;
- \(E_b/N_0=0\) dB;
- signed seven-bit soft values;
- 1,000 identical frames for every method;
- seed: `5928218492399464753`;
- frame-stream checksum: `11508365490867720138`.

### 14.1 Candidate/pruning result

| Method | Unique TEPs/frame | Scoring calls/frame | Bound checks/frame | Reduction vs classical |
|---|---:|---:|---:|---:|
| Classical OSD | 679,121.000 | 679,121.000 | 0 | 0% |
| Normal CAP-PDB | 354,339.465 | 359,253.583 | 38,308.098 | 47.824% |
| Compact two-block | 286,642.377 | 291,145.797 | 37,847.711 | 57.792% |
| Dense four-update dual | 119,889.055 | 122,419.347 | 37,058.821 | 82.346% |

Relative results:

- compact evaluated 19.105% fewer unique TEPs than normal CAP;
- dual evaluated 58.175% fewer than compact;
- dual evaluated 66.165% fewer than normal CAP.

### 14.2 Original bound-work result

| Method | Table reads/frame | Dual DP cells/frame |
|---|---:|---:|
| Normal CAP-PDB | 422,468.943 | 0 |
| Compact two-block | 2,933,046.052 | 0 |
| Dense four-update dual | 2,926,340.662 compact reads | 2,179,566,275.328 |

Interpretation:

- compact is a stronger but somewhat more table-intensive formulation;
- dense dual exposes a very large amount of safe pruning;
- dense dual’s original computational cost was far too high to call it practical.

### 14.3 Exactness result

Across all 1,000 matched frames:

- decoded-word mismatches: 0;
- best-metric mismatches: 0;
- tie-status mismatches: 0.

The same checksum allowed later solvers to be compared on the exact same channel/message stream.

---

## 15. Two-code, order, and SNR sweep

The reconstructed formulations were tested over 25 controlled cells:

| Code | Orders | SNRs | Frames/cell |
|---|---|---|---:|
| eBCH(128,64,22) | 3, 4 | 0,1,2,3,4 dB | 1,000 |
| eBCH(128,22,48) | 3,4,5 | 0,1,2,3,4 dB | 1,000 |

Total:

- 25,000 channel frames;
- 75,000 bounded-decoder outputs;
- 100,000 total decoder-frame rows.

All methods saw identical soft values within each cell. Message and standard-normal streams were held fixed across orders and SNRs within each code.

### 15.1 High-rate eBCH(128,64)

| Order | SNR | Classical | Normal | Compact | Dual |
|---:|---:|---:|---:|---:|---:|
| 3 | 0 dB | 43,745 | 33,532.9 | 30,460.0 | 18,176.4 |
| 3 | 1 dB | 43,745 | 23,766.8 | 19,140.5 | 8,984.6 |
| 3 | 2 dB | 43,745 | 11,232.7 | 7,560.9 | 2,705.4 |
| 3 | 3 dB | 43,745 | 2,809.8 | 1,668.0 | 738.0 |
| 3 | 4 dB | 43,745 | 250.2 | 158.1 | 112.7 |
| 4 | 0 dB | 679,121 | 354,339.5 | 286,642.4 | 119,889.1 |
| 4 | 1 dB | 679,121 | 202,485.4 | 147,499.8 | 58,354.7 |
| 4 | 2 dB | 679,121 | 66,153.9 | 42,263.5 | 18,616.9 |
| 4 | 3 dB | 679,121 | 10,022.3 | 6,539.9 | 3,864.7 |
| 4 | 4 dB | 679,121 | 377.9 | 261.5 | 197.7 |

### 15.2 Low-rate eBCH(128,22)

| Order | SNR | Classical | Normal | Compact | Dual |
|---:|---:|---:|---:|---:|---:|
| 3 | 0 dB | 1,794 | 1,402.7 | 1,277.4 | 1,110.6 |
| 3 | 1 dB | 1,794 | 1,264.5 | 1,154.3 | 1,062.2 |
| 3 | 2 dB | 1,794 | 1,140.5 | 1,070.3 | 1,029.4 |
| 3 | 3 dB | 1,794 | 1,019.2 | 986.3 | 974.0 |
| 3 | 4 dB | 1,794 | 827.5 | 818.1 | 816.1 |
| 4 | 0 dB | 9,109 | 4,947.8 | 3,670.8 | 2,226.3 |
| 4 | 1 dB | 9,109 | 3,623.0 | 2,495.7 | 1,681.1 |
| 4 | 2 dB | 9,109 | 2,418.6 | 1,680.4 | 1,344.8 |
| 4 | 3 dB | 9,109 | 1,612.5 | 1,248.6 | 1,161.0 |
| 4 | 4 dB | 9,109 | 1,110.5 | 965.4 | 954.7 |
| 5 | 0 dB | 35,443 | 13,617.0 | 9,666.2 | 6,602.7 |
| 5 | 1 dB | 35,443 | 9,505.6 | 6,421.4 | 5,056.5 |
| 5 | 2 dB | 35,443 | 6,216.6 | 4,067.3 | 3,636.2 |
| 5 | 3 dB | 35,443 | 3,933.1 | 2,536.4 | 2,457.5 |
| 5 | 4 dB | 35,443 | 2,321.0 | 1,515.2 | 1,509.6 |

### 15.3 Sweep conclusions

- Compact dominated normal CAP in every one of the 25 cells.
- Compact’s largest gains occurred at higher OSD orders.
- Dual was most valuable in high-rate, low-to-medium-SNR regimes.
- At high SNR on the low-rate code, dual added almost nothing because simpler exact exits and bounds already solved most frames.
- These results reject an always-dual architecture.

### 15.4 Sweep validation

- word mismatches: 0;
- best-metric mismatches: 0;
- tie-status mismatches: 0;
- every bounded unique-TEP count was no greater than the full classical list;
- every raw file contained 1,000 matched frames and a checksum;
- all 25 raw files were covered by a SHA-256 manifest.

Structural checks:

| Order | Subtrees checked | Compact violations | Dual violations | Compact below normal |
|---:|---:|---:|---:|---:|
| 3 | 1,271 | 0 | 0 | 0 |
| 4 | 1,454 | 0 | 0 | 0 |
| 5 | 1,559 | 0 | 0 | 0 |

The low-rate eBCH(128,22,48) generator was exhaustively audited:

- all 4,194,303 nonzero codewords were enumerated;
- generator was systematic;
- all rows had even parity;
- measured minimum distance was 48.

---

## 16. Attempts to make dual practical

### 16.1 Selective slack routing

Compact-bound slack was used to decide whether dual should run.

| Method | TEPs/frame | Fraction of dual pruning retained |
|---|---:|---:|
| Compact | 2,085.54 | — |
| Selective dual, slack 48 | 1,896.25 | 81.0% |
| Selective dual, slack 64 | 1,879.50 | 88.2% |
| Always dual | 1,851.92 | 100% |

Across 420 hard-region frames per method and three seeds:

- zero word mismatches;
- zero metric mismatches;
- zero tie mismatches.

Decision: useful trade-off evidence, but retained dual queries still paid the same expensive query-local DP.

### 16.2 Exact memoization

Hypothesis: identical dual query states would recur during DFS and could be cached.

Result: zero useful cache hits.

The traversal did not revisit reusable \((\text{suffix},\text{budget},\text{state},\text{cost})\) queries.

Decision: killed.

### 16.3 Parent-to-child message reuse

Hypothesis: reuse the parent subtree’s four-update dual state in DFS children.

Result:

- first dual iteration can be reused safely;
- after the first subgradient update, row multipliers change globally;
- those coefficient changes invalidate suffix messages for updates 2–4;
- maximum direct work reduction: 25%;
- target reduction required: approximately 10×;
- extra stored DP-bank state: approximately 450 KB per active DFS depth;
- OSD-4 depth storage: approximately 1.8 MB.

Decision: killed. Reuse was far too small and memory-heavy.

### 16.4 Static compiled multiplier bank

Hypothesis: precompute a bank of feasible zero-sum multiplier partitions once per frame, then query table-resident dual bounds cheaply.

A bank of 38 feasible partitions was tested.

Over 100 matched frames:

| Method | Unique TEPs/frame | Bound work/frame |
|---|---:|---:|
| Compact | 293,583.110 | 2.908M reads |
| Static compiled bank | 285,698.350 | 10.931M reads + 3.461M build transitions |
| Dynamic four-update dual | 121,630.610 | 2.163B DP cells |

The bank was exact but reproduced only the one-update dual result.

Update-count ablation:

| Updates | Unique TEPs/frame | Dense DP cells/frame |
|---:|---:|---:|
| 1 | 285,698.350 | 541.47M |
| 2 | 219,710.280 | 1.082B |
| 3 | 161,769.380 | 1.622B |
| 4 | 121,630.610 | 2.163B |

Conclusion: the strong pruning comes from query-specific consensus produced by later multiplier updates. A static bank cannot capture it.

Decision: rejected and preserved as a negative result.

---

## 17. Exact sparse/dense dual solver

### 17.1 Dense group solver

For:

- \(m=K-r\) remaining rows;
- target residual weight \(w\);
- \(2^B=32\) parity states;

the dense DP updates:

\[
m(w+1)2^B
\]

states per group solve.

### 17.2 Sparse alternative

For small \(w\), explicitly enumerating all exact subsets may be cheaper:

\[
\binom{m}{w}.
\]

The hybrid solver chooses sparse enumeration when:

\[
\binom{m}{w}
\le
m(w+1)2^B,
\]

and otherwise falls back to the original dense DP.

This is not heuristic. Both paths solve the identical group objective and return the identical witness and objective value under the required tie rules.

### 17.3 Complexity

Earlier dense four-update formulation:

\[
\mathcal O\left(
UGm2^Bp^2
\right),
\]

where:

- \(U=4\) updates;
- \(G\) parity groups;
- \(p\) OSD order.

Sparse/dense formulation:

\[
\mathcal O\left(
UG
\sum_{w=1}^{p}
\min\left\{
\binom{m}{w},
m(w+1)2^B
\right\}
\right).
\]

The formal worst-case upper bound is unchanged because difficult queries may still use dense DP. The improvement is exact, instance-sensitive, and large in the measured low-weight residual regime.

### 17.4 Result

On the canonical 1,000 frames:

| Version | Unique TEPs/frame | Bound work/frame |
|---|---:|---:|
| Dense four-update dual | 119,889.055 | 2,179,566,275 |
| Sparse/dense four-update dual | 119,889.055 | 145,698,268 |

Reduction:

- 93.315% less counted solver work;
- 14.959× improvement;
- zero pruning changes;
- zero word/metric/tie mismatches;
- no additional persistent memory.

Structural validation:

- 1,454 brute-force subtree checks;
- zero compact violations;
- zero dense/sparse dual violations;
- zero static-bank admissibility violations;
- zero compact-below-normal violations.

---

## 18. Per-update early stopping

The four-update dual retains the best admissible lower bound seen so far. After an intermediate update, if the current bound already proves:

\[
L_u>U,
\]

the subtree can be pruned immediately. Later updates are unnecessary.

### 18.1 Safety condition

The early stop is query- and residual-weight-specific:

- the intermediate value must itself be an admissible bound;
- strict inequality must already exceed the exact incumbent;
- the retained best dual bound is monotone non-decreasing across retained updates;
- stopping cannot convert a non-prunable query into an unsafe prune.

### 18.2 Result

On 1,000 canonical matched frames:

| Version | Unique TEPs/frame | Bound operations/frame |
|---|---:|---:|
| Sparse/dense four-update dual | 119,889.055 | 145.70M |
| Plus early stopping | 119,889.055 | 112.57M |

Additional reduction:

- 22.74%;
- only 15.8% of update-1 target solves reached update 4;
- 4,362 early-stop decision checks;
- zero decision-equivalence violations;
- same frame checksum;
- zero word, metric, or tie mismatches;
- ASan, UBSan, and OSD regression passed.

The initial target was below 50M operations/frame. Early stopping alone failed that target and was correctly classified as useful but insufficient.

---

## 19. Cardinality-specialized exact solvers

The first profiling assumption was that residual weight 3 dominated. Per-frame and per-residual-weight instrumentation disproved this.

Typical early-stop sparse-dual work distribution was approximately:

- weight 2: 60–70%;
- weight 1: 25–35%;
- weight 3: 10–12%;
- weight 4: zero in this path because dual was invoked below the first selected TEP level.

This was an important methodological correction: optimizing weight 3 alone would have saved only about 8% total work.

### 19.1 Weight-1 solver

For residual weight 1, the exact optimum can be found by a direct scan over remaining rows rather than maintaining a full cardinality/state DP.

### 19.2 Weight-2 solver

The dominant optimization used an exact dynamic pair solver. It selects between:

- direct pair enumeration;
- a bank of cheapest prior selections indexed by the five-bit XOR effect.

This avoids much of the repeated state work while retaining the exact best pair and witness.

### 19.3 Weight-3 solver

An incremental best-pair-by-XOR-state structure implicitly evaluates valid triples:

- construct best compatible pairs by XOR effect;
- combine with the third row;
- retain the actual witness triple;
- use direct enumeration or dense fallback when appropriate.

Its approximate work comparison for \(m\le64\) is:

\[
32m+\binom{m}{2}
\]

versus roughly:

\[
128m
\]

for the previous 32-state cardinality-3 DP, depending on precise counting.

### 19.4 Final result

On the original 1,000-frame stream:

| Statistic | Early-stop sparse dual | Specialized weights 1–3 |
|---|---:|---:|
| Mean | 112.57M | **45.15M** |
| Median | 120.82M | **48.70M** |
| p95 | 135.01M | **55.80M** |
| p99 | 137.82M | **57.16M** |
| Maximum | 139.26M | **58.13M** |

Results:

- 59.89% further computation reduction beyond early stopping;
- 48.27× below original dense dual;
- mean target below 50M passed;
- p95 and maximum remained above 50M;
- identical 119,889.055 TEPs/frame;
- identical per-frame TEP counts and dual decisions;
- zero decoded-word, best-metric, or tie mismatches;
- 1,454 structural checks passed;
- 4,362 early-stop checks passed;
- ASan, UBSan, and coding regression suite passed.

The improvement does not change the worst-case complexity class. It is an exact specialized implementation of common low-cardinality residual subproblems.

---

## 20. Counter definitions and the slide-number correction

Several historical figures came from different harnesses or measured different quantities. They must not be compared without checking their counter definitions.

### 20.1 Canonical reconstructed counters

- `unique_teps_evaluated`: union of distinct TEP masks evaluated by discovery, witnesses, and certification.
- `scoring_calls`: complete exact scoring invocations, including repeated scoring of the same TEP in different stages.
- `bound_checks`: discovery checks plus subtree-bound invocations.
- `pdb_table_lookups`: normal CAP-PDB table reads.
- `compact_table_lookups`: compact two-block convolution reads.
- `dual_queries`: dual subtree invocations.
- `dual_dp_transitions`: dense dual DP-cell updates.
- later sparse/specialized counters: recursive edges, subset completions, state transitions, or specialized solver operations according to the documented solver definition.

### 20.2 Why unique TEPs and scoring calls differ

A TEP may be:

- evaluated during discovery;
- proposed as a witness;
- encountered again during certification.

It counts once in `unique_teps_evaluated` but may create several `scoring_calls`.

### 20.3 The 448,397 versus 595,976 issue

The presentation sentence claiming:

> “It evaluates 448,397 rather than 595,976 TEPs per frame”

was not a valid canonical CAP comparison.

The saved data showed 448,397 under `stage3_ns`, meaning nanoseconds for a timing stage—not a TEP count. The approximately 596k figure came from an older CLM/CAP path with different conditions.

Therefore:

- those two values must not be presented as comparable TEP counts;
- the sentence should be removed from the slides;
- the canonical eBCH(128,64), OSD-4, 0 dB values are:

| Method | Unique TEPs/frame |
|---|---:|
| Classical OSD | 679,121 |
| Normal CAP-PDB | 354,339 |
| Compact CAP-PDB | 286,642 |
| Optimized dual CAP-PDB | 119,889 |

### 20.4 Old witness-PDB and reconstructed CAP counts

Older reports sometimes show figures such as:

- 213,023 candidates/frame for an eBCH(128,64) CAP holdout;
- 20–22k complete candidates for BCH(127,36);
- approximately 448k scored candidates for a CLM comparator at another configuration.

These are valid only within their original harnesses. They differ because of:

- code and dimension;
- OSD order;
- SNR;
- incumbent discovery;
- wrapper guards;
- witness behavior;
- candidate versus unique-TEP definitions;
- search traversal;
- tie and fallback behavior.

The July 29–30 reconstructed audit is the canonical comparison among normal, compact, and dual CAP.

---

## 21. Experimental methodology and corrections

### 21.1 Corrected timing harness

The principal original CAP latency validation used:

- AMD EPYC 9V74;
- Linux x86-64;
- GCC 13.3;
- C++17;
- `-O3 -march=native`;
- CPU pinning to logical core 8;
- paired frames decoded by every compared mode;
- rotating mode order by frame and repeat;
- repeated timings collapsed to per-frame medians;
- identical decoder storage addresses for every timed mode.

The identical-address rule was introduced after separate decoder objects caused severe cache-address-layout bias. Earlier contaminated timing numbers were discarded.

### 21.2 Why CPU timing is no longer the decisive next step

CPU timing helped validate the original software system, but for the reconstructed compact/dual study it is not the cleanest architectural measure because it depends on:

- C++ implementation quality;
- compiler optimization;
- cache locality;
- branch prediction;
- vectorization;
- data layout;
- processor microarchitecture.

The later reconstructed study therefore focused on:

- unique TEPs;
- exactness;
- table/solver operation counts;
- memory;
- per-frame operation distributions.

The next decisive comparison should be hardware-normalized through RTL and synthesis.

### 21.3 Exactness versus statistical validation

Zero mismatches validate the implementation; they are not the mathematical proof.

The proof comes from:

- non-negative omitted costs;
- relaxed feasible-set containment;
- cardinality synchronization;
- zero-sum dual multipliers;
- strict pruning;
- complete coverage or exact fallback.

Matched-frame testing checks:

- that the implementation matches the equations;
- that counters are consistent;
- that tie handling is correct;
- that solver dispatch does not change the objective;
- that no memory/undefined-behavior defects appear in the tested paths.

---

## 22. Validation catalogue

The project contains several overlapping validation ledgers. Counts should not be blindly summed as statistically independent channel draws.

### 22.1 Early CLM validation

The early selected-parity/packed-scoring work accumulated:

- main final grid: 24,000 comparisons;
- five additional seeds: 12,000;
- order/length screen: 2,100;
- publication comparator: 2,400;
- multi-channel primary grid: 9,600;
- additional channel seeds: 9,600;
- multi-precision kernel: 1,200;
- adaptive development: 19,200;
- adaptive integrated holdouts: 28,800.

Reported combined execution ledger: 108,900 paired comparisons with zero claimed-contract mismatches.

### 22.2 Precision tests

Random systematic \((63,32)\), OSD-4, 41,449 TEPs:

| Representation | Frames | Score/winner/tie mismatches |
|---|---:|---:|
| q4 | 200 | 0/0/0 |
| q8 | 200 | 0/0/0 |
| q12 | 200 | 0/0/0 |
| q16 | 200 | 0/0/0 |
| guarded float32 | 200 | 0/0/0 |
| guarded float64 | 200 | 0/0/0 |

### 22.3 Channel robustness

The early CLM exactness tests covered:

- matched AWGN;
- matched Rayleigh;
- two-state Markov burst noise decoded with an AWGN metric;
- Rayleigh fading decoded without proper CSI weighting.

Across the reported full-decoder channel comparison ledger:

- decoded-vector mismatches: 0;
- best-metric mismatches: 0;
- tie-status mismatches: 0.

Channel mismatch changes the weights and the quality of the incumbent, but does not invalidate equivalence relative to conventional OSD using the same supplied weights.

### 22.4 CAP-PDB final supported-envelope validation

Original July 21 CAP wrapper:

- 13,800 supported-envelope frames;
- 2.493× pooled speedup over exhaustive fixed-order OSD;
- CAP/base p95 ratio: 0.659;
- p99 ratio: 0.392;
- 2.53% of frames more than 5% slower;
- PDB built on 37.37% of frames.

Exactness evidence:

- 16,600 realistic held-out inputs;
- 1,600 random-soft certificate fuzz inputs;
- zero decoded, metric, and uniqueness mismatches.

### 22.5 Original CAP code-family test

1,000 frames each:

- BCH(127,36);
- eBCH(128,36);
- polar-derived systematic (128,36);
- random systematic (127,36).

Pooled speedups were approximately 2.61–2.79×.

### 22.6 Original CAP channel/mismatch test

1,000 frames each:

- matched AWGN: 2.764×;
- Markov burst mismatch: 1.180×;
- Rayleigh CSI mismatch: 1.393×;
- matched Rayleigh: 2.823×.

### 22.7 Witness ablation

- three fresh BCH/AWGN seeds;
- four-family holdout;
- zero exact-mode decoded/metric/uniqueness mismatches;
- approximately 38% candidate reduction;
- mean-latency gain approximately 1.01×;
- stronger p95/p99 effect.

### 22.8 Conflict and coupling tests

- fused conflict certificate: 1,300 unique differential frames, zero disagreements;
- dynamic conflict learner: 1,600 unique active-pruning frames, zero disagreements;
- pair-coupling oracle: 600 unique frames / 1,200 timed pairs, zero mismatch fields;
- mechanisms rejected on cost, not correctness.

### 22.9 Perfect-subtree oracle

- BCH(127,36);
- OSD-4;
- AWGN 3/4/6/8 dB;
- 900 unique frames;
- zero output/metric/uniqueness mismatches;
- 2.313× pooled idealized speedup;
- 98.052% candidate reduction.

### 22.10 Root dual

- 1,024 frames;
- one- and four-update budgets;
- exact exhaustive non-empty-TEP oracle;
- zero unsafe bound overruns;
- rejected due cost and poor low-SNR certification.

### 22.11 Reconstructed normal/compact/dual structural tests

- order 3: 1,271 random subtrees;
- order 4: 1,454;
- order 5: 1,559;
- zero compact or dual admissibility violations;
- zero compact-below-normal violations.

### 22.12 Reconstructed sweep

- 25 cells;
- 25,000 channel frames;
- 100,000 decoder-frame rows;
- zero word, metric, or tie mismatches;
- raw frame checksums and SHA-256 manifest.

### 22.13 Final optimized dual

- canonical 1,000 matched eBCH(128,64), OSD-4, 0 dB frames;
- identical checksum to dense dual;
- identical per-frame TEP and update decisions;
- 1,454 structural checks;
- 4,362 early-stop checks;
- ASan;
- UBSan;
- coding-library regression suite;
- zero failures.

---

## 23. External baseline work

### 23.1 Published exact and approximate OSD skipping

In-house faithful implementations included:

- conventional fixed-order OSD;
- exact Trivial skipping;
- approximate DAI skipping;
- approximate extra-parity skipping;
- approximate joint DAI + extra parity.

Packed implementations were checked against independent scalar interpretations. The project correctly avoided claiming that these were author-supplied binaries.

Important finding: fewer complete candidates does not necessarily mean lower runtime. Candidate construction, constraint processing, list generation, and preprocessing can dominate.

### 23.2 LC-OSD and LE-OSD

Independent LC-OSD and LE-OSD implementations were created because public author code was not located.

Validation included:

- LC semantic/list-generator checks against brute-force constrained search;
- LE checks on full-rank and rank-deficient systems;
- small-code exhaustive end-to-end checks;
- reproduction of LC behavior after adding DAI;
- exact reproduction of the LE paper’s 947-TEP / 90,085-codeword cardinality.

On a 2,000-frame BCH(127,36) holdout:

| Decoder | Errors | Mean latency | Mean complete candidates |
|---|---:|---:|---:|
| CAP-PDB | 103 | 0.485 ms | 21,591 |
| LC-OSD + DAI | 105 | 3.495 ms | far fewer |
| Base LE-OSD | 101 | 24.251 ms | far fewer |

This produced controlled in-house ratios of:

- 7.21× versus LC-OSD + DAI;
- 50.03× versus base LE-OSD.

These are not universal author-code claims.

### 23.3 Cross-code holdout

On eBCH(128,64):

- CAP-PDB had zero exactness mismatches;
- exhaustive OSD-4: 679,121 candidates/frame;
- CAP-PDB: 213,023 candidates/frame in that older harness;
- measured speedup over exhaustive OSD-4: 3.44×.

LC/LE settings had worse FER and were not iso-BLER, so their runtime ratios were correctly rejected as comparative claims.

On eBCH(64,30), CAP bypassed its own PDB because the 4,526-candidate OSD-3 list was too small.

### 23.4 ORBGRAND boundary

ORBGRAND showed a crossover:

- at hard/moderate SNR, capped ORBGRAND could disagree with exact fixed-order OSD and have long tails;
- at clean high SNR, ORBGRAND could be several times faster with no observed disagreement in a small screen.

Therefore no universal superiority claim over ORBGRAND is safe.

---

## 24. Current novelty and publication position

### 24.1 Defensible novelty

The safest core claim is:

> CAP-PDB constructs an OSD-specific, cardinality-synchronized sum of disjoint parity-state abstractions to certify exact subtree pruning over an unchanged fixed-order OSD list. Compact CAP strengthens this certificate by synchronizing a two-block cardinality allocation, while dual CAP further tightens it through admissible query-specific consensus updates.

The specialized sparse/weight solvers are implementation contributions that make the strongest dual bound evaluable. They should not be sold as a new worst-case complexity class.

### 24.2 Claims that are unsafe

Do not claim:

- first exact OSD pruning method;
- first best-first OSD;
- full-codebook maximum-likelihood decoding;
- universal per-frame latency improvement;
- universal superiority over LC-OSD, LE-OSD, ORBGRAND, or approximate skipping;
- hardware acceleration before RTL/synthesis;
- that 45.15M is a worst-case count;
- that 448,397 and 595,976 are comparable TEP counts;
- that zero mismatches are the proof.

### 24.3 Current publishability

The work is genuinely publishable as a focused exact OSD algorithm paper because it contains:

- a clean fixed-list objective;
- admissibility and equivalence theorems;
- a normal/compact/dual hierarchy;
- pointwise compact-over-normal dominance;
- strong controlled pruning gains;
- exact solver optimizations that preserve every decision;
- extensive structural, differential, sanitizer, and sweep validation;
- informative negative results.

Current realistic assessment:

- ITW/workshop/short paper: strong;
- Communications Letters-style paper: credible after final hardware or external-baseline framing;
- Transactions-scale paper: needs broader codes, architecture evidence, and more complete external comparison;
- hardware paper: premature until RTL and synthesis exist.

---

## 25. The real next step: trace-driven FPGA architecture study

Another broad software sweep is not the immediate priority. CPU runtime would mainly measure this implementation, compiler, and cache behavior.

The next experiment should answer:

> Does the stronger CAP bound reduce total decoder hardware work enough to justify its extra logic and memory?

### 25.1 Phase 0: freeze the canonical semantics

Use:

- classical OSD;
- normal CAP-PDB;
- compact CAP-PDB;
- final optimized dual CAP-PDB;
- canonical eBCH(128,64), OSD-4, 0 dB frames;
- exact counter definitions from the reconstructed audit.

### 25.2 Phase 1: trace export

Export per-query traces containing:

- suffix rank;
- remaining weight/budget;
- current parity-group states;
- incumbent threshold;
- normal/compact/dual reference bounds;
- prune/no-prune decision;
- selected sparse/dense/specialized solver;
- witness where needed;
- candidate-scoring events.

### 25.3 Phase 2: RTL engines

Implement:

1. normal CAP-PDB table-query engine;
2. compact two-block convolution engine;
3. shared branch-and-bound controller;
4. candidate parity/scoring datapath;
5. dual engine only after normal/compact are bit-exact and synthesized.

### 25.4 Phase 3: equivalence

For every trace query:

- RTL bound must equal the C++ reference;
- prune/no-prune must match;
- equality must remain unpruned;
- table-bank conflicts and pipeline stalls must not change semantics.

### 25.5 Phase 4: synthesis and place-and-route

Use one defined target first, such as a Xilinx 7-series device.

Report:

- LUTs;
- FFs;
- BRAMs;
- DSPs;
- Fmax;
- cycles/query;
- initiation interval;
- cycles/frame;
- bound-engine cycles;
- candidate-scoring cycles;
- memory bandwidth;
- energy proxy or post-route power if available.

### 25.6 Correct comparison

The metric must be:

\[
\text{total cycles/frame}
=
\text{bound cycles/frame}
+
\text{candidate-scoring cycles/frame}.
\]

A stronger bound is useful only if the cycles saved by scoring fewer TEPs exceed the cycles spent computing that bound.

Expected decision branches:

- if compact is best: compact is the practical architecture; dual is the exact pruning oracle;
- if optimized dual is competitive: dual becomes the main algorithm;
- if dual remains too expensive: use it selectively on high-slack/hard subtrees;
- if normal wins in hardware: compact/dual remain algorithmic strength results, not deployment winners.

---

## 26. Final state of every major idea

| Idea / variant | Exact? | Main outcome | Final status |
|---|---|---|---|
| Classical fixed-order OSD | Yes, over fixed list | Complete reference list | Baseline |
| Selected-parity CLM bound | Yes | Established discover–certify architecture | Foundational |
| Packed exact parity scoring | Yes | Avoids full candidate construction | Retained |
| Lazy materialization | Yes | Materialize only incumbent improvements | Retained |
| 256-query ORBGRAND early return | No theorem vs CLM | Strong AWGN gain, weak cross-channel behavior | Not headline |
| Static exact routing | Yes | Avoids bad regimes | Supporting |
| Normal CAP-PDB | Yes | 47.824% fewer TEPs at canonical point | Core contribution |
| Relaxation witnesses | Yes | ~38% fewer candidates, mainly tail benefit | Supporting |
| Multi-regime witness routing | Yes | Strong implementation speed in tested regime | Supporting |
| Uniform cost partition | Yes | Slightly tighter, slower | Killed |
| Row-coupled 0/1 partition | Yes | Weaker and slower | Killed |
| Fused conflict coupling | Yes | 24–41% fewer candidates, much slower | Killed |
| Dynamic conflict learning | Yes | Very few reusable conflicts | Killed |
| Pair-coupling oracle | Yes | Up to ~48% fewer candidates, insufficient speed | Killed |
| Perfect-subtree oracle | Diagnostic exact oracle | Proved 98.052% pruning headroom | Key diagnostic |
| Root-level dual | Yes | Too expensive, weak hard-frame certificate | Killed as root engine |
| Compact two-block CAP | Yes | Dominates normal in every tested cell | Practical winner |
| Dense four-update dual | Yes | 82.346% pruning, 2.18B DP cells | Strength oracle |
| Selective slack dual | Yes | Retains 81–88% of extra pruning | Trade-off only |
| Dual memoization | Yes | Zero useful hits | Killed |
| Parent-child dual reuse | Yes | At most 25%, large memory | Killed |
| Static compiled dual bank | Yes | Only one-update strength | Killed |
| Sparse/dense dual | Yes | 14.96× less bound work | Retained |
| Per-update early stop | Yes | Additional 22.74% reduction | Retained |
| Weight-1/2/3 specialized dual | Yes | 45.15M mean, 48.27× below dense | Current best dual |
| FPGA CAP engine | Not yet built | Required next evidence | Next step |

---

## 27. Bottom line

The project is no longer merely “an OSD implementation optimization.”

Its coherent research story is:

1. Fixed-order OSD wastes work by explicitly evaluating candidates whose best possible completion is already worse than the incumbent.
2. Selected-parity CLM showed that exact certification, packed scoring, and lazy materialization can restructure the search without changing its target.
3. Normal CAP-PDB used all parity coordinates through disjoint pattern databases synchronized by future TEP cardinality.
4. Compact CAP added a shared two-block cardinality allocation and pointwise strengthened the bound.
5. Conflict and pairwise coupling reduced candidates but failed on total cost.
6. A perfect-subtree oracle proved that major pruning headroom remained.
7. Dual CAP converted some of that headroom into an exact query-specific consensus bound, cutting canonical TEP evaluation by 82.346%.
8. Sparse/dense dispatch, update early stopping, and cardinality-specialized solvers reduced dual bound work by 48.27× without altering any result.
9. The remaining unknown is architectural, not mathematical: whether compact or optimized dual wins after mapping bound computation and candidate scoring into a fair FPGA design.

The strongest accurate one-sentence description is:

> We developed an exact hierarchy of cardinality-synchronized parity-abstraction bounds for fixed-order OSD, culminating in a dual-consistency certificate that reduces evaluated TEPs by 82.35% on eBCH(128,64), OSD-4 at 0 dB, and an exact specialized solver stack that cuts the dual certificate’s counted work from 2.18 billion to 45.15 million operations per frame without changing any decoded output, metric, tie, pruning, or update decision.

---

## 28. Primary internal records

The main source reports supporting this history are:

- `OSD_publishability_report_2026-07-16.md`
- `OSD_extended_validation_2026-07-17.md`
- `OSD_cross_algorithm_validation_2026-07-20.md`
- `OSD_hybrid_final_validation_2026-07-21.md`
- `OSD_CAP_PDB_publishability_checkpoint_2026-07-21.md`
- `OSD_novelty_reassessment_2026-07-21.md`
- `OSD_multi_regime_witness_checkpoint_2026-07-22.md`
- `OSD_conflict_coupling_kill_report_2026-07-22.md`
- `OSD_dynamic_conflict_learning_validation_2026-07-22.md`
- `OSD_selective_coupling_oracle_validation_2026-07-22.md`
- `OSD_perfect_subtree_oracle_validation_2026-07-23.md`
- `OSD_dual_global_consistency_feasibility_2026-07-24.md`
- `OSD_witness_ablation_validation_2026-07-24.md`
- `OSD_LC_LE_validated_comparison_2026-07-24.md`
- `OSD_LC_LE_generalization_validation_2026-07-25.md`
- `OSD_CAP_PDB_reconstruction_2026-07-29.md`
- `OSD_CAP_PDB_sweep_validation_2026-07-29.md`
- `OSD_dual_selective_amortization_validation_2026-07-29.md`
- `OSD_dual_incremental_reuse_feasibility_2026-07-29.md`
- `OSD_sparse_dual_compilation_validation_2026-07-30.md`
- the later early-stop and cardinality-specialized validation records contained in the July 30 cumulative checkpoint.
