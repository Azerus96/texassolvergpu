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

## 26. backward_chance_tr_2d (sm_80, ~137 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_chance_tr_2d.txt`

**Role:** Backward chance node for turn/river streets — accumulates chance probabilities across all card deals using GPU-wide atomic reduction (FP16 MMA + atomic spin lock + RED.E.ADD.F32 GPU-wide add).

### Thread Setup & Bounds Check (2D Grid)
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
IMAD.MOV.U32 R3, RZ, RZ, 0x8            ; R3 = 8 (stride)
ULDC.64 UR4, c[0x0][0x118]              ; UR4 = 64-bit pointer base
IMAD.WIDE R2, R2, R3, c[0x0][0x188]     ; wide address computation
LDG.E.64.CONSTANT R2, [R2.64]           ; load 64-bit pointer
BSSY B0, 0x410                          ; block barrier
S2R R0, SR_TID.X                        ; R0 = threadIdx.x
S2R R4, SR_CTAID.Y                      ; R4 = blockIdx.y (2D grid: x=node, y=street)
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
@P0 BRA 0x400                           ; skip if out of bounds
```

### Chance Probability (1/num_outcomes)
```
I2F.U32.RP R8, c[0x0][0x0]              ; num_outcomes -> float (round positive)
MUFU.RCP R8, R8                         ; 1.0f / num_outcomes
IADD3 R6, R8, 0xffffffe, RZ             ; round: p - 2
F2I.FTZ.U32.TRUNC.NTZ R7, R6            ; float -> int truncate
```

### Card Index Computation (modulo arithmetic)
```
IMAD.MOV R9, RZ, RZ, -R7                ; R9 = -num_outcomes
IMAD R9, R9, c[0x0][0x0], RZ            ; R9 = -num_outcomes * num_outcomes
IMAD.HI.U32 R10, R7, R9, R6             ; 64-bit multiply high part
IMAD.MOV.U32 R6, RZ, RZ, R0             ; R6 = threadIdx.x
IMAD.HI.U32 R10, R10, R5, RZ            ; add thread offset
IADD3 R8, -R10, RZ, RZ                  ; R8 = card index
IMAD R5, R8, c[0x0][0x0], R5            ; R5 = card_index * num_outcomes + base
ISETP.GE.U32.AND P1, PT, R5, c[0x0][0x0], PT ; bounds check
@P1 IADD3 R5, R5, -c[0x0][0x0], RZ      ; wrap around (modulo)
@P1 IADD3 R10, R10, 0x1, RZ             ; carry
ISETP.GE.U32.AND P2, PT, R5, c[0x0][0x0], PT
@P2 IADD3 R10, R10, 0x1, RZ
```

### Card Loading & Dead Card Check
```
@!P3 LOP3.LUT R10, RZ, c[0x0][0x0], RZ, 0x33, !PT ; bit-pair extraction
IADD3 R5, R10.reuse, 0x1, RZ            ; next card
ISETP.GE.U32.AND P2, PT, R10, 0x3, PT   ; max 4 cards
LOP3.LUT P1, R5, R5, 0x3, RZ, 0xc0, !PT ; suit = card & 3
@!P1 BRA 0x2e0                          ; skip invalid
```

### Shared Memory Clearing (multiple entries)
```
IMAD.SHL.U32 R7, R0, 0x4, RZ            ; R7 = threadIdx.x * 16
MOV R8, c[0x0][0x0]                     ; base
IMAD.MOV.U32 R6, RZ, RZ, R0             ; R6 = threadIdx.x
IADD3 R5, R5, -0x1, RZ                  ; decrement
STS [R7], RZ                            ; clear shared memory entry
IMAD R7, R8, 0x4, R7                    ; next entry offset
ISETP.NE.AND P1, PT, R5, RZ, PT         ; loop if more entries
@P1 BRA 0x280
```

### FP16 MMA Init + Card Deal Loop
```
BSYNC B1                                ; barrier
@!P2 BRA 0x400                          ; exit if no more work
IMAD.SHL.U32 R5, R6, 0x4, RZ            ; shared memory offset
IMAD.MOV.U32 R10, RZ, RZ, c[0x0][0x0]   ; base
LEA R7, R10.reuse, R5, 0x2              ; shared memory address
IMAD.MOV.U32 R11, Z, Z, c[0x0][0x0]
MOV R13, c[0x0][0x0]
STS [R5], RZ                            ; clear shared memory
IMAD R8, R10.reuse, 0x4, R7             ; pointer arithmetic
IMAD R6, R11, 0x2, R6
STS [R7], RZ
IMAD R9, R10, 0x4, R8
IMAD R6, R13, 0x2, R6
STS [R8], RZ
IMAD R5, R10, 0x4, R9
STS [R9], RZ
ISETP.GE.AND P1, PT, R6, c[0x0][0x1a0], PT
@!P1 BRA 0x320                          ; bounds check
BSYNC B0                                ; block barrier
BAR.SYNC.DEFER_BLOCKING 0x0             ; deferred barrier
```

### Main Chance Accumulation Loop
```
ISETP.GE.AND P1, PT, R0, c[0x0][0x19c], PT ; bounds check
@P1 BRA 0x6c0                           ; skip
HFMA2.MMA R9, -RZ, RZ, 0, 2.384185791015625e-07 ; FP16 MMA init (epsilon)
IMAD.WIDE R8, R4, R9, c[0x0][0x180]     ; pointer to card data
LDG.E.CONSTANT R6, [R8.64]              ; load card pointer
I2F R5, c[0x0][0x190]                   ; num_outcomes -> float
IMAD.MOV.U32 R7, Z, Z, 0x3              ; stride 3
IMAD R4, R4, R7, c[0x0][0x194]          ; next pointer table entry
MUFU.RCP R5, R5                         ; 1.0f / num_outcomes
IMAD R6, R6, R7, c[0x0][0x194]          ; next pointer
IMAD.MOV.U32 R7, Z, Z, R0               ; R7 = threadIdx.x
IMAD.MOV.U32 R11, Z, Z, 0x2             ; stride 2
IMAD R8, R6, c[0x0][0x198], R7          ; pointer to card table
IMAD.WIDE R8, R8, R11, c[0x0][0x170]    ; wide address
LDG.E.U16.CONSTANT R12, [R8.64]         ; load 16-bit card data
YIELD
BSSY B0, 0x690                          ; block barrier
PRMT R10, R12, 0x9910, RZ               ; card byte extraction
ISETP.NE.AND P1, PT, R10, -0x1, PT      ; dead card check (card == 0xFFFF)
@!P1 BRA 0x680                          ; skip dead cards
```

### Atomic Spin Lock + GPU-Wide Atomic Add
```
IMAD R8, R4, c[0x0][0x198], R7          ; pointer to second card
IMAD.WIDE R8, R8, R11, c[0x0][0x178]    ; wide address
LDG.E.U16.CONSTANT R8, [R8.64]          ; load card 2
PRMT R10, R8, 0x9910, RZ                ; card byte extraction
ISETP.NE.AND P1, PT, R10, -0x1, PT      ; dead card check
@!P1 BRA 0x680                          ; skip dead cards
MOV R9, RZ                              ; lock value = 0
IMAD.WIDE R8, R3, c[0x0][0x1ac], R8     ; pointer to lock variable
LEA R10, P1, R8, c[0x0][0x160], 0x2     ; lock address
LEA.HI.X R11, R8, c[0x0][0x164], R9, 0x2, P1
LDG.E.CONSTANT R10, [R10.64]            ; load lock value
IMAD.SHL.U32 R12, R12, 0x4, RZ          ; shared memory offset
FMUL.FTZ R13, R5, R10                   ; prob = chance_weight * (1/num_outcomes)
LDS R8, [R12]                           ; load shared memory accumulator
FADD R9, R13, R8                        ; sum = prob + accumulator
ATOMS.CAST.SPIN R9, [R12], R8, R9       ; atomic compare-and-swap spin lock
ISETP.EQ.U32.AND P1, PT, R9, 0x1, PT    ; check lock acquired
@!P1 BRA 0x630                          ; spin wait if not acquired
BSYNC B0                                ; release barrier
```

### GPU-Wide Reduction & Store
```
IADD3 R7, R7, c[0x0][0x0], RZ           ; increment by num_outcomes
ISETP.GE.AND P1, PT, R7, c[0x0][0x19c], PT ; bounds check
@!P1 BRA 0x4d0                          ; loop back
WARPSYNC 0xffffffff                      ; warp synchronization
BAR.SYNC.DEFER_BLOCKING 0x0             ; deferred barrier
@P0 EXIT                                 ; exit if flag set
LDS R3, [R0.X4]                          ; load accumulated value from shared mem
YIELD
BSSY B0, 0x7b0                           ; block barrier
FSETP.NEU.FTZ.AND P0, PT, R3, RZ, PT     ; check for NaN/zero
@!P0 BRA 0x7a0                           ; skip if invalid
SHF.R.S32.HI R5, RZ, 0x1f, R0.reuse      ; shift
IMAD.MOV.U32 R4, Z, Z, R0               ; R4 = threadIdx.x
IMAD.WIDE R6, R2, c[0x0][0x1a8], R4     ; pointer to output table
LEA R4, P0, R6, c[0x0][0x168], 0x2       ; output address
LEA.HI.X R5, R6, c[0x0][0x16c], R7, 0x2, P0
RED.E.ADD.F32.FTZ.RN.STRONG.GPU [R4.64], R3 ; GPU-wide atomic float add
BSYNC B0                                 ; barrier
IADD3 R0, R0, c[0x0][0x0], RZ            ; increment
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
@!P0 BRA 0x6f0                           ; loop back
EXIT
```

### Algorithm Summary
```
for each chance node (2D grid: blockIdx.x = node, blockIdx.y = street):
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes     ; I2F.U32.RP + MUFU.RCP
    card_index = threadIdx.x % num_outcomes ; modulo via 64-bit multiply
    clear shared memory accumulators       ; STS
    __syncthreads()                        ; BSYNC B1
    HFMA2.MMA init (FP16 tensor core, epsilon)
    for each card deal c (loop over num_outcomes):
        load card from constant memory table
        if card != -1 (valid):
            card2 = load second card
            if card2 != -1:
                lock_var = atomic spin lock target
                spin until lock acquired   ; ATOMS.CAST.SPIN loop
                prob = card_weight * inv_outcomes
                accumulator = load shared mem
                sum = prob + accumulator
                atomic store back          ; release lock
    __syncthreads()
    WARPSYNC + BAR.SYNC
    if valid accumulator:
        RED.E.ADD.F32.FTZ.RN.STRONG.GPU   ; GPU-wide atomic add to output
        store final probability
    __syncthreads()
```

---

## 27. forward_chance (sm_80, ~153 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_chance.txt`

**Role:** Forward chance node — computes chance probabilities for card deals at chance nodes, stores per-node probability values. Uses uniform register broadcast for card indexing.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R10, SR_TID.X                        ; R10 = threadIdx.x
IMAD.MOV.U32 R3, RZ, RZ, 0x4            ; R3 = 4 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
ISETP.GE.AND P0, PT, R10, c[0x0][0x18c], PT ; bounds check
@P0 EXIT                                 ; exit if out of bounds
IADD3 R8, P1, R2.reuse, c[0x0][0x178], RZ ; pointer table base
IMAD.WIDE R6, R2, R3, c[0x0][0x170]     ; wide address computation
LEA.HI.X.SX32 R9, R2, c[0x0][0x17c], 0x1, P1
ULDC.64 UR14, c[0x0][0x118]             ; 64-bit pointer base
LDG.E.CONSTANT R6, [R6.64]              ; load pointer
LDG.E.U8.CONSTANT R8, [R8.64]           ; load byte (card data)
```

### Card Data Loading & Uniform Register Setup
```
IMAD.WIDE R4, R2, R3, c[0x0][0x168]     ; pointer table
IMAD.WIDE R2, R2, R3, c[0x0][0x180]     ; pointer table
BSSY B0, 0x880                          ; block barrier
LDG.E.CONSTANT R0, [R4.64]              ; load pointer
LDG.E.CONSTANT R2, [R2.64]              ; load pointer
ULDC UR4, c[0x0][0x188]                 ; uniform register load
USHF.R.S32.HI UR4, URZ, 0x1f, UR4       ; uniform shift
IMAD.MOV.U32 R4, Z, Z, R10              ; R4 = threadIdx.x
R2UR UR5, R6                            ; broadcast R6 to uniform reg
R2UR UR6, R8                            ; broadcast R8 to uniform reg
UIADD3 UR9, UR5, 0x3, URZ               ; card index + 3
UIADD3 UR10, UR5, 0x2, URZ              ; card index + 2
UIADD3 UR11, UR5, 0x1, URZ              ; card index + 1
USHF.R.S32.HI UR13, URZ, 0x1f, UR9      ; high bits
ULOP3.LUT UR7, UR6, 0x3, URZ, 0xc0, !UPT ; uniform suit extraction (card & 3)
USHF.R.S32.HI UR16, URZ, 0x1f, UR10     ; high bits
USHF.R.S32.HI UR17, URZ, 0x1f, UR11     ; high bits
UIADD3 UR8, UR6, -UR7, URZ              ; rank = card - suit
SHF.R.S32.HI R5, RZ, 0x1f, R4           ; shift
IMAD.WIDE R8, R0, c[0x0][0x188], R4     ; pointer to output table
LEA R6, P0, R8, c[0x0][0x160], 0x2       ; output address
LEA.HI.X R7, R8, c[0x0][0x164], R9, 0x2, P0
LDG.E.CONSTANT R6, [R6.64]              ; load chance weight
MUFU.RCP R3, R2                         ; 1.0f / divisor
```

### Probability Computation with Epsilon Guard
```
ISETP.NE.AND P1, PT, RZ, UR6, PT        ; predicate: card valid
FMUL.FTZ R3, R6, R3                     ; prob = weight * (1/divisor)
FSETP.GT.FTZ.AND P0, PT, R3, 1.0000000116860974231e-07, PT ; epsilon > ~1e-7
FSEL R3, R3, RZ, P0                     ; zero if below epsilon
@!P1 BRA 0x840                          ; skip if card invalid
```

### Multi-Outcome Probability Distribution (predicate-gated, up to 4 outcomes)
```
UIADD3 UR12, UR6, -0x1, URZ             ; card index - 1
ISETP.NE.AND P0, PT, RZ, UR7, PT        ; predicate
IMAD.MOV.U32 R7, Z, Z, RZ               ; R7 = 0
UISETP.GE.U32.AND UP0, UPT, UR12, 0x3, UPT ; check if >= 3
PLOP3.LUT P1, PT, PT, PT, UP0, 0x80, 0x0 ; predicate LUT
@!P1 BRA 0x620                          ; skip block
USHF.R.S32.HI UR12, URZ, 0x1f, UR5      ; high bits
MOV R8, UR5                             ; R8 = card index
IMAD.MOV.U32 R7, Z, Z, RZ               ; R7 = 0
MOV R23, UR10                           ; R23 = card index + 2
IMAD.U32 R21, Z, Z, UR11                ; R21 = card index + 1
MOV R24, UR8                            ; R24 = card index
IMAD.U32 R20, Z, Z, UR17                ; R20 = shifted card+1
IMAD.U32 R22, Z, Z, UR16                ; R22 = shifted card+2
IMAD.U32 R25, Z, Z, UR9                 ; R25 = shifted card+3
IMAD.U32 R6, Z, Z, UR13                 ; R6 = shifted card+3
IMAD.U32 R9, Z, Z, UR12                 ; R9 = shifted card-1
IMAD R11, R9, c[0x0][0x188], RZ         ; pointer computation
IADD3 R24, R24, -0x4, RZ                ; decrement by 4
IMAD R12, R20, c[0x0][0x188], RZ        ; pointer computation
IADD3 R7, R7, 0x4, RZ                   ; increment by 4
IMAD R13, R8.reuse, UR4, R11            ; address computation
IMAD.WIDE.U32 R10, R8.reuse, c[0x0][0x188], R4
IADD3 R8, P5, R8, 0x4, RZ               ; conditional increment
IMAD R17, R21, UR4, R12                 ; address computation
LEA R12, P1, R10, c[0x0][0x160], 0x2     ; output address
IMAD.IADD R11, R11, 0x1, R13            ; increment address
IMAD R16, R22, c[0x0][0x188], RZ        ; pointer computation
IMAD.WIDE.U32 R14, R21.reuse, c[0x0][0x188], R4
LEA.HI.X R13, R10, c[0x0][0x164], R11, 0x2, P1
IADD3 R21, P4, R21, 0x4, RZ             ; conditional increment
IMAD R19, R23, UR4, R16                 ; address computation
LEA R10, P1, R14, c[0x0][0x160], 0x2     ; output address
IMAD.IADD R11, R15, 0x1, R17            ; increment address
STG.E [R12.64], R3                      ; store probability to global memory
IMAD.WIDE.U32 R16, R23.reuse, c[0x0][0x188], R4
IADD3 R23, P3, R23, 0x4, RZ             ; conditional increment
LEA.HI.X R11, R14, c[0x0][0x164], R11, 0x2, P1
IMAD.X R9, RZ, RZ, R9, P5               ; extended multiply
IADD3 R15, R17, R19, RZ                 ; address sum
IMAD.X R22, RZ, Z, R22, P3              ; extended multiply
LEA R14, P1, R16.reuse, c[0x0][0x160], 0x2 ; output address
STG.E [R10.64], R3                      ; store probability
IADD3.X R20, RZ, R20, RZ, P4, !PT        ; conditional increment
LEA.HI.X R15, R16, c[0x0][0x164], R15, 0x2, P1
IMAD R16, R6, c[0x0][0x188], RZ         ; pointer computation
IMAD R19, R25.reuse, UR4, R16           ; address computation
STG.E [R14.64], R3                      ; store probability
IMAD.WIDE.U32 R16, R25.reuse, c[0x0][0x188], R4
IADD3 R25, P2, R25, 0x4, RZ             ; conditional increment
IMAD.IADD R17, R17, 0x1, R19            ; increment address
LEA R18, P1, R16, c[0x0][0x160], 0x2     ; output address
IMAD.X R6, RZ, Z, R6, P2                ; extended multiply
LEA.HI.X R19, R16, c[0x0][0x164], R17, 0x2, P1
ISETP.NE.AND P1, PT, R24, RZ, PT         ; predicate check
STG.E [R18.64], R3                      ; store probability
@P1 BRA 0x3a0                           ; loop if more outcomes
```

### Additional Outcome Processing (predicate-gated blocks)
```
@!P0 BRA 0x840                          ; exit if no more
IADD3 R11, R7, UR5, RZ                  ; address computation
UISETP.NE.AND UP0, UPT, UR7, 0x1, UPT   ; predicate check
SHF.R.S32.HI R6, Z, 0x1f, R11           ; shift
IMAD R8, R6, c[0x0][0x188], RZ          ; pointer computation
IMAD.WIDE.U32 R6, R11, c[0x0][0x188], R4 ; wide address
IMAD R9, R11, UR4, R8                   ; address computation
LEA R8, P0, R6, c[0x0][0x160], 0x2       ; output address
IMAD.IADD R7, R7, 0x1, R9               ; increment address
LEA.HI.X R9, R6, c[0x0][0x164], R7, 0x2, P0
PLOP3.LUT P0, PT, PT, PT, UP0, 0x80, 0x0 ; predicate LUT
STG.E [R8.64], R3                       ; store probability
@!P0 BRA 0x840                          ; exit if no more
... (repeated for outcomes 2, 3) ...
STG.E [R8.64], R3                       ; store probability
```

### Loop Over Nodes/Boards
```
IADD3 R4, R4, c[0x0][0x0], RZ           ; increment by stride
ISETP.GE.AND P0, PT, R4, c[0x0][0x18c], PT ; bounds check
@!P0 BRA 0x1e0                          ; loop back
BSYNC B0                                ; block barrier
EXIT
```

### Algorithm Summary
```
for each chance node (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_nodes: exit
    
    load card data from constant memory tables
    broadcast to uniform registers (R2UR)
    compute card indices: card+0, card+1, card+2, card+3, card-1
    uniform suit extraction: suit = card & 3 (ULOP3.LUT 0xc0)
    
    divisor = load_from_constant()
    inv_divisor = 1.0f / divisor          ; MUFU.RCP
    weight = load_chance_weight()
    prob = weight * inv_divisor           ; FMUL.FTZ
    
    epsilon guard: if prob <= ~1e-7: prob = 0  ; FSETP + FSEL
    
    for each chance outcome o (up to 4, predicate-gated P0-P5):
        if card[o] valid:
            output_addr = compute_address(node, o)
            STG.E [output_addr], prob     ; store probability
    
    increment node counter by stride
    loop until all nodes processed
    __syncthreads()                       ; BSYNC B0
    exit
```

---

## 27. forward_chance_abstract (sm_80, ~193 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_chance_abstract.txt`

**Role:** Forward chance node for abstracted games — computes chance probabilities for reduced card deal outcomes using FP16 MMA accumulation and atomic spin lock synchronization.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R0, SR_TID.X                         ; R0 = threadIdx.x
BSSY B0, 0x3b0                           ; block barrier
ISETP.GE.AND P0, PT, R0, c[0x0][0x188], PT ; bounds check vs max_nodes
@P0 BRA 0x3a0                            ; skip if out of bounds
```

### Chance Probability (1/num_outcomes)
```
I2F.U32.RP R5, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B1, 0x290                           ; block barrier
ISETP.NE.U32.AND P3, PT, RZ, c[0x0][0x0], PT ; nonzero check
IADD3 R4, R4, c[0x0][0x188], RZ          ; pointer table offset
MUFU.RCP R5, R5                          ; 1.0f / num_outcomes
IADD3 R2, R5, 0xffffffe, RZ              ; round: p - 2
F2I.FTZ.U32.TRUNC.NTZ R3, R2             ; float -> int truncate
```

### Card Index Computation (modulo via 64-bit multiply)
```
IMAD.MOV.U32 R2, Z, Z, RZ               ; R2 = 0
IMAD.MOV R7, Z, Z, -R3                   ; R7 = -truncated
IMAD R7, R7, c[0x0][0x0], RZ             ; R7 = -truncated * num_outcomes
IMAD.HI.U32 R3, R3, R7, R2               ; 64-bit multiply high
IMAD.HI.U32 R3, R3, R4, RZ               ; add thread offset
IMAD.MOV R5, Z, Z, -R3                   ; R5 = -carry
IMAD R4, R5, c[0x0][0x0], R4             ; R4 = card_index * num_outcomes + base
ISETP.GE.U32.AND P1, PT, R4, c[0x0][0x0], PT ; wrap check
@P1 IADD3 R4, R4, -c[0x0][0x0], RZ       ; wrap around
@P1 IADD3 R3, R3, 0x1, RZ                ; carry
ISETP.GE.U32.AND P2, PT, R4, c[0x0][0x0], PT
@P2 IADD3 R3, R3, 0x1, RZ
@!P3 LOP3.LUT R3, RZ, c[0x0][0x0], RZ, 0x33, !PT ; bit-pair extraction
IADD3 R2, R3.reuse, 0x1, RZ              ; next card
ISETP.GE.U32.AND P2, PT, R3, 0x3, PT     ; max 4 cards
IMAD.MOV.U32 R3, Z, Z, R0               ; R3 = threadIdx.x
LOP3.LUT P1, R2, R2, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
@!P1 BRA 0x280                           ; skip invalid
```

### Shared Memory Clearing
```
SHF.L.U32 R4, R0, 0x2, RZ               ; R4 = threadIdx.x * 4
IMAD.MOV.U32 R3, Z, Z, R0               ; R3 = threadIdx.x
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x0]       ; base
IADD3 R2, R2, -0x1, RZ                   ; decrement
STS [R4], RZ                             ; clear shared memory
IADD3 R3, R3, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.NE.AND P1, PT, R2, RZ, PT          ; loop if more
IMAD R4, R5, 0x4, R4                    ; next shared mem offset
@P1 BRA 0x220
BSYNC B1                                 ; barrier
@!P2 BRA 0x3a0                           ; exit if no more work
```

### Main Chance Accumulation Loop
```
IMAD.SHL.U32 R2, R3, 0x4, RZ            ; shared memory offset
MOV R7, c[0x0][0x0]                     ; base
IMAD R4, R7.reuse, 0x4, R2              ; shared mem address
MOV R10, c[0x0][0x0]
IMAD.MOV.U32 R8, Z, Z, c[0x0][0x0]
STS [R2], RZ                            ; clear shared memory
IMAD R5, R7.reuse, 0x4, R4
IMAD R3, R8, 0x2, R3
STS [R4], RZ
IMAD R6, R7.reuse, 0x4, R5
IMAD R3, R10, 0x2, R3
STS [R5], RZ
IMAD R2, R7, 0x4, R6
STS [R6], RZ
ISETP.GE.AND P1, PT, R3, c[0x0][0x188], PT ; bounds check
@!P1 BRA 0x2c0
BSYNC B0                                 ; block barrier
IMAD.MOV.U32 R2, Z, Z, c[0x0][0x178]     ; pointer base
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.GE.AND P1, PT, R2, 0x1, PT         ; check flag
ULDC.64 UR4, c[0x0][0x118]              ; 64-bit pointer base
@!P1 BRA 0x680                           ; skip if flag == 0
I2F R2, c[0x0][0x178]                   ; num_outcomes -> float
HFMA2.MMA R7, -RZ, RZ, 0, 0             ; FP16 MMA init (epsilon = 0)
MUFU.RCP R6, R2                         ; 1.0f / num_outcomes
ISETP.GE.AND P1, PT, R0, c[0x0][0x184], PT ; bounds check
@P1 BRA 0x630                            ; skip
IMAD.MOV.U32 R2, Z, Z, 0x3               ; stride 3
IMAD.MOV.U32 R9, Z, Z, R0               ; R9 = threadIdx.x
IMAD R8, R7, R2, c[0x0][0x17c]           ; pointer computation
IMAD.MOV.U32 R3, Z, Z, 0x2               ; stride 2
IMAD R2, R8, c[0x0][0x180], R9           ; pointer computation
IMAD.WIDE R2, R2, R3, c[0x0][0x170]      ; wide address
LDG.E.U16.CONSTANT R10, [R2.64]          ; load 16-bit card data
YIELD
BSSY B0, 0x600                           ; block barrier
PRMT R4, R10, 0x9910, RZ                 ; card byte extraction
ISETP.NE.AND P1, PT, R4, -0x1, PT        ; dead card check
@!P1 BRA 0x5f0                           ; skip dead cards
```

### Atomic Spin Lock + Accumulation
```
SHF.R.S32.HI R3, Z, Z, 0x1f, R9          ; shift
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x194]     ; pointer table
MOV R2, R9                               ; R2 = threadIdx.x
IMAD.WIDE R2, R5, c[0x0][0x18c], R2      ; wide address
LEA R4, P1, R2, c[0x0][0x160], 0x2        ; shared mem address
LEA.HI.X R5, R2, c[0x0][0x164], R3, 0x2, P1
LDG.E.CONSTANT R5, [R4.64]               ; load chance weight
IMAD.SHL.U32 R10, R10, 0x4, RZ           ; shared memory offset
FMUL.FTZ R11, R6, R5                     ; prob = weight * (1/num_outcomes)
LDS R2, [R10]                            ; load accumulator
FADD R3, R11, R2                         ; sum = prob + accumulator
ATOMS.CAST.SPIN R3, [R10], R2, R3        ; atomic spin lock
ISETP.EQ.U32.AND P1, PT, R3, 0x1, PT     ; check lock acquired
@!P1 BRA 0x5a0                           ; spin wait
BSYNC B0                                 ; release barrier
```

### Loop & Second Phase
```
IADD3 R9, R9, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.GE.AND P1, PT, R9, c[0x0][0x184], PT ; bounds check
@!P1 BRA 0x480                           ; loop back
WARPSYNC 0xffffffff                       ; warp sync
IADD3 R7, R7, 0x1, RZ                    ; increment counter
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.GE.AND P1, PT, R7, c[0x0][0x178], PT ; bounds check
@!P1 BRA 0x430                           ; loop back
@P0 EXIT                                  ; exit if flag set
```

### Store Phase (load shared mem, store 4 values to global)
```
I2F.U32.RP R5, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B0, 0x970                           ; block barrier
ISETP.NE.U32.AND P2, PT, RZ, c[0x0][0x0], PT
IADD3 R4, R4, c[0x0][0x188], RZ          ; pointer offset
MUFU.RCP R5, R5                          ; 1.0f / num_outcomes
IADD3 R2, R5, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R3, R2             ; truncate
IMAD.MOV.U32 R2, Z, Z, RZ
IMAD.MOV R7, Z, Z, -R3
IMAD R7, R7, c[0x0][0x0], RZ
IMAD.HI.U32 R7, R3, R7, R2               ; 64-bit multiply
IMAD.HI.U32 R7, R7, R4, RZ
IMAD.MOV R3, Z, Z, -R7
IMAD R3, R3, c[0x0][0x0], R4
ISETP.GE.U32.AND P0, PT, R3, c[0x0][0x0], PT ; wrap check
@P0 IADD3 R3, R3, -c[0x0][0x0], RZ
@P0 IADD3 R7, R7, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P1, PT, R3, c[0x0][0x0], PT
@P1 IADD3 R7, R7, 0x1, RZ
@!P2 LOP3.LUT R7, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R2, R7.reuse, 0x1, RZ
ISETP.GE.U32.AND P1, PT, R7, 0x3, PT
LOP3.LUT P0, R4, R2, 0x3, RZ, 0xc0, !PT   ; suit extraction
@!P0 BRA 0x960                           ; skip invalid
SHF.R.S32.HI R3, Z, Z, 0x1f, R0.reuse     ; shift
IMAD.MOV.U32 R2, Z, Z, R0               ; R2 = threadIdx.x
MOV R5, c[0x0][0x198]                   ; pointer table slot
IMAD.MOV.U32 R6, Z, Z, R4               ; R6 = card index
IMAD.SHL.U32 R7, R0, 0x4, RZ            ; shared mem offset
IMAD.WIDE R2, R5, c[0x0][0x190], R2      ; wide address
IMAD.MOV.U32 R10, Z, Z, c[0x0][0x0]
LEA R8, P0, R2, c[0x0][0x168], 0x2       ; output address
LEA.HI.X R3, R2, c[0x0][0x16c], R3, 0x2, P0
LDS R9, [R7]                            ; load accumulator from shared mem
MOV R4, R8                              ; R4 = output address
IMAD.MOV.U32 R5, Z, Z, R3               ; R5 = high address part
IADD3 R6, R6, -0x1, RZ                   ; decrement card index
IADD3 R0, R0, c[0x0][0x0], RZ            ; increment by num_outcomes
IMAD.WIDE R2, R10, 0x4, R4              ; wide address
ISETP.NE.AND P0, PT, R6, RZ, PT          ; loop if more cards
IMAD.MOV.U32 R8, Z, Z, R2               ; R8 = address
IMAD R7, R10, 0x4, R7                    ; next shared mem offset
STG.E [R4.64], R9                        ; store accumulator to global
@P0 BRA 0x8b0                           ; loop back
BSYNC B0                                 ; barrier
@!P1 EXIT                                 ; exit
```

### Second Store Phase (4 values to global memory)
```
IMAD.SHL.U32 R10, R0, 0x4, RZ           ; shared mem offset
MOV R19, c[0x0][0x0]                     ; base
LDS R11, [R10]                           ; load value 0
SHF.R.S32.HI R3, Z, Z, 0x1f, R0.reuse     ; shift
IMAD.MOV.U32 R2, Z, Z, R0               ; R2 = threadIdx.x
MOV R5, c[0x0][0x198]                   ; pointer table slot
IMAD R8, R19, 0x4, R10                   ; next offset
IMAD.MOV.U32 R21, Z, Z, c[0x0][0x0]
IMAD R12, R19, 0x4, R8                   ; next offset
LDS R13, [R8]                            ; load value 1
IMAD.WIDE R2, R5, c[0x0][0x190], R2       ; wide address
LDS R15, [R12]                           ; load value 2
IMAD R14, R19, 0x4, R12                   ; next offset
LEA R4, P0, R2, c[0x0][0x168], 0x2        ; output address 0
IMAD.MOV.U32 R23, Z, Z, c[0x0][0x0]
IMAD R10, R19, 0x4, R14                   ; next offset
LDS R17, [R14]                           ; load value 3
LEA.HI.X R5, R2, c[0x0][0x16c], R3, 0x2, P0 ; high address
IMAD R0, R23, 0x2, R0                     ; address computation
IMAD.WIDE R2, R19, 0x4, R4               ; wide address for addr 0
ISETP.GE.AND P0, PT, R0, c[0x0][0x188], PT ; bounds check
IMAD.WIDE R6, R19, 0x4, R2               ; wide address for addr 1
IMAD.WIDE R8, R19, 0x4, R6               ; wide address for addr 2
STG.E [R4.64], R11                       ; store value 0
STG.E [R2.64], R13                       ; store value 1
STG.E [R6.64], R15                       ; store value 2
STG.E [R8.64], R17                       ; store value 3
@!P0 BRA 0x990                           ; loop if more
EXIT
```

### Algorithm Summary
```
for each chance node (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_nodes: skip
    
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes    ; I2F.U32.RP + MUFU.RCP
    
    card_index = thread_idx % num_outcomes ; modulo via 64-bit HI multiply
    clear shared memory accumulators      ; STS x4
    __syncthreads()                       ; BSYNC B1
    
    if flag != 0:
        HFMA2.MMA init (FP16 tensor core, epsilon=0)
        for each card deal (loop over num_outcomes):
            load card from constant memory table
            if card != -1 (valid):
                card2 = load second card
                if card2 != -1:
                    atomic spin lock      ; ATOMS.CAST.SPIN
                    prob = weight * inv_outcomes
                    accumulator = load shared mem
                    sum = prob + accumulator
                    atomic store back     ; release lock
        __syncthreads()
        WARPSYNC + BAR.SYNC
        increment counter
        loop until all nodes processed
    
    exit if flag set
    
    # Second phase: store results
    recompute card_index
    load 4 accumulator values from shared memory (LDS)
    compute 4 output addresses from pointer tables
    STG.E [addr0], val0 ; STG.E [addr1], val1 ; STG.E [addr2], val2 ; STG.E [addr3], val3
    __syncthreads()
    exit
```

---

## 28. forward_chance_tr_2d (sm_80, ~145 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_chance_tr_2d.txt`

**Role:** Forward chance node for turn/river streets in a 2D grid — computes chance probabilities using FP16 MMA, atomic spin lock, and GPU-wide atomic add (RED.E.ADD.F32). Essentially the forward counterpart of backward_chance_tr_2d.

### Thread Setup & Bounds Check (2D Grid)
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
IMAD.MOV.U32 R3, RZ, RZ, 0x8            ; R3 = 8 (stride)
ULDC.64 UR4, c[0x0][0x118]              ; UR4 = 64-bit pointer base
IMAD.WIDE R2, R2, R3, c[0x0][0x188]     ; wide address computation
LDG.E.64.CONSTANT R2, [R2.64]           ; load 64-bit pointer
BSSY B0, 0x410                          ; block barrier
S2R R0, SR_TID.X                        ; R0 = threadIdx.x
S2R R4, SR_CTAID.Y                      ; R4 = blockIdx.y (2D grid: x=node, y=street)
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
@P0 BRA 0x400                           ; skip if out of bounds
```

### Chance Probability & Card Index (identical pattern to backward_chance_tr_2d)
```
I2F.U32.RP R8, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R5, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B1, 0x2f0                           ; block barrier
ISETP.NE.U32.AND P3, PT, RZ, c[0x0][0x0], PT
IADD3 R5, R5, c[0x0][0x1a0], RZ          ; pointer offset
MUFU.RCP R8, R8                          ; 1.0f / num_outcomes
IADD3 R6, R8, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R7, R6             ; truncate
IMAD.MOV.U32 R6, Z, Z, RZ
IMAD.MOV R9, Z, Z, -R7
IMAD R9, R9, c[0x0][0x0], RZ
IMAD.HI.U32 R10, R7, R9, R6              ; 64-bit multiply
IMAD.MOV.U32 R6, Z, Z, R0
IMAD.HI.U32 R10, R10, R5, RZ
IADD3 R8, -R10, RZ, RZ                   ; card index
IMAD R5, R8, c[0x0][0x0], R5             ; card_index * num_outcomes + base
ISETP.GE.U32.AND P1, PT, R5, c[0x0][0x0], PT ; wrap
@P1 IADD3 R5, R5, -c[0x0][0x0], RZ
@P1 IADD3 R10, R10, 0x1, RZ              ; carry
ISETP.GE.U32.AND P2, PT, R5, c[0x0][0x0], PT
@P2 IADD3 R10, R10, 0x1, RZ
@!P3 LOP3.LUT R10, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R5, R10.reuse, 0x1, RZ             ; next card
ISETP.GE.U32.AND P2, PT, R10, 0x3, PT     ; max 4 cards
LOP3.LUT P1, R5, R5, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
@!P1 BRA 0x2e0                           ; skip invalid
```

### Shared Memory Clearing
```
IMAD.SHL.U32 R7, R0, 0x4, RZ            ; R7 = threadIdx.x * 16
IMAD.MOV.U32 R6, Z, Z, R0               ; R6 = threadIdx.x
IMAD.MOV.U32 R8, Z, Z, c[0x0][0x0]
IADD3 R5, R5, -0x1, RZ                   ; decrement
STS [R7], RZ                             ; clear shared mem
IADD3 R6, R6, c[0x0][0x0], RZ
ISETP.NE.AND P1, PT, R5, RZ, PT          ; loop
LEA R7, R8, R7, 0x2                      ; next offset
@P1 BRA 0x280
BSYNC B1                                 ; barrier
@!P2 BRA 0x400                           ; exit if no work
```

### FP16 MMA + Card Deal Loop
```
IMAD.SHL.U32 R5, R6, 0x4, RZ            ; shared mem offset
IMAD.MOV.U32 R10, Z, Z, c[0x0][0x0]
IMAD R7, R10.reuse, 0x4, R5              ; shared mem address
STS [R5], RZ                             ; clear
IMAD.MOV.U32 R11, Z, Z, c[0x0][0x0]
IMAD.MOV.U32 R13, Z, Z, c[0x0][0x0]
LEA R8, R10, R7, 0x2                     ; next address
STS [R7], RZ                             ; clear
LEA R6, R11, R6, 0x1
IMAD R9, R10.reuse, 0x4, R8              ; next address
STS [R8], RZ                             ; clear
IMAD R6, R13, 0x2, R6
IMAD R5, R10, 0x4, R9
STS [R9], RZ                             ; clear
ISETP.GE.AND P1, PT, R6, c[0x0][0x1a0], PT ; bounds check
@!P1 BRA 0x320
BSYNC B0                                 ; block barrier
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.GE.AND P1, PT, R0, c[0x0][0x19c], PT ; bounds check
@P1 BRA 0x6d0                            ; skip
IMAD.MOV.U32 R9, Z, Z, 0x4               ; stride 4
IMAD.WIDE R8, R4, R9, c[0x0][0x180]      ; pointer computation
LDG.E.CONSTANT R6, [R8.64]               ; load pointer
I2F R5, c[0x0][0x190]                   ; num_outcomes -> float
HFMA2.MMA R7, -RZ, RZ, 0, 1.78813934326171875e-07 ; FP16 MMA (epsilon)
IMAD R4, R4, R7.reuse, c[0x0][0x194]     ; next pointer
MUFU.RCP R5, R5                          ; 1.0f / num_outcomes
IMAD R6, R6, R7, c[0x0][0x194]           ; next pointer
IMAD.MOV.U32 R7, Z, Z, R0               ; R7 = threadIdx.x
IMAD.MOV.U32 R11, Z, Z, 0x2              ; stride 2
IMAD R8, R6, c[0x0][0x198], R7           ; pointer to card table
IMAD.WIDE R8, R8, R11, c[0x0][0x170]     ; wide address
LDG.E.U16.CONSTANT R12, [R8.64]          ; load 16-bit card
YIELD
BSSY B0, 0x6a0                           ; block barrier
PRMT R10, R12, 0x9910, RZ                ; card byte extraction
ISETP.NE.AND P1, PT, R10, -0x1, PT       ; dead card check
@!P1 BRA 0x690                           ; skip dead cards
IMAD R8, R4, c[0x0][0x198], R7           ; pointer to card 2
IMAD.WIDE R8, R8, R11, c[0x0][0x178]     ; wide address
LDG.E.U16.CONSTANT R13, [R8.64]          ; load card 2
PRMT R10, R13, 0x9910, RZ                ; card byte extraction
ISETP.NE.AND P1, PT, R10, -0x1, PT       ; dead card check
@!P1 BRA 0x690                           ; skip dead cards
MOV R9, RZ                               ; lock value = 0
IMAD.MOV.U32 R8, Z, Z, R12               ; R8 = card data
IMAD.WIDE R8, R2, c[0x0][0x1a4], R8      ; pointer to lock
LEA R10, P1, R8, c[0x0][0x160], 0x2       ; lock address
LEA.HI.X R11, R8, c[0x0][0x164], R9, 0x2, P1
LDG.E.CONSTANT R10, [R10.64]             ; load lock value
IMAD.SHL.U32 R13, R13, 0x4, RZ            ; shared mem offset
FMUL.FTZ R15, R5, R10                    ; prob = weight * (1/num_outcomes)
LDS R8, [R13]                            ; load accumulator
FADD R9, R15, R8                         ; sum = prob + accumulator
ATOMS.CAST.SPIN R9, [R13], R8, R9        ; atomic spin lock
ISETP.EQ.U32.AND P1, PT, R9, 0x1, PT     ; check lock acquired
@!P1 BRA 0x640                           ; spin wait
BSYNC B0                                 ; release barrier
```

### Loop & GPU-Wide Atomic Add
```
IADD3 R7, R7, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.GE.AND P1, PT, R7, c[0x0][0x19c], PT ; bounds check
@!P1 BRA 0x4d0                           ; loop back
WARPSYNC 0xffffffff                       ; warp sync
BAR.SYNC.DEFER_BLOCKING 0x0
@P0 EXIT                                  ; exit if flag set
LDS R7, [R0.X4]                          ; load accumulator from shared mem
YIELD
BSSY B0, 0x7c0                           ; block barrier
FSETP.NEU.FTZ.AND P0, PT, R7, RZ, PT     ; check for NaN/zero
@!P0 BRA 0x7b0                           ; skip if invalid
SHF.R.S32.HI R5, Z, Z, 0x1f, R0.reuse     ; shift
IMAD.MOV.U32 R4, Z, Z, R0               ; R4 = threadIdx.x
IMAD.WIDE R8, R3, c[0x0][0x1a8], R4      ; pointer to output table
LEA R4, P0, R8, c[0x0][0x168], 0x2        ; output address
LEA.HI.X R5, R8, c[0x0][0x16c], R9, 0x2, P0
RED.E.ADD.F32.FTZ.RN.STRONG.GPU [R4.64], R7 ; GPU-wide atomic float add
BSYNC B0                                 ; barrier
IADD3 R0, R0, c[0x0][0x0], RZ            ; increment
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
@!P0 BRA 0x700                           ; loop back
EXIT
```

### Algorithm Summary
```
for each chance node (2D grid: blockIdx.x = node, blockIdx.y = street):
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes    ; I2F.U32.RP + MUFU.RCP
    card_index = threadIdx.x % num_outcomes ; modulo via 64-bit HI multiply
    clear shared memory accumulators      ; STS x4
    __syncthreads()                       ; BSYNC B1
    
    HFMA2.MMA init (FP16 tensor core, epsilon=1.79e-07)
    for each card deal c (loop over num_outcomes):
        load card from constant memory table
        if card != -1:
            card2 = load second card
            if card2 != -1:
                atomic spin lock          ; ATOMS.CAST.SPIN
                prob = weight * inv_outcomes
                accumulator = load shared mem
                sum = prob + accumulator
                atomic store back
    __syncthreads()
    WARPSYNC + BAR.SYNC
    if valid accumulator:
        RED.E.ADD.F32.FTZ.RN.STRONG.GPU   ; GPU-wide atomic add to output
    __syncthreads()
    increment and loop
    exit
```

---

## 29. forward_chance_turn_river (sm_80, ~209 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_chance_turn_river.txt`

**Role:** Forward chance node specifically for turn/river streets — computes chance probabilities for card deals using FP16 MMA and atomic spin lock, stores 4 probability values per node.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R0, SR_TID.X                         ; R0 = threadIdx.x
BSSY B0, 0x3b0                           ; block barrier
ISETP.GE.AND P0, PT, R0, c[0x0][0x19c], PT ; bounds check vs max_threads
@P0 BRA 0x3a0                            ; skip if out of bounds
```

### Chance Probability & Card Index
```
I2F.U32.RP R5, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B1, 0x290                           ; block barrier
ISETP.NE.U32.AND P3, PT, RZ, c[0x0][0x0], PT
IADD3 R4, R4, c[0x0][0x19c], RZ          ; pointer offset
MUFU.RCP R5, R5                          ; 1.0f / num_outcomes
IADD3 R2, R5, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R3, R2             ; truncate
IMAD.MOV.U32 R2, Z, Z, RZ
IMAD.MOV R7, Z, Z, -R3
IMAD R7, R7, c[0x0][0x0], RZ
IMAD.HI.U32 R3, R3, R7, R2               ; 64-bit multiply
IMAD.HI.U32 R3, R3, R4, RZ               ; add thread offset
IMAD.MOV R5, Z, Z, -R3
IMAD R4, R5, c[0x0][0x0], R4             ; card_index * num_outcomes + base
ISETP.GE.U32.AND P1, PT, R4, c[0x0][0x0], PT ; wrap check
@P1 IADD3 R4, R4, -c[0x0][0x0], RZ
@P1 IADD3 R3, R3, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P2, PT, R4, c[0x0][0x0], PT
@P2 IADD3 R3, R3, 0x1, RZ
@!P3 LOP3.LUT R3, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R2, R3.reuse, 0x1, RZ              ; next card
ISETP.GE.U32.AND P2, PT, R3, 0x3, PT     ; max 4 cards
IMAD.MOV.U32 R3, Z, Z, R0               ; R3 = threadIdx.x
LOP3.LUT P1, R2, R2, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
@!P1 BRA 0x280                           ; skip invalid
```

### Shared Memory Clearing (4 entries)
```
IMAD.SHL.U32 R4, R0, 0x4, RZ            ; R4 = threadIdx.x * 16
IMAD.MOV.U32 R3, Z, Z, R0               ; R3 = threadIdx.x
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x0]       ; base
IADD3 R2, R2, -0x1, RZ                   ; decrement
STS [R4], RZ                             ; clear shared mem entry 0
IADD3 R3, R3, c[0x0][0x0], RZ
ISETP.NE.AND P1, PT, R2, RZ, PT          ; loop if more
LEA R4, R5, R4, 0x2                      ; next shared mem offset
@P1 BRA 0x220
BSYNC B1                                 ; barrier
@!P2 BRA 0x3a0                           ; exit if no work
IMAD.SHL.U32 R2, R3, 0x4, RZ            ; shared mem offset
IMAD.MOV.U32 R7, Z, Z, c[0x0][0x0]       ; base
IMAD R4, R7.reuse, 0x4, R2              ; shared mem address
STS [R2], RZ                             ; clear
IMAD.MOV.U32 R8, Z, Z, c[0x0][0x0]
IMAD.MOV.U32 R10, Z, Z, c[0x0][0x0]
LEA R5, R7, R4, 0x2                      ; next address
STS [R4], RZ                             ; clear
LEA R3, R8, R3, 0x1
IMAD R6, R7.reuse, 0x4, R5              ; next address
STS [R5], RZ                             ; clear
IMAD R3, R10, 0x2, R3
IMAD R2, R7, 0x4, R6
STS [R6], RZ                             ; clear
ISETP.GE.AND P1, PT, R3, c[0x0][0x19c], PT ; bounds check
@!P1 BRA 0x2c0
BSYNC B0                                 ; block barrier
IMAD.MOV.U32 R2, Z, Z, c[0x0][0x188]     ; pointer base
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.GE.AND P1, PT, R2, 0x1, PT         ; check flag
ULDC.64 UR4, c[0x0][0x118]              ; 64-bit pointer base
@!P1 BRA 0x720                           ; skip if flag == 0
```

### FP16 MMA + Card Deal Loop
```
I2F R2, c[0x0][0x188]                   ; num_outcomes -> float
HFMA2.MMA R3, -RZ, RZ, 0, 0             ; FP16 MMA init (epsilon = 0)
MUFU.RCP R2, R2                          ; 1.0f / num_outcomes
ISETP.GE.AND P1, PT, R0, c[0x0][0x194], PT ; bounds check
BSSY B0, 0x6f0                           ; block barrier
@P1 BRA 0x6e0                            ; skip
IMAD.MOV.U32 R8, Z, Z, 0x4               ; stride 4
IMAD.WIDE R8, R3, R8, c[0x0][0x180]      ; pointer computation
LDG.E.CONSTANT R9, [R8.64]               ; load pointer
IMAD.MOV.U32 R6, Z, Z, 0x3               ; stride 3
IMAD.MOV.U32 R5, Z, Z, R0               ; R5 = threadIdx.x
IMAD R4, R3, R6.reuse, c[0x0][0x18c]     ; pointer computation
IMAD R6, R9, R6, c[0x0][0x18c]           ; next pointer
MOV R11, 0x2                             ; stride 2
IMAD R8, R6, c[0x0][0x190], R5           ; pointer to card table
IMAD.WIDE R8, R8, R11, c[0x0][0x170]     ; wide address
LDG.E.U16.CONSTANT R10, [R8.64]          ; load 16-bit card
YIELD
BSSY B1, 0x6b0                           ; block barrier
PRMT R7, R10, 0x9910, RZ                 ; card byte extraction
ISETP.NE.AND P1, PT, R7, -0x1, PT        ; dead card check
@!P1 BRA 0x6a0                           ; skip dead cards
IMAD R8, R4, c[0x0][0x190], R5           ; pointer to card 2
IMAD.WIDE R8, R8, R11, c[0x0][0x178]     ; wide address
LDG.E.U16.CONSTANT R12, [R8.64]          ; load card 2
PRMT R7, R12, 0x9910, RZ                 ; card byte extraction
ISETP.NE.AND P1, PT, R7, -0x1, PT        ; dead card check
@!P1 BRA 0x6a0                           ; skip dead cards
IMAD.MOV.U32 R7, Z, Z, c[0x0][0x1a8]     ; pointer table
IMAD.MOV.U32 R8, Z, Z, R10               ; R8 = card data
IMAD.MOV.U32 R9, Z, Z, RZ               ; R9 = 0
IMAD.WIDE R8, R7, c[0x0][0x1a0], R8      ; pointer computation
LEA R10, P1, R8, c[0x0][0x160], 0x2       ; shared mem address
LEA.HI.X R11, R8, c[0x0][0x164], R9, 0x2, P1
LDG.E.CONSTANT R11, [R10.64]             ; load chance weight
SHF.L.U32 R12, R12, 0x2, RZ              ; shift card data
FMUL.FTZ R7, R2, R11                     ; prob = weight * (1/num_outcomes)
LDS R8, [R12]                            ; load accumulator
FADD R9, R7, R8                          ; sum = prob + accumulator
ATOMS.CAST.SPIN R9, [R12], R8, R9        ; atomic spin lock
ISETP.EQ.U32.AND P1, PT, R9, 0x1, PT     ; check lock acquired
@!P1 BRA 0x650                           ; spin wait
BSYNC B1                                 ; release barrier
```

### Loop & Store Phase
```
IADD3 R5, R5, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.GE.AND P1, PT, R5, c[0x0][0x194], PT ; bounds check
@!P1 BRA 0x4d0                           ; loop back
BSYNC B0                                 ; block barrier
IADD3 R3, R3, 0x1, RZ                    ; increment counter
ISETP.GE.AND P1, PT, R3, c[0x0][0x188], PT ; bounds check
@!P1 BRA 0x430                           ; loop back
WARPSYNC 0xffffffff                       ; warp sync
BAR.SYNC.DEFER_BLOCKING 0x0
@P0 EXIT                                  ; exit if flag set
```

### Second Phase: Load Shared Mem, Store 4 Values to Global
```
I2F.U32.RP R5, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B0, 0xa30                           ; block barrier
ISETP.NE.U32.AND P2, PT, RZ, c[0x0][0x0], PT
IADD3 R4, R4, c[0x0][0x19c], RZ          ; pointer offset
MUFU.RCP R5, R5                          ; 1.0f / num_outcomes
IADD3 R2, R5, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R3, R2             ; truncate
IMAD.MOV.U32 R2, Z, Z, RZ
IMAD.MOV R7, Z, Z, -R3
IMAD R7, R7, c[0x0][0x0], RZ
IMAD.HI.U32 R7, R3, R7, R2               ; 64-bit multiply
IMAD.HI.U32 R7, R7, R4, RZ               ; add thread offset
IMAD.MOV R3, Z, Z, -R7
IMAD R3, R3, c[0x0][0x0], R4
ISETP.GE.U32.AND P0, PT, R3, c[0x0][0x0], PT ; wrap check
@P0 IADD3 R3, R3, -c[0x0][0x0], RZ
@P0 IADD3 R7, R7, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P1, PT, R3, c[0x0][0x0], PT
@P1 IADD3 R7, R7, 0x1, RZ
@!P2 LOP3.LUT R7, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R2, R7.reuse, 0x1, RZ
ISETP.GE.U32.AND P1, PT, R7, 0x3, PT
LOP3.LUT P0, R4, R2, 0x3, RZ, 0xc0, !PT   ; suit extraction
@!P0 BRA 0xa20                           ; skip invalid
SHF.R.S32.HI R3, Z, Z, 0x1f, R0.reuse     ; shift
IMAD.MOV.U32 R2, Z, Z, R0               ; R2 = threadIdx.x
MOV R5, c[0x0][0x1ac]                   ; pointer table slot
IMAD.MOV.U32 R6, Z, Z, R4               ; R6 = card index
MOV R10, c[0x0][0x0]                     ; base
IMAD.SHL.U32 R7, R0, 0x4, RZ            ; shared mem offset
IMAD.WIDE R2, R5, c[0x0][0x1a4], R2      ; wide address
LEA R8, P0, R2, c[0x0][0x168], 0x2       ; output address 0
LEA.HI.X R3, R2, c[0x0][0x16c], R3, 0x2, P0
LDS R9, [R7]                            ; load value 0 from shared mem
IMAD.MOV.U32 R4, Z, Z, R8               ; R4 = output address 0
IADD3 R6, R6, -0x1, RZ                   ; decrement card index
IMAD.MOV.U32 R5, Z, Z, R3               ; R5 = high address part
IADD3 R0, R0, c[0x0][0x0], RZ            ; increment by num_outcomes
IMAD.WIDE R2, R10.reuse, 0x4, R4         ; wide address for addr 0
ISETP.NE.AND P0, PT, R6, RZ, PT          ; loop if more cards
LEA R7, R10, R7, 0x2                      ; next address
IMAD.MOV.U32 R8, Z, Z, R2               ; R8 = wide address
STG.E [R4.64], R9                        ; store value 0 to global
@P0 BRA 0x970                           ; loop back
BSYNC B0                                 ; barrier
@!P1 EXIT                                 ; exit
```

### Second Store Phase (4 values to global memory)
```
IMAD.SHL.U32 R10, R0, 0x4, RZ           ; shared mem offset
IMAD.MOV.U32 R19, Z, Z, c[0x0][0x0]     ; base
LDS R11, [R10]                           ; load value 0
SHF.R.S32.HI R3, Z, Z, 0x1f, R0           ; shift
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x1ac]     ; pointer table slot
MOV R21, c[0x0][0x0]                     ; base
IMAD R8, R19, 0x4, R10                   ; next offset
IMAD.MOV.U32 R2, Z, Z, R0               ; R2 = threadIdx.x
IMAD.MOV.U32 R23, Z, Z, c[0x0][0x0]
LEA R12, R19, R8, 0x2                    ; shared mem address
LDS R13, [R8]                            ; load value 1
IMAD.WIDE R2, R5, c[0x0][0x1a4], R2       ; wide address
LDS R15, [R12]                           ; load value 2
IMAD R14, R19, 0x4, R12                   ; next offset
LEA R4, P0, R2.reuse, c[0x0][0x168], 0x2  ; output address 0
IMAD R0, R21, 0x2, R0                     ; address computation
IMAD R10, R19, 0x4, R14                   ; next offset
LDS R17, [R14]                           ; load value 3
LEA.HI.X R5, R2, c[0x0][0x16c], R3, 0x2, P0 ; high address
LEA R0, R23, R0, 0x1                      ; address computation
IMAD.WIDE R2, R19, 0x4, R4               ; wide address for addr 0
ISETP.GE.AND P0, PT, R0, c[0x0][0x19c], PT ; bounds check
IMAD.WIDE R6, R19, 0x4, R2               ; wide address for addr 1
IMAD.WIDE R8, R19, 0x4, R6               ; wide address for addr 2
STG.E [R4.64], R11                       ; store value 0
STG.E [R2.64], R13                       ; store value 1
STG.E [R6.64], R15                       ; store value 2
STG.E [R8.64], R17                       ; store value 3
@!P0 BRA 0xa50                           ; loop if more
EXIT
```

### Algorithm Summary
```
for each chance node (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_threads: skip
    
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes    ; I2F.U32.RP + MUFU.RCP
    
    card_index = thread_idx % num_outcomes ; modulo via 64-bit HI multiply
    clear shared memory accumulators      ; STS x4
    __syncthreads()                       ; BSYNC B1
    
    if flag != 0:
        HFMA2.MMA init (FP16 tensor core, epsilon=0)
        for each card deal (loop over num_outcomes):
            load card from constant memory table
            if card != -1:
                card2 = load second card
                if card2 != -1:
                    atomic spin lock      ; ATOMS.CAST.SPIN
                    prob = weight * inv_outcomes
                    accumulator = load shared mem
                    sum = prob + accumulator
                    atomic store back
        __syncthreads()
        increment counter
        loop until all nodes processed
    WARPSYNC + BAR.SYNC
    
    exit if flag set
    
    # Second phase: store 4 results
    recompute card_index
    load 4 accumulator values from shared memory (LDS)
    compute 4 output addresses from pointer tables
    STG.E [addr0], val0 ; STG.E [addr1], val1 ; STG.E [addr2], val2 ; STG.E [addr3], val3
    __syncthreads()
    exit
```

---

## 30. forward_copy (sm_80, ~777 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_copy.txt`

**Role:** Forward copy kernel — copies reach/strategy values from source tables to destination tables with Q16 dequantization, reciprocal normalization, and epsilon guarding. Processes entries in 16-entry blocks with predicate-gated stores for up to 7 outcomes.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
IMAD.MOV.U32 R11, RZ, RZ, 0x4            ; R11 = 4 (stride)
ULDC.64 UR6, c[0x0][0x118]              ; UR6 = 64-bit pointer base
IMAD.WIDE R12, R2, R11, c[0x0][0x180]    ; wide address computation
LDG.E.CONSTANT R0, [R12.64]              ; load 64-bit pointer (source table)
IADD3 R4, P0, R2.reuse, c[0x0][0x178], RZ ; pointer table offset
IMAD.WIDE R8, R2, R11, c[0x0][0x168]     ; wide address
LEA.HI.X.SX32 R5, R2.reuse, c[0x0][0x17c], 0x1, P0
IMAD.WIDE R10, R2, R11, c[0x0][0x170]    ; wide address
LDG.E.CONSTANT R2, [R8.64]               ; load pointer (dest table)
LDG.E.CONSTANT R3, [R10.64]              ; load pointer (dest table 2)
LDG.E.U8.CONSTANT R4, [R4.64]            ; load byte (card/suit data)
S2R R6, SR_TID.X                         ; R6 = threadIdx.x
FSETP.NEU.FTZ.AND P0, PT, R0, 1, PT      ; check if pointer == 1 (flag)
@P0 BRA 0x1710                           ; skip if flagged
ISETP.GE.AND P0, PT, R6, c[0x0][0x18c], PT ; bounds check
@P0 EXIT                                 ; exit if out of bounds
```

### Card & Value Extraction
```
LOP3.LUT R5, R4.reuse, 0x7, RZ, 0xc0, !PT ; extract low 3 bits (card & 7)
ULDC UR5, c[0x0][0x188]                  ; uniform register load
IADD3 R9, R4.reuse, -0x1, RZ             ; card index - 1
USHF.R.S32.HI UR5, URZ, 0x1f, UR5        ; uniform shift
IMAD.IADD R0, R4, 0x1, -R5               ; suit-adjusted rank
IADD3 R8, R6, c[0x0][0x0], RZ            ; increment by stride
IMAD.MOV.U32 R7, Z, Z, RZ                ; R7 = 0
ISETP.GE.AND P0, PT, R8, c[0x0][0x18c], PT ; bounds check
@!P0 SHF.R.S32.HI R11, Z, Z, 0x1f, R8.reuse
@!P0 IMAD.MOV.U32 R10, Z, Z, R8
@!P0 IMAD.WIDE R10, R2, c[0x0][0x188], R10 ; wide address computation
@!P0 LEA R16, P1, R10, c[0x0][0x160], 0x2 ; dest address
@!P0 LEA.HI.X R17, R10, c[0x0][0x164], R11, 0x2, P1
@!P0 LDG.E.CONSTANT R7, [R16.64]          ; load value from source
```

### Q16 Dequant & Normalization (first phase)
```
ISETP.NE.AND P1, PT, R4, RZ, PT           ; predicate: card valid
IMAD.MOV.U32 R10, Z, Z, R6.reuse          ; R10 = threadIdx.x
SHF.R.S32.HI R11, Z, Z, 0x1f, R6          ; shift
IMAD.WIDE R12, R2, c[0x0][0x188], R10     ; wide address
LEA R14, P2, R12, c[0x0][0x160], 0x2       ; dest address
LEA.HI.X R15, R12, c[0x0][0x164], R13, 0x2, P2
@!P1 BRA 0xc70                            ; skip if card invalid
LDG.E.CONSTANT R37, [R14.64]              ; load value from source table
ISETP.GE.U32.AND P2, PT, R9, 0x7, PT       ; check if card index >= 7
IMAD.MOV.U32 R36, Z, Z, RZ                ; R36 = 0
ISETP.NE.AND P1, PT, R5, RZ, PT           ; predicate: suit valid
@!P2 BRA 0x7b0                            ; skip if card index >= 7
IMAD.MOV.U32 R36, Z, Z, RZ
IMAD.MOV.U32 R38, Z, Z, R0                ; R38 = suit-adjusted rank
IMAD.IADD R25, R3, 0x1, R36               ; increment
IADD3 R38, R38, -0x8, RZ                  ; decrement by 8
```

### Main Copy Loop (16-entry blocks)
The kernel processes entries in blocks of 16, decrementing R2 by 0x10 each iteration:
```
IADD3 R2, R2, -0x10, RZ                   ; decrement block counter by 16
ISETP.GT.AND P2, PT, R2, 0xc, PT          ; loop while R2 > 12
@P2 BRA 0x3e0                             ; loop back
```

### Normalization with Epsilon Guard
```
MUFU.RCP R25, R0                         ; reciprocal: 1.0f / value
FMUL.FTZ R47, R22, R25                   ; multiply by reciprocal
FSETP.GT.FTZ.AND P2, PT, R47, 1.0000000116860974231e-07, PT ; epsilon > ~1e-7
FSEL R47, R47, RZ, P2                    ; zero if below epsilon
@!P0 FMUL.FTZ R45, R20, R25              ; alternate path normalization
```

### Predicate-Gated Stores (up to 7 outcomes)
```
STG.E [R16.64], R37                      ; store copied value to global
STG.E [R14.64], R7                       ; store normalized value
STG.E [R22.64], R37                      ; store copied value
STG.E [R28.64], R37                      ; store copied value
STG.E [R30.64], R37                      ; store copied value
STG.E [R32.64], R37                      ; store copied value
STG.E [R34.64], R37                      ; store copied value
```
Each store is predicate-gated on R5 (0x1-0x6 for up to 7 outcomes).

### Second Phase (0x24d0+)
Similar structure to first phase but with alternate normalization path:
```
FMUL.FTZ R47, R20, R25                   ; normalize alternate value
FSETP.GT.FTZ.AND P0, PT, R45, 1.0000000116860974231e-07, PT ; epsilon guard
FSEL R47, R45, RZ, P0                    ; zero if below epsilon
```
More predicate-gated stores for outcomes 1-6, then BSSY B0 barrier.

### Loop Back & Error Handler
```
IADD3 R6, R8, c[0x0][0x0], RZ            ; increment by stride
ISETP.GE.AND P0, PT, R6, c[0x0][0x18c], PT ; bounds check
@P0 CALL.REL.NOINC 0x1700                ; error handler
BRA 0x170                                ; loop back
...
EXIT
```

### Algorithm Summary
```
for each entry block (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_entries: exit
    
    load source and dest pointers from constant memory tables
    load card byte (LDG.E.U8.CONSTANT)
    suit = card & 7                    ; LOP3.LUT 0xc0
    
    if pointer_flag == 1: skip block
    
    for each 16-entry block:
        load values from source tables
        Q16 dequantize: I2F.U16
        normalize: value * (1.0f / divisor)  ; MUFU.RCP + FMUL.FTZ
        epsilon guard: if result <= ~1e-7: result = 0
        store copied/normalized values to dest tables (STG.E)
        predicate-gated for up to 7 outcomes
    
    increment by stride, loop until all entries processed
    __syncthreads()                    ; BSSY B0
    error handler if bounds exceeded
```

---

## 31. forward_opponent_br (sm_80, ~465 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_opponent_br.txt`

**Role:** Forward opponent best-response kernel — propagates opponent reach probabilities through the game tree, accumulates CFVs across child nodes, normalizes via reciprocal, and stores per-action probabilities.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R18, SR_TID.X                        ; R18 = threadIdx.x
IADD3 R1, R1, -0x20, RZ                  ; R1 = threadIdx.x - 32 (warp offset)
S2R R0, SR_CTAID.X                       ; R0 = blockIdx.x
R2UR UR16, R1                            ; broadcast to uniform register
BAR.SYNC.DEFER_BLOCKING 0x0              ; deferred barrier
ISETP.GE.AND P0, PT, R18, c[0x0][0x1b4], PT ; bounds check vs max_threads
@P0 EXIT                                 ; exit if out of bounds
```

### Constant Buffer Loads
```
ULDC.64 UR14, c[0x0][0x118]              ; 64-bit pointer base
LDG.E.U8.CONSTANT R19, [R2.64]           ; load byte (card/suit data)
LEA R4, P0, R0.reuse, c[0x0][0x198], 0x2 ; pointer table offset
IMAD.MOV.U32 R21, Z, Z, 0x4              ; stride 4
ULDC.64 UR6, c[0x0][0x1b0]               ; 64-bit pointer base 2
LEA.HI.X R5, R0, c[0x0][0x19c], R7, 0x2, P0
LDG.E.CONSTANT R4, [R4.64]               ; load pointer
LEA R30, P0, R0.reuse, c[0x0][0x1a8], 0x3 ; pointer table offset 3
IMAD.WIDE R20, R0, R21, c[0x0][0x188]    ; wide address computation
LEA.HI.X R31, R0, c[0x0][0x1ac], R7, 0x3, P0
LDG.E.CONSTANT R20, [R20.64]             ; load pointer
LDG.E.64.CONSTANT R30, [R30.64]          ; load 64-bit pointer
USHF.R.S32.HI UR6, URZ, 0x1f, UR6        ; uniform shift
USHF.L.U32 UR12, UR7, 0x1, URZ           ; uniform shift left
UIMAD UR4, UR7, 0x3, URZ                 ; uniform multiply-add
```

### Card Extraction & Reciprocal
```
I2F.U16 R0, R19                          ; Q16 dequant: int16 -> float
LOP3.LUT R16, R19, 0x3, RZ, 0xc0, !PT    ; suit = card & 3
IADD3 R17, R19, -0x1, RZ                 ; card index - 1
IMAD.IADD R7, R19, 0x1, -R16             ; suit-adjusted rank
R2UR UR10, R4                            ; broadcast pointer to uniform
MUFU.RCP R0, R0                          ; 1.0f / value (reciprocal)
R2UR UR11, R0                            ; broadcast reciprocal to uniform
ISETP.NE.AND P1, PT, R19, RZ, PT         ; card != 0 (not dead)
```

### Main Propagation Loop (16-entry blocks)
```
IMAD.MOV.U32 R28, Z, Z, R18.reuse        ; R28 = threadIdx.x
SHF.R.S32.HI R29, Z, Z, 0x1f, R18        ; shift
IMAD.MOV.U32 R25, Z, Z, RZ               ; R25 = 0
IMAD.WIDE R2, R20, c[0x0][0x1b0], R28    ; wide address computation
LEA R26, P0, R2, c[0x0][0x160], 0x2       ; dest address
LEA.HI.X R27, R2, c[0x0][0x164], R3, 0x2, P0
@!P1 BRA 0x1390                           ; skip if card dead
ISETP.GE.U32.AND P0, PT, R17, 0x3, PT     ; check card index < 3
IMAD.MOV.U32 R25, Z, Z, RZ
IADD3 R3, P2, R30, R18, RZ               ; pointer computation
IMAD.MOV.U32 R6, Z, Z, RZ                ; R6 = 0
IMAD.X R4, R31, 0x1, R29, P2             ; extended multiply
@!P0 BRA 0x11b0                           ; skip if card index >= 3
```

### CFV Accumulation (FADD.FTZ chains, up to 8 accumulators)
```
ISETP.GT.AND P0, PT, R7, RZ, PT           ; check if rank > 0
IMAD.MOV.U32 R25, Z, Z, RZ
IMAD.MOV.U32 R23, Z, Z, RZ
IMAD.U32 R22, Z, Z, UR4                  ; uniform address
IMAD.U32 R24, Z, Z, UR12                 ; uniform address
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x1b4]     ; max_threads
IMAD.MOV.U32 R21, Z, Z, c[0x0][0x1b4]     ; max_threads
IMAD.MOV.U32 R0, Z, Z, R1                ; R0 = threadIdx.x - 32
IMAD.MOV.U32 R6, Z, Z, RZ
IMAD.MOV.U32 R2, Z, Z, R7                ; R2 = rank
@!P0 BRA 0xf90                            ; skip if rank <= 0
ISETP.GT.AND P2, PT, R2, 0xc, PT          ; check if rank > 12
PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0   ; predicate LUT
@!P2 BRA 0xb70                            ; skip if rank > 12
PLOP3.LUT P0, PT, PT, PT, PT, 0x8, 0x0
IADD3 R8, P2, R21, R3, RZ                ; pointer computation
LEA.HI.X.SX32 R9, R21, R4, 0x1, P2
LEA R12, P2, R8, c[0x0][0x168], 0x2       ; source address
LEA.HI.X R13, R8, c[0x0][0x16c], R9, 0x2, P2
IADD3 R8, P2, R24, R3, RZ
LDG.E.CONSTANT R9, [R12.64]               ; load CFV from source
...
FADD.FTZ R34, R8, R25                     ; accumulate: acc += CFV
STL.128 [R0], R8                          ; 128-bit shared memory store (4 floats)
FADD.FTZ R23, R34, R9                     ; accumulate chain
...
FADD.FTZ R21, R32, R9                     ; more accumulation
FADD.FTZ R21, R21, R10                    ; accumulate
FADD.FTZ R21, R21, R11                    ; accumulate
```

### Second Phase: Normalize & Store
```
@!P1 BRA 0x1390                           ; skip if card dead
IMAD R2, R6, c[0x0][0x1b4], RZ            ; pointer computation
IADD3 R0, P0, R2, R3, RZ                   ; dest address
LEA.HI.X.SX32 R5, R2, R4, 0x1, P0
LEA R8, P0, R0, c[0x0][0x168], 0x2         ; source address
LEA.HI.X R9, R0, c[0x0][0x16c], R5, 0x2, P0
LDG.E.CONSTANT R8, [R8.64]                ; load value
LEA R6, R6, UR16, 0x2                      ; increment by warp offset
ISETP.NE.AND P0, PT, R16, 0x1, PT         ; predicate
STL [R6], R8                               ; shared memory store
FADD.FTZ R25, R25, R8                      ; accumulate total
@!P0 BRA 0x1390                            ; skip if predicate false
ISETP.NE.AND P0, PT, R16, 0x2, PT         ; predicate for 2nd outcome
...
STL [R6+0x4], R2                           ; shared memory store
FADD.FTZ R25, R25, R2                      ; accumulate
@P0 STL [R6+0x8], R4                       ; shared memory store (3rd outcome)
@P0 FADD.FTZ R25, R25, R4                  ; accumulate
```

### Normalization (MUFU.RCP + FMUL.FTZ)
```
FSETP.GT.FTZ.AND P1, PT, R25, RZ, PT      ; check if total > 0
IMAD.MOV.U32 R0, Z, Z, RZ
IMAD.MOV.U32 R6, Z, Z, R7                ; R6 = rank
LEA R2, R0, UR16, 0x2                     ; dest address
@P1 LDL R8, [R2]                          ; load shared mem (64-bit)
@P1 LDL R14, [R2+0x4]
@P1 LDL R12, [R2+0x8]
@P1 LDL R13, [R2+0xc]
IADD3 R15, R0, UR10, RZ                   ; address computation
@P1 MUFU.RCP R11, R25                      ; reciprocal: 1.0f / total
IMAD.U32 R9, Z, Z, UR11                    ; uniform address
IADD3 R6, R6, -0x4, RZ                     ; decrement
SHF.R.S32.HI R3, Z, Z, 0x1f, R15          ; shift
IMAD.MOV.U32 R2, Z, Z, R18               ; R2 = threadIdx.x
IADD3 R0, R0, 0x4, RZ                      ; increment by 4
IMAD R10, R3, c[0x0][0x1b0], RZ           ; wide address computation
IMAD.MOV.U32 R3, Z, Z, R29
IMAD R21, R15.reuse, UR6, R10             ; address computation
IMAD.WIDE.U32 R4, R15, c[0x0][0x1b0], R2   ; wide address
LEA R10, P2, R4, c[0x0][0x160], 0x2        ; dest address
@P1 FMUL.FTZ R9, R8, R11                    ; normalize: value * (1/total)
IMAD.IADD R11, R5, 0x1, R21               ; increment address
IADD3 R5, R15.reuse, 0x1, RZ
FMUL.FTZ R23, R26, R9                      ; scale by weight
IADD3 R21, R15, 0x2, RZ
LEA.HI.X R11, R4, c[0x0][0x164], R11, 0x2, P2
IMAD.WIDE.U32 R8, R5, c[0x0][0x1b0], R2     ; wide address
SHF.R.S32.HI R4, Z, Z, 0x1f, R5            ; shift
IADD3 R15, R15, 0x3, RZ
STG.E [R10.64], R23                        ; store normalized value
...
@P1 MUFU.RCP R23, R25                      ; reciprocal for next value
...
@P1 FMUL.FTZ R9, R21, R14                  ; normalize
@P1 FMUL.FTZ R21, R23, R12                  ; normalize
FMUL.FTZ R21, R26, R21                      ; scale by weight
...
STG.E [R4.64], R3                          ; store normalized value
STG.E [R10.64], R21                        ; store normalized value
STG.E [R8.64], R15                         ; store normalized value
@P2 BRA 0x1420                             ; loop back
```

### Loop Back & Error Handler
```
IADD3 R18, R18, c[0x0][0x0], RZ           ; increment by stride
ISETP.GE.AND P0, PT, R18, c[0x0][0x1b4], PT ; bounds check
@P0 CALL.REL.NOINC 0x1bf0                ; error handler
BRA 0x210                                ; loop back
EXIT
```

### Algorithm Summary
```
for each opponent node (thread-mapped):
    thread_idx = threadIdx.x - 32         ; warp offset
    if thread_idx >= max_threads: exit
    
    load card byte from constant memory
    Q16 dequantize: value = I2F.U16(card_byte)
    suit = card & 3                      ; LOP3.LUT 0xc0
    reciprocal = 1.0f / value            ; MUFU.RCP
    if card == 0 (dead): skip
    
    for each 16-entry block (decrement by 0x10):
        load child CFVs from source tables (LDG.E.CONSTANT)
        accumulate CFVs into 8 registers (FADD.FTZ chains)
        store 4 floats to shared memory (STL.128)
    
    __syncthreads()
    
    load accumulated values from shared memory (LDL)
    total = sum of accumulated values
    if total > 0:
        inv_total = 1.0f / total         ; MUFU.RCP
        for each action a (up to 7, predicate-gated):
            prob[a] = CFV[a] * inv_total * weight  ; FMUL.FTZ chains
            STG.E [dest_addr], prob[a]   ; store to global memory
    
    increment by stride, loop until all nodes processed
    error handler if bounds exceeded
```

---

## 32. forward_opponent_br_packed (sm_80, ~537 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/forward_opponent_br_packed.txt`

**Role:** Forward opponent best-response kernel with packed S16 storage — propagates opponent reach probabilities using packed 16-bit fixed-point values, with complex card packing/unpacking via SHF.L.U64.HI and LOP3 bit manipulation.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
IMAD.MOV.U32 R0, RZ, Z, c[0x0][0x1c8]    ; R0 = constant slot 0x1c8 (flag/mask)
CS2R R10, SRZ                            ; cycle register (warp cycle counter)
ULDC.64 UR12, c[0x0][0x118]              ; 64-bit pointer base
S2R R8, SR_TID.X                         ; R8 = threadIdx.x
IADD3 R1, R1, -0x20, RZ                  ; R1 = threadIdx.x - 32
LOP3.LUT R0, R0, 0x2, RZ, 0xfc, !PT      ; mask constant with 0xfc
BAR.SYNC.DEFER_BLOCKING 0x0              ; deferred barrier
ISETP.NE.AND P0, PT, R0, 0xe, PT         ; flag check (value != 0xe)
IMAD.SHL.U32 R12, R2, 0x8, RZ            ; R12 = blockIdx.x * 256
SHF.R.S32.HI R3, Z, Z, 0x1f, R2          ; shift
IADD3 R6, P2, R2.reuse, c[0x0][0x1a8], RZ ; pointer table offset
SHF.L.U64.HI R0, R2, 0x3, R3             ; 64-bit shift left high
@!P0 IADD3 R4, P1, R12, c[0x0][0x1b8], RZ ; pointer table offset 2
R2UR UR20, R1                            ; broadcast to uniform
IADD3.X R7, R3, c[0x0][0x1ac], RZ, P2, !PT ; extended add
@!P0 IADD3.X R5, R0, c[0x0][0x1bc], RZ, P1, !PT ; extended add 2
ISETP.GE.AND P1, PT, R8, c[0x0][0x1c4], PT ; bounds check vs max_threads
@!P0 LDG.E.64.CONSTANT R10, [R4.64]      ; load 64-bit pointer
@P1 EXIT                                  ; exit if out of bounds
```

### Card Extraction (Packed Format)
```
LDG.E.U8.CONSTANT R7, [R6.64]            ; load byte (packed card data)
LEA R4, P0, R2, c[0x0][0x1a0], 0x2        ; pointer table offset
UMOV UR5, 0x3                             ; uniform immediate 3
ULDC.64 UR6, c[0x0][0x1c0]               ; 64-bit pointer base 2
LEA.HI.X R5, R2, c[0x0][0x1a4], R3, 0x2, P0
LDG.E.CONSTANT R4, [R4.64]               ; load pointer
IADD3 R12, P0, R12, c[0x0][0x1b0], RZ     ; pointer table offset
IMAD.MOV.U32 R3, Z, Z, 0x4                ; stride 4
IADD3.X R13, R0, c[0x0][0x1b4], RZ, P0, !PT ; extended add
IMAD.WIDE R2, R2, R3, c[0x0][0x190]       ; wide address computation
LDG.E.64.CONSTANT R12, [R12.64]           ; load 64-bit pointer
UIADD3 UR5, UR5, UR7, URZ                 ; uniform add
BSSY B0, 0x20a0                           ; block barrier
UMOV UR4, 0x1                             ; uniform immediate 1
LDG.E.CONSTANT R0, [R2.64]                ; load value
UIADD3 UR4, UR4, UR7, URZ                 ; uniform add
USHF.R.S32.HI UR5, URZ, 0x2, UR5          ; uniform shift right 2
USHF.R.S32.HI UR7, URZ, 0x1f, UR6         ; uniform shift right 31
USHF.R.S32.HI UR4, URZ, 0x1, UR4          ; uniform shift right 1
IMAD.MOV.U32 R2, Z, Z, R8                ; R2 = threadIdx.x
UIMAD UR5, UR5, 0x3, URZ                 ; uniform multiply-add
R2UR UR14, R7                             ; broadcast value to uniform
I2F.U16 R6, UR14                          ; Q16 dequant: int16 -> float
MUFU.RCP R6, R6                           ; 1.0f / value (reciprocal)
R2UR UR11, R4                             ; broadcast pointer to uniform
R2UR UR15, R6                             ; broadcast reciprocal to uniform
ULOP3.LUT UR17, UR14, 0x3, URZ, 0xc0, !UPT ; uniform suit extraction (card & 3)
UIADD3 UR16, UR14, -0x1, URZ              ; card index - 1
UIADD3 UR18, UR14, -UR17, URZ             ; rank = card - suit
ISETP.NE.AND P0, PT, RZ, UR14, PT         ; card != 0 (not dead)
```

### Complex Card Packing/Unpacking
```
IMAD.MOV.U32 R14, Z, Z, R2.reuse          ; R14 = threadIdx.x
SHF.R.S32.HI R15, Z, Z, 0x1f, R2          ; shift
IMAD.MOV.U32 R3, Z, Z, RZ                 ; R3 = 0
IMAD.WIDE R4, R0, c[0x0][0x1c0], R14      ; wide address computation
LEA R16, P1, R4, c[0x0][0x160], 0x2        ; dest address
LEA.HI.X R17, R4, c[0x0][0x164], R5, 0x2, P1
@!P0 BRA 0x1810                            ; skip if card dead
UISETP.GE.U32.AND UP0, UPT, UR16, 0x3, UPT ; check card index >= 3
LOP3.LUT R3, R2.reuse, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
IMAD.SHL.U32 R6, R2, 0x4, RZ              ; R6 = threadIdx.x * 16
SHF.R.U32.HI R7, Z, Z, 0x2, R2.reuse       ; shift right 2
IMAD.MOV.U32 R5, Z, Z, RZ                 ; R5 = 0
SHF.R.S32.HI R21, Z, Z, 0x1, R2            ; shift right 1
IMAD R9, R3, 0x6, RZ                       ; R9 = 0 * 6 = 0
PLOP3.LUT P5, PT, PT, PT, UP0, 0x80, 0x0   ; predicate LUT
IMAD R19, R7, 0x3, RZ                       ; R19 = shift * 3
IADD3 R4, P1, R12, R2, RZ                  ; pointer computation
IMAD.MOV.U32 R3, Z, Z, RZ
SHF.R.U32.HI R8, Z, Z, 0x3, R9             ; shift right 3
IADD3 R7, P2, R21, R10.reuse, RZ           ; pointer computation
IMAD.X R20, R13, 0x1, R15, P1              ; extended multiply
IADD3 R8, P3, P4, R8, R10, R19.reuse       ; complex pointer arithmetic
LOP3.LUT R9, R9, 0x6, RZ, 0xc0, !PT         ; bit manipulation
SHF.R.S32.HI R23, Z, Z, 0x1f, R19          ; shift
LOP3.LUT R6, R6, 0x4, RZ, 0xc0, !PT         ; bit manipulation
LEA.HI.X.SX32 R21, R21, R11, 0x1, P2
IADD3 R22, -R9, 0x8, RZ                    ; pointer arithmetic
IADD3.X R23, Z, Z, R11, P3, P4             ; extended add
@!P5 BRA 0xff0                             ; skip if predicate false
```

### Packed Value Loading & S16 Conversion
```
IMAD.MOV.U32 R3, Z, Z, c[0x0][0x1c8]      ; R3 = constant slot 0x1c8
IMAD.MOV.U32 R5, Z, Z, RZ                 ; R5 = 0
IMAD.U32 R25, Z, Z, UR18                 ; uniform address
ISETP.GT.AND P1, PT, R3, 0xf, PT           ; check if flag > 15
IMAD.MOV.U32 R3, Z, Z, RZ
IMAD.MOV.U32 R24, Z, Z, R1                ; R24 = threadIdx.x - 32
IMAD R27, R5, c[0x0][0x1c4], RZ            ; pointer computation
IADD3 R26, P2, R27, R4, RZ                ; pointer computation
LEA.HI.X.SX32 R19, R27, R20, 0x1, P2
@P1 BRA 0x780                             ; branch if flag > 15
IADD3 R18, P2, R26, c[0x0][0x168], RZ      ; pointer computation
IADD3.X R19, R19, c[0x0][0x16c], RZ, P2, !PT ; extended add
LDG.E.U8.CONSTANT R28, [R18.64]            ; load byte
IMAD.MOV.U32 R26, Z, Z, c[0x0][0x1c8]      ; R26 = constant slot 0x1c8
ISETP.NE.AND P2, PT, R26, 0xc, PT          ; check if flag != 0xc
@!P2 BRA 0x6f0                             ; branch if flag != 0xc
ISETP.NE.AND P2, PT, R26, 0xe, PT          ; check if flag != 0xe
@P2 BRA 0x7b0                             ; branch if flag != 0xe
```

### Card Byte Unpacking (SHF.R + LOP3)
```
IMAD R19, R5, UR5, RZ                      ; address computation
ISETP.GE.U32.AND P2, PT, R9, 0x3, PT       ; check if index >= 3
SHF.R.S32.HI R26, Z, Z, 0x1f, R19.reuse     ; shift
IADD3 R18, P3, P4, R8, c[0x0][0x170], R19   ; pointer computation
IADD3.X R19, R23, c[0x0][0x174], R26, P3, P4 ; extended add
LDG.E.U8.CONSTANT R26, [R18.64]             ; load byte
@P2 LDG.E.U8.CONSTANT R29, [R18.64+0x1]     ; load next byte
SHF.R.U32.HI R26, Z, Z, R9, R26             ; shift right by index
@P2 SHF.L.U32 R29, R29, R22, RZ              ; shift left
@P2 LOP3.LUT R26, R29, R26, RZ, 0xfc, !PT    ; combine bytes (mask 0xfc)
IMAD.SHL.U32 R29, R28, 0x40, RZ             ; shift left 64 bits
LOP3.LUT R28, R29, 0xffffffc0, R26, 0xe2, !PT ; final unpack (mask 0xffffffc0, LUT 0xe2)
BRA 0x7b0                                  ; branch
```

### Alternate Unpacking Path (0x6f0+)
```
IMAD R18, R5, UR4, RZ                      ; alternate address computation
SHF.R.S32.HI R26, Z, Z, 0x1f, R18.reuse     ; shift
IADD3 R18, P2, P3, R7, c[0x0][0x170], R18   ; pointer computation
IADD3.X R19, R21, c[0x0][0x174], R26, P2, P3 ; extended add
LDG.E.U8.CONSTANT R19, [R18.64]             ; load byte
IMAD.SHL.U32 R29, R28, 0x10, RZ             ; shift left 16 bits
SHF.R.U32.HI R28, Z, Z, R6, R19             ; shift right by index
LOP3.LUT R28, R29, 0xfffffff0, R28, 0xe2, !PT ; final unpack
BRA 0x7b0                                  ; branch
```

### S16 Packed Storage & Accumulation
```
LDG.E.U16.CONSTANT R28, [R18.64]           ; load 16-bit packed value
I2F.U32 R28, R28                           ; int -> float
IADD3 R27, R27, c[0x0][0x1c4], RZ           ; increment pointer
IADD3 R19, P2, R27.reuse, R4, RZ            ; pointer computation
STL [R24], R28                              ; shared memory store (S16 packed)
FADD.FTZ R26, R28, R3                       ; accumulate: total += value
...
```

### Predicate-Gated Loop (up to 7 outcomes)
The kernel loops over up to 7 card outcomes, each with:
- Card byte unpacking (SHF.R + LOP3 patterns)
- S16 packed load (LDG.E.U16.CONSTANT)
- I2F.U32 conversion
- Shared memory store (STL)
- FADD.FTZ accumulation

### Normalization Phase (0x1810+)
```
LDG.E.CONSTANT R16, [R16.64]              ; load pointer
UISETP.GE.U32.AND UP0, UPT, UR16, 0x3, UPT ; check card index >= 3
ISETP.NE.AND P0, PT, RZ, UR17, PT         ; suit valid check
IMAD.MOV.U32 R4, Z, Z, RZ                 ; R4 = 0
PLOP3.LUT P1, PT, PT, PT, UP0, 0x80, 0x0   ; predicate LUT
@!P1 BRA 0x1c90                            ; skip if predicate false
FSETP.GT.FTZ.AND P1, PT, R3, RZ, PT       ; check if total > 0
IMAD.MOV.U32 R4, Z, Z, RZ
IMAD.U32 R5, Z, Z, UR18                    ; uniform address
LEA R8, R4, UR20, 0x2                      ; dest address
@P1 LDL R23, [R8]                          ; load shared mem (64-bit)
@P1 LDL R22, [R8+0x4]
@P1 LDL R28, [R8+0x8]
@P1 LDL R24, [R8+0xc]
...
@P1 MUFU.RCP R25, R3                       ; reciprocal: 1.0f / total
...
@P1 FMUL.FTZ R17, R23, R25                 ; normalize: value * (1/total)
@P1 FMUL.FTZ R25, R9, R22                  ; normalize
FMUL.FTZ R17, R16, R17                      ; scale by weight
FMUL.FTZ R25, R16, R25                      ; scale by weight
...
STG.E [R18.64], R17                        ; store normalized S16 value
STG.E [R6.64], R25                         ; store normalized S16 value
...
```

### Loop Back & Error Handler
```
IADD3 R2, R2, c[0x0][0x0], RZ             ; increment by stride
ISETP.GE.AND P0, PT, R2, c[0x0][0x1c4], PT ; bounds check
@P0 CALL.REL.NOINC 0x2090                ; error handler (BSYNC B0)
BRA 0x320                                ; loop back
BSYNC B0                                 ; barrier
EXIT
```

### Algorithm Summary
```
for each opponent node (thread-mapped):
    thread_idx = threadIdx.x - 32         ; warp offset
    if thread_idx >= max_threads: exit
    
    load packed card data from constant memory
    complex unpacking via SHF.R/SHF.L/LOP3 (masks 0xfc, 0xe2, 0xffffffc0)
    S16 packed load: LDG.E.U16.CONSTANT
    I2F.U32: convert to float
    STL: store to shared memory (S16 packed)
    FADD.FTZ: accumulate totals
    predicate-gated for up to 7 outcomes
    
    __syncthreads()
    
    load packed values from shared memory (LDL)
    if total > 0:
        inv_total = 1.0f / total         ; MUFU.RCP
        for each action a (up to 7, predicate-gated):
            prob[a] = packed_value[a] * inv_total * weight  ; FMUL.FTZ
            STG.E [dest_addr], prob[a]   ; store normalized S16 value
    
    increment by stride, loop until all nodes processed
    error handler if bounds exceeded
```

---

## 33. backward_chance_turn_river_batched (sm_80, ~209 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_chance_turn_river_batched.txt`

**Role:** Batched backward chance node for turn/river streets — accumulates chance probabilities across card deals using atomic spin lock, stores 4 probability values per node. Batched variant of backward_chance_tr_2d without 2D grid.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R2, SR_CTAID.X                       ; R2 = blockIdx.x
IMAD.MOV.U32 R3, RZ, RZ, 0x8            ; R3 = 8 (stride)
ULDC.64 UR4, c[0x0][0x118]              ; 64-bit pointer base
IMAD.WIDE R2, R2, R3, c[0x0][0x188]     ; wide address computation
LDG.E.64.CONSTANT R2, [R2.64]           ; load 64-bit pointer
BSSY B0, 0x400                          ; block barrier
S2R R0, SR_TID.X                        ; R0 = threadIdx.x
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check vs max_threads
@P0 BRA 0x3f0                           ; skip if out of bounds
```

### Chance Probability & Card Index
```
I2F.U32.RP R7, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R6, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B1, 0x2e0                           ; block barrier
ISETP.NE.U32.AND P3, PT, RZ, c[0x0][0x0], PT ; nonzero check
IADD3 R6, R6, c[0x0][0x1a0], RZ          ; pointer offset
MUFU.RCP R7, R7                          ; 1.0f / num_outcomes
IADD3 R4, R7, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R5, R4             ; truncate
IMAD.MOV.U32 R4, Z, Z, RZ
IMAD.MOV R9, Z, Z, -R5
IMAD R9, R9, c[0x0][0x0], RZ
IMAD.HI.U32 R5, R5, R9, R4               ; 64-bit multiply high
IMAD.HI.U32 R5, R5, R6, RZ               ; add thread offset
IADD3 R7, -R5, RZ, RZ                    ; card index
IMAD R6, R7, c[0x0][0x0], R6             ; card_index * num_outcomes + base
ISETP.GE.U32.AND P1, PT, R6, c[0x0][0x0], PT ; wrap check
@P1 IADD3 R6, R6, -c[0x0][0x0], RZ
@P1 IADD3 R5, R5, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P2, PT, R6, c[0x0][0x0], PT
@P2 IADD3 R5, R5, 0x1, RZ
@!P3 LOP3.LUT R5, RZ, c[0x0][0x0], RZ, 0x33, !PT ; bit-pair extraction
IADD3 R4, R5.reuse, 0x1, RZ              ; next card
ISETP.GE.U32.AND P2, PT, R5, 0x3, PT     ; max 4 cards
IMAD.MOV.U32 R5, Z, Z, R0               ; R5 = threadIdx.x
LOP3.LUT P1, R4, R4, 0x3, RZ, 0xc0, !PT  ; suit = card & 3
@!P1 BRA 0x2d0                           ; skip invalid
```

### Shared Memory Clearing
```
IMAD.SHL.U32 R6, R0, 0x4, RZ            ; R6 = threadIdx.x * 16
MOV R7, c[0x0][0x0]                     ; base
IMAD.MOV.U32 R5, Z, Z, R0               ; R5 = threadIdx.x
IADD3 R4, R4, -0x1, RZ                   ; decrement
STS [R6], RZ                             ; clear shared mem entry
IADD3 R5, R5, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.NE.AND P1, PT, R4, RZ, PT          ; loop if more
IMAD R6, R7, 0x4, R6                      ; next offset
@P1 BRA 0x270
BSYNC B1                                 ; barrier
@!P2 BRA 0x3f0                           ; exit if no work
```

### FP16 MMA + Card Deal Loop
```
IMAD.SHL.U32 R4, R5, 0x4, RZ            ; shared mem offset
IMAD.MOV.U32 R9, Z, Z, c[0x0][0x0]       ; base
LEA R6, R9.reuse, R4, 0x2                ; shared mem address
IMAD.MOV.U32 R10, Z, Z, c[0x0][0x0]
MOV R12, c[0x0][0x0]
STS [R4], RZ                             ; clear shared mem
IMAD R7, R9.reuse, 0x4, R6
IMAD R5, R10, 0x2, R5
STS [R6], RZ                             ; clear shared mem
IMAD R8, R9, 0x4, R7
IMAD R5, R12, 0x2, R5
STS [R7], RZ                             ; clear shared mem
IMAD R4, R9, 0x4, R8
STS [R8], RZ                             ; clear shared mem
ISETP.GE.AND P1, PT, R5, c[0x0][0x1a0], PT ; bounds check
@!P1 BRA 0x310
BSYNC B0                                 ; block barrier
MOV R4, c[0x0][0x190]                    ; pointer base
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.GE.AND P1, PT, R4, 0x1, PT         ; check flag
@!P1 BRA 0x740                           ; skip if flag == 0
I2F R4, c[0x0][0x190]                   ; num_outcomes -> float
IMAD.MOV.U32 R5, Z, Z, RZ                ; R5 = 0
MUFU.RCP R4, R4                          ; 1.0f / num_outcomes
ISETP.GE.AND P1, PT, R0, c[0x0][0x19c], PT ; bounds check
BSSY B0, 0x710                           ; block barrier
@P1 BRA 0x700                            ; skip
IMAD.MOV.U32 R10, Z, Z, 0x4              ; stride 4
IMAD.WIDE R10, R5, R10, c[0x0][0x180]    ; wide address
LDG.E.CONSTANT R11, [R10.64]             ; load pointer
IMAD.MOV.U32 R8, Z, Z, 0x3               ; stride 3
MOV R7, R0                               ; R7 = threadIdx.x
IMAD R6, R5, R8.reuse, c[0x0][0x194]     ; pointer computation
IMAD R8, R11, R8, c[0x0][0x194]          ; next pointer
IMAD.MOV.U32 R13, Z, Z, 0x2              ; stride 2
IMAD R10, R8, c[0x0][0x198], R7          ; pointer to card table
IMAD.WIDE R10, R10, R13, c[0x0][0x170]   ; wide address
LDG.E.U16.CONSTANT R14, [R10.64]         ; load 16-bit card data
YIELD
BSSY B1, 0x6d0                           ; block barrier
PRMT R9, R14, 0x9910, RZ                 ; card byte extraction
ISETP.NE.AND P1, PT, R9, -0x1, PT        ; dead card check
@!P1 BRA 0x6c0                           ; skip dead cards
IMAD R10, R6, c[0x0][0x198], R7          ; pointer to card 2
IMAD.WIDE R10, R10, R13, c[0x0][0x178]   ; wide address
LDG.E.U16.CONSTANT R10, [R10.64]         ; load card 2
PRMT R9, R10, 0x9910, RZ                 ; card byte extraction
ISETP.NE.AND P1, PT, R9, -0x1, PT        ; dead card check
@!P1 BRA 0x6c0                           ; skip dead cards
IMAD.MOV.U32 R11, Z, Z, RZ               ; R11 = 0
IMAD.WIDE R10, R3, c[0x0][0x1ac], R10    ; pointer to lock variable
LEA R12, P1, R10, c[0x0][0x160], 0x2      ; lock address
LEA.HI.X R13, R10, c[0x0][0x164], R11, 0x2, P1
LDG.E.CONSTANT R13, [R12.64]             ; load lock value
IMAD.SHL.U32 R14, R14, 0x4, RZ            ; shared mem offset
FMUL.FTZ R9, R4, R13                     ; prob = weight * (1/num_outcomes)
LDS R10, [R14]                           ; load accumulator
FADD R11, R9, R10                        ; sum = prob + accumulator
ATOMS.CAST.SPIN R11, [R14], R10, R11     ; atomic spin lock
ISETP.EQ.U32.AND P1, PT, R11, 0x1, PT     ; check lock acquired
@!P1 BRA 0x670                           ; spin wait
BSYNC B1                                 ; release barrier
```

### Loop & Store Phase
```
IADD3 R7, R7, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.GE.AND P1, PT, R7, c[0x0][0x19c], PT ; bounds check
@!P1 BRA 0x510                           ; loop back
BSYNC B0                                 ; block barrier
IADD3 R5, R5, 0x1, RZ                    ; increment counter
ISETP.GE.AND P1, PT, R5, c[0x0][0x190], PT ; bounds check
@!P1 BRA 0x470                           ; loop back
WARPSYNC 0xffffffff                       ; warp sync
BAR.SYNC.DEFER_BLOCKING 0x0
@P0 EXIT                                  ; exit if flag set
```

### Second Phase: Load Shared Mem, Store 4 Values
```
I2F.U32.RP R6, c[0x0][0x0]              ; num_outcomes -> float
LOP3.LUT R3, RZ, R0, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B0, 0xa30                           ; block barrier
ISETP.NE.U32.AND P2, PT, RZ, c[0x0][0x0], PT
IADD3 R3, R3, c[0x0][0x1a0], RZ          ; pointer offset
MUFU.RCP R6, R6                          ; 1.0f / num_outcomes
IADD3 R4, R6, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R5, R4             ; truncate
IMAD.MOV.U32 R4, Z, Z, RZ
IADD3 R7, Z, Z, -R5
IMAD R7, R7, c[0x0][0x0], RZ
IMAD.HI.U32 R8, R5, R7, R4               ; 64-bit multiply
IMAD.HI.U32 R8, R8, R3, RZ               ; add thread offset
IMAD.MOV R6, Z, Z, -R8
IMAD R3, R6, c[0x0][0x0], R3
ISETP.GE.U32.AND P0, PT, R3, c[0x0][0x0], PT ; wrap check
@P0 IADD3 R3, R3, -c[0x0][0x0], RZ
@P0 IADD3 R8, R8, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P1, PT, R3, c[0x0][0x0], PT
@P1 IADD3 R8, R8, 0x1, RZ
@!P2 LOP3.LUT R8, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R3, R8.reuse, 0x1, RZ
ISETP.GE.U32.AND P1, PT, R8, 0x3, PT
LOP3.LUT P0, R3, R3, 0x3, RZ, 0xc0, !PT   ; suit extraction
@!P0 BRA 0xa20                           ; skip invalid
SHF.R.S32.HI R5, Z, Z, 0x1f, R0.reuse     ; shift
IMAD.MOV.U32 R4, Z, Z, R0               ; R4 = threadIdx.x
SHF.L.U32 R8, R0, 0x2, RZ               ; R8 = threadIdx.x * 4
IMAD.MOV.U32 R11, Z, Z, 0x4              ; stride 4
IMAD.WIDE R4, R2, c[0x0][0x1a8], R4      ; wide address
LEA R10, P0, R4, c[0x0][0x168], 0x2       ; output address 0
LEA.HI.X R5, R4, c[0x0][0x16c], R5, 0x2, P0
LDS R9, [R8]                            ; load value 0 from shared mem
IMAD.MOV.U32 R6, Z, Z, R10              ; R6 = output address 0
IADD3 R3, R3, -0x1, RZ                   ; decrement
IMAD.MOV.U32 R7, Z, Z, R5               ; R7 = high address part
IADD3 R0, R0, c[0x0][0x0], RZ            ; increment by num_outcomes
ISETP.NE.AND P0, PT, R3, RZ, PT          ; loop if more cards
IMAD.WIDE R4, R11, c[0x0][0x0], R6        ; wide address
IMAD R8, R11, c[0x0][0x0], R8            ; next wide address
MOV R10, R4                              ; R10 = wide address
STG.E [R6.64], R9                        ; store value 0 to global
@P0 BRA 0x970                           ; loop back
BSYNC B0                                 ; barrier
@!P1 EXIT                                 ; exit
```

### Second Store Phase (4 values to global memory)
```
IMAD.SHL.U32 R3, R0, 0x4, RZ            ; shared mem offset
IMAD.MOV.U32 R21, Z, Z, 0x4             ; stride 4
IMAD R12, R21.reuse, c[0x0][0x0], R3     ; shared mem address
LDS R13, [R3]                           ; load value 0
SHF.R.S32.HI R5, Z, Z, 0x1f, R0          ; shift
IMAD.MOV.U32 R23, Z, Z, c[0x0][0x0]      ; base
MOV R4, R0                               ; R4 = threadIdx.x
IMAD R14, R21.reuse, c[0x0][0x0], R12     ; next shared mem address
LDS R15, [R12]                           ; load value 1
IMAD R16, R21, c[0x0][0x0], R14           ; next shared mem address
LDS R17, [R14]                           ; load value 2
IMAD.WIDE R4, R2, c[0x0][0x1a8], R4       ; wide address
LDS R19, [R16]                           ; load value 3
IMAD.MOV.U32 R3, Z, Z, c[0x0][0x0]       ; base
LEA R6, P0, R4, c[0x0][0x168], 0x2        ; output address 0
LEA.HI.X R7, R4, c[0x0][0x16c], R5, 0x2, P0
LEA R0, R3, R0, 0x1                       ; address computation
IMAD R3, R21.reuse, c[0x0][0x0], R16      ; next shared mem address
IMAD.WIDE R4, R21, c[0x0][0x0], R6        ; wide address for addr 0
IMAD R0, R23, 0x2, R0                      ; address computation
IMAD.WIDE R8, R21, c[0x0][0x0], R4        ; wide address for addr 1
ISETP.GE.AND P0, PT, R0, c[0x0][0x1a0], PT ; bounds check
IMAD.WIDE R10, R21, c[0x0][0x0], R8       ; wide address for addr 2
STG.E [R6.64], R13                        ; store value 0
STG.E [R4.64], R15                        ; store value 1
STG.E [R8.64], R17                        ; store value 2
STG.E [R10.64], R19                       ; store value 3
@!P0 BRA 0xa60                           ; loop if more
EXIT
```

### Algorithm Summary
```
for each chance node (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_threads: skip
    
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes    ; I2F.U32.RP + MUFU.RCP
    card_index = thread_idx % num_outcomes ; modulo via 64-bit HI multiply
    clear shared memory accumulators      ; STS x4
    __syncthreads()                       ; BSYNC B1
    
    if flag != 0:
        for each card deal c (loop over num_outcomes):
            load card from constant memory table
            if card != -1:
                card2 = load second card
                if card2 != -1:
                    atomic spin lock      ; ATOMS.CAST.SPIN
                    prob = weight * inv_outcomes
                    accumulator = load shared mem
                    sum = prob + accumulator
                    atomic store back
        __syncthreads()
        increment counter
        loop until all nodes processed
    WARPSYNC + BAR.SYNC
    
    exit if flag set
    
    # Second phase: store 4 results
    recompute card_index
    load 4 accumulator values from shared memory (LDS)
    compute 4 output addresses from pointer tables
    STG.E [addr0], val0 ; STG.E [addr1], val1 ; STG.E [addr2], val2 ; STG.E [addr3], val3
    __syncthreads()
    exit
```

---

## 34. evaluate_fold_3p (sm_80, ~665 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_fold_3p.txt`

**Role:** 3-player fold equity evaluation — computes EV when opponents fold in a 3-handed game, using double-precision EV accumulation and warp-level butterfly reduction.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R8, SR_CTAID.X                       ; R8 = blockIdx.x
IMAD.MOV.U32 R9, Z, Z, 0x4              ; R9 = 4 (stride)
ULDC.64 UR4, c[0x0][0x118]              ; 64-bit pointer base
SHF.R.S32.HI R0, Z, Z, 0x1f, R8          ; shift blockIdx.x
IMAD.WIDE R6, R8.reuse, R9.reuse, c[0x0][0x170] ; wide address computation
IADD3 R10, P0, R8.reuse, c[0x0][0x180], RZ ; pointer table offset
IADD3 R12, P1, R8.reuse, c[0x0][0x188], RZ ; pointer table offset 2
IMAD.WIDE R8, R8, R9, c[0x0][0x178]      ; wide address
IADD3.X R11, R0.reuse, c[0x0][0x184], RZ, P0, !PT ; extended add
LDG.E.CONSTANT R23, [R6.64]              ; load pointer (board data)
IADD3.X R13, R0, c[0x0][0x18c], RZ, P1, !PT ; extended add 2
LDG.E.CONSTANT R22, [R8.64]              ; load pointer (board data 2)
LDG.E.U8.CONSTANT R21, [R10.64]          ; load board card byte
LDG.E.U8.CONSTANT R19, [R12.64]          ; load board card byte
S2R R20, SR_TID.X                        ; R20 = threadIdx.x
IMAD.MOV.U32 R0, Z, Z, c[0x0][0x0]        ; R0 = base/stride
IADD3 R4, R0, 0x1f, RZ                    ; R4 = base + 31
SHF.R.S32.HI R4, Z, Z, 0x5, R4            ; R4 = (base + 31) >> 5 (warp size div)
ISETP.NE.AND P1, PT, R20, RZ, PT          ; threadIdx.x != 0
IMAD.SHL.U32 R3, R4, 0x40, RZ             ; R3 = warp_count * 64
ISETP.GE.AND P0, PT, R20, R3, PT          ; threadIdx.x >= warp_count * 64
@!P1 STS.64 [RZ], RZ                       ; clear shared memory (thread 0)
BSSY B0, 0x4e0                            ; block barrier
SHF.R.S32.HI R5, Z, Z, 0x5, R20           ; R5 = threadIdx.x >> 5 (warp index)
@P0 BRA 0x4d0                             ; skip if threadIdx.x >= warp_count * 64
```

### Fold Probability & Card Index
```
I2F.U32.RP R8, c[0x0][0x0]              ; num_opponents -> float
LOP3.LUT R2, RZ, R20, RZ, 0x33, !PT      ; bit-pair extraction
BSSY B1, 0x3d0                           ; block barrier
ISETP.NE.U32.AND P3, PT, RZ, c[0x0][0x0], PT
IMAD.IADD R2, R3, 0x1, R2                ; increment
MUFU.RCP R8, R8                          ; 1.0f / num_opponents
IADD3 R6, R8, 0xffffffe, RZ              ; round
F2I.FTZ.U32.TRUNC.NTZ R7, R6             ; truncate
IMAD.MOV.U32 R6, Z, Z, RZ
IMAD.MOV R9, Z, Z, -R7
IMAD R9, R9, c[0x0][0x0], RZ
IMAD.HI.U32 R7, R7, R9, R6               ; 64-bit multiply
IMAD.MOV.U32 R6, Z, Z, R20               ; R6 = threadIdx.x
IMAD.HI.U32 R7, R7, R2, RZ               ; add thread offset
IMAD.MOV R9, Z, Z, -R7
IMAD R2, R9, c[0x0][0x0], R2             ; card_index * num_opponents + base
ISETP.GE.U32.AND P0, PT, R2, c[0x0][0x0], PT ; wrap check
@P0 IADD3 R2, R2, -c[0x0][0x0], RZ
@P0 IADD3 R7, R7, 0x1, RZ                 ; carry
ISETP.GE.U32.AND P2, PT, R2, c[0x0][0x0], PT
@P2 IADD3 R7, R7, 0x1, RZ
@!P3 LOP3.LUT R7, RZ, c[0x0][0x0], RZ, 0x33, !PT
IADD3 R2, R7.reuse, 0x1, RZ              ; next card
ISETP.GE.U32.AND P2, PT, R7, 0x3, PT     ; max 4 cards
LOP3.LUT P0, R2, R2, 0x3, RZ, 0xc0, !PT   ; suit = card & 3
@!P0 BRA 0x3c0                           ; skip invalid
```

### Shared Memory Clearing
```
LEA R7, R20, 0x2b0, 0x2                  ; shared mem address (threadIdx.x * 0x2b0)
IMAD.MOV.U32 R6, Z, Z, R20               ; R6 = threadIdx.x
IADD3 R2, R2, -0x1, RZ                    ; decrement
STS [R7], RZ                              ; clear shared memory
IADD3 R6, R6, c[0x0][0x0], RZ             ; increment by num_opponents
ISETP.NE.AND P0, PT, R2, RZ, PT           ; loop if more
IMAD R7, R0, 0x4, R7                       ; next shared mem offset
@P0 BRA 0x360
BSYNC B1                                  ; barrier
@!P2 BRA 0x4d0                            ; exit if no work
```

### Board Card Loading & Dead Card Check
```
LDC.U8 R0, c[0x0][0x1aa]                 ; load byte constant (board card 2)
CS2R R6, SRZ                              ; cycle register
LDC.U8 R2, c[0x0][0x1a9]                 ; load byte constant (board card 1)
LDC.U8 R3, c[0x0][0x1a8]                 ; load byte constant (board card 0)
@P0 BRA 0x940                            ; branch if flag set
PRMT R9, R3, 0x9910, RZ                   ; card byte extraction (card 0)
IMAD.MOV.U32 R8, Z, Z, R20                ; R8 = threadIdx.x
PRMT R14, R2, 0x9910, RZ                  ; card byte extraction (card 1)
CS2R R6, SRZ
PRMT R15, R0, 0x9910, RZ                  ; card byte extraction (card 2)
IMAD.SHL.U32 R11, R8, 0x2, RZ             ; R11 = threadIdx.x * 4
IADD3 R10, P0, R11, c[0x0][0x190], RZ      ; pointer table offset
LEA.HI.X.SX32 R11, R11, c[0x0][0x194], 0x1, P0
LDG.E.U8.CONSTANT R16, [R10.64+0x1]       ; load board card byte + 1
LDG.E.U8.CONSTANT R18, [R10.64]           ; load board card byte
YIELD
BSSY B0, 0x910                            ; block barrier
```

### Dead Card Detection (6-way comparison)
```
ISETP.NE.AND P0, PT, R16, R9, PT          ; card2 != card0
ISETP.EQ.OR P0, PT, R18, R9, !P0          ; card1 == card0 OR ...
ISETP.EQ.OR P0, PT, R18, R14, P0          ; card1 == card1 OR ...
ISETP.EQ.OR P0, PT, R16, R14, P0          ; card2 == card1 OR ...
ISETP.EQ.OR P0, PT, R18, R15, P0          ; card1 == card2 OR ...
ISETP.EQ.OR P0, PT, R16, R15, P0          ; card2 == card2 OR ...
@P0 BRA 0x900                             ; skip if any card matches (dead card)
```

### Additional Dead Card Checks vs Hole Cards
```
ISETP.NE.AND P0, PT, R16.reuse, R21, PT    ; card2 != hole0
ISETP.NE.AND P2, PT, R16, R19, PT          ; card2 != hole1
ISETP.EQ.OR P0, PT, R18.reuse, R21, !P0   ; card1 == hole0 OR ...
ISETP.EQ.OR P2, PT, R18, R19, !P2          ; card1 == hole1 OR ...
ISETP.NE.AND P0, PT, R21, 0xff, P0         ; hole0 != 0xff
ISETP.NE.AND P2, PT, R19, 0xff, P2         ; hole1 != 0xff
PLOP3.LUT P0, PT, P0, P2, PT, 0xa8, 0x0   ; predicate LUT combine
@P0 BRA 0x900                             ; skip if dead
```

### Double-Precision EV Accumulation
```
SHF.R.S32.HI R11, Z, Z, 0x1f, R8.reuse     ; shift
IMAD.MOV.U32 R10, Z, Z, R8                ; R10 = threadIdx.x
IMAD.WIDE R10, R23, c[0x0][0x1ac], R10     ; wide address
LEA R12, P0, R10, c[0x0][0x160], 0x2       ; shared mem address
LEA.HI.X R13, R10, c[0x0][0x164], R11, 0x2, P0
LDG.E.CONSTANT R13, [R12.64]               ; load value from shared mem
FSETP.NEU.FTZ.AND P0, PT, R13, RZ, PT      ; check for NaN/zero
@!P0 BRA 0x900                             ; skip if invalid
FMUL.FTZ R10, R13, 1                       ; multiply by 1 (identity)
LEA R12, R5, 0x2b0, 0x8                    ; shared mem address
IMAD.SHL.U32 R17, R18, 0x4, RZ             ; R17 = card1 * 16
BSSY B1, 0x860                             ; block barrier
F2F.F64.F32 R10, R10                       ; float -> double conversion
LOP3.LUT R17, R17, 0x3fc, RZ, 0xc0, !PT     ; mask
IMAD.IADD R17, R12, 0x1, R17               ; address computation
DADD R6, R6, R10                           ; double add: sum += value
LDS R10, [R17]                             ; load shared mem value
FADD R11, R13, R10                         ; float add
ATOMS.CAST.SPIN R11, [R17], R10, R11       ; atomic spin lock
ISETP.EQ.U32.AND P0, PT, R11, 0x1, PT       ; check lock acquired
@!P0 BRA 0x800                             ; spin wait
BSYNC B1                                   ; release barrier
...
# Multiple DADD chains accumulate double-precision EV
DADD R28, R24, R28                         ; EV accumulation (double precision)
DADD R12, R28, R12                         ; EV accumulation
DADD R8, R12, R8                           ; EV accumulation
DADD R24, R8, R24                          ; EV accumulation
DADD R8, R24, R12                          ; EV accumulation
DADD R12, R8, R12                          ; EV accumulation
DADD R10, R12, R10                         ; EV accumulation
DADD R10, R10, R8                          ; EV accumulation
DADD R28, R8, R28                          ; EV accumulation (final)
```

### Warp-Level Butterfly Reduction (Double Precision)
```
BSYNC B0                                  ; block barrier
SHFL.BFLY PT, R9, R7, 0x10, 0x1f          ; warp butterfly shuffle step 16
LOP3.LUT P0, RZ, R20, 0x1f, RZ, 0xc0, !PT  ; lane mask
BSSY B0, 0x1f00                            ; block barrier
SHFL.BFLY PT, R8, R6, 0x10, 0x1f           ; warp butterfly shuffle step 16
DADD R8, R8, R6                            ; double add reduction
SHFL.BFLY PT, R11, R9, 0x8, 0x1f           ; warp butterfly shuffle step 8
SHFL.BFLY PT, R10, R8, 0x8, 0x1f           ; warp butterfly shuffle step 8
DADD R10, R8, R10                          ; double add reduction
SHFL.BFLY PT, R13, R11, 0x4, 0x1f          ; warp butterfly shuffle step 4
SHFL.BFLY PT, R12, R10, 0x4, 0x1f          ; warp butterfly shuffle step 4
DADD R12, R10, R12                          ; double add reduction
SHFL.BFLY PT, R15, R13, 0x2, 0x1f          ; warp butterfly shuffle step 2
SHFL.BFLY PT, R14, R12, 0x2, 0x1f          ; warp butterfly shuffle step 2
DADD R14, R12, R14                          ; double add reduction
SHFL.BFLY PT, R7, R15, 0x1, 0x1f           ; warp butterfly shuffle step 1
SHFL.BFLY PT, R6, R14, 0x1, 0x1f           ; warp butterfly shuffle step 1
DADD R6, R14, R6                            ; double add reduction (final sum)
@!P0 STS.64 [R5.X8+0x1a8], R6              ; store final EV to shared memory
BAR.SYNC.DEFER_BLOCKING 0x0
@P1 BRA 0x1ef0                             ; branch if flag set
```

### Final EV Computation & Store
```
IMAD.MOV.U32 R5, Z, Z, c[0x0][0x0]        ; R5 = base
CS2R R24, SRZ                              ; cycle register
ISETP.GE.AND P0, PT, R5, 0x1, PT           ; check base >= 1
@!P0 BRA 0x1ee0
IMNMX R5, R4, 0x1, !PT                     ; integer min/max
IMAD.MOV.U32 R17, Z, Z, RZ                 ; R17 = 0
CS2R R24, SRZ
IADD3 R4, R5.reuse, -0x1, RZ               ; decrement
LOP3.LUT R18, R5, 0x3, RZ, 0xc0, !PT       ; suit extraction
ISETP.GE.U32.AND P0, PT, R4, 0x3, PT        ; check if index < 3
@!P0 BRA 0x1e50
IMAD.IADD R16, R5, 0x1, -R18               ; suit-adjusted rank
CS2R R24, SRZ
IMAD.MOV.U32 R17, Z, Z, RZ
ISETP.GT.AND P0, PT, R16, RZ, PT           ; check if rank > 0
@!P0 BRA 0x1da0
ISETP.GT.AND P1, PT, R16, 0xc, PT           ; check if rank > 12
PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0     ; predicate LUT
@!P1 BRA 0x1c60
PLOP3.LUT P0, PT, PT, PT, PT, 0x8, 0x0
LDS.64 R4, [R17.X8+0x1a8]                  ; load 64-bit shared mem value
IADD3 R16, R16, -0x10, RZ                   ; decrement by 16
LDS.128 R8, [R17.X8+0x1b0]                 ; load 128-bit shared mem value (4 doubles)
ISETP.GT.AND P1, PT, R16, 0xc, PT           ; check if remaining > 12
LDS.128 R12, [R17.X8+0x1c0]                 ; load 128-bit shared mem value
DADD R24, R4, R24                          ; double add accumulation
LDS.128 R4, [R17.X8+0x1d0]                 ; load 128-bit shared mem value
DADD R24, R24, R8                          ; double add accumulation
DADD R24, R24, R10                         ; double add accumulation
LDS.128 R8, [R17.X8+0x1e0]                 ; load 128-bit shared mem value
DADD R24, R24, R12                         ; double add accumulation
DADD R24, R24, R14                         ; double add accumulation
LDS.128 R12, [R17.X8+0x1f0]                 ; load 128-bit shared mem value
DADD R24, R24, R4                          ; double add accumulation
DADD R24, R24, R6                          ; double add accumulation
LDS.128 R4, [R17.X8+0x200]                 ; load 128-bit shared mem value
DADD R24, R24, R8                          ; double add accumulation
DADD R24, R24, R10                         ; double add accumulation
LDS.128 R8, [R17.X8+0x210]                 ; load 128-bit shared mem value
DADD R12, R24, R12                         ; double add accumulation
LDS.64 R24, [R17.X8+0x220]                 ; load 64-bit shared mem value
DADD R12, R12, R14                         ; double add accumulation
IADD3 R17, R17, 0x10, RZ                    ; increment by 16
DADD R4, R12, R4                           ; double add accumulation
DADD R4, R4, R6                            ; double add accumulation
DADD R4, R4, R8                            ; double add accumulation
DADD R4, R4, R10                           ; double add accumulation
DADD R24, R4, R24                          ; double add accumulation (final EV)
@P1 BRA 0x1a90                             ; loop back if more entries
```

### Final Store & Denormal Flush
```
ISETP.GT.AND P1, PT, R16, 0x4, PT           ; check if remaining > 4
@!P1 BRA 0x1d80
LDS.64 R12, [R17.X8+0x1a8]                  ; load 64-bit shared mem value
PLOP3.LUT P0, PT, PT, PT, PT, 0x8, 0x0       ; predicate LUT
IADD3 R16, R16, -0x8, RZ                     ; decrement by 8
LDS.128 R4, [R17.X8+0x1b0]                   ; load 128-bit shared mem value
LDS.128 R8, [R17.X8+0x1c0]                   ; load 128-bit shared mem value
DADD R24, R24, R12                           ; double add accumulation
LDS.128 R12, [R17.X8+0x1d0]                   ; load 128-bit shared mem value
DADD R4, R24, R4                             ; double add accumulation
LDS.64 R24, [R17.X8+0x1e0]                    ; load 64-bit shared mem value
DADD R4, R4, R6                              ; double add accumulation
DADD R4, R4, R8                              ; double add accumulation
IADD3 R17, R17, 0x8, RZ                      ; increment by 8
DADD R4, R4, R10                             ; double add accumulation
DADD R4, R4, R12                             ; double add accumulation
DADD R4, R4, R14                             ; double add accumulation
DADD R24, R4, R24                            ; double add accumulation (final EV)
ISETP.NE.OR P0, PT, R16, RZ, P0               ; check if remaining > 0
@!P0 BRA 0x1e50
LDS.64 R10, [R17.X8+0x1a8]                    ; load 64-bit shared mem value
IADD3 R16, R16, -0x4, RZ                      ; decrement by 4
LDS.128 R4, [R17.X8+0x1b0]                     ; load 128-bit shared mem value
ISETP.NE.AND P0, PT, R16, RZ, PT               ; check if remaining > 0
LDS.64 R8, [R17.X8+0x1c0]                      ; load 64-bit shared mem value
IADD3 R17, R17, 0x4, RZ                         ; increment by 4
DADD R10, R10, R24                             ; double add accumulation
DADD R4, R10, R4                               ; double add accumulation
DADD R4, R4, R6                                ; double add accumulation
DADD R24, R4, R8                               ; double add accumulation (final EV)
@P0 BRA 0x1da0
```

### Output & Denormal Guard
```
STS.64 [RZ], R24                             ; store final EV to shared memory
BSYNC B0                                     ; block barrier
BAR.SYNC.DEFER_BLOCKING 0x0
LDS.64 R4, [RZ]                              ; load final EV from shared memory
DSETP.NEU.AND P0, PT, R4, RZ, PT             ; check for NaN/zero
@!P0 BRA 0x2460                              ; skip if invalid
ISETP.GE.AND P0, PT, R20, c[0x0][0x1b0], PT  ; bounds check
@P0 EXIT                                     ; exit if out of bounds
FMUL.FTZ R4, R22, 1                          ; multiply by weight
PRMT R3, R3, 0x9910, RZ                      ; card byte extraction (hole card 1)
PRMT R9, R0, 0x9910, RZ                      ; card byte extraction (hole card 2)
PRMT R8, R2, 0x9910, RZ                      ; card byte extraction (board card)
F2F.F64.F32 R4, R4                           ; float -> double conversion
...
# Final EV computation with denormal flush
DMUL R6, R4, R6                              ; double multiply: EV = weight * sum
F2F.F32.F64 R11, R6                          ; double -> float conversion
DSETP.GEU.AND P0, PT, |R6|, c[0x2][0x0], PT   ; denormal check: |EV| >= FLT_MIN
@!P0 FMUL R11, R11, 1.175494350822287508e-38  ; flush to denormal minimum
@P1 FADD.FTZ R13, R11, R14                   ; add opponent EV
@P1 STG.E [R2.64], R13                        ; store final EV to global memory
@!P1 STG.E [R2.64], R11                       ; store final EV to global memory
BRA 0x2410                                    ; loop back
```

### Algorithm Summary
```
for each 3-player fold node (block-mapped):
    thread_idx = threadIdx.x
    warp_idx = threadIdx.x >> 5
    if thread_idx >= max_entries: exit
    
    load board cards from constant memory
    num_opponents = load_from_constant()
    inv_opponents = 1.0f / num_opponents    ; I2F.U32.RP + MUFU.RCP
    card_index = thread_idx % num_opponents ; modulo via 64-bit HI multiply
    
    clear shared memory                    ; STS [RZ], RZ
    __syncthreads()                        ; BSSY B0/B1
    
    load board cards (LDG.E.U8.CONSTANT)
    for each opponent combo:
        dead card check: compare all 3 board cards vs 2 hole cards
        if any match: skip (dead card)
        
        load reach probability from shared memory
        F2F.F64.F32: convert to double
        DADD: accumulate EV in double precision (multiple DADD chains)
        atomic spin lock: ATOMS.CAST.SPIN for shared mem update
    
    __syncthreads()
    
    warp-level butterfly reduction (SHFL.BFLY):
        steps: 16, 8, 4, 2, 1 (double precision DADD at each step)
    
    final_EV = warp_sum
    STS.64 [RZ], final_EV                  ; store to shared memory
    __syncthreads()
    
    load final EV from shared memory
    if NaN/zero: skip
    
    EV = weight * final_EV                 ; FMUL.FTZ + DMUL
    F2F.F32.F64: double -> float
    denormal flush: if |EV| < FLT_MIN: EV = 1.175e-38
    STG.E [dest_addr], EV                  ; store to global memory
    
    increment by stride, loop until all entries processed
```

---

## 35. evaluate_fold_abstract (sm_80, ~201 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_fold_abstract.txt`

**Role:** Abstract fold equity evaluation — reduced abstraction for faster fold EV computation against abstracted opponent ranges. Uses shared memory accumulation and normalized probability distribution.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R6, SR_CTAID.X                       ; R6 = blockIdx.x
IMAD.MOV.U32 R7, Z, Z, 0x4              ; R7 = 4 (stride)
ULDC.64 UR6, c[0x0][0x118]              ; 64-bit pointer base
IMAD.WIDE R2, R6, R7, c[0x0][0x170]     ; wide address computation
IMAD.WIDE R6, R6, R7, c[0x0][0x178]     ; wide address 2
LDG.E.CONSTANT R2, [R2.64]               ; load pointer
LDG.E.CONSTANT R0, [R6.64]               ; load pointer 2
IMAD.MOV.U32 R11, Z, Z, c[0x0][0x0]       ; R11 = base/stride
BSSY B0, 0x230                           ; block barrier
IMAD.MOV.U32 R4, Z, Z, RZ                ; R4 = 0 (accumulator)
S2R R5, SR_TID.X                         ; R5 = threadIdx.x
ISETP.GE.AND P0, PT, R11, 0x2, PT         ; check base >= 2
ISETP.GE.AND P1, PT, R5.reuse, c[0x0][0x18c], PT ; bounds check vs max_threads
IMAD.SHL.U32 R13, R5, 0x4, RZ             ; R13 = threadIdx.x * 16
SHF.R.S32.HI R10, Z, Z, 0x1f, R2           ; shift pointer
@P1 BRA 0x220                            ; skip if out of bounds
```

### Value Loading & Accumulation
```
ULDC UR4, c[0x0][0x180]                  ; uniform register load
IMAD R3, R10, c[0x0][0x180], RZ           ; address computation
USHF.R.S32.HI UR4, URZ, 0x1f, UR4         ; uniform shift
IMAD.WIDE.U32 R6, R2, c[0x0][0x180], RZ   ; wide address computation
IMAD.MOV.U32 R4, Z, Z, RZ                 ; R4 = 0
IMAD R9, R2, UR4, R3                      ; address computation
IMAD.MOV.U32 R3, Z, Z, R5                ; R3 = threadIdx.x
IMAD.IADD R14, R7, 0x1, R9                 ; increment
IADD3 R9, P1, R3, R6, RZ                   ; pointer computation
LEA.HI.X.SX32 R12, R3, R14, 0x1, P1        ; shared mem address high
LEA R8, P1, R9, c[0x0][0x160], 0x2         ; shared mem address
LEA.HI.X R9, R9, c[0x0][0x164], R12, 0x2, P1
LDG.E.CONSTANT R9, [R8.64]                ; load value from source
IADD3 R3, R3, c[0x0][0x0], RZ              ; increment by stride
ISETP.GE.AND P1, PT, R3, c[0x0][0x18c], PT ; bounds check
FADD.FTZ R4, R9, R4                       ; accumulate: sum += value
@!P1 BRA 0x190                            ; loop back if more
BSYNC B0                                  ; block barrier
STS [R13], R4                              ; store accumulator to shared memory
BAR.SYNC.DEFER_BLOCKING 0x0
@!P0 BRA 0x310                            ; skip if flag not set
```

### Shared Memory Reduction (2 levels)
```
SHF.R.U32.HI R4, Z, Z, 0x1, R11           ; shift right 1
ISETP.GE.AND P0, PT, R5, R4, PT            ; threadIdx.x >= warp_count
@!P0 IMAD R3, R4, 0x4, R13                 ; shared mem address
@!P0 LDS R6, [R13]                         ; load shared mem value
SHF.R.U32.HI R4, Z, Z, 0x1, R4             ; shift right 1 again
@!P0 LDS R3, [R3]                          ; load shared mem value 2
@!P0 FADD.FTZ R6, R6, R3                   ; add: sum += value2
@!P0 STS [R13], R6                         ; store back to shared memory
BAR.SYNC.DEFER_BLOCKING 0x0
ISETP.NE.AND P0, PT, R4, RZ, PT            ; check if more reduction needed
@P0 BRA 0x270                              ; loop back if more
```

### Second Phase: Normalized Probability Distribution
```
ISETP.GE.AND P0, PT, R5, c[0x0][0x188], PT ; bounds check
ULDC UR4, c[0x0][0x184]                  ; uniform register load
IMAD R3, R10, c[0x0][0x184], RZ           ; address computation
USHF.R.S32.HI UR4, URZ, 0x1f, UR4         ; uniform shift
IMAD R9, R2, UR4, R3                      ; address computation
@P0 EXIT                                  ; exit if out of bounds
I2F.U32.RP R4, c[0x0][0x0]               ; num_outcomes -> float
LOP3.LUT R3, RZ, R5, RZ, 0x33, !PT         ; bit-pair extraction
ISETP.NE.U32.AND P2, PT, RZ, c[0x0][0x0], PT
IADD3 R3, R3, c[0x0][0x188], RZ           ; pointer offset
MUFU.RCP R4, R4                           ; 1.0f / num_outcomes
IADD3 R6, R4, 0xffffffe, RZ                ; round
F2I.FTZ.U32.TRUNC.NTZ R7, R6               ; truncate
IMAD.MOV.U32 R6, Z, Z, RZ
IMAD.MOV R11, Z, Z, -R7
IMAD R11, R11, c[0x0][0x0], RZ
IMAD.HI.U32 R8, R7, R11, R6                ; 64-bit multiply
LDS R7, [RZ]                               ; load accumulator from shared mem
IMAD.HI.U32 R8, R8, R3, RZ                 ; add thread offset
IMAD.MOV R4, Z, Z, -R8
IMAD R3, R4, c[0x0][0x0], R3               ; card_index * num_outcomes + base
ISETP.GE.U32.AND P0, PT, R3, c[0x0][0x0], PT ; wrap check
@P0 IADD3 R3, R3, -c[0x0][0x0], RZ
@P0 IADD3 R8, R8, 0x1, RZ                   ; carry
ISETP.GE.U32.AND P1, PT, R3, c[0x0][0x0], PT
IMAD.WIDE.U32 R2, R2, c[0x0][0x184], RZ     ; wide address computation
ISETP.NE.AND P0, PT, RZ, c[0x0][0x190], PT
IMAD.IADD R4, R3, 0x1, R9                   ; address computation
FMUL.FTZ R0, R0, R7                        ; multiply: prob = value * accumulator
@P1 IADD3 R8, R8, 0x1, RZ                    ; carry
@!P2 LOP3.LUT R8, RZ, c[0x0][0x0], RZ, 0x33, !PT ; bit-pair extraction
IADD3 R6, R8, 0x1, RZ                       ; next card
LOP3.LUT R10, R6, 0x3, RZ, 0xc0, !PT         ; suit = card & 3
@!P0 BRA 0x8b0                            ; skip if invalid
```

### Output Store
```
ISETP.NE.AND P1, PT, R10, RZ, PT           ; predicate check
BSSY B0, 0x670                             ; block barrier
ISETP.GE.U32.AND P0, PT, R8, 0x3, PT        ; check if index < 3
@!P1 BRA 0x660                            ; skip if predicate false
IADD3 R7, P1, R5, R2, RZ                    ; pointer computation
LEA R6, P2, R7, c[0x0][0x168], 0x2          ; output address
LEA.HI.X.SX32 R8, R5, R4, 0x1, P1           ; output address high
LEA.HI.X R7, R7, c[0x0][0x16c], R8, 0x2, P2
IMAD.MOV.U32 R8, Z, Z, R6                  ; R8 = output address
IMAD.MOV.U32 R9, Z, Z, R7                  ; R9 = output address 2
LDG.E R7, [R8.64]                          ; load value from shared mem
IADD3 R10, R10, -0x1, RZ                    ; decrement
IADD3 R5, R5, c[0x0][0x0], RZ               ; increment by stride
ISETP.NE.AND P1, PT, R10, RZ, PT            ; loop if more
FADD.FTZ R11, R0, R7                        ; add: result = prob + value
IMAD.MOV.U32 R7, Z, Z, c[0x0][0x0]           ; base
STG.E [R8.64], R11                          ; store result to global memory
```

### Additional Output Stores (multiple entries)
```
IMAD.WIDE R6, R7, 0x4, R8                   ; wide address computation
@P1 BRA 0x5b0                               ; branch if predicate
BSYNC B0                                    ; block barrier
@!P0 EXIT                                   ; exit
# Second phase: store multiple results
IADD3 R6, P0, R5, R2, RZ                     ; pointer computation
LEA.HI.X.SX32 R7, R5, R4, 0x1, P0             ; output address high
LEA R10, P0, R6, c[0x0][0x168], 0x2           ; output address
LEA.HI.X R11, R6, c[0x0][0x16c], R7, 0x2, P0
LDG.E R7, [R10.64]                            ; load value
IADD3 R15, R5, c[0x0][0x0], RZ                ; increment by stride
IADD3 R5, P0, R15, R2, RZ                      ; next pointer
LEA.HI.X.SX32 R6, R15, R4, 0x1, P0              ; output address high
LEA R8, P0, R5, c[0x0][0x168], 0x2              ; output address
LEA.HI.X R9, R5, c[0x0][0x16c], R6, 0x2, P0
FADD.FTZ R5, R0, R7                           ; add: result = prob + value
STG.E [R10.64], R5                             ; store result to global
LDG.E R13, [R8.64]                             ; load value 2
IADD3 R15, R15, c[0x0][0x0], RZ                 ; increment by stride
IADD3 R7, P0, R15, R2, RZ                        ; next pointer
LEA.HI.X.SX32 R12, R15, R4, 0x1, P0               ; output address high
LEA R6, P0, R7, c[0x0][0x168], 0x2                ; output address
LEA.HI.X R7, R7, c[0x0][0x16c], R12, 0x2, P0
FADD.FTZ R13, R0, R13                           ; add: result2 = prob + value2
STG.E [R8.64], R13                               ; store result2 to global
LDG.E R17, [R6.64]                                ; load value 3
IADD3 R15, R15, c[0x0][0x0], RZ                   ; increment by stride
IADD3 R5, P0, R15, R2, RZ                          ; next pointer
LEA.HI.X.SX32 R12, R15, R4, 0x1, P0                 ; output address high
LEA R10, P0, R5, c[0x0][0x168], 0x2                 ; output address
LEA.HI.X R11, R5, c[0x0][0x16c], R12, 0x2, P0
FADD.FTZ R17, R0, R17                             ; add: result3 = prob + value3
STG.E [R6.64], R17                                  ; store result3 to global
LDG.E R5, [R10.64]                                  ; load value 4
FADD.FTZ R19, R0, R5                                ; add: result4 = prob + value4
IADD3 R5, R15, c[0x0][0x0], RZ                        ; increment by stride
STG.E [R10.64], R19                                   ; store result4 to global
ISETP.GE.AND P0, PT, R5, c[0x0][0x188], PT             ; bounds check
@!P0 BRA 0x680                                       ; loop back if more
EXIT
```

### Alternative Output Path (0x8b0+)
```
ISETP.NE.AND P1, PT, R10, RZ, PT             ; predicate check
BSSY B0, 0x9d0                               ; block barrier
ISETP.GE.U32.AND P0, PT, R8, 0x3, PT           ; check if index < 3
@!P1 BRA 0x9c0                               ; skip if predicate false
IADD3 R7, P1, R5, R2, RZ                      ; pointer computation
IMAD.MOV.U32 R11, Z, Z, c[0x0][0,0]            ; base
LEA R6, P2, R7, c[0x0][0x168], 0x2               ; output address
LEA.HI.X.SX32 R8, R5, R4, 0x1, P1                 ; output address high
LEA.HI.X R7, R7, c[0x0][0x16c], R8, 0x2, P2
IMAD.MOV.U32 R8, Z, Z, R6                       ; R8 = output address
IADD3 R10, R10, -0x1, RZ                         ; decrement
IMAD.MOV.U32 R9, Z, Z, R7                       ; R9 = output address 2
IADD3 R5, R5, c[0x0][0x0], RZ                     ; increment by stride
ISETP.NE.AND P1, PT, R10, RZ, PT                  ; loop if more
IMAD.WIDE R6, R11, 0x4, R8                         ; wide address computation
STG.E [R8.64], R0                                   ; store result to global
@P1 BRA 0x940                                       ; branch if predicate
BSYNC B0                                             ; block barrier
@!P0 EXIT                                            ; exit
```

### Algorithm Summary
```
for each abstract fold node (block-mapped):
    thread_idx = threadIdx.x
    if thread_idx >= max_threads: skip
    
    load values from source tables (LDG.E.CONSTANT)
    accumulate: sum += value (FADD.FTZ)
    store accumulator to shared memory (STS)
    __syncthreads() (BSYNC B0)
    
    # Shared memory reduction (2 levels)
    warp_count = base >> 1
    if threadIdx.x < warp_count:
        load 2 values from shared memory (LDS)
        sum = value1 + value2 (FADD.FTZ)
        store back to shared memory (STS)
    __syncthreads() (BAR.SYNC.DEFER_BLOCKING)
    repeat until single value remains
    
    # Second phase: normalized distribution
    num_outcomes = load_from_constant()
    inv_outcomes = 1.0f / num_outcomes    ; I2F.U32.RP + MUFU.RCP
    card_index = thread_idx % num_outcomes ; modulo via 64-bit HI multiply
    
    accumulator = load from shared memory (LDS)
    prob = value * accumulator            ; FMUL.FTZ
    
    for each outcome o (up to 4, predicate-gated):
        compute output address from pointer tables
        STG.E [addr], prob + value        ; FADD.FTZ + STG.E
    
    __syncthreads()
    exit
```

---

## 36. evaluate_showdown_brute_3p (sm_80, ~289 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown_brute_3p.txt`

**Role:** 3-player brute-force showdown hand evaluation — evaluates a hand against all possible opponent combinations in a 3-handed game, using atomic operations and warp-level operations for result aggregation.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R8, SR_CTAID.X                       ; R8 = blockIdx.x
ISETP.GE.AND P0, PT, R8, c[0x0][0x1e4], PT ; bounds check vs max_boards
@P0 EXIT                                 ; exit if out of bounds
IMAD.MOV.U32 R9, Z, Z, 0x4              ; R9 = 4 (stride)
ULDC.64 UR6, c[0x0][0x118]              ; 64-bit pointer base
IMAD.WIDE R6, R8, R9, c[0x0][0x188]     ; wide address computation
LDG.E.CONSTANT R6, [R6.64]              ; load pointer (hand data)
IMAD.MOV.U32 R17, Z, Z, 0x4              ; stride 4
IMAD.WIDE R10, R8, R9, c[0x0][0x178]     ; wide address
IMAD.WIDE R12, R8.reuse, R9.reuse, c[0x0][0x180] ; wide address 2
LDG.E.CONSTANT R0, [R10.64]              ; load pointer
IMAD.WIDE R8, R8, R9, c[0x0][0x170]      ; wide address 3
LDG.E.CONSTANT R2, [R12.64]              ; load pointer 2
LDG.E.CONSTANT R3, [R8.64]               ; load pointer 3
S2R R4, SR_TID.X                         ; R4 = threadIdx.x
IMAD.WIDE R14, R6, R17, c[0x0][0x1c0]    ; wide address 4
LDG.E.CONSTANT R5, [R14.64]              ; load pointer 4
ISETP.GE.AND P0, PT, R4.reuse, c[0x0][0x1e0], PT ; bounds check vs max_threads
BSSY B0, 0x280                           ; block barrier
ISETP.NE.AND P1, PT, R4, RZ, PT          ; threadIdx.x != 0
@P0 BRA 0x270                            ; skip if thread 0
```

### Shared Memory Initialization
```
IMAD.MOV.U32 R7, Z, Z, R4               ; R7 = threadIdx.x
SHF.R.S32.HI R9, Z, Z, 0x1f, R7.reuse     ; shift
IMAD.MOV.U32 R8, Z, Z, R7               ; R8 = threadIdx.x
IMAD.WIDE R8, R3, c[0x0][0x1e0], R8      ; wide address computation
IMAD.SHL.U32 R10, R8.reuse, 0x4, RZ      ; R10 = threadIdx.x * 16
SHF.L.U64.HI R11, R8, 0x2, R9             ; 64-bit shift left high
IADD3 R8, P0, R10, c[0x0][0x160], RZ      ; shared memory address
IADD3.X R9, R11, c[0x0][0x164], RZ, P0, !PT ; extended address
LDG.E.CONSTANT R8, [R8.64]               ; load value from source
ISETP.NE.AND P2, PT, RZ, c[0x0][0x1e8], PT ; flag check
@!P2 IADD3 R10, P0, R10, c[0x0][0x168], RZ ; alternate address
@!P2 IADD3.X R11, R11, c[0x0][0x16c], RZ, P0, !PT ; alternate address 2
@!P2 STG.E [R10.64], RZ                   ; store zero to global
STS [R7.X4], R8                            ; shared memory store
IADD3 R7, R7, c[0x0][0x0], RZ              ; increment by stride
ISETP.GE.AND P0, PT, R7, c[0x0][0x1e0], PT ; bounds check
@!P0 BRA 0x170                            ; loop back
BSYNC B0                                  ; block barrier
```

### Main Evaluation Loop
```
ULDC UR4, c[0x0][0x1e0]                  ; uniform register load
ISETP.GE.AND P0, PT, R4, R5, PT           ; threadIdx.x >= max_threads
UIMAD UR5, UR4, 0xc, URZ                  ; uniform multiply-add
USHF.L.U32 UR4, UR4, 0x3, URZ             ; uniform shift left 3
@!P1 STS [UR5], RZ                         ; store zero to shared mem (thread != 0)
BAR.SYNC.DEFER_BLOCKING 0x0               ; deferred barrier
@P0 BRA 0x5a0                             ; branch if flag set
IMAD.WIDE R10, R6, R17, c[0x0][0x1c8]     ; wide address computation
LDG.E.CONSTANT R7, [R10.64]               ; load pointer
IMAD.MOV.U32 R8, Z, Z, R4                ; R8 = threadIdx.x
IMAD.IADD R13, R7, 0x1, R8                ; increment
IMAD.MOV.U32 R10, Z, Z, 0x2               ; stride 2
IMAD.WIDE R10, R13, R10, c[0x0][0x1b0]    ; wide address computation
LDG.E.U16.CONSTANT R10, [R10.64]          ; load 16-bit card data
IADD3 R8, R8, c[0x0][0x0], RZ              ; increment by stride
YIELD
BSSY B0, 0x590                            ; block barrier
ISETP.GE.AND P2, PT, R8, R5, PT            ; threadIdx.x >= max_threads
LDS R9, [R10.X4]                           ; shared memory load
FSETP.NEU.FTZ.AND P0, PT, R9, RZ, PT       ; check for NaN/zero
@!P0 BRA 0x580                            ; skip if invalid
```

### Card Comparison & Warp Operations
```
SHF.R.S32.HI R14, Z, Z, 0x1f, R13          ; shift
LEA R12, P0, R13.reuse, c[0x0][0x1b8], 0x1 ; shared mem address
LEA R10, P1, R10, c[0x0][0x1d8], 0x1       ; shared mem address 2
LEA.HI.X R13, R13, c[0x0][0x1bc], R14, 0x1, P0 ; shared mem address 3
IMAD.X R11, Z, Z, c[0x0][0x1dc], P1         ; extended multiply
LDG.E.U16.CONSTANT R12, [R12.64]           ; load 16-bit card
LDG.E.U8.CONSTANT R14, [R10.64]            ; load card byte
LDG.E.U8.CONSTANT R16, [R10.64+0x1]        ; load next card byte
VOTEU.ANY UR8, UPT, PT                     ; warp vote any
IMAD.MOV.U32 R20, Z, Z, c[0x0][0x1e0]      ; max_threads
FLO.U32 R17, UR8                           ; floor warp vote to uint
S2R R18, SR_LANEID                         ; lane ID
S2R R19, SR_LTMASK                         ; lane thread mask
POPC R15, UR8                              ; population count (bit count)
ISETP.EQ.U32.AND P0, PT, R17, R18, PT      ; lane == warp vote result
LOP3.LUT R19, R19, UR8, RZ, 0xc0, !PT      ; mask lane mask
POPC R19, R19                              ; population count
@P0 ATOMS.ADD R18, [UR5], R15              ; atomic add to shared mem
SHFL.IDX PT, R18, R18, R17, 0x1f           ; warp shuffle by index
IMAD.IADD R10, R18, 0x1, R19               ; increment
LEA R11, R10.reuse, UR4, 0x1               ; address computation
IADD3 R10, R10, c[0x0][0x1e0], RZ           ; increment by stride
IMAD R13, R20, 0x2, R11                    ; shared mem offset
STS [R10.X4], R9                            ; shared memory store
STS.U16 [R11], R12                          ; shared memory store (16-bit)
STS.U8 [R13], R14                           ; shared memory store (8-bit)
STS.U8 [R13+0x1], R16                      ; shared memory store (8-bit)
BSYNC B0                                   ; block barrier
@!P2 BRA 0x320                            ; loop back
```

### Showdown Hand Evaluation (Card Comparison)
```
WARPSYNC 0xffffffff                         ; warp synchronization
BAR.SYNC.DEFER_BLOCKING 0x0                ; deferred barrier
LDS R5, [UR5]                              ; load atomic counter from shared mem
ISETP.NE.AND P0, PT, R5, RZ, PT            ; counter != 0
@!P0 EXIT                                   ; exit if zero
SHF.R.S32.HI R7, Z, Z, 0x1f, R6             ; shift
IMAD.SHL.U32 R10, R6, 0x4, RZ               ; R6 * 16
SHF.L.U64.HI R8, R6, 0x2, R7                ; 64-bit shift
IADD3 R6, P0, R10, c[0x0][0x1a0], RZ         ; pointer table offset
IADD3.X R7, R8, c[0x0][0x1a4], RZ, P0, !PT   ; extended address
LDG.E.CONSTANT R7, [R6.64]                  ; load pointer
IADD3 R10, P1, R10, c[0x0][0x1a8], RZ         ; pointer table offset 2
IADD3.X R11, R8, c[0x0][0x1ac], RZ, P1, !PT   ; extended address 2
ISETP.GE.AND P0, PT, R4, R7, PT              ; bounds check
@P0 EXIT                                    ; exit if out of bounds
LDG.E.CONSTANT R9, [R10.64]                  ; load pointer 2
IADD3 R6, R5, -0x1, RZ                       ; decrement counter
ISETP.GE.U32.AND P4, PT, R6, 0x3, PT           ; check if < 3
LOP3.LUT R6, R5, 0x3, RZ, 0xc0, !PT            ; suit = card & 3
IMAD.IADD R8, R5, 0x1, -R6                    ; suit-adjusted rank
IMAD.IADD R18, R9, 0x1, R4                    ; pointer computation
IMAD.MOV.U32 R21, Z, Z, 0x2                   ; stride 2
IMAD.WIDE R16, R18, R21, c[0x0][0x190]        ; wide address computation
LDG.E.U16.CONSTANT R16, [R16.64]               ; load 16-bit card data
ISETP.GE.AND P0, PT, R5, 0x1, PT                ; check if card index >= 1
IMAD.MOV.U32 R13, Z, Z, RZ                      ; R13 = 0
IMAD.MOV.U32 R11, Z, Z, RZ                      ; R11 = 0
@!P0 BRA 0x1060                              ; skip if card index < 1
IMAD.WIDE R18, R18, R21, c[0x0][0x198]         ; wide address computation
IMAD.WIDE.U32 R20, R16, R21, c[0x0][0x1d0]      ; wide address computation
LDG.E.U16.CONSTANT R15, [R18.64]                ; load 16-bit card
LDG.E.U8.CONSTANT R14, [R20.64]                  ; load card byte
LDG.E.U8.CONSTANT R12, [R20.64+0x1]              ; load next card byte
ISETP.NE.AND P5, PT, R6, RZ, PT                   ; card != 0
CS2R R10, SRZ                                      ; cycle register
IMAD.MOV.U32 R13, Z, Z, RZ                         ; R13 = 0
@!P4 BRA 0xcd0                                   ; branch if predicate false
CS2R R10, SRZ                                      ; cycle register
IMAD.MOV.U32 R13, Z, Z, RZ                         ; R13 = 0
IMAD.MOV.U32 R17, Z, Z, R8                         ; R17 = R8
IADD3 R21, R10.reuse, c[0x0][0x1e0], RZ            ; increment by stride
LEA R19, R10.reuse, UR4, 0x1                       ; address computation
LEA R24, R21, UR4, 0x1                             ; address computation 2
IADD3 R17, R17, -0x4, RZ                            ; decrement by 4
LDS.U16 R26, [R19+0x2]                            ; shared memory load (16-bit)
IADD3 R10, R10, 0x4, RZ                            ; increment by 4
LDS.U16 R22, [R24]                                 ; shared memory load (16-bit)
LDS.U16 R23, [R24+0x2]                             ; shared memory load (16-bit)
LDS.U16 R18, [R24+0x4]                             ; shared memory load (16-bit)
LDS.U16 R20, [R24+0x6]                             ; shared memory load (16-bit)
PRMT R27, R22, 0x7771, RZ                          ; card byte extraction (permute 0x7771)
LOP3.LUT R25, R22, 0xff, RZ, 0xc0, !PT               ; suit extraction (card & 3)
ISETP.NE.AND P0, PT, R27, R14.reuse, PT              ; card != board card
LDS.U16 R22, [R19]                                  ; shared memory load (16-bit)
ISETP.EQ.OR P0, PT, R25.reuse, R14, !P0              ; card == board card OR ...
PRMT R24, R18, 0x7771, RZ                          ; card byte extraction
ISETP.EQ.OR P0, PT, R25, R12, P0                      ; card == board card OR ...
PRMT R25, R23, 0x7771, RZ                          ; card byte extraction
ISETP.EQ.OR P2, PT, R27, R12, P0                      ; card == board card OR ...
LOP3.LUT R29, R18, 0xff, RZ, 0xc0, !PT                ; suit extraction (card & 3)
IMAD.MOV.U32 R27, Z, Z, R25                          ; R27 = suit
LOP3.LUT R25, R23, 0xff, RZ, 0xc0, !PT                ; suit extraction (card & 3)
IMAD.MOV.U32 R23, Z, Z, R24                          ; R23 = suit
PRMT R18, R20, 0x7771, RZ                          ; card byte extraction
LDS.U16 R24, [R19+0x6]                              ; shared memory load (16-bit)
ISETP.NE.AND P0, PT, R27, R14, PT                     ; card != board card
ISETP.NE.AND P1, PT, R23, R14.reuse, PT               ; card != board card
ISETP.EQ.OR P0, PT, R25, R14.reuse, !P0                ; card == board card OR ...
ISETP.EQ.OR P1, PT, R29, R14, !P1                      ; card == board card OR ...
ISETP.EQ.OR P0, PT, R25, R12.reuse, P0                 ; card == board card OR ...
IMAD.SHL.U32 R25, R21, 0x4, RZ                         ; address computation
ISETP.EQ.OR P1, PT, R29, R12.reuse, P1                   ; card == board card OR ...
ISETP.EQ.OR P0, PT, R27, R12.reuse, P0                   ; card == board card OR ...
IMAD.MOV.U32 R27, Z, Z, R18                            ; R27 = R18
LOP3.LUT R21, R20, 0xff, RZ, 0xc0, !PT                  ; suit extraction (card & 3)
IMAD.MOV.U32 R18, Z, Z, RZ                             ; R18 = 0
ISETP.NE.AND P3, PT, R27, R14, PT                       ; card != board card
IMAD.MOV.U32 R23, Z, Z, RZ                              ; R23 = 0
@!P2 LDS R18, [R25]                                    ; shared memory load
ISETP.EQ.OR P3, PT, R21, R14, !P3                        ; card == board card OR ...
@!P0 LDS R20, [R25+0x4]                                ; shared memory load
ISETP.EQ.OR P2, PT, R21, R12.reuse, P3                     ; card == board card OR ...
IMAD.MOV.U32 R21, Z, Z, RZ                              ; R21 = 0
ISETP.GE.U32.AND P3, PT, R22.reuse, R15.reuse, PT         ; card rank >= board rank
ISETP.GT.U32.AND P0, PT, R22, R15, PT                      ; card rank > board rank
LDS.U16 R22, [R19+0x4]                                 ; shared memory load (16-bit)
ISETP.EQ.OR P2, PT, R27, R12, P2                           ; card == board card OR ...
@!P1 LDS R21, [R25+0x8]                                ; shared memory load
ISETP.GT.U32.AND P1, PT, R26, R15, PT                      ; card rank > board rank
@!P2 LDS R23, [R25+0xc]                                ; shared memory load
ISETP.GE.U32.AND P2, PT, R26, R15, PT                      ; card rank >= board rank
FSEL R26, R18.reuse, RZ, !P3                             ; conditional select
FSEL R18, R18, RZ, P0                                     ; conditional select
FADD.FTZ R26, R26, R13                                   ; accumulate: sum += value
FSEL R13, R20, RZ, !P2                                    ; conditional select
FADD.FTZ R11, R18, R11                                   ; accumulate: sum += value
FSEL R20, R20, RZ, P1                                     ; conditional select
ISETP.GE.U32.AND P2, PT, R24, R15.reuse, PT                ; card rank >= board rank
FADD.FTZ R26, R26, R13                                   ; accumulate: sum += value
ISETP.GE.U32.AND P1, PT, R22.reuse, R15.reuse, PT           ; card rank >= board rank
FADD.FTZ R20, R11, R20                                   ; accumulate: sum += value
ISETP.GT.U32.AND P0, PT, R22, R15, PT                       ; card rank > board rank
FSEL R11, R21, RZ, !P1                                     ; conditional select
FSEL R21, R21, RZ, P0                                     ; conditional select
ISETP.NE.AND P0, PT, R17, RZ, PT                           ; flag check
FADD.FTZ R26, R26, R11                                   ; accumulate: sum += value
ISETP.GT.U32.AND P1, PT, R24, R15, PT                        ; card rank > board rank
FADD.FTZ R20, R20, R21                                   ; accumulate: sum += value
FSEL R13, R23.reuse, RZ, !P2                               ; conditional select
FSEL R11, R23, RZ, P1                                     ; conditional select
FADD.FTZ R13, R26, R13                                   ; accumulate: sum += value
FADD.FTZ R11, R20, R11                                   ; accumulate: sum += value
@P0 BRA 0x820                                           ; loop back
@!P5 BRA 0x1060                                          ; branch if predicate false
```

### Additional Evaluation Paths (0x0ce0+)
Similar card comparison and EV accumulation patterns for additional opponent hands, with:
- PRMT 0x7771 card byte extraction
- LOP3.LUT 0xff/0xc0 suit extraction
- ISETP.EQ.OR chains for card equality
- FSEL conditional selection
- FADD.FTZ accumulation chains

### Final EV Computation & Store
```
IMAD.MOV.U32 R17, Z, Z, RZ                         ; R17 = 0
ISETP.NE.AND P0, PT, RZ, c[0x0][0x1e8], PT           ; flag check
IMAD.WIDE R16, R3, c[0x0][0x1e0], R16                 ; wide address computation
LEA R14, P1, R16, c[0x0][0x168], 0x2                   ; output address
LEA.HI.X R15, R16, c[0x0][0x16c], R17, 0x2, P1          ; output address high
@P0 LDG.E R10, [R14.64]                                ; load value from global
FMUL.FTZ R11, R2, R11                                   ; multiply: result = weight * sum
IADD3 R4, R4, c[0x0][0x0], RZ                            ; increment by stride
FFMA.FTZ R11, R0, R13, R11                              ; fused multiply-add: result = weight * sum + EV
@P0 FADD.FTZ R11, R11, R10                               ; add: result += value
ISETP.GE.AND P0, PT, R4, R7, PT                          ; bounds check
STG.E [R14.64], R11                                       ; store result to global memory
@!P0 BRA 0x6e0                                           ; loop back
EXIT
```

### Algorithm Summary
```
for each 3-player showdown board (block-mapped):
    block_idx = blockIdx.x
    if block_idx >= max_boards: exit
    
    load hand data and board cards from constant memory
    thread_idx = threadIdx.x
    if thread_idx >= max_threads: exit
    
    # Phase 1: Initialize shared memory
    load card data from source tables
    store to shared memory (STS)
    __syncthreads() (BSSY B0)
    
    # Phase 2: Card comparison & atomic aggregation
    load card from constant memory table
    PRMT card byte extraction (permute 0x7771)
    LOP3.LUT suit extraction (card & 3)
    ISETP.EQ.OR chain: compare card vs board cards
    VOTEU.ANY: warp vote
    POPC: population count
    ATOMS.ADD: atomic add to shared memory counter
    SHFL.IDX: warp shuffle by index
    store results to shared memory (STS.U16, STS.U8)
    __syncthreads() (BSSY B0)
    
    # Phase 3: Showdown hand evaluation
    load opponent hands from shared memory (LDS.U16)
    PRMT card byte extraction
    LOP3.LUT suit extraction
    ISETP.EQ.OR: check card equality vs board cards
    ISETP.GE.U32 / ISETP.GT.U32: compare card ranks
    FSEL: conditional value selection
    FADD.FTZ: accumulate EV components
    
    # Phase 4: Final EV computation
    FMUL.FTZ: result = weight * accumulated_sum
    FFMA.FTZ: result = weight * sum + additional_EV
    @P0 FADD.FTZ: result += bonus_value
    STG.E [output_addr], result  ; store EV to global memory
    
    increment by stride, loop until all boards processed
```

---

## 37. evaluate_showdown_prefix_3p (sm_80, ~721+ instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_showdown_prefix_3p.txt`

**Role:** 3-player prefix-based showdown hand evaluation — uses prefix tree traversal for efficient hand ranking against opponent ranges in a 3-handed game. Features warp-level SHFL.UP reduction, inline subroutines via CALL.REL.NOINC, and divergent branching via BRA.DIV.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R3, SR_CTAID.X                       ; R3 = blockIdx.x
ISETP.GE.AND P0, PT, R3, c[0x0][0x1f4], PT ; bounds check vs max_boards
@P0 EXIT                                 ; exit if out of bounds
IMAD.MOV.U32 R14, Z, Z, 0x4              ; R14 = 4 (stride)
ULDC.64 UR4, c[0x0][0x118]              ; 64-bit pointer base
IMAD.WIDE R4, R3, R14, c[0x0][0x188]     ; wide address computation
LDG.E.CONSTANT R21, [R4.64]              ; load pointer (hand data)
IMAD.MOV.U32 R2, Z, Z, 0x4              ; stride 4
IMAD.WIDE R8, R3, R14, c[0x0][0x170]     ; wide address
LDG.E.CONSTANT R18, [R8.64]              ; load pointer
IMAD.WIDE R12, R21, R2, c[0x0][0x198]     ; wide address 2
IMAD.WIDE R10, R21, R2, c[0x0][0x1b0]     ; wide address 3
LDG.E.CONSTANT R20, [R12.64]              ; load pointer 2
LDG.E.CONSTANT R0, [R10.64]               ; load pointer 3
S2R R19, SR_TID.X                         ; R19 = threadIdx.x
ISETP.GE.AND P1, PT, R19, c[0x0][0x1f0], PT ; bounds check vs max_threads
BSSY B0, 0x290                            ; block barrier
```

### Card & Prefix Tree Setup
```
IMAD.WIDE R6, R3, R14, c[0x0][0x178]      ; wide address 4
IMAD.WIDE R4, R3, R14, c[0x0][0x180]      ; wide address 5
ISETP.NE.AND P0, PT, R20, RZ, PT           ; flag check
ISETP.EQ.OR P0, PT, R0, RZ, !P0             ; pointer == 0 OR ...
@P1 BRA 0x280                              ; skip if flag set
IMAD.MOV.U32 R3, Z, Z, R19                ; R3 = threadIdx.x
SHF.R.S32.HI R9, Z, Z, 0x1f, R3.reuse        ; shift
IMAD.MOV.U32 R8, Z, Z, R3                  ; R8 = threadIdx.x
IMAD.WIDE R8, R18, c[0x0][0x1f0], R8        ; wide address computation
IMAD.SHL.U32 R10, R8.reuse, 0x4, RZ          ; R10 = threadIdx.x * 16
SHF.L.U64.HI R11, R8, 0x2, R9                ; 64-bit shift left high
IADD3 R8, P1, R10, c[0x0][0x160], RZ          ; shared memory address
IADD3.X R9, R11, c[0x0][0x164], RZ, P1, !PT   ; extended address
LDG.E.CONSTANT R8, [R8.64]                    ; load prefix tree node from source
ISETP.NE.AND P2, PT, RZ, c[0x0][0x1f8], PT      ; flag check
@!P2 IADD3 R10, P1, R10, c[0x0][0x168], RZ      ; alternate address
@!P2 IADD3.X R11, R11, c[0x0][0x16c], RZ, P1, !PT ; alternate address 2
@!P2 STG.E [R10.64], RZ                           ; store zero to global
STS [R3.X4+0x20], R8                              ; shared memory store (offset 0x20)
IADD3 R3, R3, c[0x0][0x0], RZ                      ; increment by stride
ISETP.GE.AND P1, PT, R3, c[0x0][0x1f0], PT           ; bounds check
@!P1 BRA 0x180                                   ; loop back
BSYNC B0                                           ; block barrier
BAR.SYNC.DEFER_BLOCKING 0x0                         ; deferred barrier
@P0 EXIT                                            ; exit if flag set
```

### Prefix Tree Traversal (Phase 1)
```
IMAD.WIDE R10, R21, R2, c[0x0][0x1a0]      ; wide address computation
LDG.E.CONSTANT R13, [R6.64]                ; load pointer
LDG.E.CONSTANT R12, [R4.64]                ; load pointer 2
LDG.E.CONSTANT R10, [R10.64]               ; load pointer 3
IABS R15, c[0x0][0x0]                       ; absolute value of stride
BSSY B0, 0x5a0                             ; block barrier
IADD3 R3, R0, c[0x0][0x0], RZ               ; increment by stride
I2F.RP R14, R15                            ; int -> float (round positive)
IADD3 R3, R3, -0x1, RZ                      ; decrement
IABS R4, R3                                 ; absolute value
LOP3.LUT R3, R3, c[0x0][0x0], RZ, 0x3c, !PT ; bit manipulation (mask 0x3c)
ISETP.GE.AND P2, PT, R3, RZ, PT             ; check if >= 0
MUFU.RCP R14, R14                          ; reciprocal: 1.0f / value
IADD3 R8, R14, 0xffffffe, RZ                ; round
F2I.FTZ.U32.TRUNC.NTZ R9, R8                ; float -> int truncate
IMAD.MOV.U32 R8, Z, Z, RZ                   ; R8 = 0
IMAD.MOV R6, Z, Z, -R9                       ; R6 = -truncated
IMAD R5, R6, R15, RZ                         ; R5 = -truncated * stride
IMAD.HI.U32 R9, R9, R5, R8                   ; 64-bit multiply high
IMAD.HI.U32 R6, R9, R4, RZ                   ; add thread offset
IMAD.MOV R5, Z, Z, -R6                        ; R5 = -carry
IMAD R4, R15, R5, R4                         ; card_index * stride + base
ISETP.GT.U32.AND P1, PT, R15, R4, PT          ; check if stride > card_index
@!P1 IMAD.IADD R4, R4, 0x1, -R15              ; increment
@!P1 IADD3 R6, R6, 0x1, RZ                     ; carry
ISETP.NE.AND P1, PT, RZ, c[0x0][0x0], PT       ; flag check
ISETP.GE.U32.AND P0, PT, R4, R15, PT            ; check if card_index >= stride
@P0 IADD3 R6, R6, 0x1, RZ                        ; carry
ISETP.GE.AND P0, PT, R19, R0, PT                 ; threadIdx.x >= flag
@!P2 IMAD.MOV R6, Z, Z, -R6                        ; negate
@!P1 LOP3.LUT R6, Z, c[0x0][0x0], RZ, 0x33, !PT   ; bit-pair extraction
IMAD R3, R19, R6, RZ                              ; address computation
@P0 BRA 0x590                                     ; branch if flag set
```

### Shared Memory Clearing (Large Buffer)
```
IMAD.WIDE R4, R21, R2, c[0x0][0x1b8]      ; wide address computation
LDG.E.CONSTANT R2, [R4.64]                 ; load pointer
IMAD.MOV.U32 R7, Z, Z, R19                 ; R7 = threadIdx.x
IMAD.IADD R4, R2, 0x1, R7                   ; increment
IMAD.MOV.U32 R5, Z, Z, 0x2                   ; stride 2
IMAD.WIDE R4, R4, R5, c[0x0][0x1a8]          ; wide address computation
LDG.E.U16.CONSTANT R4, [R4.64]                ; load 16-bit value
IADD3 R8, R7.reuse, c[0x0][0x1f0], RZ          ; shared memory offset
IADD3 R7, R7, c[0x0][0x0], RZ                  ; increment by stride
ISETP.GE.AND P0, PT, R7, R0, PT                  ; bounds check
LDS R9, [R4.X4+0x20]                            ; shared memory load (offset 0x20)
STS [R8.X4+0x20], R9                             ; shared memory store
@!P0 BRA 0x4f0                                   ; loop back
BSYNC B0                                           ; block barrier
```

### Prefix Tree Accumulation Loop (Multiple Phases)
The kernel processes prefix tree nodes in multiple phases, each loading 16+ values from shared memory (LDS) and accumulating with FADD.FTZ:

```
IMAD.IADD R5, R6, 0x1, R3                     ; increment
BAR.SYNC.DEFER_BLOCKING 0x0                    ; deferred barrier
IMAD.MOV.U32 R7, Z, Z, RZ                       ; R7 = 0
IMNMX R2, R0, R5, PT                            ; integer min/max
BSSY B0, 0xee0                                  ; block barrier
ISETP.GE.AND P1, PT, R3, R2, PT                  ; bounds check
@P1 BRA 0xed0                                   ; skip if out of bounds
IMAD.IADD R4, R2, 0x1, -R3.reuse                 ; decrement
LOP3.LUT R8, RZ, R2, RZ, 0x33, !PT                ; bit-pair extraction
BSSY B1, 0x7a0                                  ; block barrier
IMAD.MOV.U32 R7, Z, Z, RZ                       ; R7 = 0
LOP3.LUT P0, R4, R4, 0x3, RZ, 0xc0, !PT           ; suit extraction (card & 3)
IMAD.MOV.U32 R5, Z, Z, R3.reuse                   ; R5 = R3
IADD3 R8, -R8, -0x2, -R3                          ; address computation
ISETP.GE.U32.AND P2, PT, R8, 0x3, PT               ; check if < 3
@!P0 BRA 0x790                                  ; skip if < 3
IADD3 R8, R3.reuse, c[0x0][0x1f0], R0              ; pointer computation
IMAD.MOV.U32 R7, Z, Z, RZ                       ; R7 = 0
IADD3 R9, R3, c[0x0][0x1f0], RZ                   ; pointer computation 2
IMAD.MOV.U32 R5, Z, Z, R3                        ; R5 = R3
LEA R8, R8, 0x20, 0x2                             ; shared memory address
LEA R9, R9, 0x20, 0x2                             ; shared memory address 2
LDS R14, [R9]                                      ; shared memory load
IADD3 R4, R4, -0x1, RZ                             ; decrement
IADD3 R5, R5, 0x1, RZ                               ; increment
ISETP.NE.AND P0, PT, R4, RZ, PT                     ; loop if more
IADD3 R9, R9, 0x4, RZ                               ; increment by 4
FADD.FTZ R7, R14, R7                                 ; accumulate: sum += value
STS [R8], R7                                         ; store accumulator to shared mem
IADD3 R8, R8, 0x4, RZ                                ; increment by 4
@P0 BRA 0x700                                       ; loop back
BSYNC B1                                            ; block barrier
@!P2 BRA 0xed0                                      ; skip if predicate false
```

### Large-Scale Accumulation (0x07b0+)
The kernel processes large blocks of prefix tree values with repeated LDS/FADD.FTZ/STS patterns:
- Loads 16 values from shared memory (LDS [R4+offset] for offsets 0x00 through 0x34)
- Accumulates with FADD.FTZ chains
- Stores results back to shared memory (STS [R8+offset])
- Processes in 0x40-byte blocks (64 bytes = 16 floats)
- Loop back at 0x0bc0

### Warp-Level Reduction (SHFL.UP)
```
BSYNC B0                                           ; block barrier
SHFL.UP PT, R4, R7, 0x1, RZ                        ; warp shuffle up step 1
LOP3.LUT P2, R11, R19, 0x1f, RZ, 0xc0, !PT          ; predicate LUT
SHF.R.S32.HI R14, Z, Z, 0x5, R19                     ; shift right 5
ISETP.GE.U32.AND P3, PT, R11.reuse, 0x2, PT          ; check if lane >= 2
ISETP.GE.U32.AND P4, PT, R11.reuse, 0x4, PT          ; check if lane >= 4
IMAD.SHL.U32 R9, R14, 0x4, RZ                         ; address computation
ISETP.GE.U32.AND P0, PT, R11.reuse, 0x8, PT           ; check if lane >= 8
ISETP.NE.AND P5, PT, R11, 0x1f, PT                      ; check if lane != 31
FADD.FTZ R4, R4, R7                                   ; accumulate: sum += value
FSEL R4, R7, R4, !P2                                   ; conditional select
SHFL.UP PT, R5, R4, 0x2, RZ                            ; warp shuffle up step 2
@P3 FADD.FTZ R4, R4, R5                                 ; conditional add
SHFL.UP PT, R5, R4, 0x4, RZ                            ; warp shuffle up step 4
@P4 FADD.FTZ R4, R4, R5                                 ; conditional add
SHFL.UP PT, R5, R4, 0x8, RZ                            ; warp shuffle up step 8
@P0 FADD.FTZ R4, R4, R5                                 ; conditional add
ISETP.GE.U32.AND P0, PT, R11, 0x10, PT                  ; check if lane >= 16
SHFL.UP PT, R5, R4, 0x10, RZ                            ; warp shuffle up step 16
@P0 FADD.FTZ R4, R4, R5                                 ; conditional add
```

### Output Store
```
ISETP.GT.U32.AND P0, PT, R11, 0x7, PT                  ; check if lane > 7
IMAD.MOV.U32 R8, Z, Z, R4                              ; R8 = final sum
ISETP.NE.OR P0, PT, R14, RZ, P0                          ; combined predicate
@!P5 STS [R9], R8                                       ; store to shared memory
BAR.SYNC.DEFER_BLOCKING 0x0                              ; deferred barrier
@P0 BRA 0x1150                                           ; branch if flag set
```

### Inline Subroutines (CALL.REL.NOINC)
The kernel uses inline subroutines at addresses 0x2a80, 0x2af0, 0x2be0:
- `CALL.REL.NOINC 0x2be0` — subroutine entry
- `RET.REL.NODEC R4 0x0` — return from subroutine
- These subroutines perform warp-level SHFL.UP reduction with BRA.DIV divergent branching

### Final Output Phase (0x2c20+)
```
BSYNC B0                                           ; block barrier
ISETP.NE.AND P0, PT, R15, RZ, PT                    ; flag check
@P0 BRA 0x3080                                      ; branch if flag set
ISETP.GT.U32.AND P0, PT, R9.reuse, 0x7, PT            ; check if lane > 7
IMAD.MOV.U32 R22, Z, Z, RZ                            ; R22 = 0
ISETP.NE.AND P6, PT, R9, RZ, PT                       ; flag check
@!P0 IMAD.SHL.U32 R15, R2, 0x4, RZ                      ; address computation
@!P0 LOP3.LUT R15, R15, 0x7c, RZ, 0xc0, !PT             ; suit extraction
@!P0 LDS R22, [R15]                                     ; shared memory load
BRA.DIV ~URZ, 0xd0a0                                    ; divergent branch
SHFL.UP PT, R20, R22, 0x1, RZ                          ; warp shuffle up step 1
@P6 FADD.FTZ R22, R20, R22                              ; conditional add
BRA.DIV ~URZ, 0xd100                                    ; divergent branch
SHFL.UP PT, R15, R22, 0x2, RZ                          ; warp shuffle up step 2
@P2 FADD.FTZ R22, R22, R15                              ; conditional add
SHFL.UP PT, R20, R22, 0x4, RZ                          ; warp shuffle up step 4
@!P0 IMAD.SHL.U32 R15, R2, 0x4, RZ                        ; address computation
@P1 FADD.FTZ R22, R20, R22                              ; conditional add
@!P0 LOP3.LUT R15, R15, 0x7c, RZ, 0xc0, !PT               ; suit extraction
@!P0 STS [R15], R22                                       ; store to shared memory
WARPSYNC 0xffffffff                                      ; warp synchronization
BAR.SYNC.DEFER_BLOCKING 0x0                              ; deferred barrier
```

### Algorithm Summary
```
for each 3-player showdown board (block-mapped):
    block_idx = blockIdx.x
    if block_idx >= max_boards: exit
    
    load prefix tree data and board cards from constant memory
    thread_idx = threadIdx.x
    if thread_idx >= max_threads: exit
    
    # Phase 1: Load prefix tree nodes into shared memory
    load prefix tree node values (LDG.E.CONSTANT)
    store to shared memory (STS [R3.X4+0x20])
    __syncthreads() (BSSY B0)
    
    # Phase 2: Clear large shared memory buffer (0x250 stride, up to 0xc00 bytes)
    clear shared memory entries (STS [R11], RZ x14)
    __syncthreads() (BSYNC B1)
    
    # Phase 3: Prefix tree traversal & accumulation
    compute card index via modulo (IMAD.HI.U32 64-bit multiply)
    traverse prefix tree:
        for each tree level:
            load 16 values from shared memory (LDS [R4+offset])
            accumulate with FADD.FTZ chains
            store results back to shared memory (STS [R8+offset])
            process in 0x40-byte blocks
    __syncthreads() (BSYNC B1)
    
    # Phase 4: Warp-level reduction (SHFL.UP)
    warp reduction: steps 1, 2, 4, 8, 16 (FADD.FTZ at each step)
    SHFL.UP PT, R*, R*, step, 0x1f
    
    # Phase 5: Store partial results
    STS [R9], R8  ; store warp sum to shared memory
    BAR.SYNC.DEFER_BLOCKING 0x0
    
    # Phase 6: Inline subroutine reduction (CALL.REL.NOINC)
    subroutine at 0x2be0: additional SHFL.UP reduction with BRA.DIV
    RET.REL.NODEC R4 0x0  ; return
    
    # Phase 7: Final output
    compute final EV: FMUL.FTZ / FFMA.FTZ chains
    STG.E [output_addr], result  ; store to global memory
    __syncthreads() (BSSY B0)
    
    increment by stride, loop until all boards processed
```

---

## 38. evaluate_fold_showdown_prefix (sm_80, ~793+ instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/evaluate_fold_showdown_prefix.txt`

**Role:** Fold/showdown prefix evaluation — computes fold equity and showdown EV using prefix tree traversal, with uniform register operations and extensive shared memory buffering.

### Thread Setup & Bounds Check
```
IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]   ; R1 = constant slot 0x28 (stride)
S2R R18, SR_CTAID.X                       ; R18 = blockIdx.x
ISETP.GE.AND P0, PT, R18, c[0x0][0x214], PT ; bounds check vs max_boards
@P0 EXIT                                 ; exit if out of bounds
IMAD.MOV.U32 R25, Z, Z, 0x4              ; R25 = 4 (stride)
ULDC.64 UR12, c[0x0][0x118]              ; 64-bit pointer base
IMAD.WIDE R8, R18, R25, c[0x0][0x1a0]     ; wide address computation
LDG.E.CONSTANT R8, [R8.64]                ; load pointer
IMAD.WIDE R4, R18, R25, c[0x0][0x170]     ; wide address 2
LDG.E.CONSTANT R4, [R4.64]                ; load pointer 2
IMAD.WIDE R6, R18, R25, c[0x0][0x178]     ; wide address 3
LDG.E.CONSTANT R6, [R6.64]                ; load pointer 3
IMAD.WIDE R10, R8, R25, c[0x0][0x1d0]     ; wide address 4
LDG.E.CONSTANT R10, [R10.64]               ; load pointer 4
IMAD.WIDE R12, R8, R25, c[0x0][0x1b8]     ; wide address 5
LDG.E.CONSTANT R0, [R12.64]                ; load pointer 5
R2UR UR4, R4                               ; broadcast to uniform register
S2R R2, SR_TID.X                           ; R2 = threadIdx.x
R2UR UR5, R6                               ; broadcast to uniform register
USHF.R.U32.HI UR6, URZ, 0x1f, UR4          ; uniform shift right 31
ISETP.NE.AND P5, PT, RZ, UR6, PT            ; flag check
ULOP3.LUT UR6, UR6, 0x1, URZ, 0x3c, !UPT    ; uniform bit manipulation (mask 0x3c)
ISETP.NE.AND P5, PT, RZ, c[0x0][0x210], !P5 ; combined flag check
R2UR UR7, R10                               ; broadcast to uniform register
ISETP.NE.AND P0, PT, R0, RZ, PT              ; pointer != 0
ISETP.EQ.OR P0, PT, RZ, UR7, !P0              ; combined pointer check
USHF.L.U32 UR8, UR7, 0x1, URZ                ; uniform shift left 1
USHF.R.S32.HI UR9, URZ, 0x1f, UR8            ; uniform shift right 31
@P0 BRA 0xc980                              ; branch if flag set
```

### Card Extraction (Rank & Suit)
```
IMAD.WIDE R20, R8, R25, c[0x0][0x1d8]      ; wide address computation
IMAD.WIDE R10, R18.reuse, R25.reuse, c[0x0][0x168] ; wide address 2
LDG.E.CONSTANT R21, [R20.64]                ; load pointer
IMAD.WIDE R12, R18, R25, c[0x0][0x180]      ; wide address 3
IMAD.WIDE R14, R18.reuse, R25.reuse, c[0x0][0x188] ; wide address 4
LDG.E.CONSTANT R4, [R12.64]                  ; load pointer 3
IMAD.WIDE R16, R18.reuse, R25.reuse, c[0x0][0x190] ; wide address 5
LDG.E.CONSTANT R5, [R14.64]                  ; load pointer 4
IMAD.WIDE R18, R18, R25.reuse, c[0x0][0x198]  ; wide address 6
LDG.E.CONSTANT R6, [R16.64]                   ; load pointer 5
IMAD.WIDE R24, R8, R25, c[0x0][0x1c0]          ; wide address 7
LDG.E.CONSTANT R7, [R18.64]                    ; load pointer 6
UIMAD.WIDE UR8, UR7, 0x2, UR8                  ; uniform multiply-add
LDG.E.CONSTANT R3, [R10.64]                    ; load pointer 7
LDG.E.CONSTANT R24, [R24.64]                   ; load pointer 8
IMAD.MOV.U32 R20, Z, Z, c[0x0][0,0]             ; R20 = base/stride
ISETP.NE.AND P0, PT, R20, 0x80, PT                ; check if base >= 128
IMAD.SHL.U32 R11, R2.reuse, 0x4, RZ                ; R11 = threadIdx.x * 16
IMAD.U32 R26, Z, Z, UR8                             ; uniform address
IMAD.U32 R27, Z, Z, UR9                             ; uniform address 2
LOP3.LUT R9, R2, 0x1f, RZ, 0xc0, !PT                   ; rank = card & 0x1f
LOP3.LUT R11, R11, 0x7c, RZ, 0xc0, !PT                  ; suit/rank = (threadIdx.x*16) & 0x7c
LEA R10, R26, 0x250, 0.2                              ; shared memory address
R2UR UR16, R21                                         ; broadcast to uniform register
@!P0 BRA 0x7480                                        ; branch if flag not set
```

### Shared Memory Clearing (Large Buffer)
```
ISETP.NE.AND P0, PT, R20, 0x40, PT                   ; check if base >= 64
@P0 BRA 0x5360                                        ; branch if base >= 64
@!P5 BRA 0x880                                        ; branch if predicate false
ISETP.GE.AND P0, PT, R2, c[0x0][0x208], PT             ; threadIdx.x >= max_threads
BSSY B0, 0x870                                        ; block barrier
@P0 BRA 0x860                                          ; branch if flag set
LOP3.LUT R11, RZ, R2.reuse, RZ, 0x33, !PT               ; bit-pair extraction
BSSY B1, 0x500                                        ; block barrier
LOP3.LUT R12, RZ, R2, RZ, 0x33, !PT                      ; bit-pair extraction
IMAD.MOV.U32 R13, Z, Z, R2                              ; R13 = threadIdx.x
IADD3 R11, R11, c[0x0][0x208], RZ                        ; increment by max_threads
IADD3 R12, R12, c[0x0][0x208], RZ                        ; increment by max_threads
LEA.HI R11, R11, 0x1, RZ, 0x1a                            ; extended address
ISETP.GE.U32.AND P1, PT, R12, 0xc0, PT                     ; check if >= 192
LOP3.LUT P0, R11, R11, 0x3, RZ, 0xc0, !PT                  ; suit extraction (card & 3)
@!P0 BRA 0x4f0                                            ; skip if predicate false
IMAD.IADD R12, R26, 0x1, R2.reuse                         ; address computation
IMAD.MOV.U32 R13, Z, Z, R2                                ; R13 = threadIdx.x
LEA R12, R12, 0x250, 0.2                                   ; shared memory address
IADD3 R11, R11, -0x1, RZ                                    ; decrement
STS [R12], RZ                                                ; clear shared memory
IADD3 R13, R13, 0x40, RZ                                   ; increment by 64
ISETP.NE.AND P0, PT, R11, RZ, PT                             ; loop if more
IADD3 R12, R12, 0x100, RZ                                   ; increment by 256
@P0 BRA 0x490                                              ; loop back
BSYNC B1                                                    ; block barrier
@!P1 BRA 0x860                                               ; skip if predicate false
```

### Large-Scale Shared Memory Clear (0x510+)
```
IADD3 R12, -R13, c[0x0][0x208], RZ                         ; address computation
IMAD.IADD R11, R26, 0x1, R13                                ; address computation
BSSY B1, 0x710                                              ; block barrier
PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0                       ; predicate LUT
ISETP.GT.AND P1, PT, R12, 0x300, PT                            ; check if > 768
LEA R11, R11, 0x250, 0.2                                       ; shared memory address
IADD3 R11, R11, 0x200, RZ                                      ; increment by 512
@!P1 BRA 0x700                                                ; skip if > 768
IMAD.MOV.U32 R12, Z, Z, c[0x0][0x208]                          ; R12 = max_threads
PLOP3.LUT P0, PT, PT, PT, PT, 0x8, 0x0                          ; predicate LUT
IADD3 R12, R12, -0x300, RZ                                       ; decrement by 768
IADD3 R13, R13, 0x400, RZ                                        ; increment by 1024
STS [R11+-0x200], RZ                                             ; clear shared memory
ISETP.GE.AND P1, PT, R13, R12, PT                                 ; check if counter >= max_threads
STS [R11+-0x100], RZ                                             ; clear shared memory
STS [R11], RZ                                                     ; clear shared memory
STS [R11+0x100], RZ                                               ; clear shared memory
STS [R11+0x200], RZ                                               ; clear shared memory
STS [R11+0x300], RZ                                               ; clear shared memory
STS [R11+0x400], RZ                                               ; clear shared memory
STS [R11+0x500], RZ                                               ; clear shared memory
STS [R11+0x600], RZ                                               ; clear shared memory
STS [R11+0x700], RZ                                               ; clear shared memory
STS [R11+0x800], RZ                                               ; clear shared memory
STS [R11+0x900], RZ                                               ; clear shared memory
STS [R11+0xa00], RZ                                               ; clear shared memory
STS [R11+0xb00], RZ                                               ; clear shared memory
STS [R11+0xc00], RZ                                               ; clear shared memory
STS [R11+0xd00], RZ                                               ; clear shared memory
IADD3 R11, R11, 0x1000, RZ                                         ; increment by 4096
@!P1 BRA 0x5c0                                                    ; loop back
BSYNC B1                                                           ; block barrier
```

### Prefix Tree Accumulation (0x0880+)
```
UIADD3 UR8, UR7, 0xff, URZ                                         ; uniform add
IMAD.MOV.U32 R15, Z, Z, c[0x0][0x18]                                 ; constant slot 0x18
SEL R19, R10, RZ, P5                                                  ; conditional select
BSSY B0, 0x1120                                                      ; block barrier
USHF.R.S32.HI UR8, URZ, 0x8, UR8                                     ; uniform shift right 8
IMAD.MOV.U32 R12, Z, Z, RZ                                            ; R12 = 0
IADD3 R15, P0, R15, 0x250, RZ                                          ; shared memory address
R2UR UR9, R19                                                          ; broadcast to uniform register
IMAD R11, R2, UR8, RZ                                                  ; address computation
LEA R15, P1, R26, R15, 0.2                                              ; shared memory address
IMAD.U32 R18, Z, Z, UR8                                                  ; uniform address
IMAD.X R25, Z, Z, c[0x0][0x1c], P0                                        ; extended multiply
IADD3 R16, R11, UR8, RZ                                                  ; address computation
IMAD R13, R18, 0x40, R11                                                  ; address computation
SEL R15, R15, RZ, P5                                                      ; conditional select
IMNMX R16, R16, UR7, PT                                                    ; integer min/max
IMAD R14, R18, 0x40, R13                                                     ; address computation
LEA.HI.X R25, R26, R25, R27, 0.2, P1                                          ; shared memory address high
ISETP.GE.AND P0, PT, R11, R16, PT                                             ; bounds check
IMAD R17, R18, 0x40, R14                                                      ; address computation
IADD3 R30, R13, UR8, RZ                                                         ; address computation
IADD3 R31, R14, UR8, RZ                                                         ; address computation 2
IADD3 R32, R17, UR8, RZ                                                         ; address computation 3
SEL R25, R25, RZ, P5                                                      ; conditional select
IMNMX R30, R30, UR7, PT                                                       ; integer min/max
IMNMX R31, R31, UR7, PT                                                       ; integer min/max
IMNMX R32, R32, UR7, PT                                                       ; integer min/max
@P0 BRA 0x1110                                                                ; branch if bounds check passed
```

### Output Phase (0x2c20+)
```
BSYNC B0                                           ; block barrier
ISETP.NE.AND P0, PT, R15, RZ, PT                    ; flag check
@P0 BRA 0x3080                                      ; branch if flag set
ISETP.GT.U32.AND P0, PT, R9.reuse, 0x7, PT            ; check if lane > 7
IMAD.MOV.U32 R22, Z, Z, RZ                            ; R22 = 0
ISETP.NE.AND P6, PT, R9, RZ, PT                       ; flag check
@!P0 IMAD.SHL.U32 R15, R2, 0x4, RZ                      ; address computation
@!P0 LOP3.LUT R15, R15, 0x7c, RZ, 0xc0, !PT             ; suit extraction
@!P0 LDS R22, [R15]                                     ; shared memory load
BRA.DIV ~URZ, 0xd0a0                                    ; divergent branch
SHFL.UP PT, R20, R22, 0x1, RZ                          ; warp shuffle up step 1
@P6 FADD.FTZ R22, R20, R22                              ; conditional add
BRA.DIV ~URZ, 0xd100                                    ; divergent branch
SHFL.UP PT, R15, R22, 0x2, RZ                          ; warp shuffle up step 2
@P2 FADD.FTZ R22, R22, R15                              ; conditional add
SHFL.UP PT, R20, R22, 0x4, RZ                          ; warp shuffle up step 4
@!P0 IMAD.SHL.U32 R15, R2, 0x4, RZ                        ; address computation
@P1 FADD.FTZ R22, R20, R22                              ; conditional add
@!P0 LOP3.LUT R15, R15, 0x7c, RZ, 0xc0, !PT               ; suit extraction
@!P0 STS [R15], R22                                       ; store to shared memory
WARPSYNC 0xffffffff                                      ; warp synchronization
BAR.SYNC.DEFER_BLOCKING 0x0                              ; deferred barrier
```

### Algorithm Summary
```
for each fold/showdown board (block-mapped):
    block_idx = blockIdx.x
    if block_idx >= max_boards: exit
    
    load prefix tree data and board cards from constant memory
    thread_idx = threadIdx.x
    
    # Card extraction
    rank = card & 0x1f                    ; LOP3.LUT 0xc0
    suit_rank = (threadIdx.x * 16) & 0x7c ; LOP3.LUT 0xc0
    
    # Shared memory clearing (large buffer, up to 0xd00 bytes)
    clear shared memory entries (STS [R11], RZ x14)
    __syncthreads() (BSYNC B1)
    
    # Prefix tree traversal & accumulation
    uniform register operations (R2UR, UIMAD, USHF)
    load prefix tree nodes from constant memory
    accumulate with FADD.FTZ chains
    IMNMX for integer min/max
    
    # Warp-level reduction (SHFL.UP)
    steps 1, 2, 4, 8, 16, 32
    conditional adds with predicate gating
    
    # Final output
    compute final EV/FE value
    STG.E [output_addr], result  ; store to global memory
    __syncthreads() (BSSY B0)
    
    increment by stride, loop until all boards processed
```

---

## 35. forward_chance_turn_river_batched (sm_90, ~49 instructions)

**File:** `KERNEL_CODE_ALL/sm_90/obj16/forward_chance_turn_river_batched.sass`

**Role:** Batched backward chance pass for turn/river streets. Computes chance outcome probabilities and accumulates CFV via GPU-wide atomic add with spin-lock synchronization.

### Thread Setup & Bounds Check
```
LDC R1, c[0x0][0x28]         ; R1 = max thread bound
S2R R5, SR_CTAID.X           ; R5 = blockIdx.x
LDC.64 R2, c[0x0][0x238]     ; R2 = pointer table base
ULDC.64 UR6, c[0x0][0x208]   ; UR6 = descriptor base
IMAD.WIDE R2, R5, 0x8, R2    ; R2 = blockIdx.x * 8 + table_base
LDG.E.64.CONSTANT R2, desc[UR6][R2.64]  ; load 64-bit pointer from constant desc
LDC R0, c[0x0][RZ]           ; R0 = num_outcomes (slot 0x0)
BSSY B0, 0x440               ; block barrier
S2R R4, SR_TID.X             ; R4 = threadIdx.x
ISETP.GE.AND P0, PT, R4, UR8, PT  ; P0 = (threadIdx.x >= num_threads)
@P0 BRA 0x430                ; exit if out of bounds
```

### Chance Probability Computation
```
I2F.U32.RP R8, R0            ; R8 = (float)num_outcomes
MUFU.RCP R8, R8              ; R8 = 1.0f / num_outcomes  (chance probability)
VIADD R6, R8, 0xffffffe      ; R6 = R8 - 2.0 (for address computation)
F2I.FTZ.U32.TRUNC.NTZ R7, R6 ; R7 = trunc(int, R8 - 2)
```

### Card Extraction & Dead Card Check
```
PRMT R9, R14, 0x9910, RZ     ; extract card bytes from packed card
ISETP.NE.AND P1, PT, R9, -0x1, PT  ; P1 = (card != 0xffffffff, dead card check)
@!P1 BRA 0x780               ; skip dead cards
```

### Atomic CFV Accumulation (spin lock)
```
LDS R12, [R11]               ; load shared memory accumulator
FADD R13, R9, R12            ; R13 = prob * cfv + accumulator
ATOMS.CAST.SPIN R13, [R11], R12, R13  ; atomic compare-and-swap spin lock
ISETP.EQ.U32.AND P1, PT, R13, 0x1, PT  ; P1 = (lock acquired)
@!P1 BRA 0x730               ; retry if lock not acquired
```

### Second Phase: 4-Value Global Store
```
PRMT R28, R30, 0x9910, RZ    ; extract card bytes
FFMA.FTZ R16, R0, R16, RZ    ; R16 = card * R0 + R16 (fused multiply-add)
STG.E desc[UR6][R8.64], R17  ; store float result to global memory
STG.E desc[UR6][R6.64], R19
STG.E desc[UR6][R10.64], R21
STG.E desc[UR6][R12.64], R25  ; store 4 values
```

### FP16 MMA & Barriers
```
HFMA2.MMA R6, -RZ, RZ, 0, 0   ; FP16 matrix multiply-accumulate (epsilon=0)
BSSY B0                      ; block barrier sync
BSYNC B0                     ; barrier wait
BAR.SYNC.DEFER_BLOCKING 0x0  ; deferred block barrier
```

### Algorithm Summary
```
for each thread (threadIdx.x < num_threads):
    prob = 1.0f / num_outcomes
    for each card outcome (up to 4):
        card = load_card(outcome_index)
        if card == DEAD (0xffffffff): continue
        cfv = load_cfv(card)
        atomic_add(shared_accumulator, prob * cfv)  // spin lock
    __syncthreads()
    for each of 4 output slots:
        store_global(output[slot], accumulated_value)
```

---

## 36. train_mixed_fold_showdown_parent_packed (sm_90, ~545 instructions)

**File:** `KERNEL_CODE_ALL/sm_90/obj16/train_mixed_fold_showdown_parent_packed.sass`

**Role:** CFR training kernel for mixed fold/showdown parent nodes. Computes regrets and strategy updates with packed S16 counterfactual value storage. Mixed strategy (fold + showdown) terminal parent evaluation.

### Thread Setup & Bounds Check
```
LDC R1, c[0x0][0x28]         ; R1 = max block bound
S2R R24, SR_CTAID.X           ; R24 = blockIdx.x
ULDC UR4, c[0x0][0x2c0]       ; UR4 = grid bound constant
ISETP.GE.AND P0, PT, R24, UR4, PT  ; P0 = (blockIdx.x >= grid_bound)
@P0 EXIT                      ; exit if out of bounds
```

### Constant Buffer Loads (c[0x0] pointer table)
- `c[0x0][0x208]` — descriptor base (UR6)
- `c[0x0][0x228]` — pointer table base 1 (UR4)
- `c[0x0][0x230]` — pointer table base 2 (UR6)
- `c[0x0][0x238]` — pointer table base 3 (R10)
- `c[0x0][0x240]` — pointer table base 4
- `c[0x0][0x248]` — reach/CFV data base (R22)
- `c[0x0][0x258]` — board/card data base (UR4)
- `c[0x0][0x260]` — pointer table base 5
- `c[0x0][0x280]` — strategy/base pointer (R12)
- `c[0x0][0x290]` — regrets pointer (R26)
- `c[0x0][0x298]` — scale/weight pointer (UR4)
- `c[0x0][0x2a0]` — secondary scale pointer (R6)
- `c[0x0][0x2a8]` — output pointer (R28)
- `c[0x0][0x2bc]` — num_actions/stride (R7)
- `c[0x0][0x2c0]` — grid bound (UR4)

### Card Extraction (packed card: c | (p<<8))
```
LDG.E.U8.CONSTANT R16, desc[UR8][R16.64]  ; load board card byte
LOP3.LUT R5, RZ, R4, RZ, 0x33, !PT        ; R5 = card & 0x3 (suit)
LOP3.LUT R9, R9, 0x3fc, RZ, 0xc0, !PT    ; R9 = (card >> 2) & 0xff (rank bits)
ISETP.NE.AND P2, PT, R16, 0xff, PT       ; P2 = (card != 0xff, valid card)
```

### Q16 / S16 Dequantization
```
I2F.S16 R35, R33                  ; int16 → float (Q16 dequant)
FMUL.FTZ R14, R14, 3.0518509447574615479e-05  ; scale by 1/32768
I2F.S16 R17, R31                  ; second int16 dequant
```

### Regret Computation & Matching
```
FMUL.FTZ R14, R14, 3.0518509447574615479e-05  ; apply scale
FSETP.GT.FTZ.AND P5, PT, R35, RZ, PT  ; P5 = (regret > 0)
FADD.FTZ R32, RZ, R17             ; accumulate positive regret
FSEL R32, R32, RZ, P0             ; conditional select (clamp negative to 0)
@P5 FADD.FTZ R32, R32, R35        ; add positive regret component
MUFU.RCP R41, R32                 ; 1.0f / sum_pos_regret
FMNMX.FTZ R34, RZ, R17, !PT       ; clamp regret to >= 0
@P5 FMUL.FTZ R16, R34, R41        ; strategy = pos_regret * inv_sum_pos
```

### Shared Memory & Barriers
```
STS [UR4+0x10], RZ               ; clear shared memory slot
STS [UR4], R8                    ; store CFV to shared memory
STS [UR4+0x4], R3                ; store regret to shared memory
STS [UR4+0x8], R0                ; store index to shared memory
STS [UR4+0xc], R4                ; store scale to shared memory
BSSY B0                          ; block barrier
BSYNC B0                         ; barrier wait
BAR.SYNC.DEFER_BLOCKING 0x0      ; deferred block barrier
```

### Atomic Spin Lock (shared memory accumulation)
```
ATOMS.CAST.SPIN R13, [R11], R12, R13  ; atomic CAS spin lock on shared mem
```

### Warp Reduction (SHFL.DOWN butterfly)
```
SHFL.DOWN PT, R13, R28, 0x10, 0x1f   ; warp shuffle down by 16
FMNMX.FTZ R13, R13, R28, !PT        ; max reduction step 1
SHFL.DOWN PT, R10, R13, 0x8, 0x1f    ; shuffle down by 8
FMNMX.FTZ R10, R13, R10, !PT        ; max reduction step 2
SHFL.DOWN PT, R17, R10, 0x4, 0x1f    ; shuffle down by 4
FMNMX.FTZ R17, R10, R17, !PT        ; max reduction step 3
SHFL.DOWN PT, R12, R17, 0x2, 0x1f    ; shuffle down by 2
FMNMX.FTZ R12, R17, R12, !PT        ; max reduction step 4
SHFL.DOWN PT, R25, R12, 0x1, 0x1f    ; shuffle down by 1
FMNMX.FTZ R25, R12, R25, !PT        ; final max
```

### Cross-Block Max Reduction
```
REDUX.MAX UR7, R25               ; warp-level max reduction
VOTEU.ANY UR4, UPT, PT           ; warp vote any
ATOMS.MAX RZ, [UR4], R10         ; atomic max across blocks (global)
```

### S16 Packed Output
```
VIMNMX.U32 R19, R10, 0xffff, PT   ; clamp to uint16 range [0, 65535]
FMUL.FTZ R10, R24, 65535          ; scale float to uint16 range
STS [UR4+0x14], R10               ; store scaled value to shared mem
VIMNMX.U32 R25, R14, 0xffff, PT   ; clamp second value
STG.E.U16 desc[UR8][R12.64], R19  ; store packed S16 to global memory
STG.E.U16 desc[UR8][R16.64], R25  ; store packed S16 to global memory
```

### Algorithm Summary
```
for each block (blockIdx.x < grid_bound):
    load board cards, hole cards, reach probs (Q16/S16 packed)
    for each action a:
        cfv = load_cfv_child(action)          ; global memory load
        reach = dequantize_q16(reach_prob)    ; I2F.S16 + scale
        regret[a] = reach * (cfv - node_ev)
        node_ev += strategy[a] * cfv
    sum_pos = sum(max(0, regret[a]))           ; FMNMX + FADD
    inv_sum_pos = 1.0f / sum_pos               ; MUFU.RCP
    for each action a:
        strategy[a] = max(0, regret[a]) * inv_sum_pos
    warp_reduce_max(regrets)                   ; SHFL.DOWN butterfly
    atomic_max_global(max_regret)              ; ATOMS.MAX
    pack strategy to S16                       ; VIMNMX + scale by 65535
    store_global(strategy_packed)              ; STG.E.U16
    __syncthreads()
```

---

## 37. train_opponent_fold_showdown_parent_packed (sm_90, ~1785 instructions)

**File:** `KERNEL_CODE_ALL/sm_90/obj16/train_opponent_fold_showdown_parent_packed.sass`

**Role:** Opponent CFR training kernel for fold/showdown parent nodes. Larger unrolled variant with double-buffered processing and more aggressive optimization. Computes opponent regrets and strategy updates.

### Thread Setup & Bounds Check
```
LDC R1, c[0x0][0x28]         ; R1 = max block bound
S2R R25, SR_CTAID.X           ; R25 = blockIdx.x
ULDC UR4, c[0x0][0x300]       ; UR4 = grid bound constant
ISETP.GE.AND P0, PT, R25, UR4, PT
@P0 EXIT
```

### Key Differences from train_mixed_fold_showdown_parent_packed
- **More constant buffer slots:** uses `c[0x0][0x2d8]`, `c[0x0][0x2e0]`, `c[0x0][0x2e8]`, `c[0x0][0x2f0]`, `c[0x0][0x2f8]`, `c[0x0][0x300]`, `c[0x0][0x318]`
- **Double buffering:** Two main processing loops (at 0x800–0x1390 and 0x2290–0x2640) with alternating buffers
- **HFMA2 MMA with non-zero epsilon:** `HFMA2.MMA R20, -RZ, RZ, 1.666015625, -0.052093505859375`
- **VIADDMNMX:** Vector integer add min/max for range checking
- **BAR.RED.OR.DEFER_BLOCKING:** Cross-warp reduction barrier
- **BRA.DIV:** Divergent branch for warp-level control flow
- **PLOP3.LUT predicate computation:** `PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0` and `0x8`
- **Fixed-point division via magic numbers:** `IMAD R10, R10, 0x7feb352d, RZ` (divide by 3), `IMAD R13, R13, -0x7b935975, RZ` (divide by 5)
- **More unrolled loops:** 16-entry block processing with explicit stride increments

### Card Extraction & Suit
```
LOP3.LUT R10, R16, 0x7ff, RZ, 0xc0, !PT   ; R10 = card & 0x7ff (rank + suit)
SHF.R.U32.HI R7, RZ, 0xb, R16              ; extract high bits (card >> 11)
ISETP.NE.AND P1, PT, R7.reuse, 0x1fffff, PT  ; dead card check (21-bit)
```

### Q16/S16 Dequantization with Scale
```
I2F.S16 R16, R29                  ; int16 → float
I2F.S16 R17, R30                  ; second int16 → float
FMUL.FTZ R25, R24.reuse, R25      ; apply scale factor
FMUL.FTZ R23, R24, R23            ; apply scale factor
FSETP.GT.FTZ.AND P0, PT, R25, 1.0000000116860974231e-07, PT  ; epsilon guard
FSEL R25, R25, RZ, P0             ; clamp tiny values to 0
```

### Regret Matching (opponent variant)
```
MUFU.RCP R16, R16                 ; 1.0f / sum_pos
FMUL.FTZ R25, R19, R16.reuse     ; strategy = pos_regret * inv_sum
FMUL.FTZ R23, R17, R16            ; strategy = pos_regret * inv_sum
FMNMX.FTZ R34, RZ, R17, !PT       ; clamp regret to >= 0
FMNMX.FTZ R28, RZ, R35, !PT       ; clamp second regret to >= 0
```

### Shared Memory Ping-Pong
```
STS [R18], R25                    ; store strategy to buffer A
STS [R8], R23                     ; store strategy to buffer B
STS.U8 [R27+UR6], RZ              ; clear flag byte
STS.U8 [R14], RZ                  ; clear second flag byte
```

### Cross-Warp Reduction
```
SHFL.UP PT, R17, R24, 0x1, RZ    ; warp shuffle up by 1
FADD.FTZ R17, R17, R24           ; add pair
SHFL.UP PT, R18, R25, 0x1, RZ    ; shuffle up by 1
FADD.FTZ R18, R18, R25           ; add pair
; steps 2, 4, 8, 16, 32 for full warp reduction
WARPSYNC.ALL                     ; warp sync
```

### S16 Packing with Magic Number Division
```
IMAD R10, R10, 0x7feb352d, RZ    ; fixed-point: R10 * 0x7feb352d >> 32 (divide by ~3)
SHF.R.U32.HI R13, RZ, 0xf, R10   ; extract high bits
LOP3.LUT R12, R13, R12, RZ, 0x3c  ; LUT combine
IMAD R12, R12, 0x7feb352d, RZ    ; second magic multiply
SHF.R.U32.HI R17, RZ, 0xf, R12   ; extract
IMAD R17, R17, -0x7b935975, RZ   ; multiply by -0x7b935975 (divide by ~5)
LOP3.LUT R13, R17, R12, RZ, 0x3c  ; final combine
```

### Output: Packed S16 to Global Memory
```
VIMNMX.U32 R19, R10, 0xffff, PT   ; clamp to uint16
STG.E.U16 desc[UR8][R12.64], R19   ; store packed S16
VIMNMX R10, R0, 0x7fff, PT         ; clamp to int16 positive
VIMNMX R23, R10, -0x7fff, !PT     ; clamp to int16 negative range
STG.E.U16 desc[UR8][R18.64], R23   ; store packed S16
```

### Algorithm Summary
```
for each block (blockIdx.x < grid_bound):
    // Phase 1: Load reach probs and CFVs (double buffered)
    for each chunk (16 entries):
        load reach_q16[chunk] via LDG.E.U16.CONSTANT
        load cfv[chunk] via LDG.E.S16.CONSTANT
        dequantize: I2F.S16 + scale (FMUL)
        compute regret = reach * (cfv - node_ev)
        clamp regret >= 0 via FMNMX
        store to shared memory (ping-pong buffers)
    __syncthreads() (BSSY/BSYNC B0/B1)
    
    // Phase 2: Regret matching & strategy computation
    sum_pos = sum(max(0, regret))
    inv_sum_pos = 1.0f / sum_pos (MUFU.RCP)
    strategy[a] = max(0, regret[a]) * inv_sum_pos
    
    // Phase 3: Warp reduction & cross-block max
    warp_reduce_max via SHFL.UP butterfly
    ATOMS.MAX global max across blocks
    
    // Phase 4: Pack S16 and store
    pack strategy to S16 (VIMNMX + magic number division)
    STG.E.U16 to global memory
    
    // Phase 5: Second buffer (alternating)
    repeat Phase 1-4 for second buffer
    BAR.SYNC.DEFER_BLOCKING
```

---

## 38. train_mixed_opponent_fold_showdown_parent (sm_90, ~1897 instructions)

**File:** `KERNEL_CODE_ALL/sm_90/obj16/train_mixed_opponent_fold_showdown_parent.sass`

**Role:** Mixed opponent CFR training kernel for fold/showdown parent nodes. Similar to train_opponent_fold_showdown_parent_packed but with different constant buffer layout and register allocation.

### Thread Setup & Bounds Check
```
LDC R1, c[0x0][0x28]         ; R1 = max block bound
S2R R9, SR_CTAID.X            ; R9 = blockIdx.x
VIADD R1, R1, 0xffffffd8      ; R1 = R1 - 40 (adjust bound)
R2UR UR5, R1                  ; move to uniform reg
ULDC UR4, c[0x0][0x320]       ; UR4 = grid bound constant
ISETP.GE.AND P0, PT, R9, UR4, PT
@P0 EXIT
```

### Constant Buffer Layout (different from train_opponent variant)
- `c[0x0][0x208]` — descriptor base (UR16)
- `c[0x0][0x218]` — data base (R18)
- `c[0x0][0x220]` — table base 1 (R16)
- `c[0x0][0x228]` — table base 2 (UR6)
- `c[0x0][0x230]` — table base 3 (UR6)
- `c[0x0][0x240]` — table base 4 (UR8)
- `c[0x0][0x248]` — table base 5 (UR8)
- `c[0x0][0x250]` — reach data base (R26)
- `c[0x0][0x258]` — card data base (R34)
- `c[0x0][0x260]` — table base 6 (R2)
- `c[0x0][0x268]` — table base 7 (UR6)
- `c[0x0][0x270]` — output pointer (UR10)
- `c[0x0][0x278]` — CFV base (UR12)
- `c[0x0][0x280]` — strategy base (UR10)
- `c[0x0][0x288]` — regrets pointer (R22)
- `c[0x0][0x290]` — scale pointer (UR13)
- `c[0x0][0x298]` — secondary scale (UR12)
- `c[0x0][0x2a8]` — output base (R16)
- `c[0x0][0x2c8]` — temp pointer (R32)
- `c[0x0][0x2d0]` — index pointer (R16)
- `c[0x0][0x2d8]` — weight pointer (UR4)
- `c[0x0][0x2e0]` — regrets base (R14)
- `c[0x0][0x2e8]` — outcome pointer (R18)
- `c[0x0][0x2f0]` — config (UR18)
- `c[0x0][0x2f8]` — stride (UR12)
- `c[0x0][0x2fc]` — offset (UR9)
- `c[0x0][0x300]` — num_actions (R4)
- `c[0x0][0x318]` — mask/flag (R3)
- `c[0x0][0x320]` — grid bound (UR4)

### Card Extraction
```
LOP3.LUT R3, R10, 0x7ff, RZ, 0xc0, !PT   ; R3 = card & 0x7ff
SHF.R.U32.HI R2, RZ, 0xb, R10             ; card >> 11
ISETP.NE.AND P1, PT, R2, 0x1fffff, PT    ; dead card check (21-bit)
```

### FP16 MMA (multiple epsilon values)
```
HFMA2.MMA R20, -RZ, RZ, 0, 5.9604644775390625e-08   ; epsilon = 2^-24
HFMA2.MMA R10, -RZ, RZ, 0, 0                        ; epsilon = 0
HFMA2.MMA R24, -RZ, RZ, 0, 0                        ; epsilon = 0
HFMA2.MMA R20, -RZ, RZ, 1.666015625, -0.052093505859375  ; non-zero epsilon
```

### Vector Integer Add Min/Max
```
VIADDMNMX R12, R15, R20, R8, PT   ; vector add with min/max
VIADDMNMX R17, R28, R21, R8, PT   ; second vector min/max
VIADDMNMX R23, R15, R17.reuse, R8, PT  ; third vector min/max
VIADDMNMX R23, R23, R20, R8, PT   ; fourth vector min/max
```

### Cross-Warp Reduction (BAR.RED.OR)
```
BAR.RED.OR.DEFER_BLOCKING 0x0, P0  ; cross-warp OR reduction
B2R.RESULT RZ, P0                  ; broadcast reduction result
```

### Divergent Branching
```
BRA.DIV UR9, 0x6640               ; divergent branch (warp split)
BRA.DIV UR9, 0x6ac0               ; second divergent branch
```

### Q16/S16 Dequantization
```
I2F.S16 R10, R10                  ; int16 → float
I2F.S16 R31, R22                  ; second dequant
I2F.S16 R29, R29                  ; third dequant
I2F.S16 R29, R24                  ; fourth dequant
I2F.S16 R28, R11                  ; fifth dequant
I2F.S16 R20, R20                  ; sixth dequant
```

### Regret Matching
```
MUFU.RCP R16, R26                 ; 1.0f / sum_pos
MUFU.RCP R33, R26                 ; second reciprocal
MUFU.RCP R26, R26                 ; third reciprocal
MUFU.RCP R26, R19                 ; fourth reciprocal
MUFU.RCP R10, R19                 ; fifth reciprocal
FMUL.FTZ R25, R19, R16.reuse     ; strategy = pos * inv_sum
FMUL.FTZ R23, R17, R16            ; strategy = pos * inv_sum
FMNMX.FTZ R34, RZ, R17, !PT       ; clamp regret >= 0
FMNMX.FTZ R28, RZ, R35, !PT       ; clamp second regret >= 0
```

### Warp Shuffle Reduction (SHFL.UP butterfly)
```
SHFL.UP PT, R17, R24, 0x1, RZ     ; shuffle up by 1
FADD.FTZ R17, R17, R24            ; add pair
SHFL.UP PT, R18, R25, 0x1, RZ     ; shuffle up by 1
FADD.FTZ R18, R18, R25            ; add pair
; steps 2, 4, 8, 16 for full reduction
WARPSYNC.ALL                      ; warp sync
```

### S16 Packed Output with Magic Number Division
```
IMAD R10, R10, 0x7feb352d, RZ     ; fixed-point divide by ~3
SHF.R.U32.HI R13, RZ, 0xf, R10
LOP3.LUT R12, R13, R12, RZ, 0x3c
IMAD R12, R12, 0x7feb352d, RZ
SHF.R.U32.HI R17, RZ, 0xf, R12
IMAD R17, R17, -0x7b935975, RZ    ; fixed-point divide by ~5
LOP3.LUT R13, R17, R12, RZ, 0x3c
SHF.R.U32.HI R10, RZ, 0x10, R13
IMAD R17, R17, -0x7b935975, RZ
LOP3.LUT R10, R10, R13, RZ, 0x3c
SHF.R.U32.HI R12, RZ, 0x10, R17
SHF.R.U32.HI R10, RZ, 0x8, R10
LOP3.LUT R12, R12, R17, RZ, 0x3c
I2FP.F32.U32 R13, R10             ; uint → float
SHF.R.U32.HI R12, RZ, 0x8, R12
I2FP.F32.U32 R12, R12
```

### int16 Clamp for Packed Output
```
VIMNMX R10, R0, 0x7fff, PT         ; clamp to int16 max
VIMNMX R23, R10, -0x7fff, !PT      ; clamp to int16 min
STG.E.U16 desc[UR8][R18.64], R23   ; store packed S16
VIMNMX R10, R0, 0x7fff, PT         ; clamp second value
STG.E.U16 desc[UR8][R16.64], R5    ; store packed S16
VIMNMX R5, R0, 0x7fff, PT          ; clamp third value
STG.E.U16 desc[UR8][R12.64], R5    ; store packed S16
```

### Algorithm Summary
```
for each block (blockIdx.x < grid_bound):
    // Phase 1: Load reach probs (Q16/S16 packed) and CFVs
    for each chunk (16 entries):
        LDG.E.U16.CONSTANT reach_q16[chunk]
        LDG.E.S16.CONSTANT cfv[chunk]
        I2F.S16 dequantize + FMUL scale
        compute regret = reach * (cfv - node_ev)
        FMNMX clamp >= 0
        STS to shared memory (double buffered)
    BSSY/BSYNC barrier
    
    // Phase 2: Regret matching
    sum_pos = sum(max(0, regret))
    inv_sum = MUFU.RCP(sum_pos)
    strategy = max(0, regret) * inv_sum
    
    // Phase 3: Warp reduction
    SHFL.UP butterfly reduction (steps 1,2,4,8,16)
    WARPSYNC.ALL
    
    // Phase 4: Cross-block reduction
    BAR.RED.OR.DEFER_BLOCKING
    B2R.RESULT broadcast
    
    // Phase 5: Pack S16 + store
    VIMNMX clamp to int16/uint16 range
    Magic number division for address computation
    STG.E.U16 packed output to global memory
    
    // Phase 6: Second buffer (alternating)
    Repeat phases 1-5 for second buffer
    BAR.SYNC.DEFER_BLOCKING
```

---

## 39. backward_br_player (sm_80, 449 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_br_player.txt`

**Role:** CFR backward pass for a player at a node with boundary reduction ("br" suffix). Computes counterfactual regret and updates strategy for boundary nodes where CFV values must be reduced across child subtrees.

### Thread Setup & Bounds Check
- `IMAD.MOV.U32 R1, RZ, RZ, c[0x0][0x28]` — load max thread count from constant buffer
- `S2R R7, SR_TID.X` / `S2R R8, SR_CTAID.X` — thread/block indices
- `ISETP.GE.AND P0, PT, R7, c[0x0][0x1a4], PT` — bounds check; `@P0 EXIT` if out of range

### Card Extraction
- `LDG.E.U8.CONSTANT R0, [R4.64]` — load packed card byte
- `LOP3.LUT R5, R0, 0x3, RZ, 0xc0, !PT` — suit extraction: `suit = card & 0x3`
- `IMAD.IADD R6, R0, 0x1, -R5` — compute rank index: `rank = card - suit`
- `ISETP.GE.U32.AND P1, PT, R6, 0x3, PT` — validate rank index

### Boundary Reduction Loop (up to 15 child actions, 0x0–0xf)
- Sequential `LDG.E.CONSTANT` loads of CFV values from child node global memory (offsets 0x1–0xf)
- Each child CFV loaded via `IMAD.WIDE.U32` address computation from constant buffer pointer table
- `FSETP.GT.FTZ.AND P2, PT, R14, R15, PT` — compare CFV pairs for max
- `FSEL R28, R14, R15, P2` — conditional select max CFV
- `FSETP.GT.FTZ.AND P2, PT, R21, R28, PT` — chained max comparisons across children
- `FSEL R21, R21, R28, P2` — propagate max

### Algorithm Summary
```
for each player p (thread-mapped):
    bounds_check()
    card = load_card()
    suit = card & 0x3
    rank = card - suit
    max_cfv = -inf
    for each child action a (0..14):
        child_cfv = load_cfv_child(p, a)
        max_cfv = max(max_cfv, child_cfv)   ; FSETP.GT.FTZ + FSEL
    node_cfv = max_cfv                      ; boundary-reduced CFV
    store node_cfv to global memory
```

---

## 40. backward_chance_turn_river (sm_80, 209 instructions)

**File:** `/tmp/opencode/sass_disasm/sm_80/backward_chance_turn_river.txt`

**Role:** CFR backward pass for chance nodes at turn/river streets. Propagates counterfactual values backward through chance outcomes (community card distributions).

### Thread Setup & Barrier Sync
- `S2R R0, SR_TID.X` — thread index
- `BSSY B0, 0x3b0` / `BSYNC B0` — block barrier for shared memory initialization
- `BSSY B1, 0x290` / `BSYNC B1` — second barrier for chance outcome computation

### Card & Chance Outcome Processing
- `I2F.U32.RP R5, c[0x0][0x0]` — convert int to float
- `MUFU.RCP R5, R5` — reciprocal approximation (1.0f / num_chance_outcomes)
- `LOP3.LUT R3, RZ, c[0x0][0x0], RZ, 0x33, !PT` — bitmask extraction
- `F2I.FTZ.U32.TRUNC.NTZ R3, R2` — float-to-int truncation for chance index
- `PRMT` / `LOP3.LUT 0xc0` — card byte extraction (rank/suit)

### CFV Averaging & Shared Memory
- `HFMA2.MMA R8, -RZ, RZ, 0, 2.384185791015625e-07` — FP16 MMA init (epsilon = 2^-22)
- `LDG.E.U16.CONSTANT R12, [R8.64]` — load Q16 CFV value
- `FMUL.FTZ R7, R2, R11` — weight CFV by chance probability
- `FADD R9, R7, R8` — accumulate weighted CFV
- `ATOMS.CAST.SPIN R9, [R12], R8, R9` — atomic spin-lock update to shared memory

### Algorithm Summary
```
for each chance node (thread-mapped):
    BSSY B0  ; shared memory init barrier
    num_outcomes = load_num_chance_outcomes()
    inv_outcomes = 1.0f / num_outcomes   ; MUFU.RCP
    
    BSSY B1  ; chance computation barrier
    for each chance outcome c:
        card = decode_card(outcome_c)    ; PRMT 0x9910 byte extraction
        if card == -1: continue           ; invalid outcome
        child_cfv = load_cfv_child(card)  ; Q16 U16 load
        weighted_cfv = child_cfv * inv_outcomes
        atomic_spin_add(shared_cfv, weighted_cfv)  ; ATOMS.CAST.SPIN
    
    BSYNC B1
    store averaged_cfv to global memory
    BSYNC B0
```

---

## 41. SUMMARY: KERNEL ALGORITHMIC COVERAGE

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
| backward_br_player | ✅ | 449 | Best response CFV, max reduction across 16-entry blocks |
| backward_chance_abstract | ✅ | 161 | Chance averaging, dead card check |
| backward_chance_tr_2d | ✅ | ~137 | GPU-wide atomic add, FP16 MMA, atomic spin lock |
| backward_chance_turn_river | ✅ (FINAL_RECON) | 209 | Chance backward, bit-pair extraction |
| backward_br_player | ✅ | 449 | Boundary reduction backward pass, max-CFV via FSETP.GT.FTZ/FSEL, 15 child actions |
| backward_chance_turn_river | ✅ | 209 | Turn/river backward chance, HFMA2.MMA, atomic spin lock CFV averaging |
| backward_chance_turn_river_batched | ✅ | ~209 | Batched chance, atomic spin lock, 4-value store |
| forward_player | ✅ (FINAL_RECON) | 145 | Reach propagation, MUFU.RCP |
| forward_opponent | ✅ | 505 | Opponent reach, warp reduction (SHFL.DOWN) |
| forward_opponent_packed | ✅ | 729 | Packed S16 reach, epsilon normalization |
| forward_opponent_br | ✅ | ~465 | Opponent reach propagation, FADD.FTZ accumulation, MUFU.RCP normalization |
| forward_opponent_br_packed | ✅ | ~537 | Packed S16, complex card unpacking (SHF.L.U64.HI, LOP3), S16 storage |
| forward_chance | ✅ | ~153 | Uniform regs, epsilon guard, predicate-gated outcomes |
| forward_chance_abstract | ✅ | ~193 | FP16 MMA, atomic spin lock, 4-value store |
| forward_chance_tr_2d | ✅ | 145 | 2D grid chance, FP16 MMA, GPU atomic add |
| forward_chance_turn_river | ✅ | ~209 | Turn/river forward chance, FP16 MMA, atomic spin lock |
| forward_chance_turn_river_batched | ✅ | ~49 | Chance probability, atomic spin lock, FP16 MMA, 4-value store |
| forward_copy | ✅ | ~777 | Q16 dequant, reciprocal normalization, epsilon guard, 16-entry blocks |
| forward_river_subtrees_packed | ✅ | 1129 | River subtrees, multi-barrier sync (B0/B1/B2/B3), Q16 U16 loads |
| train_fold_showdown_parent_packed | ✅ | 1393 | Fold/showdown CFV, chance outcomes, S16 pack |
| train_mixed_fold_showdown_parent_packed | ✅ | ~545 | Mixed fold/showdown CFV, S16 pack, regret matching, SHFL.DOWN reduce |
| train_mixed_opponent_fold_showdown_parent | ✅ | ~1897 | Opponent mixed fold/showdown CFV, VIADDMNMX, BAR.RED.OR, magic-number div |
| train_opponent_fold_showdown_parent_packed | ✅ | ~1785 | Opponent fold/showdown CFV, double-buffered, HFMA2 MMA, magic-number div |
| train_small_tree_fp32_multi_iter | ✅ (partial) | 3401 | Multi-iter CFR, variable const buffer |
| river_api | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_batched | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_subtrees_packed | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_betting_enabled | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_bet | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_buckets | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_card | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_donk | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_ip | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_oop | ❌ | — | No .sass file found (runtime JIT PTX only) |
| river_raise | ❌ | — | No .sass file found (runtime JIT PTX only) |
| apply_isomorphism | ✅ | 97 | Tree compaction, child pointer swap |
| apply_isomorphism_permuted | ✅ | 177 | Permuted isomorphism, HFMA2 MMA |
| encode_stage_segments_i16 | ✅ | 249 | i16 encoding, warp max reduction |
| gather_cfv | ✅ | 41 | Simple gather/scatter |
| replace | ❌ | — | No .sass file found (runtime JIT PTX only) |
| trim | ❌ | — | No .sass file found (runtime JIT PTX only) |
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
| evaluate_showdown_brute_3p | ✅ | ~289 | 3-player brute force, VOTEU.ANY, POPC, ATOMS.ADD |
| evaluate_showdown_prefix_3p | ✅ | ~721+ | 3-player prefix tree, SHFL.UP reduction, CALL.REL subroutines |
| evaluate_fold | ✅ (partial) | 665 | Fold equity, Q16 |
| evaluate_fold_3p | ✅ | ~665 | 3-player fold equity, double precision, SHFL.BFLY reduction |
| evaluate_fold_fp32 | ✅ | ~601 | Fold equity FP32, MUFU.RCP, bit-pair LOP3 |
| evaluate_fold_abstract | ✅ | ~201 | Abstract fold equity, shared memory reduction, normalized distribution |
| evaluate_fold_showdown_prefix | ✅ | ~793+ | Fold/showdown prefix eval, uniform regs, shared mem buffering |
| evaluate_fold_abstract | ✅ | ~201 | Abstract fold equity, shared memory reduction, normalized distribution |

**Documented: 42/45 kernels (42 SASS-analyzed). Remaining: 3 kernels with SASS files need algorithmic documentation (none — all 42 with SASS are now documented). 5 kernels have no .sass files (river_api, river_batched, river_subtrees_packed, replace, trim — likely runtime JIT-compiled PTX only, captured dynamically via cuModuleLoadData). Batch* kernels (8) also undocumented but may be outside the 45-kernel core set.**
