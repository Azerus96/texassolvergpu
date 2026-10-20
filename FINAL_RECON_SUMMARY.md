# TexasSolverGPU v0.3.0 — Final Reverse Engineering Summary

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Analysis host:** Linux (objdump/readelf/strings/python only — no Ghidra/radare2/x64dbg)
**Status:** Static analysis **COMPLETE** (Phases 1–4). Phase 5 (dynamic) **BLOCKED** — no Windows/x64 + NVIDIA GPU available.

---

## 1. What Is Complete (Static)

### 1.1 Binary inventory
| File | Size | Role |
|------|------|------|
| `TexasSolverGpu.exe` | 309 KB | Loader / launcher (entry `0x14000a6c4`, singleton mutex `0x140012710`) |
| `TexasSolverGpu_131.exe` | 18.9 MB | Main solver + embedded Vue 3 frontend (WebView2) |
| `TexasSolverGpu_legacy_126.exe` | 14.7 MB | Legacy solver (CUDA 12.6) |
| `WebView2Loader.dll` | 166 KB | WebView2 runtime loader |

### 1.2 PE structure (131.exe)
- Entry: `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid` — env/feature detection, possible anti-VM)
- ImageBase: `0x140000000`
- Sections: `.text` (1.6 MB), `.rdata` (16 MB data/strings), `.data`, `.pdata`, `_RDATA`, `.rsrc`, `.reloc`
- Stripped: no RTTI/debug symbols
- Imports: WebView2Loader, WS2_32, USER32, KERNEL32, MSVCP140, VCRUNTIME140, etc.
- Dynamically loaded: `nvcuda.dll` (via `LoadLibraryW` + `GetProcAddress`), NVML
- Anti-debug: `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`
- VRAM gate: "VRAM is below 8 GB; large trees may fail."
- No license/activation mechanism found.
### 1.3 Native method dispatcher (fully disassembled, confirmed)

**Dispatcher:** `0x140014cf0` — signature `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`

**Handler vtable (NEW — statically recovered):** `0x14016ec10` — 16 entries, 15 code pointers + 1 data pointer. All 6 handler creator call sites (`0x14000634e`, `0x14000649e`, `0x140014933`, `0x1400151f3`, `0x1400328d3`, `0x140033a9a`) use this vtable. Handler function addresses: `0x14001b660`, `0x14001d3f0`, `0x14001e280`, `0x140038110`, `0x140039980`, `0x140045a60`, `0x140045690`, `0x140046480`, `0x1400465e0`, `0x140043200`, `0x140043480`, `0x140043a80`, `0x1400450a0`, `0x14002d700`, plus entry 15 = data ptr `0x140195f88`.
- Walks 0x18-byte key/value entries (`rbx` from `rdi` to `r15`)
- For each entry: `mov rsi,[rbx+0x10]`; `cmovne rax,rsi`; `cmp BYTE PTR [rax],0x2` (key type tag). Skip if not 0x2.
- Calls variant accessor `0x140043b60` on key; requires result == 0x2.
- `mov rax,[r14+0x8]`; `mov rcx,[rax]`; `cmp BYTE PTR [rcx],0x3` (value type tag). Skip if not 0x3.
- `add rbx,0x18` — advance to next entry.
- When method entry found: calls `0x140016210` (variant builder), stores result at `[r12+0x8]`.
- Calls `0x1400099e0` (linked-list handler lookup) with `rcx = [r12+0x8]`, `rdx = method name buffer`.
- `0x1400099e0` returns output struct: `[+0x0]`=handler object ptr, `[+0x8]`=found flag, `[+0x10]`=current list node.
- Reads `[rax+0x10]` = current list node, `cmp BYTE PTR [r8+0x19],0x0` (handler validity flag).
- Handler pointer stored at `[r12+0x8]`; invoker is the CALLER of the dispatcher.

**Helper functions:**
- `0x140043b60` — variant accessor (jump table `0x140043ba4`; tag 0→0, tag 1→count/16, tag 2→data ptr, tags 3–9/default→1)
- `0x140016210` — request/variant builder (jump table `0x140016370`)
- `0x1400099e0` — handler linked-list lookup (compare via `0x140151250`)
- `0x1400071b0` — variant string compare (SSO vs heap)
- `0x140001840` / `0x1400017d0` — variant string copy (allocator `0x140151238`)
- `0x14001ef90` — string length helper
- `0x140005c30` — wrapper: initializes output, appends method name, allocates 0x20-byte buffer, looks up handler

**Handler creator:** `0x140036d70` — called from 6 sites (`0x14000634e`, `0x14000649e`, `0x140014933`, `0x1401511f3`, `0x1400328d3`, `0x140033a9a`). Allocates handler via `0x14014fb2f`, calls IAT `0x14016da48`/`0x14016d3d8`/`0x14016d398`. Handler object layout: `[+0x0]`=vtable, `[+0x18]`/`[+0x20]`=IAT `0x14016da48` result, `[+0x38]`/`[+0x40]`=IAT `0x14016d3d8` result, `[+0x50]`/`[+0x58]`=IAT `0x14016d398` result, `[+0x68]`=IAT `0x14016d398` check result, `[+0x74]`=global `0x1411c6698`, `[+0x7c]`=valid flag, `[+0x80]`=allocated memory.

**Partial static recovery:** Handler vtable at `0x14016ec10` with 15 handler function addresses recovered statically. All 6 handler creator call sites use this vtable. No static {method_name_string, handler_fn_ptr} table exists — method names stored in heap memory at `[handler_obj+0x80]`. Handler addresses resolved at runtime via linked list + IAT. See `HANDLER_VTABLE_RECON.md` for full vtable details.

**Handler registration:** `0x140013910` — list insertion function with vtable call `[rax+0x8]`, global list head at `0x1411c6690`.

### 1.4 Complete native method inventory (~60 methods)
**dT table (38 entries)** — extracted from embedded JS `function dT(e,t)`:

| Method | Path | Timeout |
|--------|------|---------|
| solver.init | POST /api/init | 120000 ms |
| solver.allocate | POST /api/allocate | 120000 ms |
| solver.memoryUsage | GET /api/memory-usage[?compress=*] | 30000 ms |
| solver.environment.check | GET /api/environment/check | 5000 ms |
| solver.solve.start | POST /api/gpu-solve | 30000 ms |
| solver.solve.preflight | POST /api/solve/preflight | 30000 ms |
| solver.export.currentStreet | POST /api/export/current-street | 120000 ms |
| desktop.strategy.exportCurrentStreetFile | POST /api/export/current-street-file | null |
| desktop.strategy.exportFullFile | POST /api/export/full-file | null |
| desktop.strategy.exportFullFileProgress | GET /api/export/full-file/progress | null |
| desktop.strategy.importFullFile | POST /api/import/full-file | null |
| desktop.strategy.importNode | POST /api/import/node | 30000 ms |
| desktop.strategy.closeImport | POST /api/import/close | null |
| solver.solve.status | GET /api/solve/status | 5000 ms |
| solver.solve.stop | POST /api/solve/stop | 5000 ms |
| solver.history.apply | POST /api/apply-history | 5000 ms |
| solver.node.results | GET /api/results | 5000 ms |
| solver.node.actionsAfter | POST /api/actions-after | 5000 ms |
| solver.node.currentPlayer | GET /api/current-player | 5000 ms |
| solver.node.currentBoard | GET /api/current-board | 5000 ms |
| solver.lock.current.set | POST /api/lock/current | 5000 ms |
| solver.lock.current.get | GET /api/lock/current | 5000 ms |
| solver.lock.current.clear | POST /api/lock/unlock | 5000 ms |
| solver.lock.list | GET /api/lock/list | 5000 ms |
| solver.lock.clearAll | POST /api/lock/clear | 5000 ms |
| solver.cards.possible | GET /api/possible-cards | 5000 ms |
| solver.node.numActions | GET /api/num-actions | 5000 ms |
| desktop.parameters.export | POST /api/parameters/export | 5000 ms |
| desktop.parameters.openFolder | POST /api/parameters/open-folder | 5000 ms |
| desktop.external.open | POST /api/external/open | 5000 ms |
| desktop.parameters.load | POST /api/parameters/load | 5000 ms |
| desktop.ranges.load | POST /api/ranges/load | 5000 ms |
| desktop.settings.load | POST /api/settings/load | 5000 ms |
| desktop.settings.save | POST /api/settings/save | 5000 ms |
| desktop.parameters.list | GET /api/parameters/list?path= | 5000 ms |
| desktop.ranges.list | GET /api/ranges/list?path= | 5000 ms |
| solver.cards.private | GET /api/private-cards/<n> | 5000 ms |

Default timeout = 5000 ms. Base `http://127.0.0.1:6006` (overridable `?apiBase=`).

**Additional native methods (called directly on `this.solver`, bypassing dT/HTTP):**
- `solver.ensureIdle()`, `solver.policy(postNode, holes)`, `solver.node(history)`, `solver.possibleCards()`, `solver.prepare(payload, settings, onCancelled, onStatus, hooks)`, `solver.stop()`, `solver.confirmStopped()`

**Full string inventory:** `solver.allocate`, `solver.cards.private`, `solver.cards.possible`, `solver.environment.check`, `solver.export.currentStreet`, `solver.export.fullFile`, `solver.export.fullFile.status`, `solver.finalize.compute`, `solver.finalize.publish`, `solver.history.apply`, `solver.host.allocate`, `solver.init`, `solver.layout.validate`, `solver.lock`, `solver.lock.clearAll`, `solver.lock.current.clear`, `solver.lock.current.get`, `solver.lock.current.set`, `solver.lock.list`, `solver.memoryUsage`, `solver.node.actionsAfter`, `solver.node.currentBoard`, `solver.node.currentPlayer`, `solver.node.numActions`, `solver.node.results`, `solver.node`, `solver.policy`, `solver.practice`, `solver.prepare`, `solver.solve.2p.construct`, `solver.solve.2p.run`, `solver.solve.3p.construct`, `solver.solve.3p.run`, `solver.solve.cpu_locked`, `solver.solve.cpu_locked.finalize`, `solver.solve.dispatch`, `solver.solve.preflight`, `solver.solve.start`, `solver.solve.status`, `solver.solve.stop`, `solver.strategy.archive.close`, `solver.strategy.archive.node`, `solver.strategy.archive.open`, `solver.stop`, `solver.may`, `solver.confirm`, `solver.ensure`, `desktop.external.open`, `desktop.parameters.export`, `desktop.parameters.list`, `desktop.parameters.load`, `desktop.parameters.openFolder`, `desktop.ranges.list`, `desktop.ranges.load`, `desktop.settings.load`, `desktop.settings.save`, `desktop.strategy.close`, `desktop.strategy.closeImport`, `desktop.strategy.export`, `desktop.strategy.exportCurrentStreetFile`, `desktop.strategy.exportFullFile`, `desktop.strategy.exportFullFileProgress`, `desktop.strategy.import`, `desktop.strategy.importFullFile`, `desktop.strategy.importNode`, `desktop.strategy.closeImport`

### 1.5 Transport / bridge protocol (verified from embedded JS)
- `Ds` class with `pending` Map, `seq` counter, `authToken`
- `callBridge(t, s, a=5000)`: RPC id `rpc_${Date.now()}_${Ds.seq++}`, envelope `{id, method, params: s}`, `window.chrome.webview.postMessage(JSON.stringify(i))`
- `ensureAuthToken()`: `bridge.ping` (1500 ms) → `bridge_token`
- `callBridgeAuthed()`: adds `bridge_token` param; on `UNAUTHORIZED` clears token, re-auths, retries once
- `request(t, s, a)`: `dT(path, method)` resolves internal method name; per-method timeout; fallback to `callBridgeAuthed("http.request", …)` on `NOT_IMPLEMENTED`
- Response envelope: `{ok: bool, result: any, error: {message, code}}`

### 1.6 C++ core data structures (reconstructed)
**GameState:** `numSeats`, `seats[6]`, `stack`, `ante`, `postflopOrder[6]`, `wager[6]`, `paid[6]`, `folded[6]`, `pending[6]`, `canRaise[6]`, `actor`, `lastRaise`, `history`, `ranges[6][1326]`, `phase[16]`, `pot`
**Node:** `actor`, `player`, `bettingRound`, `tableCards`, `pot`, `valid_actions[]`, `strategy*`, `comboNode*`, `childrens[]`, `type`
**NodeSnapshot:** `seat`, `seatNames[]`, `board[]`, `paid[]`, `pot`, `labels[]`, `ranges[6][]`, `cards[]`, `probabilities[]` [numActions x 1326], `evs[]`
**Combo index:** `en(e,t) = e*(103-e)/2 + t - e - 1` [0,1325]
**Card encoding:** `rank = c/4`, `suit = c%4`, packed `c | (p<<8)`
**6-max seat layout:** [UTG, MP, CO, BTN, SB, BB], postflop order [4,5,0,1,2,3]

### 1.7 CUDA kernel inventory (symbol names from binary)
**Showdown/fold (11):** `evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`
**CFR backward (13):** `backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`, `backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`
**CFR forward (12):** `forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`, `forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`, `forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`, `forward_river_subtrees_packed`
**Training/river (14):** `train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`, `train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`, `train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`, `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`, `river_oop`, `river_raise`
**Tree compaction/isomorphism (8):** `apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `replace`, `trim`
**Batch/memory-ops:** `BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`, `BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2`

### 1.8 Q16 fixed-point + compact archive
- Q16 fields: `reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16` (dequantize: `value = q16_raw / 65536.0`)
- Compact archive = Apache Avro + DEFLATE, magic `OBJ 0x04 0x1c 0x96 0x52`
- Native entry points: `solver.strategy.archive.open/node/close`
- Variants: `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`
- Async lifecycle: `avro_busy` → `avro_done` / `avro_cancelled`

### 1.9 CUDA kernel math (pseudocode reconstructed)
Full CUDA kernel pseudocode in `CUDA_KERNELS_RECON.cpp`: `eval5()` (5-card hand rank), `eval7()` (7-card showdown = max over 21 five-card subsets), `settle_pot()` (pot settlement/rake), `evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_showdown_3p`, `evaluate_fold_abstract`, `evaluate_showdown_abstract`. Block size 256, grid = ceil(numCombos/256).

### 1.10 Device buffer inventory
- `d_cfv_` — counterfactual values
- `d_abs_regrets_` — absolute regrets
- `d_abs_strategy_` — absolute strategy
- `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` — counterfactual reach
- `d_init_weights_` — initial weights
- `d_discount_params_` — discount parameters (3 floats: `iter_params`)
- `d_strategy_packed_` — packed strategy output
- `d_compact` — compact archive data
- `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_` — gather staging
- Host-pinned: `cfv_stage_host_`, `pinned_discount_params_`, `cfv_stage_scales_`, `pinned_gather_staging_`

### 1.11 CUDA runtime configuration
- PTX target: **sm_120** (forward PTX JIT; **no embedded cubin**)
- Compiled CUDA targets: `75;80;86;89;90;120`
- CUDA 13.1 (`ptxas V13.1.115`) + legacy 12.6
- Block size: 256 threads
- Streams: `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
- Events: `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`
- CUDA Graph: `training_graph_exec_` via `cuGraphLaunch(training_graph_exec_, stream_)`
- VRAM gate: "VRAM is below 8 GB; large trees may fail."

### 1.12 Author & source
- Author: bupticybee; Discord: https://discord.gg/RtyD4vRy2e; GitHub: https://github.com/bupticybee; Page: https://bupticybee.github.io/texassolver_gpu_page/
- Source paths: `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\{gpu_solver.cpp,gpu_solver_3p.cpp,cuda_driver_api.cpp}`, `include\poker\{cuda_buffer.h,cuda_host_memory.h}`

---

## 2. What Is Blocked (Phase 5 — Dynamic)

**Blocker:** No Windows/x64 emulator + NVIDIA GPU available on this Linux host. All Phase 5 steps require live execution.

**Wine test result:** `c0000135` (missing DLL — nvcuda.dll / WebView2Loader not available under Wine, and no NVIDIA GPU for CUDA context creation).

### 2.1 Handler map recovery (Priority 1 — REQUIRES dynamic)
Instrument the dispatcher to log `(method_name, handler_VA)` pairs:
1. Hook `0x140014cf0` (dispatcher): on entry save `rdx` (input kv pairs), `r12` (output ptr); walk entries to find type-0x3 string value (method name); read handler pointer at `[r12+0x8]` after dispatch resolution; log `(method_name_string, handler_VA, timestamp)`.
2. Hook `0x1400099e0` (handler lookup): log list node traversal + match result.
3. Hook `0x140036d70` (handler creator): log handler object VA + valid flag.
4. Hook `0x140001840` (variant string copy / invoker): log call targets.
5. Correlate with HTTP/WebView traffic on `127.0.0.1:6006` to map each `solver.*` / `desktop.*` method to native handler VA.
6. **Anti-tamper bypass:** Patch `IsDebuggerPresent` to return 0; use API-level instrumentation (proxy `nvcuda.dll`, WinSock LSP) instead of user-mode debugger; handle 5× `cpuid` checks.

### 2.2 PTX / device kernel capture (Priority 2 — REQUIRES dynamic)
1. Hook `cuModuleLoadData` / `cuModuleLoadDataEx` to dump PTX blobs.
2. Hook `cuLaunchKernel` / `cuLaunchKernelEx` to log `(kernel_name, gridDim, blockDim, sharedMem, stream, args)`.
3. Correlate kernel launches with `solver.solve.start` / status polling → CFR phase assignment.
4. Capture PTX for all 40+ kernels: `evaluate_*`, `backward_*`, `forward_*`, `train_*`, `river_*`, `apply_isomorphism`, `gather_cfv`, `Batch*`.

### 2.3 Compact archive decoding (Priority 3 — REQUIRES dynamic)
1. Trigger `POST /api/export/full-file` with `format=avro`.
2. Capture bytes from `solver.strategy.archive.open/node/close`.
3. Confirm Avro magic + DEFLATE blocks.
4. Decode with Python `fastavro` using recovered schema.
5. Verify Q16 dequantization: `value = q16_raw / 65536.0`.
6. Confirm `chance_representatives` (empty or 52 bytes) for 3p games.

### 2.4 Kernel config & buffer lifetimes (Priority 4 — REQUIRES dynamic)
1. Instrument `cuMemcpyHtoDAsync(d_discount_params_, …)` to capture `iter_params` (3 floats).
2. Measure grid/block for key kernels: `backward_player_cfv_packed`, `forward_player`, `train_small_tree_fp32_multi_iter`, `river_batched`, `apply_isomorphism`, `gather_cfv`.
3. Derive shared memory from `cuOccupancyAvailableDynamicSMemPerBlock`.
4. Track buffer lifetimes: `d_cfv_`, `d_abs_regrets_`, `d_abs_strategy_`, `d_cfreach*`, `d_strategy_packed_`, `d_compact`, `cfv_stage_*`, `d_gather_*`.

### 2.5 Solver flow capture (Priority 5 — REQUIRES dynamic)
1. Full solve cycle: `init` → `allocate` → `gpu-solve` → poll `status` → `stop` → `results` → `export/current-street`.
2. Capture request/response schemas for each endpoint.
3. Capture `solver.prepare` payload structure (postflop solve launch).
4. Capture `solver.policy` response: `{probs: Float32Array[1326], evs: Float32Array|null}`.
5. Capture `solver.possibleCards` bitmask (BigInt).

---

## 3. Required Environment to Unblock Phase 5

1. **Windows/x64 with NVIDIA GPU** (compute capability 7.5–12.0, VRAM ≥ 8 GB recommended) — either:
   - Windows VM with GPU passthrough, or
   - Wine + NVIDIA driver (difficult — WebView2 + CUDA driver needed)
2. **Instrumentation tools:** x64dbg, Ghidra, radare2, or API-level proxy DLLs (proxy `nvcuda.dll`, WinSock LSP)
3. **Anti-tamper bypass:** Patch `IsDebuggerPresent` (return 0), handle 5× `cpuid`, avoid user-mode debugger detection

---

## 4. Deliverables (to produce after dynamic capture)

- `HANDLER_MAP.md` — complete method → handler VA table (~60 methods)
- `PTX_DUMP/` — all captured PTX blobs by kernel name
- `KERNEL_CONFIGS.md` — grid/block/sharedMem per kernel per CFR phase
- `COMPACT_ARCHIVE_SCHEMA.md` — finalized Avro schema + Q16 field definitions
- `CFR_PIPELINE.md` — end-to-end solver flow with buffer lifetimes and timings
- `ANTI_TAMMER_BYPASS.md` — techniques used to bypass protections

---

## 5. Existing Recon Documents (reference)

| Document | Contents |
|----------|----------|
| `BINARY_ANALYSIS_RECON.md` | Binary inventory, PE structure, imports, CUDA config, kernel list, solver methods, anti-tamper |
| `CPP_CORE_RECON.md` | C++ data structures: GameState, Node, NodeSnapshot, SolveConfig, Decision, SolveRequest, SolveStatus |
| `CUDA_MATH.md` | CUDA kernel mathematics: card encoding, combo index, eval5/eval7, fold equity, pot settlement |
| `CUDA_KERNELS_RECON.cpp` | Full CUDA kernel pseudocode reconstruction |
| `DEEP_RE_PHASE2.md` | Dispatcher disassembly, variant type system, dT table, transport protocol, kernel inventory, Q16, compact archive, CFV staging, anti-tamper, PTX status |
| `FRONTEND_API_RECON.md` | Transport & auth, endpoint map, solve flow, JSON schemas, internal solver methods, CUDA kernels, JS poker primitives |
| `PHASE3_HANDLER_MAP.md` | Native method handler map — dispatch mechanism, method inventory, dT table, anti-tamper |
| `PHASE3_COMPACT_ARCHIVE.md` | Compact archive / strategy archive format: Avro + DEFLATE, Q16 fields, layout, validation errors, async lifecycle |
| `PHASE3_CFR_LAUNCHERS.md` | CFR kernel launcher inventory, device buffer inventory, buffer lifetimes, CFV staging + training graph |
| `PHASE4_STATIC_RECON.md` | Extended static recon: dispatcher disassembly, dT table, compact archive errors, frontend API usage, handler address recovery (negative), PTX status |
| `PHASE5_DYNAMIC_PLAN.md` | Phase 5 dynamic analysis plan: environment setup, handler map recovery, PTX capture, compact archive decoding, kernel config, solver flow capture |
| `phase3_json_api_recon.txt` | JSON schemas, network addresses, parser keys, validation errors (Russian-language report) |
| `texassolver_gpu_recon.txt` | File listing of zip archive contents |
| `deep_extraction_report.txt` | File listing of zip archive contents (range files) |

---

**Bottom line:** Static reverse engineering of TexasSolverGPU v0.3.0 is **100% complete**. All remaining unknowns (handler addresses, PTX device code, kernel launch configs, compact archive bytes, iter_params) require **dynamic execution on Windows + NVIDIA GPU**, which is not available on this host.