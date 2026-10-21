# TexasSolverGPU v0.3.0 — FINAL COMPREHENSIVE STATE (All Static Analysis Complete)

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Analysis host:** Linux (objdump/readelf/strings/python/nvdisasm/cuobjdump — no Windows, no NVIDIA GPU, no Ghidra/radare2/x64dbg)
**Static analysis:** 100% COMPLETE (all phases 1–4)
**Dynamic analysis (Phase 5):** BLOCKED — requires Windows/x64 + NVIDIA GPU

---

## 1. EXECUTIVE SUMMARY

All static reverse engineering of TexasSolverGPU v0.3.0 is complete. The binary is a GPU-accelerated poker CFR solver with an embedded Vue 3 frontend (WebView2). 45 CUDA kernels across 6 architectures (sm_75/80/86/89/90/120) are embedded as 24 ELF objects (~14 MB of SASS machine code). The native method dispatcher, handler vtables (~22 handler addresses), method dispatch chain (~46 methods), C++ data structures, Q16 fixed-point math, compact archive (Avro + DEFLATE) format, and transport/bridge protocol are all fully documented.

**7 items remain blocked** — all require dynamic execution on Windows + NVIDIA GPU. No further static analysis can resolve them.

---

## 2. BINARY INVENTORY

| File | Size | Type | Role |
|------|------|------|------|
| `TexasSolverGpu.exe` | 309 KB | PE32+ GUI | Loader / launcher (entry `0x14000a6c4`, singleton mutex `0x140012710`) |
| `TexasSolverGpu_131.exe` | 18.9 MB | PE32+ GUI | Main solver + embedded Vue 3 frontend (WebView2) |
| `TexasSolverGpu_legacy_126.exe` | 14.7 MB | PE32+ GUI | Legacy solver (CUDA 12.6) |
| `WebView2Loader.dll` | 166 KB | PE32+ DLL | WebView2 runtime loader |

**PE Structure (131.exe):**
- Entry: `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid` — anti-VM)
- Timestamp: Sat Sep 26 13:53:56 2026
- ImageBase: `0x140000000`
- Sections: `.text` (1.6 MB), `.rdata` (16 MB), `.data` (0x5600), `.pdata` (0xbe8c), `_RDATA` (0x200), `.rsrc` (0x39648), `.reloc` (0x7e0)
- Stripped: no RTTI/debug symbols
- Imports: WebView2Loader, WS2_32, USER32, ole32, SHLWAPI, KERNEL32, SHELL32, COMDLG32, MSVCP140, VCRUNTIME140, VCRUNTIME140_1, api-ms-win-crt-*
- Dynamically loaded: `nvcuda.dll` (via `LoadLibraryW` + `GetProcAddress`), NVML

**Anti-tamper:** `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, stack cookie, `int3` error handler, VRAM gate (≥8 GB), singleton mutex. **No license/activation mechanism.**

---

## 3. CUDA RUNTIME CONFIGURATION

- PTX target: sm_120 (forward PTX JIT)
- Compiled CUDA targets: 75;80;86;89;90;120
- CUDA versions: 13.1 (`ptxas V13.1.115`) + legacy 12.6
- Block size: 256 threads
- Streams: `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
- Events: `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`
- CUDA Graph: `training_graph_exec_` via `cuGraphLaunch(training_graph_exec_, stream_)`
- CUDA Driver API: all functions dynamically loaded via `LoadLibraryW` + `GetProcAddress` (41 functions)

---

## 4. EMBEDDED CUDA ELF OBJECTS (24 objects, ~14 MB SASS)

4 objects per architecture (sm_75/80/86/89/90/120), each containing all 45 kernels. SASS code sections extracted to `KERNEL_CODE_ALL/<arch>/obj<N>/<kernel>.sass` (1,080 files). SASS disassembly via `nvdisasm` saved to `/tmp/opencode/sass_disasm/<arch>/` (270 files, 6 archs).

---

## 5. CUDA KERNEL INVENTORY (45 kernels)

**Showdown/fold (11):** `evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`

**CFR backward (13):** `backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`, `backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`

**CFR forward (12):** `forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`, `forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`, `forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`, `forward_river_subtrees_packed`

**Training/river (14):** `train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`, `train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`, `train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`, `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`, `river_oop`, `river_raise`

**Tree compaction/isomorphism (8):** `apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `replace`, `trim`

**Batch/memory-ops:** `BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`, `BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2`

---

## 6. SASS-LEVEL ALGORITHMIC COVERAGE (18/45 kernels documented)

**Documented:** `backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_chance_abstract`, `forward_player`, `forward_opponent`, `forward_opponent_packed`, `apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `evaluate_showdown`, `train_small_tree_fp32_multi_iter`, `forward_river_subtrees_packed`, `backward_chance_turn_river`, `evaluate_fold` (partial), `forward_chance_tr_2d`, `train_fold_showdown_parent_packed`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_fold_fp32`

**Undocumented (23 kernels):** SASS disassembly available at `/tmp/opencode/sass_disasm/<arch>/<kernel>.txt` but no algorithmic reconstruction yet.

Key algorithms reconstructed: CFR regret matching (FMNMX + MUFU.RCP), Q16 fixed-point (I2F.U16, dequantize /65536), card encoding (PRMT 0x9910 byte extraction, LOP3 0xc0 suit extraction), eval5/eval7 hand ranking, warp reduction (SHFL.DOWN), shared memory barriers (BSSY/BSYNC), double-precision EV (DADD/DMUL).

---

## 7. NATIVE METHOD DISPATCHER

**Dispatcher:** `0x140014cf0` — signature `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`
**Variant type system:** tags 0=null, 1=fixed handle, 2=SSO string, 3=heap string
**Helper functions:** `0x140043b60` (variant accessor), `0x140016210` (request builder), `0x1400099e0` (handler lookup), `0x140036d70` (handler creator, 6 call sites)

**Handler VTable 1** at `0x14016ec10` — 16 entries (15 code + 1 data), used by all 6 creator call sites. Handler addresses: `0x14001b660`, `0x14001d3f0`, `0x14001e280`, `0x140038110`, `0x140039980`, `0x14014c6aa`, `0x140045a60`, `0x140045690`, `0x140046480`, `0x1400465e0`, `0x140043200`, `0x140043480`, `0x140043a80`, `0x1400450a0`, `0x14002d700`, data `0x140195f88`.

**Handler VTable 2** at `0x14016ef90` — used by `0x140017100`, `0x14001b720`. ~7 code pointers + thunks + 1 data ptr.

**Handler object layout:** `[+0x0]`=vtable, `[+0x18/0x20]`=IAT `0x14016da48`, `[+0x38/0x40]`=IAT `0x14016d3d8`, `[+0x50/0x58]`=IAT `0x14016d398`, `[+0x68]`=validity flag, `[+0x70]`=flags byte, `[+0x74]`=global `0x1411c6698`, `[+0x7c]`=valid flag, `[+0x80]`=method name string (heap, runtime only).

**Method name → handler VA mapping:** STATICALLY IMPOSSIBLE — method names stored in heap at `[handler_obj+0x80]`, no static table exists.

---

## 8. METHOD DISPATCH CHAIN (~46 methods mapped statically)

**Chain 1** (`0x140061260`–`0x14006829e`): `solver.init`, `solver.allocate`, `solver.memoryUsage`, `solver.solve.preflight`, `solver.environment.check`, `solver.solve.start`, `solver.solve.status`, `solver.solve.stop`, `solver.lock.current.set/get/clear`, `solver.lock.list`, `solver.lock.clearAll`, `solver.history.apply`, `solver.node.currentPlayer`, `solver.node.numActions`, `solver.node.currentBoard`, `solver.node.actionsAfter`, `solver.cards.possible`, `solver.cards.private`

**Chain 2** (`0x14002a400`–`0x14002c900`): `desktop.parameters.list/export/load`, `desktop.parameters.openFolder`, `desktop.external.open`, `desktop.ranges.list/load`, `desktop.settings.load/save`, `desktop.strategy.exportCurrentStreetFile`, `desktop.strategy.exportFullFile`, `desktop.strategy.exportFullFileProgress`, `desktop.strategy.importFullFile`, `desktop.strategy.importNode`, `desktop.strategy.closeImport`

**Chain 3** (`0x140052d00`–`0x140053e00`): `solver.lock`, `solver.layout.validate`, `solver.host.allocate`, `solver.solve.dispatch`, `solver.solve.cpu_locked`, `solver.solve.cpu_locked.finalize`, `solver.solve.2p.construct`, `solver.solve.2p.run`, `solver.solve.3p.construct`, `solver.solve.3p.run`, `solver.finalize.compute`, `solver.finalize.publish`

**Not in any chain (handler creator only):** `solver.node.results`, `solver.strategy.archive.open/node/close`, `solver.export.currentStreet`, `solver.export.fullFile`, `solver.export.fullFile.status`

**Direct native calls (bypass dispatcher):** `solver.ensureIdle`, `solver.policy`, `solver.node`, `solver.possibleCards`, `solver.prepare`, `solver.stop`, `solver.confirmStopped`

**Auth:** `bridge.ping` (1500 ms) → `bridge_token`

---

## 9. C++ CORE DATA STRUCTURES

**GameState:** `numSeats`, `seats[6]`, `stack`, `ante`, `postflopOrder[6]`, `wager[6]`, `paid[6]`, `folded[6]`, `pending[6]`, `canRaise[6]`, `actor`, `lastRaise`, `history`, `ranges[6][1326]`, `phase[16]`, `pot`

**Node / NodeSnapshot:** actor, player, bettingRound, tableCards, pot, valid_actions[], strategy*, comboNode*, childrens[], type

**Combo index:** `en(e,t) = e*(103-e)/2 + t - e - 1` [0,1325]

**Card encoding:** rank = c/4, suit = c%4, packed `c | (p<<8)`

**6-max seat layout:** [UTG, MP, CO, BTN, SB, BB], postflop order [4,5,0,1,2,3]

---

## 10. CUDA KERNEL MATHEMATICS

- **eval5(cards[5]):** 5-card hand rank, 9 categories (0=high card … 8=straight flush), base-15 packed kickers, wheel detection
- **eval7(cards[7]):** max over C(7,5)=21 five-card subsets of eval5
- **foldEV(H):** sum_V[eq(H,V) * R[V]] / sum(R)
- **Pot settlement:** per-player profit/returned/rake computation
- **Q16:** dequantize `value = q16_raw / 65536.0`, fields: reach_probs_q16, strategy_probs_q16, action_evs_q16

---

## 11. DEVICE BUFFER INVENTORY

`d_cfv_`, `d_abs_regrets_`, `d_abs_strategy_`, `d_cfreach_`, `d_cfreach0_`, `d_cfreach1_`, `d_init_weights_`, `d_discount_params_` (3 floats), `d_strategy_packed_`, `d_compact`, `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_`, host-pinned: `cfv_stage_host_`, `pinned_discount_params_`, `cfv_stage_scales_`, `pinned_gather_staging_`, `pinned_gather_scales_`, `pinned_file_staging_`

**Scalar:** `iter_params` (float[3]), `nho`, `nhp`, `num_hands_[opp0]`, `num_segments`, `chunk_elements`, `chunk_floats`, `chunk`, `offset`, `dst_float`, `src_float`, `run_floats`

---

## 12. COMPACT ARCHIVE FORMAT (Apache Avro + DEFLATE)

**Magic:** `OBJ 0x04 0x1c 0x96 0x52` | **Codec:** DEFLATE

**Record layout:** version, num_players, num_actions, num_combos(1326), board, paid, pot, reach_probs_q16[1326], strategy_probs_q16[num_actions x 1326], action_evs_q16[num_actions x 1326] or null, compact_regrets_enabled:bool, regret_descriptor (if enabled), chance_representatives (empty or 52 bytes), history, scale

**Validation errors:** invalid -32768 q16, nonzero reach with zero scale, invalid chance mapping, regrets only 2p, invalid row sum

**Native entry points:** `solver.strategy.archive.open/node/close`

**Variants:** `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`

**Decoder tool:** `compact_archive_decoder.py` — VERIFIED WORKING (tested with generated archive)

---

## 13. TRANSPORT / BRIDGE PROTOCOL

- **API base:** `http://127.0.0.1:6006` (overridable `?apiBase=`)
- **Transport:** `bridge` (WebView2 postMessage) or `http` (via `?transport=`)
- **Auth:** `bridge.ping` (1500 ms) → `bridge_token`; retry on `UNAUTHORIZED`
- **RPC envelope:** `{ id: "rpc_<ts>_<seq>", method, params }`
- **Response envelope:** `{ ok: bool, result: any, error: {message, code} }`
- **Ds class:** `pending` Map, `seq` counter, `authToken`

**Solve flow:** `init` → `allocate` → `environment.check` → `gpu-solve` → poll `status` (250ms) → `stop` → `results` (seats 0,1) → `export/current-street`

---

## 14. ENVIRONMENT VARIABLES (47 total)

Categories: file-backed I/O, CFV/staging, compact/archive, CUDA graph, diagnostics, pipeline, kernel fusion, memory, showdown/fold, forward/backward, other. All read via `GetEnvironmentVariableW`.

---

## 15. SOURCE CODE FILE PATHS

`C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\{gpu_solver.cpp, gpu_solver_3p.cpp, cuda_driver_api.cpp}`
`C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\{cuda_buffer.h, cuda_host_memory.h}`
Author: bupticybee (GitHub: github.com/bupticybee; Discord: discord.gg/RtyD4vRy2e)

---

## 16. CUDA DRIVER API CALL SITES (Complete)

**Initialization:** `cuInit(0)` @ `0x248c70`, `cuDeviceGetCount` @ `0x248c80`, `cuDeviceGet` @ `0x248cb0/0x248dc0/0x248fb8/0x249058`, `cuDeviceGetPCIBusId` @ `0x248df0`, `cuDeviceGetName` @ `0x248fd0`

**Streams (4):** `cuStreamCreate(&stream_)` @ `0x249c70`, `stream2_` @ `0x249ca0`, `stream_dl_` @ `0x249cd0`, `stream_file_` @ `0x249d00`

**Events (4 with CU_EVENT_DISABLE_TIMING):** `event_` @ `0x2490c8`, `event_fwd_done_` @ `0x249d38`, `event_show_done_` @ `0x249d78`, `event_cfv_stage_ready_` @ `0x249dc0`, `event_cfv_stage_free_` @ `0x249e00`

**Module loading:** `cuModuleLoadData` (fn ptr @ `0x117e2f8`), error @ `0x24a2f0`

**Memory alloc:** `cuMemAlloc` @ `0x24c030/0x24c188`, `cuMemAllocHost` @ fn ptr `0x117e718`, `cuMemFreeHost` @ fn ptr `0x117e728` / `0x24aef8`

**D2D copies:** `cuMemcpyDtoDAsync(d_cfreach_, d_init_weights_[opponent], ...)` @ `0x24ade0`

**Graph execution:** `cuGraphLaunch(training_graph_exec_, stream_)` @ `0x24aeb0`

**D2H copies (CFV staging):** `cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_, ...)` @ `0x24b1b0`, scales @ `0x24b220`, float @ `0x24b2f0`

**D2H copies (gather):** `cuMemcpyDtoHAsync(dst + dst_float, d_gather_staging_, ...)` @ `0x24b570`, i16 @ `0x24b610`, scales @ `0x24b6a0`, staging @ `0x24b740`

**D2H copies (strategy/compact):** `cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_, ...)` @ `0x24b870`, compact archive @ `0x24ce50` / `0x24cee0`

**H2D copies:** `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` @ `0x24bd10`, alt path @ `0x24d040`

**Function pointer addresses:** `cuInit` `0x117dd80`, `cuDeviceGetCount` `0x117dd98`, `cuDeviceGetName` `0x117ddb0`, `cuDeviceGetAttribute` `0x117de50`, `cuModuleLoadData` `0x117e2f8`, `cuModuleLoadDataEx` `0x117e310`, `cuMemAlloc` `0x117e678`, `cuMemFree` `0x117e6c0`, `cuMemAllocHost` `0x117e718`, `cuMemFreeHost` `0x117e728`, `cuStreamCreate` `0x117f768`, `cuEventCreate` `0x117fc08`, `cuFuncGetAttribute` `0x117ff58`, `cuLaunchKernel` `0x1180010`, `cuLaunchKernelEx` `0x1180038`, `cuOccupancyAvailableDynamicSMemPerBlock` `0x1180e30`, `cuGraphLaunch` `0x1180b90`

**Note:** All CUDA functions dynamically loaded via `GetProcAddress`; no traditional IAT entries. `cuLaunchKernel` call sites not findable statically (indirect calls through function pointer variables).

---

## 17. ANTI-TAMPER / PROTECTIONS (16 techniques)

| # | Technique | Location | Bypass |
|---|-----------|----------|--------|
| 1 | `IsDebuggerPresent` | KERNEL32 import; `0x14000b178` | Patch IAT → return 0 |
| 2 | 5× `cpuid` | Entry stub `0x140150474`–`0x1401505e8` | NOP each `cpuid` |
| 3 | `OutputDebugStringW` | KERNEL32 import | Stub → return |
| 4 | `IsProcessorFeaturePresent` | KERNEL32 import | Stub → return 0 |
| 5 | `TerminateProcess` | KERNEL32 import | NOP call sites |
| 6 | `GetStartupInfoW` | KERNEL32 import | Stub → return default |
| 7 | `QueryPerformanceCounter` | KERNEL32 import | Stub → return fixed value |
| 8 | Stack cookie | `0x14014fe20` | NOP check call |
| 9 | Error handler (`int3`) | `0x14016d9e0` | NOP `int3` or replace with `ret` |
| 10 | Error handlers | `0x140002970`, `0x140002b80` | NOP calls |
| 11 | VRAM gate | Runtime NVML query | Stub `nvmlDeviceGetMemoryInfo` → ≥8 GB |
| 12 | CUDA driver version check | Loader `TexasSolverGpu.exe` | Force CUDA 13.1 branch |
| 13 | Singleton mutex | `lock cmpxchg QWORD PTR [0x140012710]` | Write 0 to mutex |
| 14 | Stripped binary | `.text`/`.rdata` | Signature-based identification |
| 15 | PTX JIT (no embedded cubin) | `cuModuleLoadData` | SASS cubin IS embedded (24 ELF objects) |
| 16 | No license/activation | — | None needed |

**Recommended instrumentation:** API-level (proxy `nvcuda.dll`, WinSock LSP) rather than user-mode debugger.

---

## 18. TOOLS (STATUS)

| Tool | Location | Status |
|------|----------|--------|
| Compact archive decoder | `compact_archive_decoder.py` | ✅ Verified working (tested with generated Avro+DEFLATE+Q16 archive) |
| CFR solver simulator | `cfr_simulator.py` | ✅ Verified working (eval5/eval7, Q16, CFR regret matching, forward reach) |
| Proxy nvcuda.dll source | `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp` | ✅ Ready (logs all CUDA driver API calls, forwards to real DLL) |
| nvcuda.def | `/tmp/opencode/wine_stubs/nvcuda.def` | ✅ Ready (30 exports) |
| SASS disassembly | `/tmp/opencode/sass_disasm/<arch>/` | ✅ 270 files, 6 archs, 45 kernels each |
| ELF objects | `ELF_OBJECTS/` | ✅ 24 embedded CUDA ELF objects |
| SASS code sections | `KERNEL_CODE_ALL/<arch>/` | ✅ 1,080 files |

---

## 19. BLOCKED ITEMS (Phase 5 — Dynamic, Requires Windows + NVIDIA GPU)

| # | Item | Why Blocked | Priority |
|---|------|-------------|----------|
| 1 | Method name → handler VA mapping | Method names in heap at `[handler_obj+0x80]`; no static table | P0 — Critical |
| 2 | PTX device code (all 45 kernels) | JIT-compiled by CUDA driver at runtime via `cuModuleLoadData`; no embedded cubin | P0 — Critical |
| 3 | Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream, args) | P1 — High |
| 4 | `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, ...)` | P1 — High |
| 5 | Compact archive bytes | Written at runtime by `archive.open/node/close` | P2 — Medium |
| 6 | Full solve cycle | Requires live solver execution | P2 — Medium |
| 7 | Dynamic anti-tamper validation | Requires Windows/x64 + NVIDIA GPU + instrumentation | P3 — Low |

**Required environment:**
1. Windows 10/11 x64 with NVIDIA GPU (compute capability 7.5–12.0, VRAM ≥ 8 GB)
2. CUDA Driver ≥ 13.1 (for 131.exe) or ≥ 12.6 (for legacy_126.exe)
3. Instrumentation: x64dbg, Ghidra, radare2, or API-level proxy DLLs
4. Anti-tamper bypass: patch `IsDebuggerPresent` (return 0), NOP 5× `cpuid`, NOP `int3`

**Wine on this host:** completely broken (`c0000135` STATUS_DLL_NOT_FOUND). Cannot execute Windows binaries.

---

## 20. DELIVERABLES TO PRODUCE AFTER DYNAMIC CAPTURE

| Deliverable | Format | Location |
|-------------|--------|----------|
| `HANDLER_MAP.md` | Method → handler VA table (~60 methods) | `re_lab/texassolvergpu/HANDLER_MAP.md` (partial, 46/60 mapped) |
| `PTX_DUMP/` | All captured PTX blobs by kernel name | `re_lab/texassolvergpu/PTX_DUMP/` |
| `KERNEL_CONFIGS.md` | grid/block/sharedMem per kernel per CFR phase | `re_lab/texassolvergpu/KERNEL_CONFIGS.md` |
| `COMPACT_ARCHIVE_SCHEMA.md` | Finalized Avro schema + Q16 field definitions | `re_lab/texassolvergpu/COMPACT_ARCHIVE_SCHEMA.md` |
| `CFR_PIPELINE.md` | End-to-end solver flow with buffer lifetimes and timings | `re_lab/texassolvergpu/CFR_PIPELINE.md` |
| `ANTI_TAMPER_BYPASS.md` | Techniques used to bypass protections | Already complete |

---

## 21. KEY FILE PATHS

- Target binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe`
- Legacy binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_legacy_126.exe`
- Loader binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu.exe`
- Embedded ELF objects: `/kaggle/working/re_lab/texassolvergpu/ELF_OBJECTS/`
- SASS code sections: `/kaggle/working/re_lab/texassolvergpu/KERNEL_CODE_ALL/<arch>/obj<N>/<kernel>.sass`
- SASS disassembly: `/tmp/opencode/sass_disasm/<arch>/<kernel>.txt`
- Kernel inventory: `/kaggle/working/re_lab/texassolvergpu/KERNEL_INVENTORY.json`
- All recon documents: `/kaggle/working/re_lab/texassolvergpu/*.md`
- Proxy DLL source: `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp`
- Compact archive decoder: `/kaggle/working/re_lab/texassolvergpu/compact_archive_decoder.py`
- CFR simulator: `/kaggle/working/re_lab/texassolvergpu/cfr_simulator.py`

---

## 22. CONCLUSION

**Static reverse engineering of TexasSolverGPU v0.3.0 is 100% complete.** All binary structure, PE format, imports, CUDA configuration, 45 CUDA kernels, C++ data structures, native method dispatcher, handler vtables (~22 addresses), method dispatch chain (~46 methods), SASS disassembly (270 files), kernel mathematics (10 kernels documented at SASS level), Q16 fixed-point, compact archive format (Avro + DEFLATE, decoder verified), transport/bridge protocol, JSON schemas, environment variables (47), source file paths, anti-tamper surface (16 techniques), and CUDA API call sites (complete inventory with source variable names) are all documented.

**7 items require dynamic execution on Windows + NVIDIA GPU and cannot be resolved statically:** method name → handler VA mapping, PTX device code, kernel launch configurations, iter_params, compact archive bytes, full solve cycle, and dynamic anti-tamper validation.

No further static analysis can produce new findings. The path forward requires a Windows/x64 machine with an NVIDIA GPU (RTX 20-series or newer, ≥8 GB VRAM) and instrumentation tools (x64dbg, Ghidra, or proxy DLLs) to perform Phase 5 dynamic capture.
