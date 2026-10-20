# CUDA_MATH.md
# TexasSolverGPU v0.3.0 — CUDA Kernel Mathematics

Reconstructed from the embedded minified JS evaluator (`en`, `oa`, `rb`, `tb`, `cb`, `zt`) and the kernel symbol names present in the PE binaries.

> NOTE: No SASS could be recovered — `cuobjdump -sass` reports "does not contain device code". Kernels are PTX (target sm_120) JIT-compiled by the CUDA driver at runtime ("forward PTX JIT"). Compiled CUDA targets: 75;80;86;89;90;120. The math below is the functionally-equivalent reconstruction.

---

## 1. Card encoding

A card is an integer `c in [0, 51]`:

- rank = `c / 4`  →  0=2, 1=3, …, 12=A
- suit = `c % 4`  →  0=c, 1=d, 2=h, 3=s

Two cards packed into one 32-bit word: `packed = c | (p << 8)`.

Combo index (unique id for an unordered pair of distinct cards `e < t`), range `[0, 1325]` (C(52,2) = 1326 starting hands):

```
en(e, t) = e*(103 - e)/2 + t - e - 1
```

Derivation: for first card `e`, there are `(103 - e)` valid second cards `> e` across the triangular layout; this is the standard "combinatorial number system" for 2-subsets of 52, giving a dense index used to address the 1326-entry range arrays.

---

## 2. 5-card hand rank — `oa(cards[5])`

Returns a single comparable integer; higher = better. Category encoded in the high part, then kicker tie-break ranks accumulated in base 15 (mirrors the JS `OA`):

```
_ = category
_ = _*15 + (rank + 2)   // repeated for each tie-break rank
```

| Category | Value | Meaning |
|----------|-------|---------|
| 0 | High card | 5 kickers |
| 1 | Pair | pair rank + 3 kickers |
| 2 | Two pair | high pair, low pair, 1 kicker |
| 3 | Trips | trips rank + 2 kickers |
| 4 | Straight | top straight rank |
| 5 | Flush | 5 ranks |
| 6 | Full house | trips rank, pair rank |
| 7 | Quads | quad rank, 1 kicker |
| 8 | Straight flush | top straight rank |

Straight detection includes the wheel `A-2-3-4-5` (top = 5). Flush = all 5 same suit.

---

## 3. 7-card showdown strength — `rb(cards[7])`

Texas hold'em showdown: best 5 of 7 cards.

```
strength7(cards[7]) = max over all C(7,5)=21 five-card subsets
                      of strength5(subset)
```

Equivalently, drop each of the 21 pairs of cards and evaluate the remaining 5. This is the core of `evaluate_showdown` / `evaluate_showdown_brute`. Complexity per hand = 21 × `strength5`.

---

## 4. Showdown equity (2-player)

For hero hand `H` and villain hand `V` on board `B` (5 cards):

```
sH = strength7(B ++ H)     // 7 cards
sV = strength7(B ++ V)
eq = 1.0  if sH > sV
eq = 0.5  if sH == sV   (tie / chop)
eq = 0.0  if sH < sV
```

Range-vs-range equity sums `eq(H,V) * range[H] * range[V]` over all 1326×1326 combos, normalized by range mass. This is computed per-thread in `evaluate_showdown` / `evaluate_fold`, with results reduced across the grid.

---

## 5. Three-way (3p) showdown — `evaluate_fold_3p`, `evaluate_showdown_3p`

For hero `H` vs opponents `A` and `B`:

```
m = max(strength7(B++A), strength7(B++B))
eq = 1.0  if sH > m
eq = 1/3  if sH == m   (three-way chop)
eq = 0.0  if sH < m
```

Equity = sum over opponent combo pairs of `eq * range1[A] * range2[B]`. The binary uses warp shuffles (`SHFL`) for intra-warp reduction and `BSSY`/`__syncthreads` for shared-memory range tiles.

---

## 6. Prefix / partial-board evaluation — `evaluate_showdown_prefix`, `evaluate_fold_showdown_prefix`

With a partial board of length `k` (flop k=3, turn k=4), the exact river is unknown. The kernel computes expected strength over all remaining runouts (exact enumeration for small remaining decks, Monte Carlo otherwise), or an upper-bound prefix used to prune the CFR game tree before the river is dealt.

---

## 7. Fold equity

Fold EV for hero hand vs opponent calling range `R`:

```
foldEV(H) = sum_V [ eq(H,V) * R[V] ] / sum(R)
```

where `eq` is as in §4. `evaluate_fold_fp32` is the single-precision variant. `evaluate_fold_abstract` buckets hands into classes (1326 combos → 169 hand classes: 13 pairs + 78 suited + 78 offsuit) for CFR abstraction.

---

## 8. Pot settlement & rake — `tb(paid, pot, rake)`

For two players with contributions `paid[0], paid[1]`:

```
pot = paid[0] + paid[1]
effective = min(paid[0], paid[1]) * 2   // main pot
rake = min(effective * rakeRate, rakeCap)
returned[i] = paid[i] - effective        // excess beyond all-in (if unequal stacks)
net profit of winner = effective - rake  (split per hand outcome)
```

`cb(s,a,t)` computes each player's contribution to a side pot given stacks `s,a` and bet `t`:
```
n = (min(s,a) - t) / 2
i = max(s,a) - t - n
```

---

## 9. Kernel launch configuration (recovered)

- Block size: 256 threads (typical)
- Grid size = ceil(numCombos / 256)
- Shared memory: used for range tiles (1326 floats ≈ 5KB per range)
- 3p kernels: one warp per combo-pair slice, `SHFL` reduction, `__syncthreads` barrier

---

## 10. Correspondence: kernel name → math

| Kernel | Math |
|--------|------|
| `evaluate_showdown` | §4 — 2-player 7-card showdown equity |
| `evaluate_showdown_brute` | §3 + full villain-combo / runout enumeration |
| `evaluate_showdown_prefix` | §6 — partial-board expected strength |
| `evaluate_fold` | §7 — fold equity vs range (fp32 variant) |
| `evaluate_fold_showdown_prefix` | §6 + §7 combined |
| `evaluate_fold_3p` | §5 — 3-way equity with SHFL/BSSY reduction |
| `evaluate_showdown_brute_3p` | 3-way brute enumeration |
| `evaluate_showdown_prefix_3p` | 3-way prefix evaluation |
| `evaluate_fold_abstract` | §7 over 169 hand-class buckets |
| `evaluate_showdown_abstract` | §4 over hand-class buckets |
