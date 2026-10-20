# DEEP_RE_PHASE2.md
# TexasSolverGPU v0.3.0 — Deep Reverse Engineering (Phase 2)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64/TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase 0x140000000)
**Analysis host:** Linux (objdump/readelf/strings/python only — no Ghidra/radare2 available)
**Binary state:** stripped (no symbol table; `nm` returns "no symbols")

---

## 1. Summary of new findings (beyond the Phase-1 recon docs)

Phase 1 (BINARY_ANALYSIS_RECON.md, CPP_CORE_RECON.md, CUDA_MATH.md, CUDA_KERNELS_RECON.cpp, FRONTEND_API_RECON.md) covered the binary inventory, PE structure, C++ data-structure reconstruction, CUDA kernel math, and the frontend/endpoint schema. Phase 2 adds:

- The **native method dispatcher** disassembly (variant type system, handler resolution).
- The **complete frontend `dT` method→path table** (38 entries).
- The **full transport/bridge protocol** (postMessage RPC, `bridge_token` auth, per-method timeouts, `http.request` fallback).
- The **complete kernel/launcher symbol inventory** (40+ `.text.<kernel>` symbols — far more than the 10 `evaluate_*` kernels listed in Phase 1).
- **Q16 fixed-point** fields (`reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16`).
- **Compact archive** formats (`compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`) and capacity/overflow validation errors.
- **CFV staging**, **CUDA training graph**, **discount_params (iter_params)**, **river subsystem** symbol names.
- **Isomorphism / NodeLock** capacity limits.
- **Anti-tamper** surface (IsDebuggerPresent, cpuid checks, OutputDebugStringW).
- **PTX** status: no PTX source text embedded as plain strings (only a status string "PTX target: 120" and `ptxas` references) — PTX is JIT-compiled/loaded at runtime via `cuModuleLoadData`.

---

## 2. Native method dispatcher (addresses from `solver_131.exe` .text)

ImageBase = `0x140000000`. All RVAs below are virtual addresses.

### 2.1 Entry point
`0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474`.
The entry stub contains `cpuid` (5 occurrences in .text) — feature/environment detection (possible anti-VM/anti-analysis), not just IsDebuggerPresent.

### 2.2 Variant type system (tagged values)
The native RPC uses a **tagged variant** layout. Each value is a small struct whose first byte is a type tag:

| Type | Size | Meaning |
|------|------|---------|
| 0 | — | null / empty (accessor returns 0) |
| 1 | 0x18 bytes | fixed handle/struct |
| 2 | 0x20 bytes (SSO, inline cap 0xf=15) | short string |
| 3 | 0x28 bytes | long/heap string |
| 4 | — | null terminator |
| 5+ | — | null |

Accessed by `0x140043b60` (first-byte jump table at VA `0x140043ba4`, image-base-relative offsets):
- tag 0 → `0x140043b7c` (`xor eax,eax; ret` → 0)
- tag 1 → `0x140043b92` (returns element count / 16)
- tag 2 → `0x140043b7f` (returns data pointer)
- tags 3–9 and default (>9) → `0x140043b9b` (`mov $1,%eax; ret` → 1)

### 2.3 Dispatcher `0x140014cf0`
Signature (per recon): `(output*, input_kv_pairs, flags r8b, r8b)`. Iterates the input key/value array (entries are 0x18 bytes, walked `rbx` from `rdi` to `r15`). For each entry:
- resolves the value/key pointer, checks type byte == `0x2` (key),
- calls `0x140043b60` on it, requires result == `0x2`,
- then matches a type-`0x3` string value.

When the method entry is located, it:
- builds the request via `0x140016210` (variant builder; `dl` = type index 0–9, jump table at VA `0x140016370`; type 0 calls `0x14000dde0`, types 1–3 allocate 0x18/0x20/0x28-byte variants, type 4 null-terminates, 5+ null),
- stores the resolved handler pointer at `0x8(%r12)`,
- invokes via `0x140001840`.

### 2.4 Helpers
- `0x140016210` — request / variant builder (type dispatch on `dl`).
- `0x1400099e0` — request key-lookup: walks a linked list of key/value pairs (next pointer at `(%rbx)`, stride 0x10), compares keys with `0x140151250` (string compare).
- `0x140001840` / `0x1400017d0` — variant string copy (SSO vs heap; allocator `0x140151238`).
- `0x14001ef90` — string length helper (called by the wrapper `0x140005c30`).

> Note: the exact per-method native **handler address** table was not fully recovered statically (the dispatch uses the variant accessor rather than a simple string hash). The recon §7 already documents all method names and HTTP endpoints; this section documents the dispatch *mechanism*.

---

## 3. Frontend `dT` dispatcher (complete method → path table)

Recovered from the embedded minified Vue/Vite JS (`function dT(e,t){switch(\`${t.toUpperCase()} ${e}\`)...}`). `Tt(e,t,s)` → `Hl().request(e,t,s)` resolves `c = dT(path, method)`.

| Method | Path | Transport timeout |
|--------|------|-------------------|
| solver.init | POST /api/init | 120 s |
| solver.allocate | POST /api/allocate | 120 s |
| solver.memoryUsage | GET /api/memory-usage[?compress=*] | 30 s |
| solver.environment.check | GET /api/environment/check | 5 s |
| solver.solve.start | POST /api/gpu-solve | 30 s |
| solver.solve.preflight | POST /api/solve/preflight | 30 s |
| solver.export.currentStreet | POST /api/export/current-street | 120 s |
| desktop.strategy.exportCurrentStreetFile | POST /api/export/current-street-file | null |
| desktop.strategy.exportFullFile | POST /api/export/full-file | null |
| desktop.strategy.exportFullFileProgress | GET /api/export/full-file/progress | null |
| desktop.strategy.importFullFile | POST /api/import/full-file | null |
| desktop.strategy.importNode | POST /api/import/node | 30 s |
| desktop.strategy.closeImport | POST /api/import/close | null |
| solver.solve.status | GET /api/solve/status | 5 s |
| solver.solve.stop | POST /api/solve/stop | 5 s |
| solver.history.apply | POST /api/apply-history | 5 s |
| solver.node.results | GET /api/results | 5 s |
| solver.node.actionsAfter | POST /api/actions-after | 5 s |
| solver.node.currentPlayer | GET /api/current-player | 5 s |
| solver.node.currentBoard | GET /api/current-board | 5 s |
| solver.lock.current.set | POST /api/lock/current | 5 s |
| solver.lock.current.get | GET /api/lock/current | 5 s |
| solver.lock.current.clear | POST /api/lock/unlock | 5 s |
| solver.lock.list | GET /api/lock/list | 5 s |
| solver.lock.clearAll | POST /api/lock/clear | 5 s |
| solver.cards.possible | GET /api/possible-cards | 5 s |
| solver.node.numActions | GET /api/num-actions | 5 s |
| desktop.parameters.export | POST /api/parameters/export | 5 s |
| desktop.parameters.openFolder | POST /api/parameters/open-folder | 5 s |
| desktop.external.open | POST /api/external/open | 5 s |
| desktop.parameters.load | POST /api/parameters/load | 5 s |
| desktop.ranges.load | POST /api/ranges/load | 5 s |
| desktop.settings.load | POST /api/settings/load | 5 s |
| desktop.settings.save | POST /api/settings/save | 5 s |
| desktop.parameters.list | GET /api/parameters/list?path= | 5 s |
| desktop.ranges.list | GET /api/ranges/list?path= | 5 s |
| solver.cards.private | GET /api/private-cards/<n> | 5 s |

Default timeout = 5 s. Base `http://127.0.0.1:6006` (overridable `?apiBase=`).

---

## 4. Transport / bridge protocol

`request(path, opts)`:
1. `method` = opts.method (default GET); body parsed by `cT` (JSON.parse if string).
2. `c = dT(path, method)` → internal method name.
3. Per-method timeout `p(c)` (table in §3).
4. Try `callBridgeAuthed(c, {apiBase,path,method,body,bridge_token}, timeout)`.
5. On error code `NOT_IMPLEMENTED` → fallback `callBridgeAuthed("http.request", {...})` (raw HTTP to apiBase).
6. Non-ok → throw `Bridge request failed` / `BRIDGE_ERROR`.

`callBridge(t, s, a=5000)`:
- Requires `pT()` (native bridge available) else `NO_NATIVE_BRIDGE`.
- RPC id `rpc_${Date.now()}_${Ds.seq++}`; envelope `{id, method, params: s}`.
- `window.chrome.webview.postMessage(JSON.stringify(i))`.
- Pending map `Ds.pending[id]` = {resolve, reject, timer}; timeout → `BRIDGE_TIMEOUT`.

`ensureAuthToken()` / `callBridgeAuthed()`:
- `bridge.ping` (1500 ms) → `authToken = result.bridge_token`.
- On `UNAUTHORIZED`: clear token, re-auth, retry once.

---

## 5. Complete kernel / launcher symbol inventory

Recovered `.text.<kernel>` symbol strings (host-side CUDA kernel launchers; device PTX loaded via `cuModuleLoadData`). Categorized:

**Showdown / fold evaluation (10):**
`evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`.

**CFR backward pass (9):**
`backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`, `backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`.

**CFR forward pass (10):**
`forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`, `forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`, `forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`, `forward_river_subtrees_patched`… (`forward_river_subtrees_packed`).

**Training / river (8):**
`train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`, `train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`, `train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`, plus `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, river_donk`, `river_ip`, `river_oop`, `river_raise`.

**Tree compaction / isomorphism / misc (8):**
`apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `replace`, `trim`.

> This list supersedes the Phase-1 kernel table (which listed only the 10 `evaluate_*` kernels). The full CFR pipeline (backward/forward/train/river/isomorphism) is now enumerated.

---

## 6. Q16 fixed-point & compact archive

**Q16 fields** (fixed-point, 16.16 or Q16):
- `reach_probs_q16`
- `strategy_probs_q16`
- `action_evs_q16`

**Compact archive** (Avro-like serialization, per BINARY_ANALYSIS_RECON.md §6):
- `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`
- `compact regret descriptor`, `compact regret hand map`, `compact regret layout`

**Capacity / overflow validation errors** (native strings):
- `compact regret descriptor exceeds uint32 range`
- `packed strategy offset exceeds int32 range`
- `compact regret hand map exceeds descriptor range`
- `compact regret layout only supports two-player games`
- `persistent river storage offset exceeds int32 range`
- `GPU exact-layout capacity exceeded: this tree needs …`
- `GPU node/schedule capacity exceeded: this tree has …` / `…reachable tree has …`
- `GPU hand-index capacity exceeded for player …`
- `GPU workspace-row capacity exceeded: this schedule needs …`
- `one upper/river-batch slice exceeds the signed int32 entry-offset limit`
- `GPU isomorphism schedule capacity exceeded at depth …` / `while building a depth slice: entry offsets and counts must fit signed int32`
- `GPU isomorphism permutation capacity exceeded for player …` / `too many 2P isomorphism permutations`
- `GPU NodeLock capacity exceeded: the concatenated lock table would contain more than …`

---

## 7. CFV staging, CUDA graph, discount params, river

**CFV staging buffers/events:**
- `d_cfv_`, `cfv_stage_device_`, `cfv_stage_host_`, `cfv_stage_host_device_ptr_`, `cfv_stage_i16_`, `cfv_stage_scales_`, `cfv_buf_size`
- `event_cfv_stage_free_`, `event_cfv_stage_ready_` (`cuEventCreate(…, CU_EVENT_DISABLE_TIMING)`)
- `cfv_stage_free_`, `cfv_stage_ready_` events; `cfv_stage_scales_`
- `pinned_gather_staging_`, `gather_staging_capacity_` (`cuMemHostAlloc … CU_MEMHOSTALLOC_PORTABLE`)
- `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_`

**Discount parameters:**
- `d_discount_params_` — 3 floats (`iter_params`); `pinned_discount_params_`; `cuMemcpyHtoDAsync( d_discount_params_, iter_params, 3*sizeof(float), stream_ )`.

**CUDA training graph:**
- `training_graph_exec_` — `cuGraphLaunch(training_graph_exec_, stream_)`; `cuGraphExecDestroy(training_graph_exec_)`.

**Timing log formats** (native strings):
- `[gpu timing] submit=%.3f training_sync=%.3f pin_wait=%.3f exploit=%.3f finalize=%.3f total=%.3f ms`
- `[gpu finalize] p0_gpu=%.3f p0_scatter=%.3f p1_gpu=%.3f p1_scatter=%.3f before_unpack=%.3f unpack=%.3f total=%.3f ms`
- `[gpu] persistent backward eligibility: roots=%zu/%zu nodes=%zu`
- `[gpu] river forward a2: player=%zu roots=%zu/%zu nodes=%zu/%zu opponent_nodes=%zu/%zu max_nodes=%zu`
- `[gpu finalize] staged CFV enabled: max_plan=%llu total_p0=%llu total_p1=%llu chunks_p0=%u chunks_p1=%u device_capacity=%llu host_capacity=%llu direct_host=%d`

---

## 8. Anti-tamper / protections

- `IsDebuggerPresent` (KERNEL32), `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`.
- 5× `cpuid` in .text (environment/feature detection, possible anti-VM).
- Stripped binary (no RTTI/debug symbols).
- PTX JIT-compiled at runtime (`cuModuleLoadData`; no embedded cubin; PTX target sm_120; compiled CUDA targets 75;80;86;89;90;120; CUDA 13.1 / legacy 12.6).
- `ptxas` referenced repeatedly ("Cuda compilation tools, release 13.1, V13.1.115") — PTX assembly invoked at runtime.
- VRAM gate: "VRAM is below 8 GB; large trees may fail."
- No obvious license/activation mechanism found in strings.

---

## 9. PTX status (negative result)

Searched `all_strings.txt` for PTX directives (`.version`, `.target sm_`, `.entry`, `.param`, `.reg`, `.shared`, `.loc`) and PTX instructions (`mov.u32`, `ld.global`, `bar.sync`, `add.f32`, etc.) — **no matches**. No UTF-16 PTX either. Only:
- status string: `Compiled CUDA targets: 75;80;86;89;90;120; PTX target: 120.`
- warning: `This GPU requires forward PTX JIT and a compatible NVIDIA driver; no native cubin is embedded.`

Conclusion: PTX device code is **not stored as plain text** in the binary; it is generated/compiled at runtime (likely via `ptxas`/nvcc subprocess or downloaded) and fed to `cuModuleLoadData`. Kernel *math* was already reconstructed in CUDA_MATH.md / CUDA_KERNELS_RECON.cpp from the embedded JS evaluator.

---

## 10. Source paths & author (from Phase 1, re-confirmed)

- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp`
- Author: bupticybee; Discord https://discord.gg/RtyD4vRy2e; GitHub https://github.com/bupticybee

---

## 11. Recommended next steps (Phase 3)

1. **Dynamic analysis:** run under a Windows/x64 emulator or instrument the CUDA driver API to capture the PTX blobs passed to `cuModuleLoadData` (the actual device kernels).
2. **Native handler map:** instrument `0x140014cf0`/`0x140001840` to log method name → handler address for each `solver.*` call.
3. **Compact archive format:** decode `compact_v2` / `compact_H9` (Q16) from a real `snapshot` export to confirm the fixed-point encoding.
4. **CFR algorithm:** map `backward_player_cfv*` / `forward_player*` / `train_*` launchers to grid/block configs and memory-buffer lifetimes.
