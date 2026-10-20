# BINARY_ANALYSIS_RECON.md
# TexasSolverGPU v0.3.0 — Native Binary Analysis

## 1. Binary Inventory

| File | Size | Type | Role |
|------|------|------|------|
| `TexasSolverGpu.exe` | 309 KB | PE32+ GUI | Loader / launcher |
| `TexasSolverGpu_131.exe` | 18.9 MB | PE32+ GUI | Main solver + embedded Vue frontend |
| `TexasSolverGpu_legacy_126.exe` | 14.7 MB | PE32+ GUI | Legacy solver (CUDA 12.6) |
| `WebView2Loader.dll` | 166 KB | PE32+ DLL | WebView2 runtime loader |

## 2. PE Structure — TexasSolverGpu_131.exe

- **Entry point:** `0x1401505e8` → calls `0x140150fc0` → jmp `0x140150474`
- **Timestamp:** Sat Sep 26 13:53:56 2026
- **ImageBase:** `0x140000000`
- **Sections:** .text (1.6MB code), .rdata (16MB data/strings), .data, .pdata, _RDATA, .rsrc, .reloc
- **Resources:** IDI_APPICON icon, PNG image (no embedded PTX/cubin)
- **Subsystem:** Windows GUI, HIGH_ENTROPY_VA, DYNAMIC_BASE, NX_COMPAT

## 3. Imports

**Standard:** WebView2Loader, WS2_32, USER32, ole32, SHLWAPI, KERNEL32, SHELL32, COMDLG32, MSVCP140, VCRUNTIME140, VCRUNTIME140_1, api-ms-win-crt-*

**Not imported directly (loaded dynamically):** nvcuda.dll (via LoadLibraryW + cuGetProcAddress), nvml (NVML)

Key dynamic-loaded CUDA Driver API functions:
- `cuInit`, `cuDeviceGetCount`, `cuDeviceGet`, `cuDeviceGetName`, `cuDeviceGetAttribute`, `cuDeviceComputeCapability`, `cuDeviceTotalMem`
- `cuCtxCreate`, `cuCtxDestroy`, `cuCtxSetCurrent`, `cuCtxGetDevice`, `cuCtxSynchronize`
- `cuStreamCreate`, `cuStreamCreateWithPriority`, `cuEventCreate`, `cuEventRecord`
- `cuModuleLoadData`, `cuModuleLoadDataEx`, `cuModuleLoadFatBinary`
- `cuMemAlloc`, `cuMemAllocAsync`, `cuMemAllocHost`, `cuMemHostAlloc`, `cuMemFreeHost`
- `cuMemcpyHtoD`, `cuMemcpyHtoDAsync`, `cuMemcpyDtoH`, `cuMemcpyDtoHAsync`, `cuMemcpyDtoD`, `cuMemcpyDtoDAsync`
- `cuLaunchKernel`, `cuLaunchKernelEx`, `cuFuncGetAttribute`, `cuGetErrorString`
- `cuOccupancyAvailableDynamicSMemPerBlock`, `cuOccupancyMaxActiveBlocksPerMultiprocessor`, etc.
- `cuGraphLaunch`

NVML functions: `nvmlInit_v2`, `nvmlDeviceGetHandleByPciBusId_v2`, `nvmlDeviceGetMemoryInfo`, `nvmlShutdown`

Anti-debug: `IsDebuggerPresent` (KERNEL32)

## 4. CUDA Runtime Configuration

- **PTX target:** sm_120 (forward PTX JIT — no embedded cubin)
- **Compiled CUDA targets:** 75;80;86;89;90;120
- **Driver message:** "This GPU requires forward PTX JIT and a compatible NVIDIA driver; no native cubin is embedded."
- **CUDA versions supported:** 13.1 and legacy 12.6
- **VRAM warning:** "VRAM is below 8 GB; large trees may fail."
- **Block size:** 256 threads
- **Streams:** `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
- **Events:** `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`, `event_`

## 5. CUDA Kernels (symbol names in binary)

| Kernel | Purpose |
|--------|---------|
| `evaluate_showdown` | 2-player 7-card showdown equity |
| `evaluate_showdown_brute` | Brute-force villain-combo/runout enumeration |
| `evaluate_showdown_prefix` | Partial-board expected strength |
| `evaluate_fold` | Fold equity vs range (fp32 variant: `evaluate_fold_fp32`) |
| `evaluate_fold_showdown_prefix` | Combined fold + showdown with prefix board |
| `evaluate_fold_3p` | 3-way equity with SHFL/BSSY reduction |
| `evaluate_showdown_brute_3p` | 3-way brute enumeration |
| `evaluate_showdown_prefix_3p` | 3-way prefix evaluation |
| `evaluate_fold_abstract` | Fold equity over 169 hand-class buckets |
| `evaluate_showdown_abstract` | Showdown over hand-class buckets |

## 6. CFR Solver Internals (from string table)

### GPU buffers (device pointers)
- `d_cfv_` — counterfactual values buffer
- `d_abs_regrets_` — absolute regrets
- `d_abs_strategy_` — absolute strategy
- `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` — counterfactual reach
- `d_init_weights_` — initial weights
- `d_discount_params_` — discount parameters (3 floats: `iter_params`)
- `d_strategy_packed_` — packed strategy output
- `d_compact` — compact archive data
- `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_` — gather staging

### Host-pinned memory
- `cfv_stage_host_` (CU_MEMHOSTALLOC_PORTABLE \| CU_MEMHOSTALLOC_DEVICEMAP)
- `pinned_discount_params_`
- `cfv_stage_scales_`

### CFV staging
- `cfv_stage_free_`, `cfv_stage_ready_` events
- `cfv_stage_device_`, `cfv_stage_host_`, `cfv_stage_scales_`

### Training graph
- `training_graph_exec_` — CUDA Graph executable
- `cuGraphLaunch(training_graph_exec_, stream_)` — graph-based training loop

### CFR algorithms
- `backward_player_cfv`, `backward_player_cfv_packed` — backward CFR pass
- `persistent backward eligibility` — persistent node tracking
- `river forward a2` — river forward pass
- `compressed CFV finalize` — compressed result finalization
- Q16 fixed-point format: `compact_v2`, `compact_H9`, `compact regret descriptor`

### Timing log format
```
[gpu timing] submit=%.3f training_sync=%.3f pin_wait=%.3f exploit=%.3f finalize=%.3f total=%.3f ms
[gpu finalize] p0_gpu=%.3f p0_scatter=%.3f p1_gpu=%.3f p1_scatter=%.3f before_unpack=%.3f unpack=%.3f total=%.3f ms
[gpu] persistent backward eligibility: roots=%zu/%zu nodes=%zu
[gpu] river forward a2: player=%zu roots=%zu/%zu nodes=%zu/%zu opponent_nodes=%zu/%zu max_nodes=%zu
[gpu finalize] staged CFV enabled: max_plan=%llu total_p0=%llu total_p1=%llu chunks_p0=%u chunks_p1=%u device_capacity=%llu host_capacity=%llu direct_host=%d
```

### Compact archive errors (Avro-like serialization)
- "Compact archive has nonzero reach values with a zero scale"
- "Compact archive chance mapping is invalid"
- "Compact strategy archive contains invalid -32768 q16"

## 7. Solver Methods (native dispatch)

From the `dT()` dispatcher and string table — **methods not in FRONTEND_API_RECON.md are marked NEW**:

| Method | Endpoint | Status |
|--------|----------|--------|
| `solver.init` | POST /api/init | Documented |
| `solver.allocate` | POST /api/allocate | Documented |
| `solver.host.allocate` | — | NEW |
| `solver.layout.validate` | — | NEW |
| `solver.memoryUsage` | GET /api/memory-usage | Documented |
| `solver.environment.check` | GET /api/environment/check | Documented |
| `solver.solve.2p.construct` | — | Documented |
| `solver.solve.2p.run` | — | Documented |
| `solver.solve.3p.construct` | — | Documented |
| `solver.solve.3p.run` | — | Documented |
| `solver.solve.dispatch` | — | NEW |
| `solver.solve.preflight` | POST /api/solve/preflight | Documented |
| `solver.solve.start` | POST /api/gpu-solve | Documented |
| `solver.solve.status` | GET /api/solve/status | Documented |
| `solver.solve.stop` | POST /api/solve/stop | Documented |
| `solver.solve.cpu_locked` | — | Documented |
| `solver.solve.cpu_locked.finalize` | — | Documented |
| `solver.finalize.compute` | — | NEW |
| `solver.finalize.publish` | — | NEW |
| `solver.history.apply` | POST /api/apply-history | Documented |
| `solver.node.results` | GET /api/results | Documented |
| `solver.node.actionsAfter` | POST /api/actions-after | Documented |
| `solver.node.currentPlayer` | GET /api/current-player | Documented |
| `solver.node.currentBoard` | GET /api/current-board | Documented |
| `solver.node.numActions` | GET /api/num-actions | Documented |
| `solver.lock` | — | NEW |
| `solver.lock.current.set/get/clear` | POST/GET/POST /api/lock/current | Documented |
| `solver.lock.list` | GET /api/lock/list | Documented |
| `solver.lock.clearAll` | POST /api/lock/clear | Documented |
| `solver.cards.possible` | GET /api/possible-cards | Documented |
| `solver.cards.private` | GET /api/private-cards/<n> | Documented |
| `solver.export.currentStreet` | POST /api/export/current-street | Documented |
| `solver.export.fullFile` | — | NEW |
| `solver.export.fullFile.status` | — | NEW |

## 8. Native Dispatch Mechanism

Function `0x140014cf0` is the native method dispatcher:
- Input: output struct (rcx), input key-value pairs (rdx), flags (r8b, r9b)
- Iterates through entries looking for type `0x2` (key) and type `0x3` (string value)
- Calls `0x140043b60` on the key string (hash/lookup to map method names to handler IDs)
- Calls `0x140016210` to assemble the native request
- Wrapper `0x140005c30` initializes output, appends method name, allocates 0x20-byte buffer, looks up handler

## 9. Source Code Paths (from C++ type info / exception strings)

```
C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h
C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h
C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp
C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp
C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp
```

## 10. Author & Project Links

- **Author:** bupticybee
- **Discord:** https://discord.gg/RtyD4vRy2e
- **GitHub:** https://github.com/bupticybee
- **Page:** https://bupticybee.github.io/texassolver_gpu_page/

## 11. Vite Chunks (embedded frontend)

- `index-DQB6irIW.js` — core runtime
- `PlayPanel-BLI34wKZ.js` — play panel
- `StudyPanel-D7tMTKne.js` — study panel
- `PlayRangeStrategy-B24TSCYs.js` — range strategy component

## 12. Default Solve Config

```json
{
  "maxIterations": 500,
  "target": 0.5,
  "maxRaises": 2,
  "bet": "50",
  "raise": "100",
  "rakePercent": 0,
  "rakeCapBB": 0,
  "advanced": false,
  "oopFlopBet": "50", "oopFlopRaise": "100",
  "ipFlopBet": "50", "ipFlopRaise": "100",
  "oopTurnBet": "50", "oopTurnRaise": "100",
  "ipTurnBet": "50", "ipTurnRaise": "100",
  "oopRiverBet": "50", "oopRiverRaise": "100",
  "ipRiverBet": "50", "ipRiverRaise": "100",
  "oopTurnDonk": "50", "oopRiverDonk": "50",
  "addAllinFlopIp": true, "addAllinTurnIp": true, "addAllinRiverIp": true,
  "addAllinFlopOop": true, "addAllinTurnOop": true, "addAllinRiverOop": true,
  "addAllinThreshold": 100
}
```

Validation: maxIterations 1–100000, target 0–100%, maxRaises 1–3, bet/raise 0–1000%

## 13. Loader (TexasSolverGpu.exe)

Entry point `0x14000a6c4`:
- Calls `0x14000aae4` (init)
- Jumps to `0x14000a550`
- Uses atomic cmpxchg at `0x140012710` (singleton mutex)
- Calls `0x14000b178` to check launch conditions
- No WebView2Loader or nvcuda.dll imports — pure launcher

## 14. Anti-Tamper / Protections

- `IsDebuggerPresent` imported from KERNEL32.dll (basic anti-debug)
- No obvious license/activation mechanism found in strings
- Stripped binary (no RTTI/debug symbols)
- PTX JIT-compiled at runtime (no embedded cubin to extract)

## 15. 6-Max Support

- Seats: UTG, MP, CO, BTN, SB, BB
- Postflop order: [4,5,0,1,2,3]
- Policy format version 1 or 2
- Preflop JSON tree validated against `classes` array
- Quick start scans `6max_range/` directory for scenarios

## 16. PioSolver Config Import

Parses `.txt`/`.cfg`/`.conf` files with fields:
- `Range0`, `Range1`, `Pot`, `EffectiveStacks`, `Rake.Fraction`, `Rake.Cap`
- `AddAllinOnlyIfLessThanThisTimesThePot`
- `FlopConfig.BetSize/RaiseSize`, `TurnConfig.*`, `RiverConfig.*`
- `TurnConfig.AddAllin`, `RiverConfig.AddAllin` (IP variants too)
- Chip scaling: scales pot/stack/rake by common factor