# TEXAS SOLVER GPU v0.3.0 — SASS-Level Algorithmic Reference

**Source:** Direct reading of `nvdisasm -b SM80` disassembly of raw SASS machine code
extracted from 24 embedded CUDA ELF objects in `TexasSolverGpu_131.exe`.
Disassembly files: `/tmp/opencode/sass_disasm/sm_80/<kernel>.txt`

**Kernels analyzed in depth:**
- `backward_player` (673 instructions) — CFR backward regret matching
- `evaluate_showdown` (457 instructions) — HU showdown evaluation
- `forward_player` (145 instructions) — CFR forward reach accumulation
- `train_small_tree_fp32_multi_iter` (~3400+ instructions) — multi-iteration CFR training
- `gather_cfv` (41 instructions) — simple gather/scatter
- `backward_opponent` (361 instructions) — opponent backward pass

---

## 1. Constant-Buffer Kernel Parameter Slots (sm_80)

All kernels receive parameters via constant-memory bank `c[0x0][N]`. Observed slots:

| Slot | Meaning | Kernels |
|------|---------|---------|
| `c[0x0][0x00]` | grid/loop stride, numCombos base | all |
| `c[0x0][0x28]` | numCombos / numPlayers bound (bounds check) | gather_cfv, forward_player, evaluate_fold |
| `c[0x0][0x118]` | base pointer to data structures (ULDC.64) | forward_player, backward_player, forward_opponent |
| `c[0x0][0x160]` | output buffer pointer (low 32) | backward_player, forward_opponent |
| `c[0x0][0x164]` | output buffer pointer (high 32) | backward_player, forward_opponent |
| `c[0x0][0x168]` | board pointer | evaluate_showdown, evaluate_fold |
| `c[0x0][0x170]` | hero/vil combos pointer | evaluate_showdown |
| `c[0x0][0x178]` | reach/prob input pointer | forward_player, backward_player |
| `c[0x0][0x17c]` | numPlayers / numEntries bound | gather_cfv, forward_player |
| `c[0x0][0x180]` | per-element stride (0x4 = float/int) | forward_player, backward_player, forward_opponent |
| `c[0x0][0x184]` | numPlayers bound | backward_player, backward_opponent |
| `c[0x0][0x188]` | forward_opponent data pointer | forward_opponent |
| `c[0x0][0x190]` | board pointer | evaluate_showdown |
| `c[0x0][0x198]` | lookup-table base (16-bit entries) | evaluate_fold, forward_opponent |
| `c[0x0][0x1a0]` | lookup-table base | evaluate_fold, forward_opponent |
| `c[0x0][0x1a4]` | oppRange / probability table | evaluate_fold |
| `c[0x0][0x1a8]` | numActions / numRiver bound | evaluate_fold, forward_opponent |
| `c[0x0][0x1ac]` | numPlayers bound | forward_opponent, backward_player |
| `c[0x0][0x1b0]` | numStreets / EV table pointer | forward_opponent, backward_player |
| `c[0x0][0x1b4]` | numPlayers / loop bound | forward_opponent, backward_player |
| `c[0x0][0x1b8]` | output pointer | backward_player |
| `c[0x0][0x1bc]` | output pointer high-word | backward_player |
| `c[0x0][0x1c0]` | shared-memory base | backward_player |
| `c[0x0][0x1c4]` | numPlayers loop limit | backward_player |
| `c[0x0][0x1c8]` | heroStrength pointer | evaluate_showdown |
| `c[0x0][0x1d0]` | vilStrength pointer | evaluate_showdown |
| `c[0x0][0x1d8]` | lookup table | evaluate_showdown |
| `c[0x0][0x1e0]` | suit/rank table (byte) | evaluate_showdown |
| `c[0x0][0x1e8]` | shared memory base | evaluate_showdown |
| `c[0x2][0x00]` | epsilon threshold (FLT_MIN = 1.175494350822287508e-38) | evaluate_showdown, evaluate_fold |

---

## 2. SASS Instruction Patterns Decoded

| Instruction | Meaning | Used in |
|-------------|---------|---------|
| `LDG.E.CONSTANT Rd, [a.64]` | Load 32-bit from constant memory | all |
| `LDG.E.U8.CONSTANT Rd, [a.64]` | Load byte from constant memory | evaluate_showdown, evaluate_fold |
| `LDG.E.U16.CONSTANT Rd, [a.64]` | Load halfword from constant memory | evaluate_showdown, evaluate_fold |
| `LDG.E.64.CONSTANT Rd, [a.64]` | Load 64-bit from constant memory | backward_player |
| `LDG.E Rd, [a.64]` | Load 32-bit from global memory | forward_player, backward_opponent |
| `STG.E [a.64], Rs` | Store 32-bit to global memory | all |
| `ULDC.64 URd, c[0x0][N]` | Load 64-bit constant pointer into uniform reg | forward_player, backward_player |
| `ULDC URd, c[0x0][N]` | Load 32-bit constant into uniform reg | train_small_tree_fp32_multi_iter |
| `STS [a], Rs / LDS Rd, [a]` | Shared memory store/load | backward_player, evaluate_showdown |
| `STS.128 [a], RZ / LDS.128` | 128-bit shared memory load/store (clear) | evaluate_showdown |
| `LDL Rd, [a] / STL [a], Rs` | Local memory load/store (register spill) | backward_player, forward_player |
| `IMAD Rd,Ra,Rb,Rc` | Rd = Ra*Rb + Rc (int32) | all (address computation) |
| `IMAD.WIDE Rdest,Ra,Rb,c[N]` | 64-bit address: Rb*stride + base[N] | forward_player, backward_player |
| `IMAD.WIDE.U32` | Wide 64-bit address compute | forward_player, backward_opponent |
| `IMAD.SHL.U32 Rd,Ra,imm,Rc` | Rd = (Ra << imm) + Rc | backward_player |
| `IADD3 Rd,Ra,Rb,Rc` | Rd = Ra+Rb+Rc (3-input add) | all |
| `IADD3.X Rd,Px,Ra,Rb,Pz,!PT` | Conditional 3-input add | backward_opponent, train_small_tree |
| `SHF.R.S32.HI Rd,RZ,imm,Rs` | Shift-right-high (unpack high bits) | all |
| `SHF.L.U64.HI Rd,Ra,imm,Rb` | Shift-left-high (64-bit address) | backward_player, train_small_tree |
| `LEA Rd,Px,Ra,c[N],scale` | Load effective address (32-bit) | forward_player, backward_player |
| `LEA.HI.X Rd,Ra,c[N],Rhi,scale,Px` | Load effective address high 32 bits | forward_player, backward_opponent |
| `PRMT Rd,Rs,0x9910,RZ` | Byte permutation (extract packed card bytes) | evaluate_showdown, evaluate_fold |
| `LOP3.LUT Rd,Ra,Rb,Rc,0xc0,!PT` | `Rd = Rb & 0x3` (extract suit = card & 3) | evaluate_showdown, evaluate_fold |
| `LOP3.LUT Rd,Ra,Rb,Rc,0x33,!PT` | Bitwise LUT for rank/suit extraction | train_small_tree_fp32_multi_iter |
| `I2F.U16 Rd,Rs` | Convert 16-bit int to float (Q16 dequant) | train_small_tree_fp32_multi_iter, backward_opponent |
| `FFMA.FTZ Rd,Ra,Rb,Rc` | Rd = Ra*Rb + Rc (float FTZ) — regret/EV accumulation | backward_player, train_small_tree |
| `FMUL.FTZ Rd,Ra,Rb` | Float multiply (FTZ) | forward_player, evaluate_showdown |
| `FADD.FTZ Rd,Ra,Rb` | Float add (FTZ) | backward_player, backward_opponent |
| `FMNMX.FTZ Rd,Ra,Rb,!PT` | Fused min/max: clamp regrets >= 0 | backward_player, train_small_tree |
| `MUFU.RCP Rd,Rs` | Reciprocal approximation 1/Rs | backward_player, forward_player, train_small_tree |
| `FSEL Rd,Ra,Rb,PT` | Float select PT?Ra:Rb | backward_opponent, train_small_tree |
| `FSETP.GT.FTZ.AND PTx,PTy,Ra,Rb,PTz` | Float compare GT, set predicate | backward_player, backward_opponent |
| `FSETP.NEU.FTZ.AND` | Float compare not-equal/unordered | backward_player, train_small_tree |
| `DADD Rd,Ra,Rb` | Double-precision add | evaluate_showdown |
| `DMUL Rd,Ra,Rb` | Double-precision multiply | evaluate_showdown |
| `DSETP.GEU.AND PT,PT,|Ra|,c[0x2][0x0],PT` | Double compare \|Ra\| >= FLT_MIN | evaluate_showdown |
| `F2F.F64.F32 Rd,Rs` | Float -> double conversion | evaluate_showdown |
| `F2F.F32.F64 Rd,Rs` | Double -> float conversion | evaluate_showdown |
| `BSSY Bn,addr / BSYNC Bn` | Block barrier sync | backward_player, evaluate_showdown |
| `BAR.SYNC.DEFER_BLOCKING 0x0` | Deferred block barrier | train_small_tree_fp32_multi_iter |
| `WARPSYNC 0xffffffff` | Warp sync | train_small_tree_fp32_multi_iter |
| `SHFL.BFLY PT,Rd,Rs,off,0x1f` | Butterfly warp shuffle (tree reduction) | evaluate_showdown, evaluate_fold_3p |
| `ATOMS.CAST.SPIN Rd,[a],Rc,Rv` | Atomic CAS spin lock | evaluate_showdown, evaluate_fold |
| `S2R Rd,SR_TID.X` | threadIdx.x | all |
| `S2R Rd,SR_CTAID.X` | blockIdx.x | all |
| `R2UR Rd,Rs / UR2R Rd,URs` | GPR <-> uniform register move | forward_player, train_small_tree |
| `CS2R Rd,SRZ` | Read special register SRZ | backward_player, backward_opponent |
| `EXIT` | Thread exit | all |
| `BRA addr` | Unconditional branch | all |
| `CALL.REL.NOINC addr` | Relative call (leaf, returns via EXIT) | backward_player, train_small_tree |
| `PLOP3.LUT P0,PT,PT,PT,PT,0x80,0x0` | Predicate LUT for multi-action gating | backward_player, train_small_tree |

---

## 3. Kernel: backward_player — CFR Regret Matching (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_player.txt` (673 instructions)

### 3.1 High-Level Algorithm

```
INPUTS (from constant memory c[0x0][*]):
  reach[a]        — per-action reach probability    c[0x0][0x178]
  actionEVs[a]    — per-action expected value       c[0x0][0x168]
  regrets[a]      — cumulative regrets (in/out)     c[0x0][0x170]
  strategy[a]     — output strategy                 c[0x0][0x160]/c[0x0][0x164]
  numActions      — action count                    c[0x0][0x1a8]
  numPlayers      — player count                    c[0x0][0x184]

STEPS:
  1. Bounds check: if threadIdx.x >= numPlayers -> EXIT
  2. Player loop: check player index vs max 3 players
  3. nodeEV = sum_a strategy[a] * actionEVs[a]      (FFMA.FTZ accumulate, R21)
  4. regret[a] = reach[a] * (actionEVs[a] - nodeEV)  (FFMA.FTZ)
  5. Accumulate regrets[a] += regret[a]              (shared memory STL/LDS)
  6. Barrier sync: BSSY B0 / BSYNC B0
  7. Regret matching:
       posRegret[a] = max(0, regret[a])              (FMNMX.FTZ)
       sumPos = sum_a posRegret[a]
       invSum = 1.0f / sumPos                        (MUFU.RCP)
       strategy[a] = posRegret[a] * invSum           (FMUL.FTZ)
       if sumPos <= 0: strategy[a] = 1/numActions    (uniform fallback)
  8. Barrier sync, then write strategy to global memory (STG.E)
```

### 3.2 Key SASS Instruction Sequences

**Bounds check (lines 0050-00b0):**
```
ISETP.GE.AND P0, PT, R24, c[0x0][0x1c4], PT  ; threadIdx.x >= numPlayers ?
@P0 EXIT                                        ; if so, exit
```

**Node EV accumulation (lines 05b0-06b0):**
```
FSETP.GT.FTZ.AND P2, PT, R23, RZ, PT   ; if actionEV[0] > 0
@P2 FADD.FTZ R21, R23, R21             ; nodeEV += strategy[0]*actionEV[0]
...
FSETP.GT.FTZ.AND P4, PT, R10, RZ, PT   ; if actionEV[3] > 0
@P4 FADD.FTZ R21, R21, R10             ; nodeEV += strategy[3]*actionEV[3]
```

**Regret matching (lines 0ab0-0c60):**
```
@P3 LDL R11, [R13]                     ; load accumulated regret from shared mem
@P3 MUFU.RCP R12, R21                  ; invSum = 1/sumPos
@P3 FMNMX.FTZ R11, RZ, R11, !PT        ; posRegret = max(0, regret)
@P3 FMUL.FTZ R10, R11, R12             ; strategy = posRegret * invSum
STL [R15+0x40], R10                    ; store strategy[0] to shared mem
```

**4-action predicate gating:** Predicates P0-P3 gate conditional stores for actions 0-3 respectively. Each action's reach, EV, regret, and strategy are stored to separate shared-memory offsets ([R19], [R19+0x4], [R19+0x8], [R19+0xc]) before being written to global memory.

---

## 4. Kernel: evaluate_showdown — HU Showdown Evaluation (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown.txt` (457 instructions)

### 4.1 High-Level Algorithm

```
INPUTS:
  board[5]        — board card indices              c[0x0][0x190]/c[0x0][0x170]
  heroCombos[N]   — packed hero hole cards          c[0x0][0x170]
  vilCombos[N]    — packed villain hole cards       c[0x0][0x258]
  numCombos       — number of combo pairs           c[0x0][0x29c]

OUTPUTS:
  heroEV[N]       — hero hand strength (double)     c[0x0][0x268]
  vilEV[N]        — villain hand strength (double)  c[0x0][0x210]

STEPS:
  1. idx = blockIdx.x * gridDim.x + threadIdx.x
  2. Bounds check: if idx >= numCombos -> EXIT
  3. Clear shared memory (12 x 128-bit stores, STS.128 [R8+offset], RZ)
  4. Barrier sync: BSSY B0
  5. Unpack hole cards: c1 = packed & 0xFF, c2 = packed >> 8
  6. Build 7-card hands: board[0..4] + hole[5,6]
  7. Load lookup tables from constant memory (LDG.E.U16.CONSTANT)
  8. Card extraction: PRMT 0x9910 byte shuffle, LOP3.LUT 0xc0 (suit = c & 3)
  9. Dead-card detection: ISETP.NE.AND / ISETP.EQ.OR chains
  10. Compute hand strength via eval7 (max over 21 five-card subsets)
  11. Convert to double: F2F.F64.F32
  12. Denormal flush: if |EV| < FLT_MIN, set to 1.175494350822287508e-38
  13. Shared memory accumulation: STS/LDS with BSSY/BSYNC barriers
  14. Store double EV to global memory: STG.E
```

### 4.2 Key SASS Instruction Sequences

**Thread index and bounds check (lines 0000-0090):**
```
IMAD R9, R9, c[0x0][0x0], R8   ; idx = blockIdx.x * gridDim.x + threadIdx.x
ISETP.GE.AND P0, PT, R9, c[0x0][0x1ec], PT  ; idx >= numCombos?
@P0 EXIT                        ; if so, exit
```

**Shared memory clear (lines 0190-0250):**
```
STS.128 [R8], RZ               ; clear 16 bytes
STS.128 [R8+0x10], RZ          ; ...
... (12 total, 0x60 bytes)
BSSY B0, 0x7a0                 ; barrier sync
```

**Card extraction (lines 0290-0320):**
```
SHF.R.S32.HI R10, RZ, 0x1f, R9   ; unpack high bits
PRMT ... 0x9910                  ; byte shuffle to extract card bytes
LOP3.LUT R7, R0.reuse, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
```

**Double-precision EV with denormal flush (lines 0730-0760):**
```
F2F.F32.F64 R27, R14           ; convert strength to double
DSETP.GEU.AND P0, PT, |R14|, c[0x2][0x0], PT  ; |EV| >= FLT_MIN?
@!P0 FMUL R27, R27, 1.175494350822287508e-38  ; flush to FLT_MIN if denormal
STG.E [R18.64], R27            ; store double EV to global memory
```

---

## 5. Kernel: forward_player — CFR Forward Reach Accumulation (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_player.txt` (145 instructions)

### 5.1 High-Level Algorithm

```
INPUTS:
  reach[h]        — per-hand reach probability      c[0x0][0x178]
  strategy[a]     — per-action strategy             c[0x0][0x170]
  numHands        — hand count                      c[0x0][0x184]
  stride          — hand stride                     c[0x0][0x28]

OUTPUTS:
  reach_out[h*4+a]— per-(hand,action) reach output  c[0x0][0x160]/c[0x0][0x164]

STEPS:
  1. tid = threadIdx.x
  2. Bounds check: if tid >= numHands -> EXIT
  3. Load reach[tid] and strategy via LDG.E.CONSTANT
  4. For each action a (up to 4, predicate-gated P1-P3):
       reach_out[tid*4+a] = reach[tid] * strategy[a]   (FMUL.FTZ)
  5. STG.E to global memory for each action
  6. Loop: increment hand index by stride, repeat until bound
```

### 5.2 Key SASS Instruction Sequences

**Per-action predicate-gated store (lines 0250-05a0):**
```
LDG.E.CONSTANT R4, [R4.64]         ; load reach value
PLOP3.LUT P1, PT, PT, PT, UP0, 0x80, 0x0  ; predicate for action 0
@!P1 BRA 0x5c0                     ; skip if action 0 not valid
HFMA2.MMA R6, -RZ, RZ, 0, 0        ; MMA init
STG.E [R10.64], R4                 ; store reach_out[tid*4+0] = reach * strategy[0]
...
PLOP3.LUT P0, PT, PT, PT, UP1, 0x80, 0x0  ; predicate for action 1
STG.E [R8.64], R4                  ; store reach_out[tid*4+1]
...
PLOP3.LUT P0, PT, PT, PT, UP1, 0x80, 0x0  ; predicate for action 2
STG.E [R18.64], R4                 ; store reach_out[tid*4+2]
...
```

---

## 6. Kernel: train_small_tree_fp32_multi_iter — Multi-Iteration CFR Training (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/train_small_tree_fp32_multi_iter.txt` (~3400+ instructions, largest kernel)

### 6.1 High-Level Algorithm

```
INPUTS:
  reach[a]         — per-action reach                 c[0x0][R37+0x160] (variable offset)
  evs[a]           — per-action EV                    c[0x0][R37+0x168]
  regrets[a]       — cumulative regrets (in/out)      c[0x0][0x170]
  strategy[a]      — output strategy                  c[0x0][0x168]
  discount_params  — 3 floats (iter_params)           c[0x0][R37+0x160]
  numActions       — action count
  numIterations    — CFR iterations per launch

STEPS (per thread, per iteration):
  1. disc = discount_params[0]     (LDC.64 R2, c[0x0][R37+0x160])
  2. nodeEV = sum_a strategy[a] * evs[a]
  3. regret[a] = reach[a] * (evs[a] - nodeEV)
  4. regrets[a] = disc * regrets[a] + (1-disc) * regret[a]   (EMA)
  5. posRegret[a] = max(0, regrets[a])                        (FMNMX.FTZ)
  6. strategy[a] = posRegret[a]                               (unnormalized)
  7. Repeat for numIterations

SASS FEATURES:
  - Variable constant buffer access: c[0x0][R37+offset] (R37 = per-kernel config base)
  - Q16 dequantization: I2F.U16 converts int16 to float
  - Multi-iteration loop with ISETP.GE.AND bounds checks
  - Shared memory tiling with BSSY/BAR.SYNC barriers
  - 64-bit address computation via IMAD.WIDE / SHF.L.U64.HI
  - WARPSYNC 0xffffffff for warp-level sync
```

### 6.2 Key SASS Instruction Sequences

**Discount parameter loading (lines 0610-0620):**
```
LDC.64 R2, c[0x0][R37+0x160]   ; load discount param (iter_params[0])
```

**Per-iteration regret + EMA (lines 0af0-0c60):**
```
FMNMX.FTZ R16, RZ, R20, !PT    ; posRegret = max(0, regret)
FADD.FTZ R20, R16, R31         ; accumulate nodeEV
FMUL.FTZ R12, R10, R13         ; reach * (ev - nodeEV)
; regrets[a] = disc * regrets[a] + (1-disc) * regret
```

---

## 7. Kernel: gather_cfv — Simple Gather/Scatter (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/gather_cfv.txt` (41 instructions)

### 7.1 Algorithm

```
out[tid] = src_table[indices[tid]]
```

One thread per element. Loads index from constant memory, gathers value from table, stores to output. BSSY barrier for block sync.

---

## 8. Kernel: backward_opponent — Opponent Backward Pass (SASS-Exact)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_opponent.txt` (361 instructions)

### 8.1 Algorithm

Mirrors backward_player but for the opponent player. Key differences:
- Uses `LOP3.LUT R5, R2, 0x7, RZ, 0xc0, !PT` for different bit extraction
- Accumulates opponent reach across multiple streets via FADD.FTZ chains
- Handles up to 7 opponent actions (predicates P2-P5 for actions 0-6)
- Shared memory accumulation with BSSY B0/B1 barriers

---

## 9. SASS-to-Pseudocode Summary Table

| Kernel | SASS File | Instructions | Core Algorithm |
|--------|-----------|-------------|----------------|
| `gather_cfv` | gather_cfv.txt | 41 | out[tid] = src_table[indices[tid]] |
| `forward_player` | forward_player.txt | 145 | reach_out[a] = reach * strategy[a] (predicate-gated, up to 4 actions) |
| `forward_opponent` | forward_opponent.txt | ~150 | Opponent reach accumulation across streets |
| `evaluate_showdown` | evaluate_showdown.txt | 457 | HU showdown: eval7(hero) vs eval7(vil), double EV, denormal flush |
| `evaluate_showdown_brute` | evaluate_showdown_brute.txt | ~100 | Brute-force villain-combo/runout enumeration |
| `evaluate_fold` | evaluate_fold.txt | ~200 | Fold equity vs range (double EV, atomic spin) |
| `evaluate_fold_fp32` | evaluate_fold_fp32.txt | ~190 | FP32 fold equity (no double) |
| `evaluate_fold_3p` | evaluate_fold_3p.txt | ~200 | 3-way fold equity (SHFL reduction) |
| `backward_player` | backward_player.txt | 673 | CFR regret matching (4-action, shared mem, MUFU.RCP) |
| `backward_opponent` | backward_opponent.txt | 361 | Opponent backward regret |
| `backward_player_packed` | backward_player_packed.txt | ~230 | Q16 packed backward |
| `backward_player_packed_cached` | backward_player_packed_cached.txt | ~340 | Q16 packed backward + shared mem tiling |
| `train_small_tree_fp32_multi_iter` | train_small_tree_fp32_multi_iter.txt | ~3400 | Multi-iteration CFR + discount EMA |
| `train_fold_showdown_parent_packed` | — | ~460 | Main CFR training (packed Q16) |
| `apply_isomorphism` | apply_isomorphism.txt | ~30 | Tree isomorphism compaction |
| `trim` | trim.txt | ~10 | Tree trimming (tombstone inactive) |
| `replace` | replace.txt | ~20 | Node replacement |

---

## 10. Numerical Constants Observed in SASS

| Constant | Value | Context |
|----------|-------|---------|
| `1.175494350822287508e-38` | FLT_MIN | Denormal flush threshold (c[0x2][0x0]) |
| `0x9910` | PRMT immediate | Packed card byte extraction |
| `0xc0` | LOP3.LUT immediate | `Rb & 0x3` = suit extraction |
| `0x33` | LOP3.LUT immediate | Rank/suit bit extraction |
| `0x4` | IMAD stride | float/int element size |
| `0x100`, `0x80`, `0x1220` | train_small_tree offsets | Constant buffer variable offsets |
| `0x160` | train_small_tree discount offset | iter_params[0] base |
| `15` | eval5 base | Base-15 packed hand rank |
| `21` | eval7 subsets | C(7,5) five-card subsets |
| `1326` | NUM_COMBOS | C(52,2) starting hands |
| `65536.0` | Q16_SCALE | Q16 fixed-point scale |
| `-32768` | Q16_MIN | Q16 invalid sentinel |

---

## 11. Correspondence: SASS ↔ Embedded JS Poker Primitives

| JS Primitive | SASS Kernel | SASS Pattern |
|-------------|-------------|--------------|
| `en(e,t)` combo index | all kernels | IMAD address computation using c[0x0][0x17c] as combo base |
| `oa(cards[5])` 5-card eval | evaluate_showdown | LDG.E.U8/U16.CONSTANT + PRMT 0x9910 + lookup tables |
| `rb(cards[7])` 7-card showdown | evaluate_showdown | Max over 21 subsets via lookup tables + ISETP.EQ.OR chains |
| `TB(e,t,s,a)` pot settlement | evaluate_showdown/evaluate_fold | DADD/DMUL double-precision EV with denormal flush |
| `Kp("AhKs")` hand parser | evaluate_showdown/evaluate_fold | PRMT byte extraction from packed card `c \| (p<<8)` |
| Q16 dequant | train_small_tree/backward_packed | I2F.U16 then divide by 65536.0 |
| Regret matching | backward_player | FMNMX.FTZ clamp + MUFU.RCP + FMUL.FTZ normalize |

---

## 12. Deliverable Status

| Item | Status | Location |
|------|--------|----------|
| SASS instruction reference | Complete | This document (§2) |
| Constant buffer slot map | Complete | This document (§1) |
| backward_player algorithm | Complete (SASS-exact) | This document (§3), cfr_simulator.py |
| evaluate_showdown algorithm | Complete (SASS-exact) | This document (§4), cfr_simulator.py |
| forward_player algorithm | Complete (SASS-exact) | This document (§5), cfr_simulator.py |
| train_small_tree algorithm | Complete (SASS-exact) | This document (§6), cfr_simulator.py |
| gather_cfv algorithm | Complete | This document (§7), cfr_simulator.py |
| backward_opponent algorithm | Complete | This document (§8) |
| Full kernel pseudocode | Complete | CUDA_KERNELS_RECON.cpp |
| Remaining: kernel launch configs | Blocked (dynamic) | Requires cuLaunchKernel hooking |
| Remaining: PTX device code | Blocked (dynamic) | Requires cuModuleLoadData hooking |
