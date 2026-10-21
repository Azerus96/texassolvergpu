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

## 13. backward_player_cfv (sm_80, ~513 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_player_cfv.txt`

**Role:** CFR backward pass computing counterfactual values (CFV) for a player at a decision node, using Q16 fixed-point dequantization and regret matching.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (num_players or stride)
S2R R15, SR_TID.X                        ; threadIdx.x
S2R R0, SR_CTAID.X                       ; blockIdx.x
ISETP.GE.AND P0, PT, R15, c[0x0][0x1bc], PT ; P0 = (threadIdx.x >= max_threads)
@P0 EXIT                                  ; exit if out of bounds
```

### Constant Buffer Loads (c[0x0] slot)
| Slot | Role |
|------|------|
| `c[0x0][0x118]` | ULDC.64 UR8 — 64-bit pointer base (d_cfv_ / d_abs_regrets_) |
| `c[0x0][0x1a8]` | Pointer table base |
| `c[0x0][0x1ac]` | Pointer table base 2 |
| `c[0x0][0x1b0]` | Pointer table base 3 |
| `c[0x0][0x1b4]` | Pointer table base 4 |
| `c[0x0][0x1b8]` | Pointer table base 5 |
| `c[0x0][0x190]` | Pointer table base 6 |
| `c[0x0][0x1a0]` | Pointer table base 7 |
| `c[0x0][0x1a4]` | Pointer table base 8 |
| `c[0x0][0x170]` | CFV/reach data array base |
| `c[0x0][0x174]` | CFV/reach data array base 2 |
| `c[0x0][0x1bc]` | max thread count / grid bound |

### Q16 Card Extraction (packed card: c \| (p << 8))
```
LDG.E.U8.CONSTANT R16, [R4.64]         ; load packed card byte
I2F.U16 R14, R16                        ; Q16 dequant: int16 -> float
LOP3.LUT R12, R16.reuse, 0x3, RZ, 0xc0, !PT ; suit = card & 3
IADD3 R13, R16, -0x1, RZ                ; card index adjustment
IMAD.IADD R7, R16, 0x1, -R12            ; suit-adjusted rank
```

### Regret Computation Loop (up to 6 actions via predicates P0-P3)
- `MUFU.RCP R14, R14` — reciprocal (1.0f / sumPos) for normalization
- `FADD.FTZ R21, R23, R21` — node_EV += strategy[a] * actionEV[a]
- `FADD.FTZ R28, R8, R25` — CFV accumulation chain
- `FFMA.FTZ` — fused multiply-add for EV accumulation at 0x1560, 0x16b0
- `FMNMX.FTZ` — clamp regrets to >= 0
- `STL.128 [R0], R8` — 128-bit shared memory store (4 floats)
- `STL [R11], R8` — shared memory store for regret values

### Shared Memory & Barriers
```
BSSY B0 / BSYNC B0                      ; block barrier (shared memory sync)
STL.128 [R0+0x10], R8                   ; shared memory store (offset 16)
STL.128 [R0+0x20], R8                   ; shared memory store (offset 32)
STL.128 [R0+0x30], R8                   ; shared memory store (offset 48)
```

### Global Memory Stores
```
STG.E [R2.64], R21                      ; store final CFV to global memory
STG.E [R4.64], R26                      ; store regret value
```

### Algorithm Summary
```
for each player p (thread-mapped):
    node_EV = 0
    for each action a (up to 6, predicated P0-P3):
        action_EV = load_cfv_child(p, a)   ; global memory load via LDG.E.CONSTANT
        reach = Q16_dequant(load_reach(p)) ; I2F.U16
        CFV += reach * action_EV           ; FADD.FTZ / FFMA.FTZ chain
        node_EV += strategy[a] * action_EV
    sum_pos = sum(max(0, regret[p][a]))    ; FMNMX clamp
    inv_sum_pos = 1.0f / sum_pos           ; MUFU.RCP
    for each action a:
        strategy[a] = max(0, regret[a]) * inv_sum_pos
    store CFV[p] to global memory          ; STG.E
    store regrets to shared memory         ; STL.128
    __syncthreads()                        ; BSSY/BSYNC
```

---

## 14. backward_player_packed_a2 (sm_80, ~281 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_player_packed_a2.txt`

**Role:** CFR backward pass with packed S16 (signed 16-bit) fixed-point storage for regrets, variant "a2" (alternate accumulation path).

### Thread Setup
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]
S2R R0, SR_CTAID.X                       ; blockIdx.x
IMAD.SHL.U32 R14, R0.reuse, 0x8, RZ     ; block offset * 8
S2R R6, SR_TID.X                         ; threadIdx.x
BSSY B0, 0x340                           ; block barrier
```

### Constant Buffer Slots
- `c[0x0][0x118]` — pointer base (ULDC.64 UR6)
- `c[0x0][0x1a0]`, `c[0x0][0x1a8]`, `c[0x0][0x1ac]` — pointer tables
- `c[0x0][0x1b0]`, `c[0x0][0x1b4]`, `c[0x0][0x1b8]` — pointer tables
- `c[0x0][0x1bc]` — max count
- `c[0x0][0x1c0]`, `c[0x0][0x1c4]`, `c[0x0][0x1c8]`, `c[0x0][0x1cc]` — flag/pointer tables
- `c[0x0][0x1e0]`, `c[0x0][0x1e4]` — iteration/counter slots

### S16 Packed Regret Operations
```
I2F.S16 R15, R22                        ; signed int16 -> float (Q16 signed)
I2F.S16 R17, R20
I2F.S16 R26, R29
I2F.S16 R27, R30
I2F.S16 R29, R30
I2F.S16 R28, R32
FMNMX.FTZ R13, RZ, R17, !PT             ; clamp regret to >= 0 (max(0, regret))
FMNMX.FTZ R23, RZ, R15, !PT
MUFU.RCP R12, R11                       ; reciprocal: 1.0f / sumPos
FMUL.FTZ R13, R13, R12.reuse            ; normalize: pos_regret / sumPos
FMUL.FTZ R23, R23, R12
STG.E.U16 [R2.64], R5                   ; store S16 packed result
STG.E.U16 [R10.64], R13
```

### Warp/Atomic Reduction
```
SHFL.DOWN PT, R9, R12, 0x10, 0x1f       ; warp shuffle down 16
FMNMX.FTZ R9, R9, R12, !PT              ; max reduction step 1
SHFL.DOWN PT, R14, R9, 0x8, 0x1f        ; step 8
FMNMX.FTZ R14, R9, R14, !PT
SHFL.DOWN PT, R11, R14, 0x4, 0x1f       ; step 4
FMNMX.FTZ R11, R14, R11, !PT
SHFL.DOWN PT, R16, R11, 0x2, 0x1f       ; step 2
FMNMX.FTZ R16, R11, R16, !PT
SHFL.DOWN PT, R13, R16, 0x1, 0x1f       ; step 1
FMNMX.FTZ R13, R16, R13, !PT
REDUX.MAX UR5, R13                      ; warp-level max
ATOMS.MAX RZ, [RZ], R9                  ; atomic max across blocks
```

### S16 Clamping & Packing
```
F2I.FTZ.U32.TRUNC.NTZ R11, R11          ; float -> int truncate
IMNMX R5, R11, 0xffff, PT               ; clamp to S16 unsigned range
IMNMX R9, R0, -0x7fff, !PT              ; clamp to S16 signed range
IMNMX R13, R13, 0xffff, PT
```

### Algorithm Summary
```
for each player p (thread-mapped):
    for each action block:
        load 8 S16 regrets from constant memory (LDG.E.S16)
        for each action a in block:
            regret[a] = S16_to_float(value)   ; I2F.S16
            if regret[a] > 0: sum_pos += regret[a]
    __syncthreads()                      ; BSSY/BSYNC
    warp_max_reduction(sum_pos)          ; SHFL.DOWN + FMNMX butterfly
    atomic_max across blocks             ; ATOMS.MAX
    inv_sum_pos = 1.0f / sum_pos         ; MUFU.RCP
    for each action a:
        strategy[a] = max(0, regret[a]) * inv_sum_pos
        strategy[a] = clamp_S16(strategy[a]) ; IMNMX
    store packed S16 strategy            ; STG.E.U16
```

---

## 15. backward_player_packed_cached (sm_80, ~774+ instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_player_packed_cached.txt`

**Role:** CFR backward pass with packed S16 regrets loaded from constant memory cache (faster than global memory).

### Constant Memory Caching
```
LDG.E.S16.CONSTANT R29, [R22.64]       ; signed 16-bit from constant memory (cached regret)
LDG.E.S16.CONSTANT R20, [R26.64]
LDG.E.S16.CONSTANT R30, [R22.64]
LDG.E.S16.CONSTANT R32, [R22.64]
LDG.E.S16.CONSTANT R33, [R22.64]
LDG.E.S16.CONSTANT R35, [R22.64]
```

### S16 Processing Loop (8-action blocks)
- Processes 8 actions per iteration (R25 incremented by 8: `IADD3 R25, R25, 0x8, RZ`)
- `I2F.S16` conversion for each value
- `FMNMX.FTZ` — clamp to >= 0
- `FFMA.FTZ` — fused multiply-add for accumulation (e.g., `FFMA.FTZ R37, R35, R37, R8`)
- `FSEL R35, R23, R12, P3` — conditional select based on predicate

### Shared Memory Operations
```
STS [R31.X4+0x20], R26                 ; shared memory store (cached value)
STS [R28], R27
STS [R34], R29
LDS R24, [0x10]                         ; shared memory load
LDS R18, [0x10]
LDS R25, [R28]
LDS R30, [R36]
LDS R13, [R36]
LDS R28, [R34]
LDS R25, [R32.X4+0x20]
```

### Normalization & Packing
```
MUFU.RCP R10, R11                      ; reciprocal for normalization
MUFU.RCP R12, R12
FMUL.FTZ R31, R31, R10                 ; scale by reciprocal
FMNMX.FTZ R31, RZ, R24, !PT            ; clamp
FMUL.FTZ R31, R31, R10
STG.E.U16 [R26], R31                   ; store packed S16
STG.E.U16 [R30], R25
STG.E.U16 [R24], R35
```

### Algorithm Summary
```
for each player p (thread-mapped):
    for each 8-action block:
        load 8 S16 regrets from constant memory cache (LDG.E.S16.CONSTANT)
        cache to shared memory (STS)
        for each action a in block:
            regret[a] = S16_to_float(value)
            if regret[a] > 0: sum_pos += regret[a]
    __syncthreads()                    ; BSSY/BSYNC
    inv_sum_pos = 1.0f / sum_pos       ; MUFU.RCP
    for each action a:
        strategy[a] = max(0, regret[a]) * inv_sum_pos
        strategy[a] = clamp_S16        ; FMNMX
    store packed S16 to global memory  ; STG.E.U16
```

---

## 16. backward_opponent_boundary_iso (sm_80, 585 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_opponent_boundary_iso.txt`

**Role:** Opponent CFV averaging at tree boundary/isomorphism nodes — averages CFVs across chance outcomes using FP16 half-precision MMA (tensor core) instructions.

### Thread Setup & Bounds
```
S2R R3, SR_CTAID.X                     ; blockIdx.x
ISETP.GE.AND P0, PT, R3, c[0x0][0x180], PT ; bounds check
SEL R0, RZ, c[0x0][0x180], !P0
IMAD.IADD R0, R3, 0x1, -R0             ; remaining work count
ISETP.GE.AND P1, PT, R0, c[0x0][0x1a0], PT
@P0 EXIT P1
S2R R6, SR_TID.X                       ; threadIdx.x
ISETP.GE.AND P0, PT, R6, c[0x0][0x1d8], PT ; bounds check vs max_threads
@P0 EXIT
```

### Half-Precision MMA (Tensor Core)
```
HFMA2.MMA R0, -RZ, RZ, 0, 0            ; FP16 MMA init (accumulator = 0)
HFMA2.MMA R22, -RZ, RZ, 0, 2.384185791015625e-07 ; FP16 MMA with epsilon
HFMA2.MMA R11, -RZ, RZ, 0, 1.1920928955078125e-07 ; FP16 MMA with smaller epsilon
```

### Opponent CFV Averaging Loop (up to 8 actions)
```
FADD.FTZ R0, R0, R25                   ; accumulate opponent CFV (outcome 0)
FADD.FTZ R0, R0, R21                   ; outcome 1
FADD.FTZ R0, R0, R24                   ; outcome 2
FADD.FTZ R0, R0, R27                   ; outcome 3
FADD.FTZ R0, R0, R26                   ; outcome 4
FADD.FTZ R0, R0, R20                   ; outcome 5
FADD.FTZ R0, R0, R19                   ; outcome 6
FADD.FTZ R0, R0, R21                   ; outcome 7
```

### Constant Memory Pointer Tables
- `c[0x0][0x160]`, `c[0x0][0x164]`, `c[0x0][0x168]`, `c[0x0][0x16c]` — 4 pointer table slots (child CFV arrays)
- `c[0x0][0x1d4]` — scale/epsilon constant
- `c[0x0][0x1d0]`, `c[0x0][0x1c8]` — pointer tables for child nodes
- `c[0x0][0x1c0]`, `c[0x0][0x1b8]` — pointer tables

### Shared Memory & Barriers
```
STS [RZ], RZ                            ; clear shared memory
BSSY B0, 0x3b0 / BSYNC B0               ; block barrier
BSSY B0, 0x9c0 / BSYNC B0               ; second barrier phase
```

### Global Memory Stores
```
STG.E [R18.64], R10                     ; store averaged CFV
STG.E [R12.64], R0                      ; store node CFV
```

### Algorithm Summary
```
for each boundary/isomorphism node (thread-mapped):
    sum = 0
    for each opponent chance outcome o (up to 8):
        if valid:
            child_cfv = load_cfv(opponent_child, o)
            sum += child_cfv              ; FADD.FTZ chain
    __syncthreads()                     ; BSSY/BSYNC
    avg_cfv = sum * scale               ; FP16 HFMA2.MMA accumulation
    store avg_cfv to global memory      ; STG.E
```

---

## 17. forward_opponent_packed (sm_80, 729 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_opponent_packed.txt`

**Role:** CFR forward pass — opponent reach probability propagation with packed S16 storage.

### Thread Setup
```
IMAD.SHL.U32 R14, R0, 0x8, RZ          ; block offset * 8
S2R R4, SR_TID.X                        ; threadIdx.x
BSSY B0, 0x2f0                          ; block barrier
BSYNC B0
ISETP.GE.AND P0, PT, R4, c[0x0][0x1cc], PT ; bounds check
@P0 EXIT
```

### Reach Propagation (per action, S16 packed)
```
I2F.S16 R15, R14                        ; signed int16 -> float
I2F.S16 R28, R28
I2F.S16 R14, R14
I2F.S16 R27, R26
I2F.S16 R28, R26
FSEL R27, R28, RZ, P4                   ; conditional: max(0, value) via predicate
FSEL R26, R14, RZ, P3
STL [R16+0x4], R27                      ; shared memory store
STL [R16], R26
FADD.FTZ R25, R26, R25                  ; reach accumulation
FADD.FTZ R25, R25, R27
```

### Normalization with Epsilon Guard
```
FSETP.GT.FTZ.AND P2, PT, R25, 1.0000000116860974231e-07, PT ; epsilon > ~1e-7
MUFU.RCP R22, R25                       ; reciprocal: 1.0f / sum
FMUL.FTZ R26, R15, R22                  ; reach_out = reach * (1/sum)
FMUL.FTZ R14, R21, R22                  ; scale by strategy
FSEL R28, R14, RZ, P2                   ; conditional zero if below epsilon
FSEL R12, R12, 0.5, P0                  ; fallback to 0.5
STG.E [R18.64], R28                     ; store normalized reach
```

### Card Extraction
```
PRMT R10, R12, 0x9910, RZ               ; card byte extraction from packed c | (p<<8)
PRMT R17, R15, 0x7610, R17              ; alternate permutation
PRMT R24, R27, 0x9910, RZ               ; dead card check
ISETP.NE.AND P1, PT, R24, -0x1, PT      ; card != -1 (not dead)
```

### Shared Memory & Barriers
```
STL [R16], R15                          ; shared memory store
STL [R16+0x4], R15
STL [R16+0x8], R28
STL [R16+0xc], R27
STL [R16+0x10], R27
STL [R16+0x14], R27
STL [R16+0x18], R28
STL [R16+0x1c], R27
BSSY B0, 0xee0 / BSYNC B0               ; block barrier (multiple phases)
```

### Global Memory Stores (S16 packed)
```
STG.E.U16 [R2.64], R5                   ; store S16 packed reach
STG.E [R18.64], R21                     ; store float reach
STG.E [R14.64], R13                     ; store normalized reach
```

### Algorithm Summary
```
for each opponent node (thread-mapped):
    for each action a (up to 8, S16 packed):
        reach[a] = S16_to_float(load_packed_reach(a))
        if reach[a] > 0: sum += reach[a]    ; FSEL conditional
    __syncthreads()
    if sum > epsilon (~1e-7):
        for each action a:
            reach_out[a] = reach[a] / sum   ; MUFU.RCP + FMUL
    else:
        reach_out[a] = 0.5                  ; fallback
    store packed S16 reach_out            ; STG.E.U16
```

---

## 18. forward_chance_tr_2d (sm_80, 145 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_chance_tr_2d.txt`

**Role:** Forward chance node for turn/river streets — computes chance probabilities for card deals in a 2D grid.

### 2D Grid Setup
```
S2R R2, SR_CTAID.X                      ; blockIdx.x
S2R R4, SR_CTAID.Y                      ; blockIdx.y  (2D grid: x = node, y = street/round)
S2R R0, SR_TID.X                        ; threadIdx.x
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
@P0 BRA 0x400
```

### Chance Probability Computation
```
I2F.U32.RP R8, c[0x0][0x0]              ; int -> float (round positive) — num_outcomes
MUFU.RCP R8, R8                         ; 1.0f / num_outcomes
IADD3 R6, R8, 0xffffffe, RZ             ; round: p - 2
F2I.FTZ.U32.TRUNC.NTZ R7, R6            ; float -> int truncate
LOP3.LUT R5, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction (0x33 = 0b00110011)
```

### Card Loading & Dead Card Check
```
LDG.E.U16.CONSTANT R12, [R8.64]         ; card 0 (16-bit)
LDG.E.U16.CONSTANT R13, [R8.64]         ; card 1
PRMT R10, R12, 0x9910, RZ               ; card byte extraction
PRMT R10, R13, 0x9910, RZ
ISETP.NE.AND P1, PT, R10, -0x1, PT      ; dead card check (card == 0xFFFF)
@!P1 BRA 0x690                          ; skip dead cards
```

### FP16 MMA Accumulation
```
HFMA2.MMA R7, -RZ, RZ, 0, 1.78813934326171875e-07 ; FP16 MMA with epsilon
```

### Atomic Operations & GPU-Wide Reduction
```
ATOMS.CAST.SPIN R9, [R13], R8, R9       ; atomic compare-and-swap spin lock
ISETP.EQ.U32.AND P1, PT, R9, 0x1, PT    ; check lock acquired
@!P1 BRA 0x640                          ; spin wait
RED.E.ADD.F32.FTZ.RN.STRONG.GPU [R4.64], R7 ; GPU-wide atomic float add
WARPSYNC 0xffffffff                     ; warp synchronization
BSSY B0 / BSYNC B0                      ; block barrier
```

### Global Memory Store
```
STG.E [R4.64], R7                       ; store chance probability
```

### Algorithm Summary
```
for each chance node (2D grid: blockIdx.x = node, blockIdx.y = street):
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes     ; MUFU.RCP
    for each card deal c:
        if card != -1 (valid):
            prob = chance_weight(c) * inv_outcomes
            HFMA2.MMA accumulate (FP16 tensor core)
    atomic_spin_lock                     ; ATOMS.CAST.SPIN
    GPU_wide_atomic_add                  ; RED.E.ADD.F32
    store chance probability             ; STG.E
```

---

## 19. apply_isomorphism_permuted (sm_80, 177 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/apply_isomorphism_permuted.txt`

**Role:** Tree isomorphism compaction (permuted variant) — swaps isomorphic child subtrees and accumulates values using FP16 MMA.

### Thread Setup
```
MOV R1, c[0x0][0x28]                    ; work count / stride
S2R R14, SR_TID.X                       ; threadIdx.x
ISETP.GE.AND P0, PT, R14, c[0x0][0x1a0], PT ; bounds check
@P0 EXIT
S2R R9, SR_CTAID.X                      ; blockIdx.x
```

### FP16 MMA Initialization
```
HFMA2.MMA R2, -RZ, RZ, 0, 0             ; FP16 MMA accumulator init
HFMA2.MMA R19, -RZ, RZ, 0, 0            ; second accumulator
HFMA2.MMA R19, -RZ, RZ, 0, 0            ; third accumulator
```

### Card Loading (16-bit from constant memory)
```
LDG.E.U16.CONSTANT R29, [R28.64]        ; card data
LDG.E.U16.CONSTANT R18, [R18.64]
LDG.E.U16.CONSTANT R19, [R18.64]
LDG.E.U16.CONSTANT R27, [R28.64]
PRMT (implied via LOP3 for suit extraction)
LOP3.LUT R4, R29, 0x3, RZ, 0xc0, !PT    ; suit = card & 3
```

### Child Pointer Processing (up to 8 children)
```
IMAD.WIDE R2, R9, R0, c[0x0][0x168]     ; child pointer table base
LDG.E.CONSTANT R3, [R2.64]              ; load child pointer
IMAD.WIDE R6, R3, c[0x0][0x19c], R14    ; child offset * thread
LDG.E R27, [R12.64]                     ; load child value
IADD3 R16, P1, R16, 0x8, RZ             ; increment by 8 (8 children per iteration)
IADD3 R24, P2, R24, 0x10, RZ            ; increment by 16
IADD3 R20, P4, R20, 0x10, RZ
IADD3 R10, P3, R10, 0x10, RZ
IADD3 R22, P5, R22, 0x10, RZ
```

### Value Accumulation
```
FADD.FTZ R26, R26, R27                  ; accumulate child CFV
FADD.FTZ R27, R6, R19                   ; accumulate
FADD.FTZ R27, R10, R27
```

### Global Memory Store
```
STG.E [R12.64], R27                     ; store accumulated value
STG.E [R20.64], R0                      ; store node value
EXIT
```

### Algorithm Summary
```
for each isomorphism group (thread-mapped):
    load child pointers from constant memory table
    for each child (up to 8, permuted order):
        child_cfv = load_cfv(child)
        if child subtrees isomorphic:
            swap child pointers
            sum += child_cfv              ; FADD.FTZ / HFMA2.MMA
    store compacted value to global memory   ; STG.E
```

---

## 20. train_fold_showdown_parent_packed (sm_80, 1393 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/train_fold_showdown_parent_packed.txt`

**Role:** Training kernel for fold/showdown parent nodes — computes CFVs for terminal fold/showdown actions, packs to S16, handles chance outcomes for turn/river cards.

### Thread Setup & Bounds
```
S2R R10, SR_CTAID.X                     ; blockIdx.x
ISETP.GE.AND P0, PT, R10, c[0x0][0x26c], PT ; bounds check
@P0 EXIT
S2R R0, SR_TID.X                        ; threadIdx.x
BSSY B0, 0x460                          ; block barrier
```

### Card Loading & 11-bit Field Extraction
```
LDG.E.U8.CONSTANT R12, [R12.64]         ; board card byte
SHF.R.U32.HI R4, RZ, 0xb, R2            ; 11-bit field extraction (shift right 11)
LOP3.LUT R5, R2, 0x7ff, RZ, 0xc0, !PT   ; mask 0x7ff (card index)
```

### Chance Probability (turn/river)
```
I2F.U32.RP R5, c[0x0][0x0]              ; int -> float (round positive)
MUFU.RCP R5, R5                         ; 1.0f / num_chance_outcomes
IADD3 R2, R5, 0xffffffe, RZ             ; round: p - 2
F2I.FTZ.U32.TRUNC.NTZ R3, R2            ; float -> int truncate
```

### Bit-Pair Extraction
```
LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction (0x33)
LOP3.LUT R10, RZ, c[0x0][0x0], RZ, 0x33, !PT
```

### FP16 MMA Accumulation
```
HFMA2.MMA R7, -RZ, RZ, 0, 1.78813934326171875e-07 ; FP16 MMA with epsilon
```

### Shared Memory Caching (STS.64 / LDS)
```
STS.64 [0x8], R2                        ; 64-bit shared memory store
STS.64 [RZ], R22                        ; store CFV
STS [0x38], RZ
STS.64 [0x30], R24
STS.64 [0x10], R4
STS.64 [0x28], R8
STS.64 [0x18], R12
LDS R24, [0x10]                         ; shared memory load
LDS R18, [0x10]
LDS R25, [R28]
LDS R30, [R36]
```

### S16 Packing & Storage
```
STS [0x20], R4                          ; store S16 packed
STS [0x24], R5
STG.E.U16 (implied via packed stores)
```

### Constant Buffer Slots
- `c[0x0][0x118]` — ULDC.64 UR12 — pointer base
- `c[0x0][0x1a0]`, `c[0x0][0x1a8]`, `c[0x0][0x1ac]` — pointer tables
- `c[0x0][0x198]`, `c[0x0][0x1b8]`, `c[0x0][0x1bc]` — pointer tables
- `c[0x0][0x168]`, `c[0x0][0x16c]`, `c[0x0][0x178]`, `c[0x0][0x180]` — pointer tables
- `c[0x0][0x264]`, `c[0x0][0x268]` — iteration/counter slots
- `c[0x0][0x26c]` — max thread bound

### Algorithm Summary
```
for each fold/showdown parent node (thread-mapped):
    load board cards (LDG.E.U8.CONSTANT)
    extract 11-bit card index (SHF.R 0xb + LOP3 0x7ff)
    for each chance outcome (turn/river card):
        if card valid (not dead):
            chance_prob = 1.0f / num_outcomes   ; I2F.U32.RP + MUFU.RCP
            child_cfv = load_cfv(child)
            HFMA2.MMA accumulate (FP16 tensor core)
            cache to shared memory (STS.64)
    __syncthreads()                     ; BSSY B0/B1
    pack CFVs to S16                    ; fixed-point quantization
    store packed CFVs to global memory
```

---

## 22. evaluate_showdown_brute (sm_80, ~329 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown_brute.txt`

**Role:** Brute-force 5-card showdown hand evaluation — evaluates a hand against all possible opponent combinations.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]
S2R R8, SR_CTAID.X                         ; blockIdx.x
ISETP.GE.AND P0, PT, R8, c[0x0][0x1e4], PT ; bounds check vs max_boards
@P0 EXIT
S2R R0, SR_TID.X                           ; threadIdx.x
```

### Constant Buffer Pointer Tables
| Slot | Role |
|------|------|
| `c[0x0][0x118]` | ULDC.64 UR10 — 64-bit pointer base |
| `c[0x0][0x188]`, `c[0x0][0x170]`, `c[0x0][0x178]` | Hand/card data pointer tables |
| `c[0x0][0x180]`, `c[0x0][0x168]` | Pointer tables |
| `c[0x0][0x1c0]`, `c[0x0][0x1c8]` | Pointer tables |
| `c[0x0][0x1b0]`, `c[0x0][0x1b8]`, `c[0x0][0x1bc]` | Pointer tables |
| `c[0x0][0x1d8]`, `c[0x0][0x1dc]` | Pointer tables |
| `c[0x0][0x1e0]` | Counter/stride slot |
| `c[0x0][0x1e4]` | Max thread/board bound |

### Card Loading
```
LDG.E.CONSTANT R6, [R16.64]              ; hand data
LDG.E.CONSTANT R5, [R10.64]              ; board/opponent card
LDG.E.CONSTANT R4, [R12.64]              ; card data
LDG.E.CONSTANT R3, [R14.64]              ; card data
LDG.E.CONSTANT R2, [R8.64]               ; card data
LDG.E.U16.CONSTANT R12, [R12.64]         ; 16-bit card data
LDG.E.U8.CONSTANT R16, [R12.64]          ; individual card byte
LDG.E.U8.CONSTANT R18, [R12.64+0x1]      ; next card byte
```

### Shared Memory Operations
```
STS [R11.X4], R10                        ; shared memory store
LDS R10, [R12.X4]                        ; shared memory load
```

### Warp-Level Operations
```
ATOMS.ADD R20, [UR5], R17                ; atomic add to shared memory
POPC R17, UR6                            ; population count (bit count)
VOTEU.ANY UR6, UPT, PT                   ; warp vote (any thread true)
SHFL.IDX PT, R11, R20, R19, 0x1f         ; warp shuffle by index
FLO.U32 R19, UR6                         ; floor to uint
POPC R22, R21                            ; second population count
```

### Barriers
```
BSSY B0, 0x240 / BSYNC B0                ; block barrier (init)
BSSY B0, 0x550                           ; block barrier (main loop)
BAR.SYNC.DEFER_BLOCKING 0x0              ; deferred block barrier
```

### Algorithm Summary
```
for each board (block-mapped):
    load hand cards and board cards
    clear shared memory (STS [RZ], RZ)
    __syncthreads()
    for each opponent combo (thread-mapped, strided):
        load opponent cards from global memory
        evaluate 5-card hand rank (brute force vs all 21 subsets)
        atomic add win/loss/tie to shared memory (ATOMS.ADD)
        warp vote / population count for early termination
    __syncthreads()
    store showdown result to global memory
```

---

## 23. evaluate_showdown_prefix (sm_80, ~2673 instructions — very large)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown_prefix.txt`

**Role:** Prefix-based showdown evaluation — more efficient than brute force, uses prefix trees for hand ranking.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]
S2R R16, SR_CTAID.X                        ; blockIdx.x
ISETP.GE.AND P0, PT, R16, c[0x0][0x1ec], PT ; bounds check
@P0 EXIT
S2R R6, SR_TID.X                           ; threadIdx.x
```

### Constant Buffer Pointer Tables
| Slot | Role |
|------|------|
| `c[0x0][0x118]` | ULDC.64 UR6 — 64-bit pointer base |
| `c[0x0][0x188]`, `c[0x0][0x168]`, `c[0x0][0x1a0]` | Pointer tables |
| `c[0x0][0x1b8]`, `c[0x0][0x1c0]`, `c[0x0][0x170]` | Pointer tables |
| `c[0x0][0x178]`, `c[0x0][0x180]`, `c[0x0][0x1a8]` | Pointer tables |
| `c[0x0][0x1e8]` | Pointer table |

### Card Extraction & Comparison
```
LOP3.LUT R10, R6, 0x1f, RZ, 0xc0, !PT    ; extract low 5 bits (rank)
LOP3.LUT R11, R11, 0x7c, RZ, 0xc0, !PT   ; extract bits 2-6 (suit/rank)
IMNMX R13, R3, R18, PT                   ; integer min/max (card rank compare)
IMNMX R17, R3, R18, PT
IMNMX R25, R3, R20, PT
IMNMX R27, R3, R22, PT
IMNMX R15, R3, R18, PT                   ; min/max for straight detection
```

### FP16 MMA
```
HFMA2.MMA R15, -RZ, RZ, 0, 0             ; FP16 MMA accumulator init
```

### Shared Memory
```
LDS.128 R8, [R0]                          ; 128-bit shared memory load (prefix tree node)
BSSY B0, 0xa30 / BSYNC B0                 ; block barrier
BSSY B1, 0x5e0 / BAR.SYNC.DEFER_BLOCKING 0x0 ; second barrier phase
```

### Algorithm Summary
```
for each hand (block-mapped):
    load hole cards and board cards
    extract card ranks/suits via LOP3 bit masks
    traverse prefix tree for hand classification:
        for each card in hand:
            compare against prefix tree nodes (IMNMX min/max)
            follow matching prefix branch
    use FP16 HFMA2.MMA for probability accumulation
    __syncthreads() between tree levels
    store hand rank / showdown result
```

---

## 24. evaluate_showdown_abstract (sm_80, ~345 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown_abstract.txt`

**Role:** Abstract showdown evaluation — reduced abstraction for faster hand ranking against abstracted opponent ranges.

### Thread Setup
```
MOV R1, c[0x0][0x28]                      ; work count
S2R R8, SR_CTAID.X                        ; blockIdx.x
HFMA2.MMA R9, -RZ, RZ, 0, 2.384185791015625e-07 ; FP16 MMA with epsilon
S2R R7, SR_TID.X                          ; threadIdx.x
ISETP.GE.AND P1, PT, R7, c[0x0][0x1a4], PT ; bounds check (max_boards)
ISETP.GE.AND P0, PT, R7, c[0x0][0x1a0], PT ; bounds check (max_threads)
```

### Uniform Register Operations
```
R2UR UR6, R2                              ; register -> uniform register
R2UR UR10, R4
R2UR UR11, R8
USHF.R.S32.HI UR4, URZ, 0x1f, UR6         ; uniform shift right high
UIMAD UR7, UR4, UR7, URZ                  ; uniform multiply-add
UIMAD UR5, UR6, UR5, UR7
UIAD3 UR6, -UR13, UR9, URZ                ; unsigned 3-address add
ULOP3.LUT UR13, UR9, 0x3, URZ, 0xc0, !UPT ; uniform suit extraction (card & 3)
```

### FP16 MMA
```
HFMA2.MMA R9, -RZ, RZ, 0, 2.384185791015625e-07 ; init with epsilon
HFMA2.MMA R15, -RZ, RZ, 0, 0              ; second accumulator
HFMA2.MMA R5, -RZ, RZ, 0, 0               ; third accumulator
```

### Shared Memory (128-bit)
```
LDS.128 R8, [R0]                          ; 128-bit shared memory load
STS [R9.X4], R4                           ; shared memory store
```

### Predicate Operations
```
PLOP3.LUT P0, PT, PT, PT, UP0, 0x80, 0x0  ; predicate 3-input LUT
PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0
PLOP3.LUT P0, PT, PT, PT, PT, 0x8, 0x0
```

### Algorithm Summary
```
for each abstract hand (thread-mapped):
    load hole cards and board cards
    R2UR moves to uniform registers for broadcast
    UIAD3 / UIMAD uniform arithmetic for card indexing
    HFMA2.MMA accumulate showdown equity (FP16 tensor core)
    LDS.128 load prefix tree node from shared memory
    PLOP3.LUT predicate operations for control flow
    __syncthreads() (BSYNC B0)
    store abstracted showdown result
```

---

## 25. evaluate_fold_fp32 (sm_80, ~601 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_fold_fp32.txt`

**Role:** Fold equity evaluation in FP32 — computes EV when all opponents fold.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]
S2R R12, SR_CTAID.X                        ; blockIdx.x
SHF.R.S32.HI R0, RZ, 0x1f, R12
S2R R6, SR_TID.X                           ; threadIdx.x
ISETP.GE.AND P0, PT, R6, c[0x0][0x1ac], PT ; bounds check
```

### Constant Buffer Slots
| Slot | Role |
|------|------|
| `c[0x0][0x118]` | ULDC.64 UR6 — 64-bit pointer base |
| `c[0x0][0x168]`, `c[0x0][0x178]`, `c[0x0][0x180]` | Board/card pointer tables |
| `c[0x0][0x170]`, `c[0x0][0x17c]`, `c[0x0][0x184]` | Pointer tables |
| `c[0x0][0x0]` | Counter/stride |
| `c[0x0][0x1a2]` | LDC.U8 byte load |

### Card Loading
```
LDG.E.CONSTANT R0, [R10.64]              ; board data
LDG.E.CONSTANT R2, [R12.64]              ; board data
LDG.E.U8.CONSTANT R3, [R14.64]           ; board card byte
LDG.E.U8.CONSTANT R4, [R16.64]           ; board card byte
```

### Fold Equity Probability
```
I2F.U32.RP R13, c[0x0][0x0]              ; int -> float (round positive) — num_opponents
MUFU.RCP R13, R13                        ; 1.0f / num_opponents
IADD3 R10, R13, 0xffffffe, RZ            ; round: p - 2
F2I.FTZ.U32.TRUNC.NTZ R11, R10           ; float -> int truncate
```

### Bit-Pair Extraction
```
LOP3.LUT R12, RZ, R6, RZ, 0x33, !PT      ; bit-pair extraction (0x33 = 0b00110011)
LOP3.LUT R11, RZ, c[0x0][0x0], RZ, 0x33, !PT
```

### Modulo Arithmetic (64-bit)
```
IMAD.HI.U32 R11, R11, R15, R10           ; high 32 bits of 64-bit multiply
IMAD.HI.U32 R11, R11, R12, RZ            ; second high multiply
```

### Shared Memory Clearing
```
STS [RZ], RZ                              ; clear shared memory
STS [R12.X4+0x160], RZ                    ; clear shared memory (offset 0x160)
STS [R10], RZ
STS [R14], RZ
STS [R18], RZ
```

### Barriers
```
BSSY B0, 0x4c0 / BSYNC B0                 ; block barrier
BSSY B1, 0x3b0 / BSYNC B1                 ; second barrier phase
BAR.SYNC.DEFER_BLOCKING 0x0               ; deferred barrier
LDC.U8 R9, c[0x0][0x1a2]                  ; load byte from constant memory
```

### Algorithm Summary
```
for each fold node (thread-mapped):
    load board cards (LDG.E.U8.CONSTANT)
    compute fold probability: 1.0f / num_opponents (I2F.U32.RP + MUFU.RCP)
    clear shared memory for fold equity accumulation
    __syncthreads() (BSSY B0/B1)
    for each opponent combo:
        compute fold EV = hand_equity * fold_probability
        accumulate into shared memory
    __syncthreads()
    store fold equity result to global memory
```

---

## 26. SUMMARY: KERNEL ALGORITHMIC COVERAGE

| Kernel | Documented | SASS Lines | Key Algorithms |
|--------|-----------|------------|----------------|
| backward_player | ✅ (FINAL_RECON) | 673 | Regret matching, FMNMX, MUFU.RCP |
| backward_player_cfv | ✅ | ~513 | CFV backward, Q16 dequant, regret matching |
| backward_player_cfv_packed | ✅ | 601 | Packed CFV, Q16, regret matching |
| backward_player_packed | ✅ | ~790 | S16 packed regrets, 8-action blocks |
| backward_player_packed_a2 | ✅ | ~281 | Packed S16, warp/atomic max reduction |
| backward_player_packed_cached | ✅ | ~774+ | Packed S16 + constant memory cache |
| backward_opponent | ✅ | 361 | Opponent CFV averaging, 1/num_outcomes |
| backward_opponent_boundary_iso | ✅ | 585 | Opponent CFV at boundary, HFMA2 MMA |
| backward_br_player | ❌ | — | Not yet disassembled |
| backward_chance_abstract | ✅ | 161 | Chance averaging, dead card check |
| backward_chance_tr_2d | ❌ | — | Not yet disassembled |
| backward_chance_turn_river | ✅ (FINAL_RECON) | 209 | Chance backward, bit-pair extraction |
| backward_chance_turn_river_batched | ❌ | — | Not yet disassembled |
| forward_player | ✅ (FINAL_RECON) | 145 | Reach propagation, MUFU.RCP |
| forward_opponent | ✅ | 505 | Opponent reach, warp reduction (SHFL.DOWN) |
| forward_opponent_packed | ✅ | 729 | Packed S16 reach, epsilon normalization |
| forward_opponent_br | ❌ | — | Not yet disassembled |
| forward_opponent_br_packed | ❌ | — | Not yet disassembled |
| forward_chance | ❌ | — | Not yet disassembled |
| forward_chance_abstract | ❌ | — | Not yet disassembled |
| forward_chance_tr_2d | ✅ | 145 | 2D grid chance, FP16 MMA, GPU atomic add |
| forward_chance_turn_river | ❌ | — | Not yet disassembled |
| forward_chance_turn_river_batched | ❌ | — | Not yet disassembled |
| forward_copy | ❌ | — | Not yet disassembled |
| forward_river_subtrees_packed | ✅ (FINAL_RECON) | 1113 | River subtrees, multi-barrier sync |
| train_fold_showdown_parent_packed | ✅ | 1393 | Fold/showdown CFV, chance outcomes, S16 pack |
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
| apply_isomorphism_permuted | ✅ | 177 | Permuted isomorphism, HFMA2 MMA |
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
| evaluate_showdown_brute | ✅ | ~329 | Brute-force hand eval, ATOMS.ADD, POPC, VOTEU |
| evaluate_showdown_prefix | ✅ | ~2673 | Prefix tree eval, HFMA2 MMA, IMNMX compare |
| evaluate_showdown_abstract | ✅ | ~345 | Abstract eval, R2UR/uniform regs, PLOP3 predicates |
| evaluate_showdown_brute_3p | ❌ | — | Not yet disassembled |
| evaluate_showdown_prefix_3p | ❌ | — | Not yet disassembled |
| evaluate_fold | ✅ (partial) | 665 | Fold equity, Q16 |
| evaluate_fold_fp32 | ✅ | ~601 | Fold equity FP32, MUFU.RCP, bit-pair LOP3 |
| evaluate_fold_3p | ❌ | — | Not yet disassembled |
| evaluate_fold_showdown_prefix | ❌ | — | Not yet disassembled |
| evaluate_fold_abstract | ❌ | — | Not yet disassembled |

**Documented: 22/45 kernels. Remaining: 23 kernels require SASS analysis.**
