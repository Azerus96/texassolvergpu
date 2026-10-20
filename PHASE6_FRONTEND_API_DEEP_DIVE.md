# PHASE6_FRONTEND_API_DEEP_DIVE.md
# TexasSolverGPU v0.3.0 — Phase 6: Embedded Frontend & API Protocol Deep-Dive

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static extraction of the embedded Vite frontend bundle packed in `.rdata`; recovered directly from minified JS strings (offsets via `strings -t x`).
**Status:** Complete. Complements Phases 1–5 (static RE 100%; Phase 5 dynamic still BLOCKED — no Windows/x64 + NVIDIA GPU).

---

## 6.1 Embedded frontend bundle location

The `.rsrc` section holds only icons/manifest. The frontend is **not** a resource — it is packed as a virtual filesystem inside `.rdata` (file VMA `0x14016d000`, size `0x105346e`), intermixed with C++ runtime strings.

Extracted blob (file offset `0x22a000`+) contains, in order:
- Minified Vue 3 + Vite app JS (main bundle)
- `index.html` (blob offset `0x245e90` → file `0x245e98`)
- Asset path/mime table listing:
  - `/assets/index-DQB6irIW.js` (main entry, loaded by `<script type="module">`)
  - `/assets/index-CRI2SV16.css`
  - `/assets/PlayPanel-BLI34wKZ.js` + `.css`
  - `/assets/PlayRangeStrategy-B24TSCYs.js` + `.css`
  - `/assets/StudyPanel-D7tMTKne.js` + `.css`
  - `/index.html`

The main bundle (`frontend_blob.bin`) was recovered in full and contains the App, router, the `dT` router, the `Ds` bridge, and the solve-request builder `ms()`.

---

## 6.2 Application bootstrap (App component)

- `apiBase` defaults to `http://127.0.0.1:6006` (overridable via `?apiBase=` URL param).
- `transport` defaults to `bridge` (overridable via `?transport=http`).
- Transport selector: `bridge` → `Pl` (Ds bridge); `http` → `Jr` (native fetch); mixed → `Qg(new Pl, new Jr)` fallback chain.
- Author links: Discord `https://discord.gg/RtyD4vRy2e`, GitHub `https://github.com/bupticybee`, page `https://bupticybee.github.io/texassolver_gpu_page/`; author "bupticybee".
- Default quick-start config `Ce` (see §6.5): preflop HU-ish spot `Qs Jh 2h`, pot 50, eff 250, max 3 raises.
- `desktop` mode when `?desktop=1`.

---

## 6.3 Transport / bridge protocol (class `Ds`)

Native bridge = WebView2 `window.chrome.webview.postMessage(JSON.stringify(...))`.

- `Ds.callBridge(method, params, timeout=5000)`:
  - RPC id `rpc_${Date.now()}_${seq++}`; envelope `{id, method, params}`; returns Promise; per-call timeout → `BRIDGE_TIMEOUT`.
- `Ds.ensureAuthToken()`: `callBridge("bridge.ping", {}, 1500)` → `result.bridge_token` stored as `authToken`.
- `Ds.callBridgeAuthed(method, params, timeout)`: merges `bridge_token`; on `UNAUTHORIZED` clears token, re-pings, retries once.
- `Ds.request(apiBase, path, {method, body})`:
  - Resolves internal method name via `dT(path, method)`.
  - Per-method timeout (see §6.4).
  - Tries native bridge `callBridgeAuthed(method, {apiBase,path,method,body,...})`; on `NOT_IMPLEMENTED` falls back to `callBridgeAuthed("http.request", {apiBase,path,method,body})`.
  - Response envelope `{ok: bool, result: any, error: {message, code}}`; throws `BRIDGE_ERROR` on `!ok`.
- `Ds.probe(timeout=180)`: `bridge.ping` health check.

---

## 6.4 Method → HTTP-path routing (`dT(e, t)`) — 38 methods

| Method | HTTP | Timeout |
|---|---|---|
| `solver.init` | POST /api/init | 120s |
| `solver.allocate` | POST /api/allocate | 120s |
| `solver.memoryUsage` | GET /api/memory-usage[?compress=*] | 30s |
| `solver.environment.check` | GET /api/environment/check | 5s |
| `solver.solve.start` | POST /api/gpu-solve | 30s |
| `solver.solve.preflight` | POST /api/solve/preflight | 30s |
| `solver.export.currentStreet` | POST /api/export/current-street | 120s |
| `desktop.strategy.exportCurrentStreetFile` | POST /api/export/current-street-file | null |
| `desktop.strategy.exportFullFile` | POST /api/export/full-file | null |
| `desktop.strategy.exportFullFileProgress` | GET /api/export/full-file/progress | null |
| `desktop.strategy.importFullFile` | POST /api/import/full-file | null |
| `desktop.strategy.importNode` | POST /api/import/node | 30s |
| `desktop.strategy.closeImport` | POST /api/import/close | null |
| `solver.solve.status` | GET /api/solve/status | 5s |
| `solver.solve.stop` | POST /api/solve/stop | 5s |
| `solver.history.apply` | POST /api/apply-history | 5s |
| `solver.node.results` | GET /api/results | 5s |
| `solver.node.actionsAfter` | POST /api/actions-after | 5s |
| `solver.node.currentPlayer` | GET /api/current-player | 5s |
| `solver.node.currentBoard` | GET /api/current-board | 5s |
| `solver.lock.current.set` | POST /api/lock/current | 5s |
| `solver.lock.current.get` | GET /api/lock/current | 5s |
| `solver.lock.current.clear` | POST /api/lock/unlock | 5s |
| `solver.lock.list` | GET /api/lock/list | 5s |
| `solver.lock.clearAll` | POST /api/lock/clear | 5s |
| `solver.cards.possible` | GET /api/possible-cards | 5s |
| `solver.node.numActions` | GET /api/num-actions | 5s |
| `desktop.parameters.export` | POST /api/parameters/export | 5s |
| `desktop.parameters.openFolder` | POST /api/parameters/open-folder | 5s |
| `desktop.external.open` | POST /api/external/open | 5s |
| `desktop.parameters.load` | POST /api/parameters/load | 5s |
| `desktop.ranges.load` | POST /api/ranges/load | 5s |
| `desktop.settings.load` | POST /api/settings/load | 5s |
| `desktop.settings.save` | POST /api/settings/save | 5s |
| `desktop.parameters.list` | GET /api/parameters/list?path= | 5s |
| `desktop.ranges.list` | GET /api/ranges/list?path= | 5s |
| `solver.cards.private` | GET /api/private-cards/<n> | 5s |

Default timeout 5000 ms. Methods called directly on `this.solver` (bypass dT/HTTP): `solver.ensureIdle()`, `solver.policy(postNode, holes)`, `solver.node(history)`, `solver.possibleCards()`, `solver.prepare(payload, settings, onCancelled, onStatus, hooks)`, `solver.stop()`, `solver.confirmStopped()`.

---

## 6.5 Solve-request schema (function `ms(b)` + `wt(b)` + `es(b)`)

`ms(b)` builds the payload for `solver.prepare`/`solver.solve.start`. Fields (defaults from `Ce`):

```
oopRange        : number[1326]   (0..1 probabilities; built by es() validator)
ipRange         : number[1326]
boardText       : string         ("Qs Jh 2h")
startingPot     : number         (50)
effectiveStack  : number         (250)
rakeRate        : number         (0)
rakeCap         : number         (0)
oopFlopBet      : string         ("50,100")
oopFlopRaise    : string         ("50,100")
ipFlopBet       : string
ipFlopRaise     : string
oopTurnBet      : string         ("100")
oopTurnRaise    : string         ("100")
ipTurnBet       : string
ipTurnRaise     : string
oopRiverBet     : string         ("100")
oopRiverRaise   : string         ("100")
ipRiverBet      : string
ipRiverRaise    : string
oopTurnDonk     : string         ("100")
oopRiverDonk    : string         ("100")
maxRaiseNumber  : number         (3, clamped 0..255)
addAllinThreshold: number        (200; <=10 -> *100)
addAllinFlopIp/TurnIp/RiverIp/FlopOop/TurnOop/RiverOop : boolean (true)
```

`es(b)` validates/normalizes a range to `number[1326]` (0/fallback on invalid).
`wt(b)` chip-scaling: `Vp([startingPot, effectiveStack, rakeCap])` → `chipScale`; if `!==1`, scales `startingPot`/`effectiveStack`/`rakeCap` via `Ea(x, scale)`; throws `quick_start.chip_scale_failed` if no common scale.
Request returned as `{config, chipScale}`.

`ce()` default `Ce` confirms UI preflop ranges `oopRange=ei(50,15)`, `ipRange=ei(30,0)` (helper `ei(lo,hi)` builds a linear 1326 ramp).

---

## 6.6 PioSOLVER config importer (`fT(text, defaults)`)

Parses `#key=value` / `#key value` lines (Pio `.cfg` style):
- Required: `Range0` (oop), `Range1` (ip) via `bd()` → `qa()` range parser.
- `Pot`, `EffectiveStacks`, `Rake.Fraction`, `Rake.Cap` → numbers.
- `FlopConfig.BetSize/RaiseSize`, `TurnConfig.*`, `RiverConfig.*` → comma size lists via `Ts()` (accepts `.` or `,` decimals).
- `TurnConfigIP.*`, `RiverConfigIP.*` for IP street sizes.
- `AddAllinOnlyIfLessThanThisTimesThePot`, `AddAllin` flags (bool via `il()`).
- Merges over defaults `t`.

---

## 6.7 Data formats

**Range file** (`*.txt`): comma-separated `Hand:prob` pairs, e.g.
`AA:1.0,AKs:1.0,A9o:0.374,22:0.128,K5s:0.9,...` — 1326 combos, Pio notation (suffix `s`=suited, `o`=offsuit, bare=pair). Each combo maps to one index via combo-index `en(e,t)=e*(103-e)/2+t-e-1` [0,1325]. Card encoding: `rank=c/4`, `suit=c%4`, packed `c|(p<<8)`.

**Game tree JSON** (`ranges\play\spin_go\2p_spin_go_20bb.json`): recursive `Node` — `{betting round, childrens[], player, pot, type, strategy:{card_strings[]}}`; `type` ∈ {chance, action}; terminal nodes have `childrens:null`. Matches C++ `Node` struct (actor, player, bettingRound, tableCards, pot, valid_actions[], strategy*, comboNode*, childrens[], type).

**Q16 fixed point:** `reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16`; dequant `value = q16_raw / 65536.0`.

**Compact strategy archive:** Apache Avro + DEFLATE; magic `OBJ 0x04 0x1c 0x96 0x52`; variants `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`; entry points `solver.strategy.archive.open/node/close`; lifecycle `avro_busy → avro_done / avro_cancelled`. Supports 1–3 players. HTTP error codes seen: `OK`, `BAD_REQUEST`, `INTERNAL_ERROR`.

**VRAM/environment stats structure:** `{count, devices[], version_raw, cc_major, cc_minor, cc, vram_bytes, vram_gb, estimated_peak_bytes, total_bytes, free_bytes, headroom_bytes, reserve_bytes, available, reason, source}`; statuses `recommended/fallback/unknown`, colors `red/yellow/green`. VRAM gate: "VRAM is below 8 GB; large trees may fail."

---

## 6.8 Anti-tamper / startup guards (static)

- Entry `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid` — env/feature detection, possible anti-VM).
- `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`.
- Package selection at launch: compares CUDA driver version; strings "CUDA driver is not available.", "No CUDA device detected.", "Driver can run CUDA 13.1 and legacy CUDA 12.6 packages.", "Driver can run legacy CUDA 12.6 package only.", "Driver is too old. Upgrade NVIDIA driver.", "Use TexasSolverGpu_131.exe (fallback: TexasSolverGpu_legacy_126.exe).", "No compatible package can run on this machine."
- No license/activation mechanism found.
- `nvcuda.dll` + NVML loaded dynamically (`LoadLibraryW`/`GetProcAddress`).

---

## 6.9 What remains (Phase 5 dynamic — still BLOCKED)

Requires Windows/x64 + NVIDIA GPU (CC 7.5–12, VRAM ≥ 8 GB):
1. Handler map (~60 methods): hook dispatcher `0x140014cf0` + lookup `0x1400099e0` + creator `0x140036d70` to log `(method_name, handler_VA)`. No static {name→ptr} table exists (resolved via linked list + IAT at runtime).
2. PTX dump: hook `cuModuleLoadData[Ex]`; PTX target sm_120, no embedded cubin; 40+ kernels (`evaluate_*`, `backward_*`, `forward_*`, `train_*`, `river_*`, `Batch*`).
3. Kernel configs: hook `cuLaunchKernel` (grid/block/smem/stream); block size 256; streams `stream_`,`stream2_`,`stream_dl_`,`stream_file_`; CUDA Graph `training_graph_exec_`.
4. `iter_params` (3 discount floats): hook `cuMemcpyHtoDAsync(d_discount_params_)`.
5. Compact archive bytes: trigger `POST /api/export/full-file` format=avro; capture `solver.strategy.archive.open/node/close`.
6. Full solve cycle + response schemas: `init→allocate→gpu-solve→poll status→stop→results→export/current-street`; `solver.policy` → `{probs: Float32Array[1326], evs}`; `solver.possibleCards` → bitmask BigInt.
7. Anti-tamper bypass: patch `IsDebuggerPresent`→0, handle 5× `cpuid`, prefer API-level proxy (`nvcuda.dll`, WinSock LSP) over user-mode debugger. Wine test failed `c0000135` (missing `nvcuda.dll`/WebView2, no GPU).

Deliverables after dynamic capture: `HANDLER_MAP.md`, `PTX_DUMP/`, `KERNEL_CONFIGS.md`, `COMPACT_ARCHIVE_SCHEMA.md`, `CFR_PIPELINE.md`, `ANTI_TAMPER_BYPASS.md`.

---

## 6.10 Author & source references

Author: bupticybee; Discord `https://discord.gg/RtyD4vRy2e`; GitHub `https://github.com/bupticybee`; page `https://bupticybee.github.io/texassolver_gpu_page/`.
Build: CUDA 13.1 (`ptxas V13.1.115`) + legacy 12.6; compiled targets `75;80;86;89;90;120`; PTX target sm_120 (forward JIT).
Source paths: `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\{gpu_solver.cpp,gpu_solver_3p.cpp,cuda_driver_api.cpp}`, `include\poker\{cuda_buffer.h,cuda_host_memory.h}`.
Launcher note: "Use TexasSolverGpu_131.exe (fallback: TexasSolverGpu_legacy_126.exe)."
