# TexasSolverGPU v0.3.0 — POKER_GPU_ Environment Variables & Runtime Config Recon

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Source:** Static string extraction from `.rdata` section of `TexasSolverGpu_131.exe`
**Status:** NEW — not present in any prior recon document

---

## 1. POKER_GPU_* Environment Variables (47 total)

All read via `GetEnvironmentVariableW`. These control runtime behavior, diagnostics, performance tuning, and file-backed operation.

| # | Variable | Purpose / Behavior |
|---|----------|-------------------|
| 1 | `POKER_GPU_FILE_BACKED_DIR` | Authoritative directory for file-backed operation. Error: "POKER_GPU_FILE_BACKED_DIR is too long", "POKER_GPU_FILE_BACKED_DIR must be an absolute path", "Set POKER_GPU_FILE_BACKED_DIR to an absolute..." |
| 2 | `POKER_GPU_FILE_BACKED_RESULTS` | File-backed results output |
| 3 | `POKER_GPU_FILE_BACKED_THRESHOLD_MB` | Threshold (MB) for file-backed operation |
| 4 | `POKER_GPU_FILE_COALESCE_GAP` | Coalescing gap for file-backed writes |
| 5 | `POKER_GPU_FILE_QUEUE_BUDGET` | Budget for file-backed I/O queue |
| 6 | `POKER_GPU_CFV_STAGING` | CFV (counterfactual value) staging control |
| 7 | `POKER_GPU_COMPACT_BACKWARD_CACHE` | Compact backward pass cache |
| 8 | `POKER_GPU_COMPACT_INVALID_REGRETS` | Handle invalid regrets in compact mode |
| 9 | `POKER_GPU_COMPACT_REGRET_ALIGNMENT` | Regret alignment for compact archives |
| 10 | `POKER_GPU_CUDA_GRAPH` | CUDA graph usage control (`training_graph_exec_`) |
| 11 | `POKER_GPU_DIAG_FILE_BACKING` | Diagnostic: file backing |
| 12 | `POKER_GPU_DIAG_FINALIZE` | Diagnostic: finalize phase |
| 13 | `POKER_GPU_DIAG_GRAPH` | Diagnostic: CUDA graph |
| 14 | `POKER_GPU_DIAG_PERSISTENT_BACKWARD` | Diagnostic: persistent backward |
| 15 | `POKER_GPU_DIAG_RIVER_FORWARD` | Diagnostic: river forward |
| 16 | `POKER_GPU_DIAG_TIMING` | Diagnostic: timing output (`[gpu timing] ...`) |
| 17 | `POKER_GPU_DUAL_RIVER_PIPELINE` | Dual river pipeline mode |
| 18 | `POKER_GPU_EXACT_FOLD_REDUCTION` | Exact fold reduction optimization |
| 19 | `POKER_GPU_FINALIZE_GPU_ENCODE` | GPU-encoded finalize |
| 20 | `POKER_GPU_FOLD_FP32` | FP32 fold evaluation |
| 21 | `POKER_GPU_FOLD_SHOWDOWN_BLOCK` | Fold/showdown block size |
| 22 | `POKER_GPU_FORCE_OFF64` | Force 64-bit offset mode |
| 23 | `POKER_GPU_FORWARD_BLOCK` | Forward pass block size |
| 24 | `POKER_GPU_FUSE_FOLD_SHOW` | Fuse fold+showdown kernels |
| 25 | `POKER_GPU_FUSE_MIXED_FOLD_SHOW` | Fuse mixed fold+showdown |
| 26 | `POKER_GPU_FUSE_OPPONENT_TERMINAL_PARENT` | Fuse opponent terminal parent |
| 27 | `POKER_GPU_FUSE_SHOW_CONTINUATION` | Fuse showdown continuation |
| 28 | `POKER_GPU_FUSE_TERMINAL_PARENT` | Fuse terminal parent |
| 29 | `POKER_GPU_HOST_WORKSPACE` | Host workspace directory |
| 30 | `POKER_GPU_MAPPED_FILE_WRITES` | Memory-mapped file writes |
| 31 | `POKER_GPU_MAPPED_TRIM_BYTES` | Trim bytes for mapped files |
| 32 | `POKER_GPU_MEMORY_BREAKDOWN` | Memory breakdown reporting |
| 33 | `POKER_GPU_MIXED_GENERIC_UPDATE` | Mixed generic CFR update |
| 34 | `POKER_GPU_OPP_BACKWARD_BLOCK` | Opponent backward block |
| 35 | `POKER_GPU_OPP_CARD_PREFIX` | Opponent card prefix |
| 36 | `POKER_GPU_OWN_PARENT_BLOCK` | Own parent block |
| 37 | `POKER_GPU_OWN_PARENT_REGCACHE` | Own parent register cache |
| 38 | `POKER_GPU_PACK_CARD_METADATA` | Pack card metadata |
| 39 | `POKER_GPU_PACKED_STATE` | Packed state mode |
| 40 | `POKER_GPU_PACKED_STATE_MIN_SAVINGS_MB` | Minimum savings (MB) for packed state |
| 41 | `POKER_GPU_PERSISTENT_RIVER_FORWARD` | Persistent river forward |
| 42 | `POKER_GPU_RIVER_BATCH` | River batch mode |
| 43 | `POKER_GPU_RIVER_BATCH_NODES` | River batch node count |
| 44 | `POKER_GPU_SHOWDOWN_64_MAX_OPP` | Max opponents for 64-bit showdown |
| 45 | `POKER_GPU_SHOWDOWN_BLOCK` | Showdown block size |
| 46 | `POKER_GPU_SMALL_TREE_FUSED` | Small tree fused kernel |
| 47 | `POKER_GPU_BALANCE_RIVER_PAIRS` | Balance river pairs |

---

## 2. Source Code File Paths (from embedded strings)

| Path | Role |
|------|------|
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp` | Main GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp` | 3-player GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp` | CUDA driver API wrapper |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h` | CUDA buffer header |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h` | CUDA host memory header |

**Author:** bupticybee
**GitHub:** https://github.com/bupticybee
**Page:** https://bupticybee.github.io/texassolver_gpu_page/
**Discord:** https://discord.gg/RtyD4vRy2e

---

## 3. API Client Details (from embedded Vue 3 frontend)

### POST /api/allocate
Body: `{ "require_exact_2p_gpu_layout": true }`

### POST /api/gpu-solve
Body: `{ "max_iterations": int, "target_exploitability": float, "compute_initial_exploitability": bool, "verbose": bool }`

### POST /api/solve/preflight
Body: `{}` — returns severity ("critical"|"warning"|null) and VRAM info

### GET /api/solve/status — Response Schema (enriched from JS code)
```json
{
  "running": bool,
  "iteration": int,
  "max_iterations": int,
  "exploitability": float | null,
  "exploitability_iteration": int,
  "exploitability_elapsed_ms": int,
  "player0_exploitability": float | null,
  "player1_exploitability": float | null,
  "elapsed_ms": int,
  "stop_requested": bool,
  "phase": "preparing|allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed|idle",
  "last_error": string | null
}
```

### POST /api/export/current-street
Body: `{ history: [], max_nodes: int }` → `{ payload: snapshot }`

### POST /api/export/current-street-file
Body: JSON — exports current street to file

### POST /api/export/full-file
Body: JSON — exports full strategy archive (Avro format)

### GET /api/export/full-file/progress
Returns export progress

### POST /api/import/full-file
Body: `{}` — imports full strategy archive

### POST /api/import/node
Body: JSON — imports a single node

### POST /api/import/close
Body: `{ "session_id": string }` — closes import session

### GET /api/environment/check
Returns GPU/driver/package status with severity levels

---

## 4. Timing / Debug Strings (from binary)

| String | Context |
|--------|---------|
| `GPU iteration: 0 / %u (exploitability = %.4e)` | Initial iteration log |
| `GPU iteration: %u / %u (exploitability = %.4e)` | Per-iteration progress |
| `[gpu timing] submit=%.3f training_sync=%.3f pin_wait=%.3f exploit=%.3f finalize=%.3f total=%.3f ms` | Per-iteration timing breakdown |
| `[abs_finalize] %s gpu_cfv=%.1fms expand=%.1fms total=%.1fms` | Absolute finalize timing |
| `[gpu finalize] p0_gpu=%.3f p0_scatter=%.3f p1_gpu=%.3f p1_scatter=%.3f before_unpack=%.3f unpack=%.3f total=%.3f ms` | GPU finalize phase timing |
| `[gpu finalize] staged CFV enabled: max_plan=%llu total_p0=%llu total_p1=%llu chunks_p0=%u chunks_p1=%u device_capacity=%llu host_capacity=%llu direct_host=%d` | Staged CFV configuration |
| `gpu_solver_buffer_usage` | Buffer usage metric |
| `gpu_solver_error.log` | Error log file |

---

## 5. Format Options (from embedded frontend)

| Format | Description |
|--------|-------------|
| `six100` | Cash 6max 100bb |
| `six50` | Cash 6max 50bb |
| `sixAnte` | Cash 6max Ante 50bb (ante 0.16bb) |
| `spin2` | Spin & Go HU (heads-up) |
| `spin3` | Spin & Go 3-max |
| `hu` | HU Cash |

---

## 6. Additional Native Method Strings (not in if-else chain, referenced in .rdata)

| Method | Context |
|--------|---------|
| `solver.finalize.compute` | CFV finalize computation |
| `solver.finalize.publish` | Publish finalize results |
| `solver.host.allocate` | Host memory allocation |
| `solver.solve.preflight` | Preflight check (VRAM, GPU) |
| `solver.solve.dispatch` | Solve dispatch |
| `solver.solve.cpu_locked` | CPU-locked solve |
| `solver.solve.cpu_locked.finalize` | CPU-locked finalize |
| `solver.solve.2p.construct` | 2-player tree construction |
| `solver.solve.2p.run` | 2-player solve run |
| `solver.solve.3p.construct` | 3-player tree construction |
| `solver.solve.3p.run` | 3-player solve run |
| `solver.layout.validate` | Layout validation |
| `solver.strategy.archive.open` | Archive open |
| `solver.strategy.archive.node` | Archive node access |
| `solver.strategy.archive.close` | Archive close |
| `solver.export.fullFile` | Full file export |
| `solver.export.fullFile.status` | Full file export status |
| `solver.export.currentStreet` | Current street export |
| `solver.memoryUsage` | Memory usage query |
| `solver.host.allocate` | Host allocation |

---

## 7. Batch Mode (from embedded frontend)

Batch solver supports multiple boards with:
- `boards_input`: text input of board cards (e.g. `Qs Jh 2h`)
- `randomBoardCount`: 1-25 random boards
- `max_25_error`: max 25 boards
- `duplicate_card_error`: duplicate card validation
- `invalid_board_len`: board must be 3-5 cards
- `batch.persistence_warning`, `batch.persistence_limit_warning`, `batch.persistence_space_warning`
- `snapshotBytes`, `resultKey` per job
- `exportMaxNodes`: 100-100000 nodes per export

---

## 7. Quick Start Formats

| Key | Name | Stack | Ante |
|-----|------|-------|------|
| `six` | Cash 6max 100bb | 10000 | 0 |
| `six50` | Cash 6max 50bb | 5000 | 0 |
| `sixAnte` | Cash 6max Ante 50bb | 10000 | 16 (0.16bb) |
| `spin2` | Spin & Go HU | variable | 0 |
| `spin3` | Spin & Go 3-max | variable | 0 |
| `hu` | HU Cash | 100bb | 0 |

---

**Bottom line:** 47 POKER_GPU_* environment variables, 5 source code paths, enriched API schemas, timing strings, batch mode, and 6 quick-start formats discovered — none documented in prior recon. All remaining unknowns (method→handler mapping, PTX, kernel launch configs, iter_params, compact archive bytes) still require Windows + NVIDIA GPU dynamic execution.