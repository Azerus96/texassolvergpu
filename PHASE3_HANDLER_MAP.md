# PHASE3_HANDLER_MAP.md
# TexasSolverGPU v0.3.0 — Phase 3: Native Method Handler Map

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64/TexasSolverGpu_131.exe` (ImageBase `0x140000000`, stripped)
**Status:** Static recovery of method *names* and dispatch *mechanism*. Handler vtable + 15 handler function addresses recovered statically (`HANDLER_VTABLE_RECON.md`). Per-method native handler **address mapping** was NOT recovered statically (method names in heap memory at `[handler_obj+0x80]`). Dynamic instrumentation required (see §8).

Source: `strings -n 5` (`all_strings.txt`), DEEP_RE_PHASE2.md §2 (dispatcher), FRONTEND_API_RECON.md §2-§5 (dT table + transport), BINARY_ANALYSIS_RECON.md §7-§8.

---

## 1. Dispatch mechanism (native)

- **Entry point:** `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474`. Contains 5× `cpuid` (env/feature detection, possible anti-VM).
- **Dispatcher:** `0x140014cf0` — signature `(output*, input_kv_pairs, flags r8b, r8b)`. Walks 0x18-byte key/value entries (`rbx` from `rdi` to `r15`). For each entry: resolves key pointer, requires type byte `0x2` (key), calls `0x140043b60` (variant accessor; jump table at VA `0x140043ba4`), requires result `0x2`, then matches a type-`0x3` string value (the method name).
- **Variant accessor `0x140043b60`:** tag 0→0; tag 1→element count/16; tag 2→data pointer; tags 3–9/default→1.
- **Request builder:** `0x140016210` (type dispatch on `dl`; jump table at VA `0x140016370`; type 0→`0x14000dde0`; types 1–3 allocate 0x18/0x20/0x28-byte variants; type 4 null-terminates; 5+ null).
- **Handler pointer storage:** resolved handler stored at `0x8(%r12)`, invoked via `0x140001840`.
- **Helpers:** `0x1400099e0` (key lookup over linked list of kv pairs, stride 0x10, compare via `0x140151250`); `0x140001840`/`0x1400017d0` (variant string copy, SSO vs heap, allocator `0x140151238`); `0x14001ef90` (string length, wrapper `0x140005c30`).
- **Wrapper `0x140005c30`:** initializes output, appends method name, allocates 0x20-byte buffer, looks up handler.

> The handler *address table* itself was not recovered as a static structure. The dispatch resolves `method name → handler ID → handler address` at runtime through `0x140014cf0`/`0x140001840`. Instrument these functions to log each `(method name, handler address)` pair.

---

## 2. Complete native method inventory

Recovered string symbols, grouped. HTTP-equivalent paths per FRONTEND_API_RECON.md.

### 2.1 Init / allocate / environment
| Method | HTTP endpoint | Notes |
|--------|---------------|-------|
| `solver.init` | POST /api/init | Builds & uploads solve init template (ranges, pot, stack, bet sizes) |
| `solver.allocate` | POST /api/allocate | Allocates GPU solver memory after init |
| `solver.host.allocate` | — | NEW (Phase 2) — host-side allocation |
| `solver.memoryUsage` | GET /api/memory-usage | GPU memory report |
| `solver.environment.check` | GET /api/environment/check | Driver/GPU/CUDA probe |
| `solver.layout.validate` | — | NEW — validates GPU tree/layout capacity |
| `solver.confirm` | — | NEW — confirmation/ack method |
| `solver.ensure` | — | NEW — ensure-idle / precondition |
| `solver.prepare` | — | NEW — prepare solve |
| `solver.policy` | — | NEW — policy query |
| `solver.practice` | — | NEW — play/drill mode |
| `solver.stop` | — | NEW |
| `solver.may` | — | NEW |

### 2.2 Solve (2p / 3p / dispatch / CPU-locked)
| Method | HTTP endpoint | Notes |
|--------|---------------|-------|
| `solver.solve.start` | POST /api/gpu-solve | `{max_iterations, target_exploitability, compute_initial_exploitability}` |
| `solver.solve.status` | GET /api/solve/status | Poll: running/iteration/exploitability/phase |
| `solver.solve.stop` | POST /api/solve/stop | Request stop |
| `solver.solve.preflight` | POST /api/solve/preflight | Preflight check |
| `solver.solve.dispatch` | — | NEW — routes 2p vs 3p |
| `solver.solve.2p.construct` | — | 2-player tree construction |
| `solver.solve.2p.run` | — | 2-player GPU solve launch |
| `solver.solve.3p.construct` | — | 3-player tree construction |
| `solver.solve.3p.run` | — | 3-player GPU solve launch |
| `solver.solve.cpu_locked` | — | CPU-locked solve |
| `solver.solve.cpu_locked.finalize` | — | CPU-locked finalize |

### 2.3 Finalize / publish
| Method | Notes |
|--------|-------|
| `solver.finalize.compute` | NEW — compute final results |
| `solver.finalize.publish` | NEW — publish/persist results |

### 2.4 Node / history / lock / cards
| Method | HTTP endpoint | Notes |
|--------|---------------|-------|
| `solver.history.apply` | POST /api/apply-history | Apply action history to node |
| `solver.node.results` | GET /api/results | Node strategy + EVs |
| `solver.node.actionsAfter` | POST /api/actions-after | Available actions |
| `solver.node.currentPlayer` | GET /api/current-player | Actor to act |
| `solver.node.currentBoard` | GET /api/current-board | Board cards |
| `solver.node.numActions` | GET /api/num-actions | Action count |
| `solver.node` | — | NEW — generic node accessor |
| `solver.lock.current.set` | POST /api/lock/current | Set node lock |
| `solver.lock.current.get` | GET /api/lock/current | Get lock |
| `solver.lock.current.clear` | POST /api/lock/unlock | Clear lock |
| `solver.lock.list` | GET /api/lock/list | List locks |
| `solver.lock.clearAll` | POST /api/lock/clear | Clear all locks |
| `solver.lock.clear` | — | NEW (variant) |
| `solver.lock` | — | NEW — generic lock accessor |
| `solver.cards.possible` | GET /api/possible-cards | Possible hole cards |
| `solver.cards.private` | GET /api/private-cards/<n> | Private cards for seat n |
| `solver.possible` | — | NEW |

### 2.5 Export / import (strategy archive — Avro)
| Method | HTTP endpoint | Notes |
|--------|---------------|-------|
| `solver.export.currentStreet` | POST /api/export/current-street | Snapshot `{history, max_nodes}` → `{payload}` |
| `solver.export.fullFile` | — | NEW — full strategy export |
| `solver.export.fullFile.status` | — | NEW — export progress |
| `solver.strategy.archive.open` | — | NEW — open Avro archive |
| `solver.strategy.archive.node` | — | NEW — write/archive a node |
| `solver.strategy.archive.close` | — | NEW — close archive |

### 2.6 Desktop (shell/file/range/settings)
| Method | HTTP endpoint | Notes |
|--------|---------------|-------|
| `desktop.strategy.exportCurrentStreetFile` | POST /api/export/current-street-file | |
| `desktop.strategy.exportFullFile` | POST /api/export/full-file | |
| `desktop.strategy.exportFullFileProgress` | GET /api/export/full-file/progress | |
| `desktop.strategy.importFullFile` | POST /api/import/full-file | |
| `desktop.strategy.importNode` | POST /api/import/node | |
| `desktop.strategy.closeImport` | POST /api/import/close | |
| `desktop.strategy.export` / `.close` | — | NEW |
| `desktop.parameters.export` | POST /api/parameters/export | |
| `desktop.parameters.openFolder` | POST /api/parameters/open-folder | |
| `desktop.parameters.load` | POST /api/parameters/load | |
| `desktop.parameters.list` | GET /api/parameters/list?path= | |
| `desktop.parameters.open` | — | NEW |
| `desktop.ranges.load` | POST /api/ranges/load | Load range JSON |
| `desktop.ranges.list` | GET /api/ranges/list?path= | |
| `desktop.settings.load` | POST /api/settings/load | |
| `desktop.settings.save` | POST /api/settings/save | |
| `desktop.external.open` | POST /api/external/open | Open external URL |

---

## 3. Transport / bridge (summary)

- API base: `http://127.0.0.1:6006` (overridable `?apiBase=`).
- Default transport: **native bridge** via `window.chrome.webview.postMessage(JSON.stringify({id, method, params}))`; RPC id `rpc_<ts>_<seq>`.
- Fallback: `http.request` bridge (raw HTTP) on `NOT_IMPLEMENTED`.
- Auth: `bridge.ping` → `bridge_token`; retry once on `UNAUTHORIZED`.
- Envelope: `{id, method, params}` → `{ok, result, error:{message,code}}`.
- Timeouts: 120s (init/allocate/memoryUsage/export.currentStreet), 30s (solve.start/preflight/ranges.load/importNode), default 5s.
- `dT(path, method)` resolves internal method name (38-entry table, DEEP_RE_PHASE2.md §3); `Tt(e,t,s)` → `Hl().request(e,t,s)`.

---

## 4. dT method→path table (38 entries)

(solver.init, solver.allocate, solver.memoryUsage, solver.environment.check, solver.solve.start, solver.solve.preflight, solver.export.currentStreet, desktop.strategy.exportCurrentStreetFile, desktop.strategy.exportFullFile, desktop.strategy.exportFullFileProgress, desktop.strategy.importFullFile, desktop.strategy.importNode, desktop.strategy.closeImport, solver.solve.status, solver.solve.stop, solver.history.apply, solver.node.results, solver.node.actionsAfter, solver.node.currentPlayer, solver.node.currentBoard, solver.lock.current.set, solver.lock.current.get, solver.lock.current.clear, solver.lock.list, solver.lock.clearAll, solver.cards.possible, solver.node.numActions, desktop.parameters.export, desktop.parameters.openFolder, desktop.external.open, desktop.parameters.load, desktop.ranges.load, desktop.settings.load, desktop.settings.save, desktop.parameters.list, desktop.ranges.list, solver.cards.private)

---

## 5. Method names present ONLY as strings (no dT entry confirmed)

`solver.cpp`, `solver.memory`, `solver.node`, `solver.policy`, `solver.practice`, `solver.stop`, `solver.may`, `solver.prepare`, `solver.confirm`, `solver.ensure`, `solver.lock.clear`, `desktop.strategy.close`, `desktop.strategy.import`, `desktop.strategy.export`.

These may be internal/legacy methods or handled via `http.request` fallback rather than `dT`.

---

## 6. Anti-tamper surface relevant to handler instrumentation

- `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`.
- 5× `cpuid` in `.text`.
- Stripped (no RTTI/symbols). PTX JIT-loaded at runtime (`cuModuleLoadData`).

Instrumentation must account for anti-debug (e.g., instrument via driver/API proxy rather than a user-mode debugger, or patch `IsDebuggerPresent`).

---

## 7. Handler VTable Recovery (Static — NEW)

**Handler vtable at `0x14016ec10`** (`.rdata` section) — all 6 handler creator call sites (`0x14000634e`, `0x14000649e`, `0x140014933`, `0x1400151f3`, `0x1400328d3`, `0x140033a9a`) use this same vtable:

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b660` | Code — cleanup/destroy |
| 1 | `0x14001d3f0` | Code — memory accessor thunk → IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Code — memory accessor thunk → IAT `0x14016d8b8` |
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

**15 code pointers recovered statically.** Handler object method names stored in heap memory at `[handler_obj+0x80]` — static method-name → handler-address mapping remains impossible without dynamic execution. See `HANDLER_VTABLE_RECON.md` for full details.

---

## 8. Recommended dynamic handler recovery

1. Launch `TexasSolverGpu_131.exe` under a Windows/x64 emulator or instrument `nvcuda.dll` + WinSock.
2. Hook `0x140014cf0` (dispatcher) and `0x140001840` (invoker).
3. On each dispatch, read the type-`0x3` string value (method name) from the kv pair and log it together with the handler pointer stored at `0x8(%r12)` / the call target of `0x140001840`.
4. Correlate with HTTP traffic on `127.0.0.1:6006` to map each `solver.*` / `desktop.*` method to its native handler address and request/response schema.
5. Read `[handler_obj+0x80]` at runtime to extract method name strings from each handler object.
