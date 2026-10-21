# TEXAS SOLVER GPU v0.3.0 — SASS Disassembly Reference

**Source:** `KERNEL_CODE/sm_80/<kernel>.sass` (raw SASS machine code, disassembled with `nvdisasm -b SM80`)
**Target:** sm_80 (NVIDIA Ampere, CUDA 13.1 toolchain)
**Disassembler:** `/usr/local/cuda/bin/nvdisasm`

---

## 1. Constant-Buffer Kernel Parameters (`c[0x0][N]`)

All kernels receive parameters via constant-memory bank 0. Each slot `c[0x0][N]` holds a 32-bit value
(pointer, count, or flag). Observed slots (sm_80):

| Slot | Typical Meaning | Kernels |
|------|----------------|---------|
| `c[0x0][0x00]` | grid/loop stride or numCombos base | all |
| `c[0x0][0x28]` | numCombos / numPlayers bound (bounds check) | gather_cfv, forward_player, evaluate_fold |
| `c[0x0][0x118]` | base pointer to data structures (ULDC.64 load) | forward_player, backward_player, forward_opponent |
| `c[0x0][0x160]` | output buffer pointer | backward_player, forward_opponent |
| `c[0x0][0x164]` | output buffer high-word (64-bit addr) | backward_player, forward_opponent |
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
| `c[0x2][0x00]` | epsilon threshold (`1.175494350822287508e-38` = FLT_MIN) | evaluate_showdown, evaluate_fold |

---

## 2. SASS Instruction Patterns (Decoded)

### 2.1 Memory Access

| Instruction | Meaning | Example |
|-------------|---------|---------|
| `LDG.E.CONSTANT Rd, [a.64]` | Load 32-bit from constant memory (global const cache) | `LDG.E.CONSTANT R4, [R4.64]` |
| `LDG.E.U8.CONSTANT Rd, [a.64]` | Load byte (8-bit) from constant memory | `LDG.E.U8.CONSTANT R22, [R4.64]` |
| `LDG.E.U16.CONSTANT Rd, [a.64]` | Load halfword (16-bit) from constant memory | `LDG.E.U16.CONSTANT R22, [R22.64]` |
| `LDG.E Rd, [a.64]` | Load 32-bit from global memory | `LDG.E R9, [R16.64]` |
| `STG.E [a.64], Rs` | Store 32-bit to global memory | `STG.E [R18.64], R27` |
| `ULDC.64 URd, c[0x0][N]` | Load 64-bit constant (pointer) into uniform reg | `ULDC.64 UR4, c[0x0][0x118]` |
| `STS [a], Rs` | Store to shared memory | `STS [R7.X4], R10` |
| `LDS Rd, [a]` | Load from shared memory | `LDS R6, [R17]` |
| `LDS.64 / LDS.128` | 64/128-bit shared memory load | `LDS.128 R8, [R17.X8+0x1b0]` |
| `STS.64 / STS.128` | 64/128-bit shared memory store | `STS.128 [R8], RZ` |
| `LDL Rd, [a]` | Load from local memory (register spill) | `LDL R8, [R20]` |
| `STL [a], Rs` | Store to local memory | `STL [R19], R23` |

### 2.2 Address Computation

| Instruction | Meaning |
|-------------|---------|
| `IMAD.WIDE Rdest, Ra, Rb, c[0x0][N]` | 64-bit address: `Rb * stride + base[N]` |
| `LEA Rd, Px, Ra, c[0x0][N], scale` | Load effective address (32-bit) |
| `LEA.HI.X Rd, Ra, c[0x0][N], Rhi, scale, Px` | Load effective address high 32-bits (64-bit addr) |
| `LEA.HI.X.SX32` | Signed 32-extended high address |
| `IMAD Rd, Ra, Rb, Rc` | `Rd = Ra * Rb + Rc` (int32) |
| `IMAD.WIDE.U32` | Wide 64-bit address compute |
| `IMAD.SHL.U32 Rd, Ra, imm, Rc` | `Rd = (Ra << imm) + Rc` |
| `IMAD.HI.U32` | High 32-bits of 64-bit multiply |
| `IADD3 Rd, Ra, Rb, Rc` | `Rd = Ra + Rb + Rc` (3-input add) |
| `IADD3.IADD Rd, imm, Rc` | `Rd = Ra + imm + Rc` (immediate fold) |
| `IADD3.X Rd, Px, Ra, Rb, Pz, !PT` | Conditional 3-input add |
| `SHF.R.S32.HI Rd, RZ, imm, Rs` | Shift right high (unpack high bits) |
| `SHF.L.U64.HI Rd, Ra, imm, Rb` | Shift left high (64-bit address) |

### 2.3 Card Unpacking (Packed Cards `c \| (p << 8)`)

The SASS uses `PRMT ... 0x9910` and `LOP3.LUT ... 0xc0` to extract bytes/nibbles from packed card data:

| Instruction | Meaning | Decoded |
|-------------|---------|---------|
| `PRMT Rd, Rs, 0x9910, RZ` | Byte permutation — extract individual bytes from 32-bit packed value | used repeatedly in evaluate_showdown/evaluate_fold |
| `LOP3.LUT Rd, Ra, Rb, Rc, 0xc0, !PT` | `Rd = Rb & 0x3` → extract **suit** from card (`c & 3`) | `LOP3.LUT R7, R0.reuse, 0x3, RZ, 0xc0` |
| `LOP3.LUT Rd, Ra, Rb, Rc, 0x33, !PT` | Bitwise LUT for rank/suit extraction | `LOP3.LUT R4, RZ, R20, RZ, 0x33` |
| `I2F.U16 Rd, Rs` | Convert 16-bit int to float | card value → float |
| `I2F.U32.RP Rd, c[0x0][0x0]` | Convert uint to float (round positive) | int count → float |

Card encoding: `rank = c >> 2` (0=2 .. 12=A), `suit = c & 3` (0=c,1=d,2=h,3=s).

### 2.4 Arithmetic

| Instruction | Meaning |
|-------------|---------|
| `FFMA.FTZ Rd, Ra, Rb, Rc` | **Fused multiply-add**: `Rd = Ra * Rb + Rc` (float, flush-to-zero) — heavily used in backward_player for `regret = reach * (ev - nodeEV)` |
| `FMUL.FTZ Rd, Ra, Rb` | Float multiply (FTZ) |
| `FADD.FTZ Rd, Ra, Rb` | Float add (FTZ) |
| `FMNMX.FTZ Rd, Ra, Rb, !PT` | Fused min/max: `Rd = PT ? min/max(Ra,Rb) : ...` — used for clamping regrets to >= 0 |
| `MUFU.RCP Rd, Rs` | Reciprocal approximation: `Rd = 1.0f / Rs` — used for division |
| `FSEL Rd, Ra, Rb, PT` | Float select: `Rd = PT ? Ra : Rb` — conditional move |
| `FSETP.GT.FTZ.AND PTx, PTy, Ra, Rb, PTz` | Float compare: set predicate if `Ra > Rb` |
| `FSETP.NEU.FTZ.AND` | Float compare: not-equal / unordered |
| `DADD Rd, Ra, Rb` | Double-precision add |
| `DMUL Rd, Ra, Rb` | Double-precision multiply |
| `DSETP.GEU.AND PT, PT, |Ra|, c[0x2][0x0], PT` | Double compare: `|Ra| >= epsilon` (denormal check) |
| `F2F.F64.F32 Rd, Rs` | Float → double conversion |
| `F2F.F32.F64 Rd, Rs` | Double → float conversion |

### 2.5 Control Flow & Synchronization

| Instruction | Meaning |
|-------------|---------|
| `BSSY Bn, addr` | Block barrier sync (arrive at barrier `Bn`, jump if not all arrived) |
| `BSYNC Bn` | Block barrier sync (wait) |
| `BAR.SYNC.DEFER_BLOCKING 0x0` | Deferred block barrier |
| `SHFL.BFLY PT, Rd, Rs, offset, 0x1f` | Butterfly warp shuffle (tree reduction across warp) |
| `ATOMS.CAST.SPIN Rd, [a], Rcmp, Rval` | Atomic compare-and-swap spin lock — used for shared-memory accumulation |
| `ISETP.*` / `PLOP3.LUT` | Integer/predicate set + predicate LUT |
| `CS2R Rd, SRZ` | Read special register SRZ |
| `S2R Rd, SR_TID.X` | `threadIdx.x` |
| `S2R Rd, SR_CTAID.X` | `blockIdx.x` |
| `R2UR Rd, Rs` / `UR2R Rd, URs` | GPR ↔ uniform register move |
| `UIMOV URd, imm` | Uniform register immediate move |
| `UIADD3 URd, Ra, Rb, URc` | Uniform register 3-input add |
| `UIMAD URd, Ra, Rb, URc` | Uniform register multiply-add |
| `USHF.R.S32.HI` / `USHF.L.U32` | Uniform register shift |
| `EXIT` | Thread exit |
| `BRA addr` | Unconditional branch |
| `CALL.REL.NOINC addr` | Relative call (no increment — leaf function, returns via EXIT) |
| `NOP` | No-op (padding at end of kernel) |

### 2.6 Predicates

- `P0`–`P7`: regular predicates
- `PT`: predicate true (always-set)
- `UP0`–`UP1`: uniform predicates
- `UPT`: uniform predicate true
- `@P0 BRA addr` / `@!P0 BRA addr` : branch if predicate set / not set
- `@P0 STG.E ...` : conditional store

---

## 3. Kernel-by-Kernel SASS Summary (sm_80)

| Kernel | Code Size | Key SASS Patterns | Logic |
|--------|-----------|-------------------|-------|
| `gather_cfv` | 640 B | LDG.E.CONSTANT, STG.E, BSSY | Simple gather/scatter |
| `forward_player` | 1664 B | ULDC.64, MUFU.RCP, FMUL.FTZ, STG.E | CFR forward reach accumulation |
| `forward_opponent` | 7680 B | PRMT 0x9910, FFMA.FTZ, FSEL, MUFU.RCP | Opponent forward reach across streets |
| `evaluate_showdown` | 9216 B | LDG.E.U8/U16, PRMT, DADD/DMUL, SHFL.BFLY, ATOMS.CAST.SPIN | HU showdown eval, double-precision EV |
| `evaluate_showdown_brute` | 5120 B | lookup tables, card-equality chains | Brute-force equity enumeration |
| `evaluate_showdown_prefix` | 42368-46592 B | extensive shared memory, DP EV | Prefix board showdown (largest eval kernel) |
| `evaluate_fold` | 10496 B | LDG.E.U8, ISETP.EQ.OR chains, DADD, SHFL.BFLY, ATOMS.CAST.SPIN | Fold equity vs range |
| `evaluate_fold_fp32` | 9600 B | float-only (no DP) | FP32 fold equity |
| `evaluate_fold_3p` | 10368 B | SHFL reduction, nested combo loops | 3-way fold equity |
| `backward_player` | 11264 B | FFMA.FTZ, MUFU.RCP, FMNMX, FSEL, LDL/STL | CFR backward regret computation |
| `backward_player_cfv` | 6144 B | FFMA.FTZ | CFV backward |
| `backward_player_cfv_packed` | 7168 B | LDG.E.U16, Q16 dequant | Packed Q16 CFV |
| `backward_player_packed` | 11392-25088 B | Q16 packed, shared mem tiling | Packed backward |
| `backward_opponent` | 2816-3456 B | FFMA, FSETP | Opponent backward regret |
| `backward_opponent_boundary_iso` | 6016 B | iso_map permutation | Boundary opponent with isomorphism |
| `backward_br_player` | 3456 B | simple FFMA | Betting-round backward |
| `forward_chance` | 1664-2816 B | simple FMUL | Chance forward |
| `forward_chance_tr_2d` / `_batched` | 3072 B | FMUL chain | Turn+river chance |
| `backward_chance_abstract` / `tr_2d` / `turn_river` | 2176-6656 B | FMUL chain | Chance backward |
| `forward_river_subtrees_packed` | 17280-18688 B | Q16 packed | River subtree forward |
| `apply_isomorphism` / `_permuted` | 1280-2304 B | LDS/STS X4, BSSY, PRMT | Tree isomorphism compaction |
| `encode_stage_segments_i16` | 3712-4096 B | Q16 pack | Stage segment encoding |
| `train_fold_showdown_parent_packed` | 23040-27776 B | Q16, FFMA, FSEL | Main CFR training (packed) |
| `train_opponent_fold_showdown_parent_packed` | 24704-27776 B | Q16 | Opponent training |
| `train_mixed_fold_showdown_parent_packed` | 6912-7680 B | Q16 | Mixed training |
| `train_mixed_opponent_fold_showdown_parent` | 26240-29440 B | float | Mixed opponent training |
| `train_small_tree_fp32_multi_iter` | 46208-52096 B | FFMA, MUFU, FSEL, shared mem, multi-iter loop | Largest kernel: multi-iteration CFR |

---

## 4. How to Disassemble More Kernels

```bash
# Disassemble a raw SASS binary for a specific architecture
nvdisasm -b SM80 KERNEL_CODE/sm_80/<kernel>.sass

# Available architectures: SM75, SM80, SM86, SM89, SM90, SM120
# All kernels exist in KERNEL_CODE/<arch>/<kernel>.sass (45 kernels per arch)

# Full disassembly of all sm_80 kernels:
for k in KERNEL_CODE/sm_80/*.sass; do
  nvdisasm -b SM80 "$k" > "${k%.sass}.disasm"
done
```

---

## 5. Verification Against Embedded JS Poker Primitives

The SASS-derived kernels match the embedded Vue 3 poker primitives found in the PE binary's `.rdata` section:

- `en(e,t)` combo index → SASS `IMAD` address computation using `c[0x0][0x17c]` as combo base
- `oa(cards[5])` 5-card eval → SASS lookup-table driven via `LDG.E.U8/U16.CONSTANT` + `PRMT 0x9910`
- `rb(cards[7])` 7-card showdown → SASS `eval7` equivalent (max over 21 subsets) implemented via lookup tables + `ISETP.EQ.OR` chains
- `TB(e,t,s,a)` pot settlement → matches `settle_pot()` in recon
- `Kp("AhKs")` hand parser → SASS `PRMT` byte extraction from packed card `c \| (p << 8)`

The double-precision `DADD`/`DMUL`/`DSETP.GEU` pattern in `evaluate_showdown` and `evaluate_fold` corresponds to the JS `TB()` function's precise EV computation with denormal-flush handling (`1.175494350822287508e-38` = FLT_MIN threshold loaded from `c[0x2][0x0]`).
