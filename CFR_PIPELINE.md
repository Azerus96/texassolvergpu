# TexasSolverGPU v0.3.0 — CFR Pipeline & Buffer Lifetimes

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Status:** STATIC — reconstructed from binary analysis, SASS disassembly, and frontend JS
**Blocked:** Runtime buffer values, kernel launch configs, iter_params require dynamic capture

---

## 1. End-to-End Solve Flow

```
Frontend (Vue 3 + WebView2)
    │
    │ POST /api/init  (solver.init)
    ▼
Native: solver.init handler (0x14006a680)
    │  Initialize solver session, parse init template
    │  Validate ranges (1326 combos each), board, stack, antes
    │
    │ POST /api/allocate  (solver.allocate)
    ▼
Native: solver.allocate handler
    │  Allocate GPU device buffers via CUDA Driver API
    │  Create CUDA context, streams, events
    │  Upload initial weights, discount params
    │
    │ POST /api/gpu-solve  (solver.solve.start)
    ▼
Native: solver.solve.start handler (0x1400157a0 + 0x14010b9a0)
    │  Launch CUDA training graph (cuGraphLaunch)
    │  Run CFR iterations: backward + forward passes
    │
    │ GET /api/solve/status  (poll every 250ms)
    ◀──────────────────────────────────────────────┐
    │  running, iteration, exploitability, phase   │
    └──────────────────────────────────────────────┘
    │  (repeat until running == false)
    ▼
POST /api/solve/stop  (solver.solve.stop)
    │
    ▼
GET /api/results  (solver.node.results)
    │  Returns per-seat strategy, ranges, EVs
    ▼
POST /api/export/current-street  (solver.export.currentStreet)
    │  Returns NodeSnapshot with probabilities and EVs
```

### Per-Board Batch Loop

```
for each board:
    1. POST /api/init      → init template (ranges, board, pot, stack, bet sizes)
    2. POST /api/allocate  → allocate GPU memory
    3. POST /api/gpu-solve → start CFR training
    4. GET  /api/solve/status  (poll every 250ms until running==false)
    5. POST /api/solve/stop  → stop solver
    6. GET  /api/results     → node strategy + EVs (seats 0 and 1)
    7. POST /api/export/current-street  → snapshot JSON
    8. Persist snapshot to disk
```

---

## 2. CUDA Kernel Execution Order (Inferred)

CFR training iteration (per `train_small_tree_fp32_multi_iter` SASS):

```
Phase 1: Backward Pass (CFV computation)
├── backward_player / backward_player_cfv_packed    → compute regrets
├── backward_opponent / backward_opponent_boundary_iso → opponent regrets
├── backward_chance_abstract / backward_chance_turn_river → chance node regrets
└── backward_br_player / backward_chance_tr_2d / backward_chance_turn_river_batched

Phase 2: Regret Matching (strategy update)
├── backward_player kernels apply FMNMX (clamp regrets to >=0)
├── MUFU.RCP (reciprocal) for normalization
├── FMUL.FTZ for strategy = posRegret / sumPos

Phase 3: Forward Pass (reach accumulation)
├── forward_player / forward_opponent / forward_opponent_packed
├── forward_chance / forward_chance_abstract / forward_chance_tr_2d
├── forward_chance_turn_river / forward_chance_turn_river_batched
├── forward_copy / forward_river_subtrees_packed

Phase 4: Terminal Evaluation (showdown/fold)
├── evaluate_showdown / evaluate_showdown_brute / evaluate_showdown_prefix
├── evaluate_showdown_abstract / evaluate_showdown_brute_3p / evaluate_showdown_prefix_3p
├── evaluate_fold / evaluate_fold_fp32 / evaluate_fold_3p
├── evaluate_fold_showdown_prefix / evaluate_fold_abstract

Phase 5: Tree Compaction
├── apply_isomorphism / apply_isomorphism_permuted
├── trim / replace / gather_cfv / encode_stage_segments_i16

Phase 6: River Subtrees
├── river_api / river_batched / river_subtrees_packed
├── river_bet / river_raise / river_call / river_check / river_donk
└── river_ip / river_oop / river_buckets / river_card / river_betting_enabled
```

**CUDA Graph:** `training_graph_exec_` launched via `cuGraphLaunch(training_graph_exec_, stream_)`

---

## 3. Device Buffer Inventory & Lifetimes

| Buffer | Purpose | CUDA API | Lifetime |
|--------|---------|----------|----------|
| `d_cfv_` | Counterfactual values | `cuMemAlloc` / `cuMemcpyDtoH` | Allocated at allocate(), freed at solve stop |
| `d_abs_regrets_` | Absolute regrets | `cuMemAlloc` | Allocated at allocate(), freed at solve stop |
| `d_abs_strategy_` | Absolute strategy | `cuMemAlloc` | Allocated at allocate(), freed at solve stop |
| `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` | Counterfactual reach (per player) | `cuMemAllocAsync` / `cuMemcpyDtoDAsync` | Allocated at allocate(), reused across iterations |
| `d_init_weights_` | Initial combo weights | `cuMemcpyHtoD` | Uploaded once at allocate() |
| `d_discount_params_` | Discount parameters (3 floats: iter_params) | `cuMemcpyHtoDAsync` at VA `0x24bd10` | Uploaded each iteration |
| `d_strategy_packed_` | Packed strategy output | `cuMemcpyDtoHAsync` at VA `0x24b870` | Downloaded after solve |
| `d_compact` | Compact archive data | `cuMemAlloc` | Allocated for export |
| `d_gather_staging_` / `d_gather_scales_` / `d_gather_i16_` | Gather staging for compaction | `cuMemAlloc` | Allocated at allocate() |

### Host-Pinned Buffers

| Buffer | Purpose | CUDA API |
|--------|---------|----------|
| `cfv_stage_host_` | Host-pinned CFV staging | `cuMemAllocHost` |
| `pinned_discount_params_` | Host-pinned discount params | `cuMemAllocHost` |
| `cfv_stage_scales_` | Host-pinned CFV scales | `cuMemAllocHost` |
| `pinned_gather_staging_` | Host-pinned gather staging | `cuMemAllocHost` |

### Buffer Lifetime Timeline

```
allocate()
├── cuCtxCreate → CUDA context
├── cuStreamCreate ×4 (stream_, stream2_, stream_dl_, stream_file_)
├── cuEventCreate ×4 (event_cfv_stage_free_, event_cfv_stage_ready_, event_fwd_done_, event_show_done_)
├── cuMemAlloc ×N (device buffers)
├── cuMemAllocHost ×N (pinned host buffers)
├── cuMemcpyHtoD (init_weights, discount_params)
└── cuModuleLoadData ×6 (embedded ELF objects, one per arch)

gpu-solve() → CUDA Graph
├── cuGraphLaunch(training_graph_exec_, stream_)  [per iteration]
│   ├── backward_* kernels (CFV + regrets)
│   ├── forward_* kernels (reach accumulation)
│   ├── evaluate_* kernels (showdown/fold EVs)
│   ├── train_* kernels (multi-iteration CFR)
│   ├── river_* kernels (river betting)
│   ├── apply_isomorphism / trim / gather_cfv (compaction)
│   └── cuMemcpyHtoDAsync(d_discount_params_, iter_params, ...)  [per iteration]

solve/stop()
├── cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_, ...)  [VA 0x24b1b0]
├── cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_, ...)  [VA 0x24b870]
├── cuStreamSynchronize / cuCtxSynchronize
├── cuMemFree / cuMemFreeHost ×N
└── cuCtxDestroy
```

---

## 4. CUDA Streams & Events

| Stream | Purpose |
|--------|---------|
| `stream_` | Main training stream (CUDA graph execution) |
| `stream2_` | Secondary stream (overlap compute/copy) |
| `stream_dl_` | Download stream (DTOH copies) |
| `stream_file_` | File I/O stream (archive writes) |

| Event | Purpose |
|-------|---------|
| `event_cfv_stage_free_` | CFV stage buffer free |
| `event_cfv_stage_ready_` | CFV stage buffer ready |
| `event_fwd_done_` | Forward pass complete |
| `event_show_done_` | Showdown evaluation complete |

---

## 5. CUDA Driver API Call Sites (Static)

| Code VA | Call | Context |
|---------|------|---------|
| `0x24aeb0` | `cuGraphLaunch(training_graph_exec_, stream_)` | CUDA graph execution |
| `0x24bd10` | `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` | Discount params upload |
| `0x24ade0` | `cuMemcpyDtoDAsync(d_cfreach_.dev_ptr(), ...)` | CFV reach copy |
| `0x24b1b0` | `cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), ...)` | CFV stage download |
| `0x24b870` | `cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_.dev_ptr(), ...)` | Strategy packed download |

### Dynamically Loaded CUDA Driver API Functions

`cuInit`, `cuDeviceGetCount`, `cuDeviceGet`, `cuDeviceGetName`, `cuDeviceGetAttribute`, `cuDeviceComputeCapability`, `cuDeviceTotalMem`, `cuCtxCreate`, `cuCtxDestroy`, `cuCtxSetCurrent`, `cuCtxGetDevice`, `cuCtxSynchronize`, `cuStreamCreate`, `cuStreamCreateWithPriority`, `cuEventCreate`, `cuEventRecord`, `cuModuleLoadData`, `cuModuleLoadDataEx`, `cuModuleLoadFatBinary`, `cuMemAlloc`, `cuMemAllocAsync`, `cuMemAllocHost`, `cuMemHostAlloc`, `cuMemFreeHost`, `cuMemcpyHtoD`, `cuMemcpyHtoDAsync`, `cuMemcpyDtoH`, `cuMemcpyDtoHAsync`, `cuMemcpyDtoD`, `cuMemcpyDtoDAsync`, `cuLaunchKernel`, `cuLaunchKernelEx`, `cuFuncGetAttribute`, `cuGetErrorString`, `cuOccupancyAvailableDynamicSMemPerBlock`, `cuOccupancyMaxActiveBlocksPerMultiprocessor`, `cuGraphLaunch`

### NVML Functions

`nvmlInit_v2`, `nvmlDeviceGetHandleByPciBusId_v2`, `nvmlDeviceGetMemoryInfo`, `nvmlShutdown` (VRAM gate: ≥8 GB required)

---

## 6. CUDA Runtime Configuration

| Setting | Value |
|---------|-------|
| PTX target | sm_120 (forward PTX JIT) |
| Compiled targets | sm_75, sm_80, sm_86, sm_89, sm_90, sm_120 |
| CUDA version | 13.1 (ptxas V13.1.115) + legacy 12.6 |
| Block size | 256 threads |
| Grid size | ceil(num_combos / 256) = ceil(1326/256) = 6 |
| Embedded ELF objects | 24 (4 per arch × 6 archs) |
| Kernels per object | 45 (sm_75–90), 47 (sm_120 PTX-only) |
| Total kernels | 45 unique names |

---

## 7. Q16 Fixed-Point Encoding

| Field | Encoding | Dequantization |
|-------|----------|----------------|
| `reach_probs_q16` | int16 LE | `value = raw / 65536.0` |
| `strategy_probs_q16` | int16 LE | `value = raw / 65536.0` |
| `action_evs_q16` | int16 LE | `value = raw / 65536.0` |
| Invalid marker | `-32768` (0x8000) | Uninitialized/invalid |

---

## 8. Card Encoding & Combo Index

```
Card c in [0,51]: rank = c/4 (0=2, ..., 12=A), suit = c%4 (0=c, 1=d, 2=h, 3=s)
Two cards packed: c | (p << 8)

Combo index: en(e,t) = e*(103-e)/2 + t - e - 1  for e < t, range [0,1325]
Total combos: C(52,2) = 1326
```

---

## 9. 6-Max Seat Layout

| Index | Seat | Postflop Order |
|-------|------|----------------|
| 0 | UTG | 4 |
| 1 | MP | 5 |
| 2 | CO | 0 |
| 3 | BTN | 1 |
| 4 | SB | 2 |
| 5 | BB | 3 |

Postflop order: [SB, BB, UTG, MP, CO, BTN] → indices [4, 5, 0, 1, 2, 3]

---

## 10. Blocked Items (Require Dynamic Capture)

| Item | Why Blocked | Priority |
|------|-------------|----------|
| Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream, args) | P1 — High |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync` at VA `0x24bd10` | P1 — High |
| PTX device code | JIT-compiled by CUDA driver via `cuModuleLoadData`; no embedded cubin | P0 — Critical |
| Compact archive bytes | Written at runtime by `archive.open/node/close` | P2 — Medium |
| Full solve cycle buffer values | Requires live solver execution on Windows + GPU | P2 — Medium |
| Dynamic anti-tamper validation | Requires Windows/x64 + NVIDIA GPU + instrumentation | P3 — Low |

---

## 11. Tools

| Tool | Location | Purpose |
|------|----------|---------|
| CFR simulator (CPU reference) | `re_lab/texassolvergpu/cfr_simulator.py` | eval5/eval7, Q16, CFR regret matching, forward reach |
| Compact archive decoder | `re_lab/texassolvergpu/compact_archive_decoder.py` | Decodes Avro + DEFLATE + Q16 archives |
| Proxy nvcuda.dll | `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp` | Logs CUDA driver API calls (for Windows+GPU) |
| WebView2 stub | `/tmp/opencode/webview2_stub/webview2_stub.c` | Enables WebView2 init (partial, for testing) |
