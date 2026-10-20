# TexasSolverGPU v0.3.0 — Handler Map (Static Recovery)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly (objdump/binutils only — no Ghidra/IDA, no dynamic execution)
**Status:** Handler addresses recovered statically; method-name → handler-address mapping BLOCKED (requires dynamic)

---

## 1. Handler VTables (Statically Recovered)

### VTable 1 at `0x14016ec10` — Used by all 6 handler creator call sites

| Entry | VA | Type | Description |
|-------|-----|------|-------------|
| 0 | `0x14001b660` | Code | Cleanup / destroy |
| 1 | `0x14001d3f0` | Code | Memory accessor → IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Code | Memory accessor → IAT `0x14016d8b8` |
| 3 | `0x140038110` | Code | Substantial handler (~0x190 bytes) |
| 4 | `0x140039980` | Code | Small accessor (10 bytes) |
| 5 | `0x14014c6aa` | IAT thunk | → `0x14016d348` |
| 6 | `0x140045a60` | Code | Handler |
| 7 | `0x140045690` | Code | Large handler |
| 8 | `0x140046480` | Code | Handler |
| 9 | `0x1400465e0` | Code | Handler |
| 10 | `0x140043200` | Code | Handler |
| 11 | `0x140043480` | Code | Handler |
| 12 | `0x140043a80` | Code | Handler |
| 13 | `0x1400450a0` | Code | Handler |
| 14 | `0x14002d700` | Code | Handler |
| 15 | `0x140195f88` | Data | Data pointer (not code) |

**15 code pointers + 1 data pointer recovered.**

### VTable 2 at `0x14016ef90` — Used by functions `0x140017100`, `0x14001b720`

| Entry | VA | Type | Description |
|-------|-----|------|-------------|
| 0 | `0x14001b720` | Code | Sets vtable, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| 1 | `0x140014fb65` | Possible IAT thunk | Odd address |
| 2 | `0x140014fb5f` | Possible IAT thunk | Odd address |
| 3 | `0x1400382c0` | Code | Real function |
| 4 | `0x140039940` | Code | Real function |
| 5 | `0x140014fb59` | Possible IAT thunk | Odd address |
| 6 | `0x140045ad0` | Code | Real function |
| 7–14 | `0x140014fb53`…`0x140014fb35` | Possible IAT thunks | 6-byte spaced odd addresses |
| 15 | `0x140195f860` | Data | Data pointer |

**~7 code pointers + thunks + 1 data pointer recovered.**

**Total: ~22 handler function addresses recovered statically.**

---

## 2. Handler Creator: `0x140036d70` — 6 Call Sites

All 6 sites allocate a handler object, set vtable `0x14016ec10`, call IAT init functions, store method name at `[handler_obj+0x80]`, set valid flag `[+0x7c]=1`, and register via `0x140013910` (linked-list insertion, global head `0x1411c6690`).

| # | Call Site VA | RDX Source (method name) | Setup Pattern |
|---|-------------|-------------------------|---------------|
| 1 | `0x14000634e` | `rsi` (function arg #3) | `mov rdx,rsi` |
| 2 | `0x14000649e` | `rsi` (function arg #3) | `mov rdx,rsi` |
| 3 | `0x140014933` | `r14` (preserved reg/local) | `mov rdx,r14` |
| 4 | `0x1400151f3` | `r14` (preserved reg/local) | `mov rdx,r14` |
| 5 | `0x1400328d3` | `rbx` (local stack var) | `mov rdx,rbx` |
| 6 | `0x140033a9a` | `rbx` (local stack var) | `mov rdx,rbx` |

**Handler object setup (before creator call):**
- `lea rax,[rip+...] # 0x14016ec10` → `mov [handler_obj+0x0], rax` (vtable)
- `mov BYTE PTR [handler_obj+0x7c], 0x0` (valid flag = 0)
- `mov BYTE PTR [handler_obj+0x71], 0x0`
- `call 0x14016d400` (IAT init)
- `mov QWORD PTR [handler_obj+0x80], 0x0` (allocated memory = 0)
- `mov QWORD PTR [handler_obj+0x74], [0x1411c6698]` (global pointer)
- `mov QWORD PTR [handler_obj+0x68], 0x0`

**Creator call:**
- `mov rdx, <method_name_ptr>` → `call 0x140036d70`

**After creator returns (if rax != 0):**
- Handler object stored in calling function's data structure
- Error path calls IAT `0x14016d2c8` with `edx=0x2`

---

## 3. Handler Object Layout (Confirmed)

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]`/`[+0x20]` | IAT `0x14016da48` result | Handler interface ptr 1 |
| `[+0x38]`/`[+0x40]` | IAT `0x14016d3d8` result | Handler interface ptr 2 |
| `[+0x50]`/`[+0x58]` | IAT `0x14016d398` result | Handler interface ptr 3 |
| `[+0x68]` | IAT `0x14016d398` check | Validity / ref-count result |
| `[+0x74]` | global `0x1411c6698` | Shared global pointer |
| `[+0x7c]` | valid flag | Set to 1 by creator |
| `[+0x80]` | allocated memory | **Method name string (heap, runtime only)** |

---

## 4. Dispatcher: `0x140014cf0` — 19 Call Sites

Signature: `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`

Walks 0x18-byte key/value entries, finds type-0x2 string key + type-0x3 string value (the "method" entry), calls variant builder `0x140016210`, then handler lookup `0x1400099e0` (linked-list, compare via `0x140151250`). Returns handler object ptr at `[r12+0x8]`.

| # | Call Site VA | Flags (r8b, r9b) | Input Source |
|---|-------------|-------------------|--------------|
| 1 | `0x140015660` | r8b=1, r9b=2 | Function arg buffer (`movups` from `rdx`) |
| 2 | `0x14001e688` | r8b=0, r9b=2 | Function arg buffer (`movups` from `rdx`) |
| 3 | `0x140022c95` | r8b=1, r9b=2 | — |
| 4 | `0x140022d03` | r8b=1, r9b=2 | — |
| 5 | `0x140022d6b` | — | — |
| 6 | `0x140022daf` | — | — |
| 7 | `0x140023650` | — | — |
| 8 | `0x1400236be` | — | — |
| 9 | `0x140023726` | — | — |
| 10 | `0x14002376a` | — | — |
| 11 | `0x1400243ae` | — | — |
| 12 | `0x14002443a` | — | — |
| 13 | `0x1400244e6` | — | — |
| 14 | `0x140024581` | — | — |
| 15 | `0x140024630` | — | — |
| 16 | `0x1400246d4` | — | — |
| 17 | `0x140024718` | — | — |
| 18 | `0x1400259e4` | — | — |
| 19 | `0x140025ab0` | — | — |
| 20 | `0x140025b09` | — | — |

**Pattern:** Input kv-pairs buffer passed via `rdx` (function argument). Key type tag 0x2, value type tag 0x3. Method name stored as type-0x3 string value in the buffer. Handler pointer returned at `[r12+0x8]`; invoker is the CALLER.

---

## 5. Native Method Inventory (~60 Methods)

### 5a. dT Table (38 entries — from embedded JS `dT(method,path)`)

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

### 5b. Additional Native Methods (called directly on `this.solver`, bypassing dT/HTTP)

- `solver.ensureIdle()`
- `solver.policy(postNode, holes)`
- `solver.node(history)`
- `solver.possibleCards()`
- `solver.prepare(payload, settings, onCancelled, onStatus, hooks)`
- `solver.stop()`
- `solver.confirmStopped()`

### 5c. Full String Inventory (from `.rdata`)

Additional method name strings present as static strings in `.rdata` (with code cross-references):

`solver.host.allocate`, `solver.layout.validate`, `solver.solve.dispatch`,
`solver.solve.2p.construct`, `solver.solve.2p.run`, `solver.solve.3p.construct`,
`solver.solve.3p.run`, `solver.solve.cpu_locked`, `solver.solve.cpu_locked.finalize`,
`solver.finalize.compute`, `solver.finalize.publish`,
`solver.strategy.archive.open`, `solver.strategy.archive.node`, `solver.strategy.archive.close`,
`solver.export.fullFile`, `solver.export.fullFile.status`,
`solver.lock`, `solver.may`, `solver.confirm`, `solver.ensure`

---

## 6. Method-Name → Handler-Address Mapping (BLOCKED)

**Status: Cannot be resolved statically.**

**Reason:** All 6 handler creator call sites pass the method name via register (`rsi`, `r14`, or `rbx`) which originates from the caller's function argument or local variable — none can be traced back to a static string load without full execution. All 19 dispatcher call sites receive the method name as part of a key/value pairs buffer passed via `rdx` (function argument), loaded via `movups`, not via `lea rip+` to a static string.

**Handler objects are heap-allocated at runtime** by `0x140036d70`, form a linked list (head at `0x1411c6690`), and method name strings are stored in heap memory at `[handler_obj+0x80]`. No static `{method_name_string, handler_fn_ptr}` table exists in `.rdata` or `.data`.

**Required for dynamic recovery:**
1. Launch `TexasSolverGpu_131.exe` under Windows/x64 + NVIDIA GPU
2. Hook dispatcher `0x140014cf0`: log `(method_name_string, handler_VA)` on each dispatch
3. Hook handler creator `0x140036d70`: log handler object VA + method name from `[handler_obj+0x80]`
4. Correlate with HTTP/WebView traffic on `127.0.0.1:6006`
5. Anti-tamper: patch `IsDebuggerPresent`→0, handle 5× `cpuid`, use API-level proxy DLLs

---

## 7. IAT References (for Dynamic Hooking)

| VA | Function | Use |
|----|----------|-----|
| `0x14016da48` | — | Handler interface ptr 1 (stored at `[+0x18]`/`[+0x20]`) |
| `0x14016d3d8` | — | Handler interface ptr 2 (stored at `[+0x38]`/`[+0x40]`) |
| `0x14016d398` | — | Handler interface ptr 3 (stored at `[+0x50]`/`[+0x58]`) |
| `0x14016d400` | — | Handler init (called during creation) |
| `0x14016d2c8` | — | Error handler (called if creator returns 0) |
| `0x14016d298` | — | String init/allocator |
| `0x14016d3f8` | — | String accessor (called on `[+0x38]`) |
| `0x14016da60` | — | String length (called on `[+0x80]` method name) |
| `0x14016daa8` | — | String compare/copy |
| `0x14016d348` | — | IAT thunk target (vtable entry 5) |
| `0x14016d410` | — | IAT jump target (vtable 2 entry 0) |

---

## 8. Source References

- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp`
- Author: bupticybee; Discord: https://discord.gg/RtyD4vRy2e; GitHub: https://github.com/bupticybee
