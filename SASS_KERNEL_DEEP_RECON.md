# TexasSolverGPU v0.3.0 — SASS Kernel Deep Reverse Engineering

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Architecture:** sm_80 SASS disassembly (via `nvdisasm -b SM80`)
**Location:** `/tmp/opencode/sass_disasm/sm_80/` (45 files) + `/tmp/opencode/sass_disasm/*.txt` (22 sm_80 files)

---

## 1. NEWLY DOCUMENTED KERNELS

The following kernels were NOT covered in `FINAL_RECON_STATE.md` §12. Detailed SASS-level algorithmic reconstruction below.

---

## 2. backward_player_cfv_packed (sm_80, 601 instructions)

**File:** `/tmp/opencode/sass_disasm/backward_player_cfv_packed.txt`

**Role:** CFR backward pass for a player at a node, with packed (Q16) counterfactual value storage.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]      ; R1 = constant slot 0x28 (likely num_players or stride)
S2R R8, SR_CTAID.X                          ; R8 = blockIdx.x
S2R R9, SR_TID.X                            ; R9 = threadIdx.x
ISETP.GE.AND P2, PT, R9, c[0x0][0x1d4], PT  ; P2 = (threadIdx.x >= max_threads)
@P2 EXIT                                     ; exit if out of bounds
```

### Constant Buffer Loads (constant buffer slot c[0x0])
- `c[0x0][0x118]` — ULDC.64 UR18 — 64-bit pointer (likely d_cfv_ or d_abs_regrets_ base)
- `c[0x0][0x1d8]` — num_actions or action count
- `c[0x0][0x1b8]` — pointer table base (array of device pointers)
- `c[0x0][0x1bc]` — pointer table base 2
- `c[0x0][0x1b0]` — pointer table base 3
- `c[0x0][0x1c8]` — pointer table base 4
- `c[0x0][0x1c0]` — pointer table base 5
- `c[0x0][0x1cc]` — pointer table base 6
- `c[0x0][0x1c4]` — pointer table base 7
- `c[0x0][0x1b4]` — pointer table base 8
- `c[0x0][0x1d4]` — max thread count / grid bound
- `c[0x0][0x1a8]` — pointer table base 9
- `c[0x0][0x1ac]` — pointer table base 10
- `c[0x0][0x1d0]` — pointer table base 11 (wide address computation)
- `c[0x0][0x198]` — pointer table base 12
- `c[0x0][0x170]` — CFV/reach data array base
- `c[0x0][0x174]` — CFV/reach data array base 2
- `c[0x0][0x178]` — CFV/reach data array base 3
- `c[0x0][0x17c]` — CFV/reach data array base 4

### Card Extraction (packed card format: c | (p << 8))
```
LDG.E.U8.CONSTANT R6, [R6.64]              ; load 1 byte (card data)
R2UR UR13, R6                               ; move to uniform register
I2F.U16 R8, UR13                            ; Q16 dequantization: int16 -> float
LOP3.LUT UR16, UR13, 0x3, URZ, 0xc0, !UPT   ; suit = R13 & 0x3
```

### Regret Computation Loop (up to 6 actions via predicates P0-P5)
- `MUFU.RCP R8, R8` — reciprocal approximation (1.0f / value) for normalization
- `FMNMX.FTZ` — clamp regrets to >= 0 (max(0, regret))
- `FADD.FTZ R24, R26, R3` — accumulate: node_EV += strategy[a] * action_EV[a]
- `FFMA.FTZ R23, R9, R27, R23` — fused multiply-add for EV accumulation
- `FMUL.FTZ R27, R24, R29` — normalize: pos_regret / sum_pos

### Shared Memory & Barriers
```
BSSY B0, 0x24e0                             ; block barrier (shared memory sync)
BSYNC B0                                    ; barrier wait
STL [R22], R26                              ; shared memory store (32-bit)
STL [R22+0x4], R19                          ; shared memory store (offset 4)
STL [R22+0x8], R25                          ; shared memory store (offset 8)
STL [R22+0xc], R25                          ; shared memory store (offset 12)
```

### Global Memory Stores
```
STG.E [R20.64], R9                          ; store float to global memory
STG.E [R26.64], R24                         ; store float to global memory
```

### Algorithm Summary
```
for each player p (thread-mapped):
    node_EV = 0
    for each action a (up to 6, predicated):
        action_EV = load_cfv_child(p, a)      ; global memory load
        reach = load_reach(p)                  ; Q16 dequantized
        regret[p][a] = reach * (action_EV - node_EV)
        node_EV += strategy[a] * action_EV
    sum_pos = sum(max(0, regret[p][a]) for all a)
    inv_sum_pos = 1.0f / sum_pos               ; MUFU.RCP
    for each action a:
        strategy[a] = max(0, regret[p][a]) * inv_sum_pos
    store strategy[p][*] to global memory
    store node_EV to shared memory
__syncthreads()
```

---

## 3. backward_player_packed (sm_80, ~790 instructions)

**File:** `/tmp/opencode/sass_disasm/backward_player_packed.txt`

**Role:** CFR backward pass with packed S16 (signed 16-bit) fixed-point storage for regrets.

### Key Differences from cfv_packed Variant
- Uses `LDG.E.U16.CONSTANT` — loads 16-bit values from constant memory
- `PRMT R0, R24, 0x9910, RZ` — card byte extraction from packed `c | (p << 8)`
- `ISETP.NE.AND P0, PT, R0, -0x1, PT` — dead card check (card == 0xFFFF = -1)
- `I2F.S16 R45, R42` — signed 16-bit to float conversion (Q16 signed)
- `FSETP.GT.FTZ.AND P6, PT, R45.reuse, RZ, PT` — check if value > 0
- `STL [R41], R45` — shared memory store (S16 value)
- `FADD.FTZ R44, R45, R44` — regret accumulation

### Action Loop Structure
- Processes 8 actions per iteration loop (R31 incremented by 8 each iteration)
- `LDG.E.S16.CONSTANT R42, [R24.64]` — loads signed 16-bit from constant memory
- Uses `LDL` for 64-bit loads from shared memory
- Complex constant memory addressing with `c[0x0][0x178]`, `c[0x0][0x17c]`, `c[0x0][0x1f8]`

### Algorithm Summary
```
for each player p (thread-mapped):
    for each action block of 8:
        load 8 S16 regrets from constant memory
        for each action a in block:
            if card != -1 (not dead):
                regret[a] = S16_to_float(value)
                if regret[a] > 0:
                    sum_pos += regret[a]
        __syncthreads()
    inv_sum_pos = 1.0f / sum_pos
    for each action a:
        strategy[a] = max(0, regret[a]) * inv_sum_pos
    store packed S16 strategy to global memory
```

---

## 4. backward_opponent (sm_80, 361 instructions)

**File:** `/tmp/opencode/sass_disasm/backward_opponent.txt`

**Role:** CFR backward pass for opponent modeling at chance/terminal nodes.

### Thread Setup
```
S2R R12, SR_TID.X                          ; threadIdx.x
S2R R10, SR_CTAID.X                        ; blockIdx.x
ISETP.GE.AND P0, PT, R12, c[0x0][0x184], PT ; bounds check vs max_threads
@P0 EXIT
I2F R0, c[0x0][0x178]                      ; num_players -> float
MUFU.RCP R0, R0                            ; 1.0f / num_players (chance weight)
```

### Card & Chance Outcome Processing
- `LDG.E.U16.CONSTANT R4, [R4.64]` — loads 16-bit card data from constant memory
- `PRMT R7, R4, 0x9910, RZ` — card byte extraction
- `ISETP.NE.AND P5, PT, R7, -0x1, PT` — dead card check
- Multiple loops over opponent hands/outcomes (predicates P0-P5)

### CFV Averaging
```
FADD.FTZ R14, R14, R5                      ; sum child CFV (outcome 0)
FADD.FTZ R14, R14, R9                      ; sum child CFV (outcome 1)
FADD.FTZ R14, R14, R3                      ; sum child CFV (outcome 2)
FADD.FTZ R14, R14, R7                      ; sum child CFV (outcome 3)
BSSY B0, 0x850 / BSYNC B0                  ; block synchronization
FMUL.FTZ R3, R0, R14                       ; average: sum * (1/num_outcomes)
STG.E [R4.64], R3                          ; store averaged CFV
```

### Algorithm Summary
```
for each opponent chance node (thread-mapped):
    sum = 0
    for each chance outcome o (card deal):
        if card != -1 (valid):
            child_cfv = load_cfv(opponent, o)
            sum += child_cfv
    __syncthreads()
    avg_cfv = sum * (1.0f / num_outcomes)    ; uniform chance weighting
    store avg_cfv to global memory
```

---

## 5. backward_chance_abstract (sm_80, 161 instructions)

**File:** `/tmp/opencode/sass_disasm/backward_chance_abstract.txt`

**Role:** Chance node backward pass for abstracted games (reduced chance outcomes).

### Thread Setup
```
S2R R12, SR_TID.X
ISETP.GE.AND P0, PT, R12, c[0x0][0x184], PT ; bounds check
@P0 EXIT
I2F R0, c[0x0][0x178]                      ; num_outcomes -> float
MUFU.RCP R0, R0                            ; 1.0f / num_outcomes
```

### Card Loading (16-bit from constant memory)
```
LDG.E.U16.CONSTANT R4, [R4.64]             ; card 0
LDG.E.U16.CONSTANT R18, [R18.64]           ; card 1
LDG.E.U16.CONSTANT R6, [R8.64]             ; card 2
LDG.E.U16.CONSTANT R2, [R2.64]             ; card 3
PRMT R7, R4, 0x9910, RZ                    ; card byte extraction
PRMT R17, R18, 0x9910, RZ
PRMT R5, R6, 0x9910, RZ
PRMT R3, R2, 0x9910, RZ
ISETP.NE.AND P5, PT, R7, -0x1, PT          ; dead card check (card 0)
ISETP.NE.AND P4, PT, R7, -0x1, PT          ; dead card check (card 1)
ISETP.NE.AND P2, PT, R5, -0x1, PT          ; dead card check (card 2)
ISETP.NE.AND P3, PT, R3, -0x1, PT          ; dead card check (card 3)
```

### CFV Summation & Averaging
```
@P5 FADD.FTZ R14, R14, R5                  ; add CFV if card 0 valid
@P4 FADD.FTZ R14, R14, R9                  ; add CFV if card 1 valid
@P2 FADD.FTZ R14, R14, R3                  ; add CFV if card 2 valid
@P3 FADD.FTZ R14, R14, R7                  ; add CFV if card 3 valid
BSSY B0, 0x850 / BSYNC B0                  ; block sync
FMUL.FTZ R3, R0, R14                       ; multiply by 1/num_outcomes
STG.E [R4.64], R3                          ; store averaged CFV
```

### Algorithm Summary
```
for each chance node (thread-mapped):
    sum = 0
    for each of 4 possible chance outcomes:
        if card != -1 (valid outcome):
            sum += load_cfv(outcome)
    __syncthreads()
    parent_cfv = sum * (1.0f / num_valid_outcomes)
    store parent_cfv to global memory
```

---

## 6. forward_opponent (sm_80, 505 instructions)

**File:** `/tmp/opencode/sass_disasm/forward_opponent.txt`

**Role:** CFR forward pass — opponent reach probability propagation.

### Thread Setup
```
S2R R14, SR_TID.X                          ; threadIdx.x
S2R R0, SR_CTAID.X                         ; blockIdx.x
ISETP.GE.AND P0, PT, R14, c[0x0][0x1b4], PT ; bounds check
@P0 EXIT
MUFU.RCP R0, R0                            ; reciprocal for normalization
```

### Reach Propagation (per action)
- `FMUL.FTZ R7, R20, R7` — reach_out = reach * strategy[a]
- `FMUL.FTZ R17, R7, R8` — scale by normalization factor
- `FMUL.FTZ R14, R2, 3.0518509447574615479e-05` — Q16 scale factor (1/65536 * 2)
- `FMNMX.FTZ R9, RZ, R9, !PT` — clamp to >= 0 (max(0, value))

### Shared Memory (128-bit vector stores)
```
STL.128 [R0], R8                           ; store 4 floats to shared memory
STL.128 [R0+0x10], R8                      ; store next 4 floats
STL.128 [R0+0x20], R8                      ; store next 4 floats
STL.128 [R0+0x30], R8                      ; store next 4 floats
BSYNC B0                                    ; block barrier
```

### Warp-Level Reduction
```
SHFL.DOWN PT, R3, R16, 0x10, 0x1f          ; warp shuffle down by 16
FMNMX.FTZ R3, R3, R16, !PT                  ; min/max reduction step 1
SHFL.DOWN PT, R2, R3, 0x8, 0x1f             ; warp shuffle down by 8
FMNMX.FTZ R2, R3, R2, !PT                   ; reduction step 2
SHFL.DOWN PT, R7, R2, 0x4, 0x1f             ; warp shuffle down by 4
FMNMX.FTZ R7, R2, R7, !PT                   ; reduction step 3
SHFL.DOWN PT, R4, R5, 0x4, 0x1f             ; warp shuffle down by 4
FMNMX.FTZ R5, R0, R5, !PT                   ; reduction step 4
```

### Normalization with Epsilon Check
```
FSETP.GT.FTZ.AND P2, PT, R9, 1.0000000116860974231e-07, PT ; epsilon > ~1e-7
FSEL R27, R9, RZ, P2                        ; conditional: if epsilon, use value, else 0
FMUL.FTZ R27, R18, R9                       ; normalize by sum
MUFU.RCP R11, R17                           ; reciprocal for division
FMUL.FTZ R9, R8, R11                        ; multiply by reciprocal
```

### Algorithm Summary
```
for each opponent node (thread-mapped):
    for each action a:
        reach_out[a] = reach * strategy[a]
        reach_out[a] = max(0, reach_out[a])     ; FMNMX clamp
    sum = warp_reduce_sum(reach_out[*])        ; SHFL.DOWN reduction
    if sum > epsilon:
        for each action a:
            reach_out[a] = reach_out[a] / sum   ; MUFU.RCP + FMUL
    store reach_out[*] to global memory
    __syncthreads()
```

---

## 7. apply_isomorphism (sm_80, 97 instructions)

**File:** `/tmp/opencode/sass_disasm/apply_isomorphism.txt`

**Role:** Tree isomorphism compaction — swaps isomorphic child subtrees and accumulates values.

### Thread Setup
```
S2R R8, SR_CTAID.X                         ; blockIdx.x
ISETP.GE.AND P0, PT, R0, 0x1, PT            ; check if work exists
@!P0 EXIT                                   ; exit if no work
BSSY B0, 0x2c0 / BSYNC B0                   ; block barrier
```

### Isomorphism Swap (shared memory)
```
LDS R15, [R10.X4]                           ; load child A pointer from shared mem
LDS R13, [R8.X4]                            ; load child B pointer from shared mem
STS [R8.X4], R15                            ; swap: child B <- child A
STS [R10.X4], R13                           ; swap: child A <- child B
```

### Value Accumulation
```
FADD.FTZ R9, R8, R9                         ; accumulate CFV/regret values
STG.E [R6.64], R9                           ; store accumulated value to global
```

### Loop Over Isomorphism Groups
```
UIADD3 UR4, UR4, 0x1, URZ                   ; increment group counter
BSSY B0, 0x510 / BAR.SYNC.DEFER_BLOCKING 0x0 ; block sync
@!P1 BRA 0x100                               ; loop back if more groups
EXIT
```

### Algorithm Summary
```
for each isomorphism group (block-mapped):
    load child pointers from shared memory
    if subtrees are isomorphic:
        swap child pointers in shared memory
        accumulate values from duplicate subtrees
    __syncthreads()
store compacted tree values to global memory
```

---

## 8. encode_stage_segments_i16 (sm_80, 249 lines)

**File:** `/tmp/opencode/sass_disasm/encode_stage_segments_i16.txt`

**Role:** Encodes floating-point stage segments into 16-bit fixed-point (i16) format for compact archive storage.

### Warp Max Reduction (finds max absolute value)
```
FMNMX.FTZ R2, |R5|, R2, !PT                 ; max(|value|, current_max)
SHFL.DOWN PT, R5, R2, 0x10, 0x1f            ; warp shuffle reduction step 16
FMNMX.FTZ R5, R5, R2, !PT
SHFL.DOWN PT, R4, R5, 0x8, 0x1f             ; step 8
FMNMX.FTZ R4, R5, R4, !PT
SHFL.DOWN PT, R7, R4, 0x4, 0x1f             ; step 4
FMNMX.FTZ R7, R4, R7, !PT
SHFL.DOWN PT, R6, R7, 0x2, 0x1f             ; step 2
FMNMX.FTZ R6, R7, R6, !PT
SHFL.DOWN PT, R9, R6, 0x1, 0x1f             ; step 1
FMNMX.FTZ R9, R6, R9, !PT
STS [R8.X4], R9                             ; store max to shared memory
```

### Scale Computation (Newton-Raphson refinement)
```
MUFU.RCP R4, R7                             ; initial reciprocal: 1.0 / max_abs
FFMA R5, -R7, R4, 1                         ; refinement: 1 - scale*max (FMA)
FFMA R5, R4, R5, R4                         ; refinement: scale * (1 - scale*max) + scale
FFMA R0, R5, 32767, RZ                      ; scale to int16 range
FFMA R4, -R7, R0, 32767                     ; clamp check
```

### Float16 Encoding (special cases for zero, inf, denormals)
```
FSETP.NEU.FTZ.AND P0, PT, R19.reuse, RZ, PT ; check for zero
UMOV UR4, 0x46fffe00                        ; float16 max value bits
FSEL R7, R19, 1, P0                         ; select scale or 1
FCHK P0, R6, R7                             ; check for NaN/Inf
FFMA R5, -R7, R4, 1                         ; compute scaled value
FFMA R5, R4, R5, R4                         ; refine
FFMA R0, R5, 32767, RZ                      ; scale to int16 range
F2I.TRUNC.NTZ R7, R6                        ; float -> int truncate
STG.E.U16 [R4.64], R7                       ; store int16 to global memory
```

### Error Handling Path
```
CALL.REL.NOINC 0xb60                        ; error handler (NaN/Inf)
MOV R0, 0x9f0                                ; error code
STS [0x80], R0                               ; store error status
```

### Algorithm Summary
```
for each value segment (warp-mapped):
    max_abs = warp_reduce_max(|values[*])    ; SHFL.DOWN reduction
    scale = 1.0f / max_abs                    ; MUFU.RCP + Newton-Raphson (FFMA)
    if max_abs == 0 or NaN/Inf:
        handle error case
    else:
        for each value in segment:
            i16_value = truncate(value * scale * 32767)
            store i16_value to global memory
```

---

## 9. gather_cfv (sm_80, 41 instructions)

**File:** `/tmp/opencode/sass_disasm/gather_cfv.txt`

**Role:** Simple gather/scatter — gathers counterfactual values from a source table using an index array.

```
S2R R11, SR_TID.X                           ; threadIdx.x
ISETP.GE.AND P0, PT, R11, c[0x0][0x17c], PT ; bounds check
@P0 EXIT
LDG.E.CONSTANT R7, [R6.64]                  ; load index value from table
STG.E [R4.64], R7                           ; out[tid] = src_table[indices[tid]]
BSSY B0                                      ; block barrier
EXIT
```

### Algorithm Summary
```
out[threadIdx.x] = src_table[indices[threadIdx.x]]
```

---

## 10. evaluate_fold (sm_80, 665 lines) — Partial Analysis

**File:** `/tmp/opencode/sass_disasm/evaluate_fold.txt`

**Role:** Fold equity evaluation — computes EV when opponent folds.

### Thread Setup
```
S2R R2, SR_CTAID.X
S2R R20, SR_TID.X
ISETP.GE.AND P0, PT, R20, c[0x0][0x0], PT  ; bounds check
```

### Card Loading
```
LDG.E.U8.CONSTANT R22, [R4.64]              ; board card 0
LDG.E.U8.CONSTANT R21, [R6.64]              ; board card 1
PRMT R12, R3, 0x9910, RZ                    ; card byte extraction
PRMT R11, R2, 0x9910, RZ
PRMT R10, R0, 0x9910, RZ
```

### Shared Memory & Sync
```
STS.64 [RZ], RZ                              ; clear shared memory
BSSY B0, 0x4e0 / BSYNC B0                    ; block barrier
BSSY B1, 0x3d0 / BSYNC B1                    ; block barrier
```

### Reciprocal & Fold EV
```
I2F.U32.RP R5, c[0x0][0x0]                   ; int -> float (round positive)
MUFU.RCP R5, R5                              ; 1.0f / count
FSEL R8, R8, 255, !P1                        ; conditional select
FSEL R8, R8, 16383, P2
FSEL R8, R8, 65535, !P3
```

---

## 11. train_small_tree_fp32_multi_iter (sm_80, 3401 instructions) — Partial Analysis

**File:** `/tmp/opencode/sass_disasm/train_small_tree_fp32_multi_iter.txt`

**Role:** Multi-iteration CFR training kernel — largest kernel (~50 KB SASS code).

### Variable Constant Buffer Access
```
LDC.64 R10, c[0x0][R37+0x268]              ; 64-bit load with variable offset R37
LDC R14, c[0x0][R37+0x168]                  ; 32-bit load with variable offset
LDC.64 R2, c[0x0][R37+0x1f8]               ; 64-bit load with variable offset
LDC R15, c[0x0][R37+0x270]                 ; 32-bit load with variable offset
```
**R37** is a register holding a variable offset into constant buffer slot 0 — enables dynamic parameter access per iteration.

### Multi-Iteration Loop
```
IMAD.MOV.U32 R37, RZ, RZ, 0x30             ; R37 = 0x30 (iteration stride)
ISETP.GE.AND P0, PT, R4, R11, PT            ; loop bound check
@P0 BRA 0x530                                ; loop back
BSYNC B0                                     ; block sync per iteration
```

### Q16 Dequantization
```
I2F.U16 R7, R4                              ; unsigned 16-bit -> float
```

### Card Extraction
```
LOP3.LUT R12, R4.reuse, 0x3, RZ, 0xc0, !PT ; suit = R4 & 3
```

### EV Accumulation (FFMA chains)
```
FFMA.FTZ R31, R30, R39, R31                ; EV += action_EV * strategy
FFMA.FTZ R31, R40, R41, R31                ; EV += next action
FMNMX.FTZ R16, RZ, R20, !PT                ; clamp to >= 0
FADD.FTZ R20, R16, R31                     ; accumulate
```

### Shared Memory (128-bit stores)
```
STL.128 [R38], R16                          ; store 4 floats to shared mem
```

### Algorithm Summary (partial)
```
for each iteration (outer loop):
    load discount_params from constant memory (variable offset)
    for each node (thread-mapped):
        for each action:
            load Q16 reach, strategy, action_EV
            dequantize: I2F.U16
            compute: reach * action_EV (FFMA)
            accumulate node_EV (FADD)
        compute regret: reach * (action_EV - node_EV)
        accumulate regret into shared memory (STL.128)
    __syncthreads()
    update strategy via regret matching (FMNMX, MUFU.RCP, FMUL)
    store updated strategy to global memory
```

---

## 12. SUMMARY: KERNEL ALGORITHMIC COVERAGE

| Kernel | Documented | SASS Lines | Key Algorithms |
|--------|-----------|------------|----------------|
| backward_player | ✅ (FINAL_RECON) | 673 | Regret matching, FMNMX, MUFU.RCP |
| backward_player_cfv | ❌ | — | Not yet disassembled |
| backward_player_cfv_packed | ✅ | 601 | Packed CFV, Q16, regret matching |
| backward_player_packed | ✅ | ~790 | S16 packed regrets, 8-action blocks |
| backward_player_packed_a2 | ❌ | — | Not yet disassembled |
| backward_player_packed_cached | ❌ | — | Not yet disassembled |
| backward_opponent | ✅ | 361 | Opponent CFV averaging, 1/num_outcomes |
| backward_opponent_boundary_iso | ❌ | — | Not yet disassembled |
| backward_br_player | ❌ | — | Not yet disassembled |
| backward_chance_abstract | ✅ | 161 | Chance averaging, dead card check |
| backward_chance_tr_2d | ❌ | — | Not yet disassembled |
| backward_chance_turn_river | ✅ (FINAL_RECON) | 209 | Chance backward, bit-pair extraction |
| backward_chance_turn_river_batched | ❌ | — | Not yet disassembled |
| forward_player | ✅ (FINAL_RECON) | 145 | Reach propagation, MUFU.RCP |
| forward_opponent | ✅ | 505 | Opponent reach, warp reduction (SHFL.DOWN) |
| forward_opponent_packed | ❌ | — | Not yet disassembled |
| forward_opponent_br | ❌ | — | Not yet disassembled |
| forward_opponent_br_packed | ❌ | — | Not yet disassembled |
| forward_chance | ❌ | — | Not yet disassembled |
| forward_chance_abstract | ❌ | — | Not yet disassembled |
| forward_chance_tr_2d | ❌ | — | Not yet disassembled |
| forward_chance_turn_river | ❌ | — | Not yet disassembled |
| forward_chance_turn_river_batched | ❌ | — | Not yet disassembled |
| forward_copy | ❌ | — | Not yet disassembled |
| forward_river_subtrees_packed | ✅ (FINAL_RECON) | 1113 | River subtrees, multi-barrier sync |
| train_fold_showdown_parent_packed | ❌ | — | Not yet disassembled |
| train_mixed_fold_showdown_parent_packed | ❌ | — | Not yet disassembled |
| train_mixed_opponent_fold_showdown_parent | ❌ | — | Not yet disassembled |
| train_opponent_fold_showdown_parent_packed | ❌ | — | Not yet disassembled |
| train_small_tree_fp32_multi_iter | ✅ (partial) | 3401 | Multi-iter CFR, variable const buffer |
| river_api | ❌ | — | Not yet disassembled |
| river_batched | ❌ | — | File not found at top level |
| river_subtrees_packed | ❌ | — | Not yet disassembled |
| river_betting_enabled | ❌ | — | Not yet disassembled |
| river_bet | ❌ | — | Not yet disassembled |
| river_buckets | ❌ | — | Not yet disassembled |
| river_card | ❌ | — | Not yet disassembled |
| river_donk | ❌ | — | Not yet disassembled |
| river_ip | ❌ | — | Not yet disassembled |
| river_oop | ❌ | — | Not yet disassembled |
| river_raise | ❌ | — | Not yet disassembled |
| apply_isomorphism | ✅ | 97 | Tree compaction, child pointer swap |
| apply_isomorphism_permuted | ❌ | — | Not yet disassembled |
| encode_stage_segments_i16 | ✅ | 249 | i16 encoding, warp max reduction |
| gather_cfv | ✅ | 41 | Simple gather/scatter |
| replace | ❌ | — | Not yet disassembled |
| trim | ❌ | — | Not yet disassembled |
| BatchAsync | ❌ | — | Not yet disassembled |
| BatchAsync_v2 | ❌ | — | Not yet disassembled |
| BatchDecompressAsync | ❌ | — | Not yet disassembled |
| BatchMemOp | ❌ | — | Not yet disassembled |
| BatchMemOpNode | ❌ | — | Not yet disassembled |
| BatchMemOpNodeGetParams | ❌ | — | Not yet disassembled |
| BatchMemOpNodeSetParams | ❌ | — | Not yet disassembled |
| BatchMemOp_v2 | ❌ | — | Not yet disassembled |
| evaluate_showdown | ✅ (FINAL_RECON) | 457 | Showdown eval, double precision |
| evaluate_showdown_brute | ❌ | — | Not yet disassembled |
| evaluate_showdown_prefix | ❌ | — | Not yet disassembled |
| evaluate_showdown_abstract | ❌ | — | Not yet disassembled |
| evaluate_showdown_brute_3p | ❌ | — | Not yet disassembled |
| evaluate_showdown_prefix_3p | ❌ | — | Not yet disassembled |
| evaluate_fold | ✅ (partial) | 665 | Fold equity, Q16 |
| evaluate_fold_fp32 | ❌ | — | Not yet disassembled |
| evaluate_fold_3p | ❌ | — | Not yet disassembled |
| evaluate_fold_showdown_prefix | ❌ | — | Not yet disassembled |
| evaluate_fold_abstract | ❌ | — | Not yet disassembled |

**Documented: 10/45 kernels. Remaining: 35 kernels require SASS analysis.**
