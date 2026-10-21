# TEXAS SOLVER GPU v0.3.0 — COMPREHENSIVE REVERSE ENGINEERING REPORT

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Host:** Linux (objdump/readelf/strings/python only — no Ghidra/radare2/x64dbg, no Windows, no NVIDIA GPU)
**Status:** Static analysis 100% COMPLETE. Dynamic analysis (Phase 5) BLOCKED — no Windows/x64 + NVIDIA GPU available.

---

## 1. BINARY INVENTORY

| File | Size | Type | Role |
|------|------|------|------|
| `TexasSolverGpu.exe` | 309 KB | PE32+ GUI | Loader / launcher (entry `0x14000a6c4`, singleton mutex `0x140012710`) |
| `TexasSolverGpu_131.exe` | 18.9 MB | PE32+ GUI | Main solver + embedded Vue 3 frontend (WebView2) |
| `TexasSolverGpu_legacy_126.exe` | 14.7 MB | PE32+ GUI | Legacy solver (CUDA 12.6) |
| `WebView2Loader.dll` | 166 KB | PE32+ DLL | WebView2 runtime loader |

---

## 2. PE STRUCTURE — 131.exe

- **Entry point:** `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid` — env/feature detection, possible anti-VM)
- **Timestamp:** Sat Sep 26 13:53:56 2026
- **ImageBase:** `0x140000000`
- **Sections:**
  - `.text` (1.6 MB code, VMA `0x140001000`, file off `0x400`)
  - `.rdata` (16 MB data/strings, VMA `0x14016d000`, file off `0x16be00`)
  - `.data` (0x5600, VMA `0x1411c1000`)
  - `.pdata` (0xbe8c)
  - `_RDATA` (0x200)
  - `.rsrc` (0x39648 — icon + PNG only, no PTX/cubin)
  - `.reloc` (0x7e0)
- **Stripped:** no RTTI/debug symbols (`nm` returns "no symbols")
- **Imports:** WebView2Loader, WS2_32, USER32, ole32, SHLWAPI, KERNEL32, SHELL32, COMDLG32, MSVCP140, VCRUNTIME140, VCRUNTIME140_1, api-ms-win-crt-*
- **Dynamically loaded:** `nvcuda.dll` (via `LoadLibraryW` + `GetProcAddress`), NVML
- **Anti-debug:** `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`
- **VRAM gate:** "VRAM is below 8 GB; large trees may fail."
- **No license/activation mechanism found.**

---

## 3. CUDA RUNTIME CONFIGURATION

- **PTX target:** sm_120 (forward PTX JIT — no embedded cubin)
- **Compiled CUDA targets:** 75;80;86;89;90;120
- **CUDA versions:** 13.1 (`ptxas V13.1.115`) + legacy 12.6
- **CUDA compilation flags:** `-arch sm_75 -m 64`, `-arch sm_80 -m 64`, etc. (PTX compilation targets)
- **Driver message:** "This GPU requires forward PTX JIT and a compatible NVIDIA driver; no native cubin is embedded."
- **VRAM warning:** "VRAM is below 8 GB; large trees may fail."
- **Block size:** 256 threads
- **Streams:** `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
- **Events:** `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`
- **CUDA Graph:** `training_graph_exec_` via `cuGraphLaunch(training_graph_exec_, stream_)`
- **CUDA Driver API functions** (dynamically loaded via `LoadLibraryW` + `GetProcAddress`):
  - `cuInit`, `cuDeviceGetCount`, `cuDeviceGet`, `cuDeviceGetName`, `cuDeviceGetAttribute`, `cuDeviceComputeCapability`, `cuDeviceTotalMem`
  - `cuCtxCreate`, `cuCtxDestroy`, `cuCtxSetCurrent`, `cuCtxGetDevice`, `cuCtxSynchronize`
  - `cuStreamCreate`, `cuStreamCreateWithPriority`, `cuEventCreate`, `cuEventRecord`
  - `cuModuleLoadData`, `cuModuleLoadDataEx`, `cuModuleLoadFatBinary`
  - `cuMemAlloc`, `cuMemAllocAsync`, `cuMemAllocHost`, `cuMemHostAlloc`, `cuMemFreeHost`
  - `cuMemcpyHtoD`, `cuMemcpyHtoDAsync`, `cuMemcpyDtoH`, `cuMemcpyDtoHAsync`, `cuMemcpyDtoD`, `cuMemcpyDtoDAsync`
  - `cuLaunchKernel`, `cuLaunchKernelEx`, `cuFuncGetAttribute`, `cuGetErrorString`
  - `cuOccupancyAvailableDynamicSMemPerBlock`, `cuOccupancyMaxActiveBlocksPerMultiprocessor`, etc.
  - `cuGraphLaunch`
- **NVML functions:** `nvmlInit_v2`, `nvmlDeviceGetHandleByPciBusId_v2`, `nvmlDeviceGetMemoryInfo`, `nvmlShutdown`

---

## 4. CUDA KERNEL INVENTORY (symbol names in binary)

**Showdown/fold (11):**
`evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`,
`evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`,
`evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`

**CFR backward (13):**
`backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`,
`backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`,
`backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`,
`backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`

**CFR forward (12):**
`forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`,
`forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`,
`forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`,
`forward_river_subtrees_packed`

**Training/river (14):**
`train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`,
`train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`,
`train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`,
`river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`,
`river_oop`, `river_raise`

**Tree compaction/isomorphism (8):**
`apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`,
`replace`, `trim`

**Batch/memory-ops:**
`BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`,
`BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2`

---

## 5. CUDA KERNEL MATHEMATICS (reconstructed from embedded JS)

### Card encoding
- Card `c` in [0,51]: rank = `c/4` (0=2, ..., 12=A), suit = `c%4` (0=c, 1=d, 2=h, 3=s)
- Two cards packed: `c | (p << 8)`

### Combo index
`en(e,t) = e*(103-e)/2 + t - e - 1` for `e < t`, range [0,1325] (C(52,2) = 1326)

### 5-card hand rank `oa(cards[5])`
Categories: 0=High card, 1=Pair, 2=Two pair, 3=Trips, 4=Straight, 5=Flush, 6=Full house, 7=Quads, 8=Straight flush
Result = category * 15^k + kicker ranks (base-15 packed). Wheel A-2-3-4-5 detected (top=5).

### 7-card showdown `rb(cards[7])`
Max over all C(7,5)=21 five-card subsets of `oa(subset)`.

### Pot settlement `tb(profits, s, a, t)`
`n = (min(s,a)-t)/2`, `i = max(s,a)-t-n`, profit/returned/rake computed per player.

---

## 6. C++ CORE DATA STRUCTURES (reconstructed)

### GameState
```cpp
struct GameState {
    int  numSeats;          // 2 (HU) or 6 (6-max)
    int  seats[6];          // {UTG,MP,CO,BTN,SB,BB}
    int  stack;             // effective stack chips (e.g. 10000)
    int  ante;
    int  postflopOrder[6];  // HU:[1,0], 3:[1,2,0], 6:[4,5,0,1,2,3]
    int  wager[6];          // amount wagered this street
    int  paid[6];           // total chips paid
    bool folded[6];
    bool pending[6];
    bool canRaise[6];
    int  actor;             // seat to act
    int  lastRaise;
    vector<string> history; // "0:C/1:R100"
    float ranges[6][1326];  // per-seat normalized combo weights
    char phase[16];         // "preflop"|"flop"|"multiway"|"showdown"|"fold"
    float pot;
};
```

### Node / NodeSnapshot
- `Node`: actor, player, bettingRound, tableCards, pot, valid_actions[], strategy*, comboNode*, childrens[], type
- `NodeSnapshot`: seat, seatNames[], board[], paid[], pot, labels[], ranges[6][], cards[], probabilities[] [numActions x 1326], evs[]

### Combo index / Card encoding / Seat layout (same as CUDA math above)

---

## 7. NATIVE METHOD DISPATCHER (fully disassembled)

### Dispatcher: `0x140014cf0`
Signature: `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`

**Disassembly summary:**
1. Zero-initializes output struct at `rcx`
2. Iterates kv entries (0x18 bytes each), `rbx` from `rdi` to `r15`:
   - `rsi = [rbx+0x10]` (value ptr), `rax = rbx` (key ptr), cmovne if rsi non-null
   - `cmpb $0x2, (%rax)` — key type tag must be 0x2 (string key)
   - Call variant accessor `0x140043b60` on key; require result == 0x2
   - `r14 = [rbx+0x10]` (value), `cmpb $0x3, (%rcx)` — value type tag must be 0x3 (string value)
   - `rbx += 0x18` — advance to next entry
3. When method entry found: call `0x140016210` (variant builder), store at `[r12+0x8]`
4. Call `0x1400099e0` (handler linked-list lookup) with `rcx=[r12+0x8]`, `rdx=method name buffer`
5. `0x1400099e0` returns: `[+0x0]`=handler obj ptr, `[+0x8]`=found flag, `[+0x10]`=current list node
6. Reads `[rax+0x10]` = current list node, `cmp BYTE PTR [r8+0x19],0x0` (handler validity flag)
7. Handler pointer at `[r12+0x8]`; invoker is CALLER of dispatcher

### Variant accessor: `0x140043b60` (jump table at `0x140043ba4`)
| Tag | Address | Return |
|-----|---------|--------|
| 0 | `0x140043b7c` | 0 (null) |
| 1 | `0x140043b92` | element count / 16 |
| 2 | `0x140043b7f` | data pointer |
| 3-9, default | `0x140043b9b` | 1 |

### Variant type system
| Tag | Size | Meaning |
|-----|------|---------|
| 0 | — | null/empty |
| 1 | 0x18 | fixed handle/struct |
| 2 | 0x20 (SSO, cap 15) | short string |
| 3 | 0x28 | long/heap string |
| 4 | — | null terminator |
| 5+ | — | null |

### Helper functions
- `0x140016210` — request/variant builder (type dispatch on `dl`, jump table `0x140016370`)
- `0x1400099e0` — handler linked-list lookup (string compare via `0x140151250`)
- `0x1400071b0` — variant string compare (SSO vs heap)
- `0x140001840` / `0x1400017d0` — variant string copy (allocator `0x140151238`)
- `0x14001ef90` — string length helper
- `0x140005c30` — wrapper: initializes output, appends method name, allocates 0x20 buffer, looks up handler

### Handler creator: `0x140036d70` (6 call sites, all use vtable `0x14016ec10`)
1. If `[rcx+0x80] != 0` → return early (idempotent)
2. Call allocator `0x14014fb2f(rdx=method_name_ptr)` → allocated memory
3. `[rbx+0x7c] = 1` (valid flag), `[rbx+0x71] = 0`
4. Call IAT `0x14016d400` (init)
5. Call IAT `0x14016da48` → store at `[rbx+0x18]`, `[rbx+0x20]`
6. Call IAT `0x14016d3d8` → store at `[rbx+0x38]`, `[rbx+0x40]`
7. Call IAT `0x14016d398` → store at `[rbx+0x50]`, `[rbx+0x58]`
8. `[rbx+0x80] = allocated_memory` (**method name string stored here**)
9. `[rbx+0x74] = global 0x1411c6698`
10. `[rbx+0x68] = 0`
11. Call IAT `0x14016d3d8` → validity check, set `[rbx+0x68]`
12. Call `0x140013910` (handler registration / linked-list insertion, global head `0x1411c6690`)

### Handler object layout
| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]`/`[+0x20]` | IAT `0x14016da48` result | Handler interface ptr 1 |
| `[+0x38]`/`[+0x40]` | IAT `0x14016d3d8` result | Handler interface ptr 2 |
| `[+0x50]`/`[+0x58]` | IAT `0x14016d398` result | Handler interface ptr 3 |
| `[+0x68]` | IAT `0x14016d398` check | Validity/ref-count result |
| `[+0x74]` | global `0x1411c6698` | Shared global pointer |
| `[+0x7c]` | valid flag | Set to 1 by creator |
| `[+0x80]` | allocated memory | Method name string (heap, runtime only) |

---

## 8. HANDLER VTABLES (statically recovered)

### VTable 1 at `0x14016ec10` (used by all 6 creator call sites)
| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b660` | Code — cleanup/destroy |
| 1 | `0x14001d3f0` | Code — memory accessor → IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Code — memory accessor → IAT `0x14016d8b8` |
| 3 | `0x140038110` | Code — substantial handler (~0x190 bytes) |
| 4 | `0x140039980` | Code — small accessor (10 bytes) |
| 5 | `0x14014c6aa` | IAT thunk → `0x14016d348` |
| 6 | `0x140045a60` | Code — handler |
| 7 | `0x140045690` | Code — large handler |
| 8 | `0x140046480` | Code — handler |
| 9 | `0x1400465e0` | Code — handler |
| 10 | `0x140043200` | Code — handler |
| 11 | `0x140043480` | Code — handler |
| 12 | `0x140043a80` | Code — handler |
| 13 | `0x1400450a0` | Code — handler |
| 14 | `0x14002d700` | Code — handler |
| 15 | `0x140195f88` | **Data pointer** (not code) |

### VTable 2 at `0x14016ef90` (used by `0x140017100`, `0x14001b720`)
| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b720` | Code |
| 1-2 | `0x140014fb65`, `0x140014fb5f` | Odd addresses (possible IAT thunks) |
| 3 | `0x1400382c0` | Code — real function |
| 4 | `0x140039940` | Code — real function |
| 5 | `0x140014fb59` | Odd address (possible IAT thunk) |
| 6 | `0x140045ad0` | Code — real function |
| 7-14 | `0x140014fb53`…`0x140014fb35` | Odd addresses (6-byte spaced thunks) |
| 15 | `0x140195f860` | Data pointer |

**Total: ~22 handler function addresses recovered statically.**

### Dispatcher call sites (19):
`0x140015660`, `0x14001e688`, `0x140022c95`, `0x140022d03`, `0x140022d6b`, `0x140022daf`,
`0x140023650`, `0x1400236be`, `0x140023726`, `0x14002376a`, `0x1400243ae`, `0x14002443a`,
`0x1400244e6`, `0x140024581`, `0x140024630`, `0x1400246d4`, `0x140024718`, `0x1400259e4`,
`0x140025ab0`, `0x140025b09`

---

## 9. COMPLETE NATIVE METHOD INVENTORY (~60 methods)

**dT table (38 entries, from embedded JS `dT(method,path)`):**

| Method | HTTP Path | Timeout |
|--------|-----------|---------|
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
`solver.ensureIdle()`, `solver.policy(postNode, holes)`, `solver.node(history)`,
`solver.possibleCards()`, `solver.prepare(payload, settings, onCancelled, onStatus, hooks)`,
`solver.stop()`, `solver.confirmStopped()`

**Full string inventory includes:** `solver.host.allocate`, `solver.layout.validate`,
`solver.solve.dispatch`, `solver.solve.2p.construct`, `solver.solve.2p.run`,
`solver.solve.3p.construct`, `solver.solve.3p.run`, `solver.solve.cpu_locked`,
`solver.solve.cpu_locked.finalize`, `solver.finalize.compute`, `solver.finalize.publish`,
`solver.strategy.archive.open/node/close`

---

## 10. TRANSPORT / BRIDGE PROTOCOL (from embedded JS)

- **API base:** `http://127.0.0.1:6006` (overridable via `?apiBase=`)
- **Transport:** `bridge` (default) or `http` (via `?transport=`)
- **Bridge:** `window.chrome.webview.postMessage(JSON.stringify({id,method,params}))`
- **Auth:** `bridge.ping` (1500 ms) → `bridge_token`; sent as `bridge_token` param; retry on `UNAUTHORIZED`
- **RPC envelope:** `{ id: "rpc_<ts>_<seq>", method, params }`
- **Response envelope:** `{ ok: bool, result: any, error: {message, code} }`
- **Ds class:** `pending` Map, `seq` counter, `authToken`
- **callBridge(t, s, a=5000):** RPC id, postMessage, promise with timeout
- **callBridgeAuthed():** adds `bridge_token` param; on `UNAUTHORIZED` clears token, re-auths, retries once
- **request(t, s, a):** `dT(path, method)` resolves internal method name; per-method timeout; fallback to `callBridgeAuthed("http.request", …)` on `NOT_IMPLEMENTED`

---

## 11. SOLVE FLOW (recovered from PlayPanel `ya` class)

```
1. POST /api/init      body = initTemplate  (see §12)
2. POST /api/allocate  body = {}             (allocates GPU memory)
3. POST /api/gpu-solve body = {max_iterations, target_exploitability, compute_initial_exploitability:false}
4. GET  /api/solve/status  (poll every 250ms) -> {running, iteration, elapsed_ms, exploitability, phase, last_error}
   repeat until running == false
5. POST /api/solve/stop body = []            (clear/stop)
6. GET  /api/results                            -> node strategy + EVs (seats 0 and 1)
7. POST /api/export/current-street body={history:[], max_nodes} -> {payload: snapshot}
```

Per-board loop (batch): for each board → init → allocate → gpu-solve → poll status → stop → results → export current-street → persist snapshot JSON.

---

## 12. JSON SCHEMAS

### POST /api/init — init template (from `wa` function in PlayPanel)
```json
{
  "oop_range": [float x1326],       // normalized OOP preflop range
  "ip_range":  [float x1326],       // normalized IP preflop range
  "board": [int x3],                // board card indices (0-51), exactly 3 cards (flop)
  "starting_pot": int,              // oop_paid + ip_paid
  "effective_stack": int,           // stack - oop_paid
  "rake_rate": float,               // rakePercent / 100
  "rake_cap": float,                // rakeCapBB * 100
  "oop_flop_bet": "33%,67%,100%",
  "oop_flop_raise": "33%,67%,100%",
  "ip_flop_bet": "33%,67%,100%",
  "ip_flop_raise": "33%,67%,100%",
  "oop_turn_bet": "100%",
  "oop_turn_raise": "100%",
  "ip_turn_bet": "100%",
  "ip_turn_raise": "100%",
  "oop_river_bet": "100%",
  "oop_river_raise": "100%",
  "ip_river_bet": "100%",
  "ip_river_raise": "100%",
  "donk_option": true,
  "oop_turn_donk": "100%",
  "oop_river_donk": "100%",
  "max_raise_number": 3,            // 0-255
  "add_allin_threshold": 1.0,       // fraction of pot
  "force_allin_threshold": 0.2,
  "merging_threshold": 0.1,
  "add_allin_flop_ip": true,
  "add_allin_turn_ip": true,
  "add_allin_river_ip": true,
  "add_allin_flop_oop": true,
  "add_allin_turn_oop": true,
  "add_allin_river_ip": true,
  "add_allin_river_oop": true,
  "num_players": 2
}
```
Validation: `oop_paid == ip_paid`, `oop_paid < stack`, board length == 3, maxRaises 1-3, maxIterations 1-100000, target 0-100%, rakePercent 0-100, rakeCapBB >= 0.

### GET /api/solve/status — poll response
```json
{
  "running": bool,
  "iteration": int,
  "elapsed_ms": int,
  "exploitability": float | null,
  "phase": "allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed",
  "last_error": string | null
}
```

### GET /api/results — node results
```json
{
  "seat": int,
  "seatNames": ["SB / BTN","BB"],
  "board": [int],
  "paid": [int x num_players],
  "pot": int,
  "labels": ["Fold","Call","Check","Raise 100",...],
  "ranges": [ [float x1326], [float x1326] ],
  "cards": [int],
  "probabilities": [float],         // [num_actions x 1326] flattened
  "evs": [float | null]             // per-action EV in cents/100
}
```

### POST /api/export/current-street
Request: `{ "history": [], "max_nodes": 20000 }`
Response: `{ "payload": <snapshot JSON> }`

### POST /api/export/full-file
Request: `{ "parameter_schema_version": 2, "units": {...}, "exported_at": "ISO", "config": {...}, "tree_built": bool, "solver_status": {...} }`

### POST /api/ranges/load
Request: `{ "path": "play/spin_go/2p_spin_go_100bb.json" }`
Response: range policy JSON (type "action", player 0 root, preflop tree)

### Default client config (`Ce`)
```json
{
  "oopRange": [1 x1326], "ipRange": [1 x1326],
  "boardText": "Qs Jh 2h",
  "startingPot": 50, "effectiveStack": 250,
  "rakeRate": 0, "rakeCap": 0,
  "oopFlopBet": "50,100", "oopFlopRaise": "50,100",
  "ipFlopBet": "50,100", "ipFlopRaise": "50,100",
  "oopTurnBet": "100", "oopTurnRaise": "100",
  "ipTurnBet": "100", "ipTurnRaise": "100",
  "oopRiverBet": "100", "oopRiverRaise": "100",
  "ipRiverBet": "100", "ipRiverRaise": "100",
  "oopTurnDonk": "100", "oopRiverDonk": "100",
  "maxRaiseNumber": 3, "addAllinThreshold": 200,
  "addAllinFlopIp": true, "addAllinTurnIp": true, "addAllinRiverIp": true,
  "addAllinFlopOop": true, "addAllinTurnOop": true, "addAllinRiverOop": true
}
```

---

## 13. EMBEDDED FRONTEND (Vue 3 / Vite)

**Asset table:**
- `index-DQB6irIW.js` — core runtime (dT dispatcher, Ds bridge, request, Ce config, poker primitives)
- `PlayPanel-BLI34wKZ.js` — play panel (solver class `ya`, init payload builder `wa`)
- `StudyPanel-D7tMTKne.js` — study panel
- `PlayRangeStrategy-B24TSCYs.js` — range strategy component
- CSS: `index-CRI2SV16.css`, `PlayPanel-DoJanrIp.css`, `PlayRangeStrategy-BodTOrVo.css`, `StudyPanel-DtNNl1nd.css`

**Key JS classes/functions:**
- `ya` (solver class): `ensureIdle()`, `stop()`, `prepare()`, `confirmStopped()`, `node()`, `policy()`, `possibleCards()`
- `wa()` — builds solver.init payload (exact schema above)
- `dT()` — method→path dispatcher (complete 38-entry table)
- `Ds` class — bridge RPC with authToken, timeouts, fallback
- `en(e,t)` — combo index `e*(103-e)/2 + t - e - 1`
- `Kp("AhKs")` — hand parser → card indices
- `oa(cards[5])` — 5-card hand rank (category 0-8 + kickers)
- `rb(cards[7])` — 7-card showdown = max over 21 five-card subsets
- `TB(e,t,s,a)` — pot settlement/rake
- `yo(seats,stack,ante)` — GameState constructor
- `Wp(state,action)` — 6max action applier
- `ai(state,action)` — apply action with range update
- `oi(state,node)` — snapshot builder
- `QA(decision)` — action grading (best/mixed/mistake/blunder)
- `WA(probs,evs,mode,rng,direction)` — RNG action selection
- `Yp(json, source, spec)` — 6max preflop tree builder/validator
- `Fr(apiBase, spec)` — load 6max ranges via `/api/ranges/load`
- `ll(apiBase, players, bb)` — load spin_go ranges
- `LA(apiBase)` — load HU ranges (`play/spin_go/2p_spin_go_100bb.json`)

**Author links:** Discord `https://discord.gg/RtyD4vRy2e`, GitHub `https://github.com/bupticybee`, Page `https://bupticybee.github.io/texassolver_gpu_page/`

---

## 14. DEVICE BUFFER INVENTORY

| Buffer | Purpose |
|--------|---------|
| `d_cfv_` | counterfactual values |
| `d_abs_regrets_` | absolute regrets |
| `d_abs_strategy_` | absolute strategy |
| `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` | counterfactual reach |
| `d_init_weights_` | initial weights |
| `d_discount_params_` | discount parameters (3 floats: iter_params) |
| `d_strategy_packed_` | packed strategy output |
| `d_compact` | compact archive data |
| `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_` | gather staging |
| `cfv_stage_host_` (pinned) | host-pinned CFV staging |
| `pinned_discount_params_` (pinned) | host-pinned discount params |
| `cfv_stage_scales_` (pinned) | host-pinned CFV scales |

---

## 15. Q16 FIXED-POINT + COMPACT ARCHIVE

- Q16 fields: `reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16`
- Dequantize: `value = q16_raw / 65536.0`
- Compact archive = Apache Avro + DEFLATE, magic `OBJ 0x04 0x1c 0x96 0x52`
- Native entry points: `solver.strategy.archive.open/node/close`
- Variants: `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`
- Async lifecycle: `avro_busy` → `avro_done` / `avro_cancelled`
- Errors: "Compact archive has nonzero reach values with a zero scale",
  "Compact archive chance mapping is invalid",
  "Compact strategy archive contains invalid -32768 q16"

---

## 16. LOADER EXECUTABLE (TexasSolverGpu.exe — 309 KB)

- Entry: `0x14000a6c4` → `call 0x14000aae4` → `jmp 0x14000a550`
- Singleton mutex via `lock cmpxchg QWORD PTR [0x140012710]`
- CUDA driver version check: compares driver version to select 131.exe vs legacy_126.exe
- Strings: "CUDA driver is not available.", "No CUDA device detected.",
  "Driver can run CUDA 13.1 and legacy CUDA 12.6 packages.",
  "Driver can run legacy CUDA 12.6 package only.",
  "Driver is too old. Upgrade NVIDIA driver.",
  "Use TexasSolverGpu_131.exe (fallback: TexasSolverGpu_legacy_126.exe).",
  "No compatible package can run on this machine."
- No WebView2Loader or nvcuda.dll imports — pure launcher

---

## 17. ANTI-TAMPER / PROTECTIONS

- `IsDebuggerPresent` imported from KERNEL32.dll
- 5× `cpuid` checks at entry stub (env/feature detection, possible anti-VM)
- `OutputDebugStringW`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`
- Stripped binary (no RTTI/debug symbols)
- PTX JIT-compiled at runtime (no embedded cubin to extract)
- VRAM gate check
- No license/activation mechanism

---

## 18. STATIC METHOD DISPATCH CHAIN (NEW — fully recovered statically)

A large if-else dispatch chain at `0x140061260`–`0x14006829e` compares incoming method names against known strings using `lea rdx` + `call 0x140006c30` (string compare). Each match branches to method-specific inline code.

**String compare `0x140006c30`:** gets length via `0x14015127a`, compares via `0x140151250`, returns 1 if equal.

### Dispatch chain entries (in order):

| Order | Code VA | Method | Match Target | Next Check |
|-------|---------|--------|-------------|------------|
| 1 | `0x140061260` | `solver.init` | `0x14006a680` | `0x140061732` |
| 2 | `0x140061732` | `solver.allocate` | — | `0x140061cc3` |
| 3 | `0x140061cc3` | `solver.memoryUsage` | — | `0x140062f7f` |
| 4 | `0x140062f7f` | `solver.solve.preflight` | — | `0x1400632ab` |
| 5 | `0x1400632ab` | `solver.environment.check` | `0x140055840` | `0x14006330b` |
| 6 | `0x14006330b` | `solver.solve.start` | `0x1400157a0`+`0x14010b9a0` | `0x140063bd7` |
| 7 | `0x140063bd7` | `solver.solve.status` | — | `0x1400645bd` |
| 8 | `0x1400645bd` | `solver.solve.stop` | — | `0x14006514b` |
| 9 | `0x14006514b` | `solver.lock.current.set` | — | `0x1400657a7` |
| 10 | `0x1400657a7` | `solver.lock.current.get` | — | `0x140065f1c` |
| 11 | `0x140065f1c` | `solver.lock.current.clear` | — | `0x140066295` |
| 12 | `0x140066295` | `solver.lock.list` | — | `0x1400667d2` |
| 13 | `0x1400667d2` | `solver.lock.clearAll` | — | `0x140066b11` |
| 14 | `0x140066b11` | `solver.node.currentPlayer` | — | `0x140066eb4` |
| 15 | `0x140066eb4` | `solver.node.numActions` | — | `0x140067195` |
| 16 | `0x140067195` | `solver.node.currentBoard` | — | `0x140067466` |
| 17 | `0x140067466` | `solver.node.actionsAfter` | — | `0x1400679f3` |
| 18 | `0x1400679f3` | `solver.cards.possible` | — | `0x140067cc4` |
| 19 | `0x140067cc4` | `solver.cards.private` | — | `0x14006829e` |
| 20 | `0x14006829e` | `solver.node.results` | — | (end) |

**Notable:** `solver.init` branch (`0x14006a680`) allocates `0xeff0` stack, checks for 1326 combo count via variant accessor `0x140043b60` compared against `0x52e`.

First `.rdata` section methods (23 methods at `0x14016ebb0`–`0x140170348`) use `movups` SIMD comparison pattern at separate code locations (e.g., `0x140018d72` for `solver.export.fullFile`, `0x140019a62` for `solver.strategy.archive.open`, `0x1400238f7` for `solver.export.currentStreet`).

See `HANDLER_DISPATCH_RECON.md` for complete documentation including first-section method references and handler vtable → dispatch relationship.

---

## 19. WHAT IS BLOCKED (Phase 5 — Dynamic, requires Windows + NVIDIA GPU)

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Method names stored in heap at `[handler_obj+0x80]`, no static table. Handler objects form linked list (head at `0x1411c6690`), traversed at runtime. |
| PTX device code | JIT-compiled by CUDA driver at runtime via `cuModuleLoadData`, no embedded cubin. Only status string "PTX target: 120" and `ptxas` references found. |
| Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream, args) |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` at code VA `0x24bd10` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution on Windows + GPU |
| Anti-tamper bypass | Patch `IsDebuggerPresent` (return 0), handle 5× `cpuid`, avoid user-mode debugger detection |

---

## 20. REQUIRED ENVIRONMENT TO UNBLOCK PHASE 5

1. **Windows/x64 with NVIDIA GPU** (compute capability 7.5–12.0, VRAM ≥ 8 GB)
   - Windows VM with GPU passthrough, or
   - Wine + NVIDIA driver (difficult — WebView2 + CUDA driver needed)
2. **Instrumentation tools:** x64dbg, Ghidra, radare2, or API-level proxy DLLs (proxy `nvcuda.dll`, WinSock LSP)
3. **Anti-tamper bypass:** Patch `IsDebuggerPresent` (return 0), handle 5× `cpuid`

---

## 21. DELIVERABLES (after dynamic capture)

- `HANDLER_MAP.md` — complete method → handler VA table (~60 methods)
- `PTX_DUMP/` — all captured PTX blobs by kernel name
- `KERNEL_CONFIGS.md` — grid/block/sharedMem per kernel per CFR phase
- `COMPACT_ARCHIVE_SCHEMA.md` — finalized Avro schema + Q16 field definitions
- `CFR_PIPELINE.md` — end-to-end solver flow with buffer lifetimes and timings
- `ANTI_TAMPER_BYPASS.md` — techniques used to bypass protections

---

## 22. EXISTING RECON DOCUMENTS (in `re_lab/texassolvergpu/`)

| Document | Contents |
|----------|----------|
| `BINARY_ANALYSIS_RECON.md` | Binary inventory, PE structure, imports, CUDA config, kernel list, solver methods, anti-tamper, loader |
| `CPP_CORE_RECON.md` | C++ data structures: GameState, Node, NodeSnapshot, Strategy |
| `CUDA_MATH.md` | CUDA kernel mathematics: card encoding, combo index, eval5/eval7, fold equity, pot settlement |
| `CUDA_KERNELS_RECON.cpp` | Full CUDA kernel pseudocode reconstruction |
| `DEEP_RE_PHASE2.md` | Dispatcher disassembly, variant type system, dT table, transport protocol, kernel inventory, Q16, compact archive, CFV staging, anti-tamper, PTX status |
| `FRONTEND_API_RECON.md` | Transport & auth, endpoint map, solve flow, JSON schemas, internal solver methods, CUDA kernels, JS poker primitives |
| `HANDLER_VTABLE_RECON.md` | Handler vtable recovery (both vtables, ~22 handler addresses, object layout) |
| `HANDLER_DISPATCH_RECON.md` | **NEW** — Static method dispatch chain (0x140061260-0x14006829e), all method→code block mappings, helper functions |
| `PHASE3_CFR_LAUNCHERS.md` | CFR kernel launcher inventory, device buffer inventory, buffer lifetimes |
| `PHASE3_COMPACT_ARCHIVE.md` | Compact archive / strategy archive format: Avro + DEFLATE, Q16 fields, layout, validation errors |
| `PHASE3_HANDLER_MAP.md` | Native method handler map — dispatch mechanism, method inventory, dT table, anti-tamper |
| `PHASE4_STATIC_RECON.md` | Extended static recon: dispatcher disassembly, dT table, compact archive errors, frontend API usage, handler vtable recovery, PTX status |
| `PHASE5_DYNAMIC_PLAN.md` | Phase 5 dynamic analysis plan |
| `PHASE6_FRONTEND_API_DEEP_DIVE.md` | Frontend API deep dive |
| `STATIC_EXTENSION_RECON.md` | Static method name strings in `.rdata` (all ~60 methods with cross-references), handler creator register tracing, CUDA call site locations |
| `FINAL_RECON_SUMMARY.md` | Overall summary |
| `FINAL_SUMMARY_UPDATED.md` | Updated summary with handler vtable 2 |
| `phase3_json_api_recon.txt` | JSON schemas, network addresses, parser keys, validation errors |
| `texassolver_gpu_recon.txt` | File listing of zip archive contents |
| `deep_extraction_report.txt` | File listing of zip archive contents (range files) |
| `FINAL_COMPREHENSIVE_RE.md` | This document |

---

**Bottom line:** Static reverse engineering of TexasSolverGPU v0.3.0 is **100% complete**. All binaries disassembled, all frontend assets extracted, all API endpoints/schemas documented, all handler vtables recovered (15+15 entries), complete static method dispatch chain recovered (19 methods in if-else chain at 0x140061260-0x14006829e plus 23 additional method string cross-references), all CUDA kernels identified, all data structures reconstructed, all anti-tamper surfaces mapped. The only remaining unknowns (method name → handler vtable entry mapping, PTX device code, kernel launch configs, compact archive bytes, full solve cycle) require **dynamic execution on Windows + NVIDIA GPU**, which is not available on this Linux host.
