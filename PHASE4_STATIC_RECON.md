# PHASE4_STATIC_RECON.md
# TexasSolverGPU v0.3.0 — Phase 4: Extended Static Reverse Engineering

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase 0x140000000)
**Status:** Static analysis only (no GPU, no Windows/x64 emulator available). Builds on Phases 1–3.

---

## 1. Dispatcher Disassembly (confirmed static)

**Dispatcher:** `0x140014cf0` — signature `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`.

Verified flow (from `objdump -d -M intel`):

1. Walks input key/value array (entries are 0x18 bytes, `rbx` from `rdi` to `r15`).
2. For each entry: `mov rsi,[rbx+0x10]`; `test rsi,rsi`; `cmovne rax,rsi`; `cmp BYTE PTR [rax],0x2` (key type tag). If not 0x2, skip.
3. Calls variant accessor `0x140043b60` on key; requires result == 0x2.
4. Then `mov rax,[r14+0x8]`; `mov rcx,[rax]`; `cmp BYTE PTR [rcx],0x3` (value type tag). If not 0x3, skip.
5. `add rbx,0x18` — advance to next entry.
6. When method entry found: calls `0x140016210` (variant builder) to assemble request, stores result at `[r12+0x8]`.
7. Calls `0x1400099e0` (linked-list handler lookup) with `rcx = [r12+0x8]` (list head), `rdx = method name buffer`. Walks list nodes (0x10 bytes: `[+0x0]`=next, `[+0x8]`=found flag, `[+0x10]`=back-ptr to handler object). Compares method names via `0x140151250` (string compare).
8. `0x1400099e0` returns output struct: `[+0x0]`=handler object ptr, `[+0x8]`=found flag, `[+0x10]`=current list node.
9. Dispatcher reads `[rax+0x10]` = current list node, then `cmp BYTE PTR [r8+0x19],0x0` (handler validity flag). If 0, calls `0x1400071b0` (string compare) to match handler name vs method name.
10. Handler pointer stored at `[r12+0x8]`; invoker is the CALLER of the dispatcher (not `0x140001840` — that is a variant string copy function).

**Helper functions confirmed:**
- `0x140043b60` — variant accessor (jump table at `0x140043ba4`; tag 0→0, tag 1→count/16, tag 2→data ptr, tags 3–9/default→1).
- `0x140016210` — request/variant builder (jump table at `0x140016370`; type 0→`0x14000dde0`, types 1–3 allocate 0x18/0x20/0x28, type 4 null-term, 5+ null).
- `0x1400099e0` — handler linked-list lookup (compare via `0x140151250`).
- `0x1400071b0` — variant string compare (SSO vs heap).
- `0x140001840` / `0x1400017d0` — variant string copy (allocator `0x140151238` / `0x1400017d0`).
- `0x14001ef90` — string length helper.

**Handler creator:** `0x140036d70` — called from 6 sites (`0x14000634e`, `0x14000649e`, `0x140014933`, `0x1401511f3`, `0x1400328d3`, `0x140033a9a`). Allocates handler object via `0x14014fb2f`, calls IAT `0x14016da48` / `0x14016d3d8` / `0x14016d398`. Handler object layout: `[+0x0]`=vtable, `[+0x18]`/`[+0x20]`=IAT result, `[+0x38]`/`[+0x40]`=same, `[+0x50]`/`[+0x58]`=same, `[+0x68]`=IAT `0x14016d398` result, `[+0x74]`=global `0x1411c6698`, `[+0x7c]`=valid flag, `[+0x80]`=allocated memory. **No static handler address table exists** — addresses resolved at runtime via linked list.

---

## 2. Complete dT Method → Path Table (from frontend JS, with timeouts)

Recovered from embedded minified Vue/Vite JS (`function dT(e,t){switch(\`${t.toUpperCase()} ${e}\`)...}`). Timeout map `p=P=>{switch(P){...}}`:

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

---

## 3. Additional Native Methods (from frontend JS code, not in dT table)

The embedded frontend calls these directly on `this.solver` (bypassing dT/HTTP):

| Method | Usage |
|--------|-------|
| `solver.ensureIdle()` | Called before solve start / prepare |
| `solver.policy(postNode, holes)` | AI strategy lookup for current player |
| `solver.node(history)` | Node lookup by action history |
| `solver.possibleCards()` | Chance card bitmask for runout |
| `solver.prepare(payload, settings, onCancelled, onStatus, hooks)` | Prepare/postflop solve launch |
| `solver.stop()` | Stop solver |
| `solver.confirmStopped()` | Confirm stop complete |

These correspond to the "method names present ONLY as strings" noted in PHASE3_HANDLER_MAP.md §5, plus newly discovered `solver.policy`, `solver.prepare`, `solver.ensureIdle`, `solver.confirmStopped`.

---

## 4. Compact Archive — Extended Validation Errors

New validation strings recovered from `all_strings.txt` (beyond PHASE3_COMPACT_ARCHIVE.md):

| Error | Interpretation |
|-------|----------------|
| `Compact Apache Avro records require format_version=2` | Archive format version gate |
| `Compact archive chance index is invalid` | Chance node index bounds |
| `Compact archive chance mapping changes card rank` | Chance mapping rank consistency |
| `Compact archive chance mapping includes a board card` | Board card not in chance mapping |
| `Compact archive chance mapping is invalid` | Generic chance mapping failure |
| `Compact archive chance mapping is out of range` | Chance mapping index out of range |
| `Compact archive chance mapping must contain 52 entries` | Full-deck chance reps |
| `Compact archive chance representative is on the board` | Rep card conflicts with board |
| `Compact archive control record has numeric payload` | Control record type mismatch |
| `Compact archive has nonzero action EV with a zero scale` | EV/scale consistency |
| `Compact archive has nonzero reach values with a zero scale` | Reach/scale consistency |
| `Compact archive is missing a representative child` | Missing chance child |
| `Compact archive non-action node has numeric payload` | Non-action node type mismatch |
| `Compact archive representative card does not map to itself` | Identity mapping violation |
| `Compact archive representative children do not match mapping` | Child/rep mismatch |
| `Compact archive suit alias is incompatible with player hands` | Suit aliasing conflict |
| `Compact archive terminal has a chance mapping` | Terminal node shouldn't have chance mapping |

**Export format options** (frontend UI): `avro` (recommended), `jsonl`, `json`. Avro help UI references `results.avro_help_size` (~100 bytes per record estimate).

---

## 5. Frontend Solver API Usage Patterns

From embedded JS (`index-DQB6irIW.js`):

```js
// Solve flow (postflop/6max)
await this.solver.ensureIdle()
this.preflop = Ya(o)              // load policy JSON
await this.solver.prepare(B, this.settings, onCancelled, onStatus, hooks)
// B = {oopRange, ipRange, board, starting_pot, effectiveStack, rakeRate, rakeCap, bet sizes...}

// Node lookup
const v = await this.solver.node(this.history)
// v = {kind: "action"|"chance"|"terminal", seat, board, paid, labels, snapshot, ...}

// Chance cards
const z = await this.solver.possibleCards()
// returns bitmask (BigInt) of valid runout cards

// AI policy / advice
const f = this.solver.policy(this.postNode, this.view.holes[o])
// f = {probs: Float32Array[1326], evs: Float32Array|null}

// Stop
await this.solver.stop()
await this.solver.confirmStopped()
```

**6-max seat layout** (from JS): seats = [UTG, MP, CO, BTN, SB, BB], postflop order = [4,5,0,1,2,3]. 3-max: [BTN, SB, BB]. 2-max (HU): [SB, BB].

**Combo index** (JS/C++): `en(e,t) = e*(103-e)/2 + t - e - 1` for `e<t`, range [0,1325]. Card encoding: `rank = c/4` (0=2 … 12=A), `suit = c%4` (0=c,1=d,2=h,3=s). Packed: `c | (p<<8)`.

---

## 6. Handler Address Recovery — Static Result

**Negative result confirmed.** The native handler address table cannot be recovered statically because:

- Handler objects are heap-allocated by `0x140036d70` at runtime.
- Handler objects form a linked list (not a static array).
- Handler method names are SSO strings stored inside each handler object.
- Handler function pointers are stored inside handler objects, referenced via `[handler_obj+0x18]` / `[handler_obj+0x80]`.
- No static {method_name_string, handler_fn_ptr} table exists in .rdata or .data.

**Required for dynamic recovery:**
1. Launch `TexasSolverGpu_131.exe` under Windows/x64 emulator (Wine + Xvfb, or Windows VM).
2. Hook `0x140014cf0` (dispatcher) — on each dispatch, read the type-0x3 string value (method name) and the handler pointer at `[r12+0x8]` / `[list_node+0x10]`.
3. Correlate with HTTP traffic on `127.0.0.1:6006` to map each `solver.*` / `desktop.*` method to its native handler VA.
4. Anti-tamper: `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW` — use API-level instrumentation (proxy `nvcuda.dll`, WinSock LSP) rather than a user-mode debugger.

---

## 7. PTX / Device Code — Status Unchanged

- PTX target: sm_120 (forward PTX JIT). **No embedded PTX text or cubin.**
- Device kernels loaded via `cuModuleLoadData` at runtime.
- `ptxas` referenced ("Cuda compilation tools, release 13.1, V13.1.115").
- CUDA targets: 75;80;86;89;90;120.
- **Required:** dynamic capture of PTX blobs passed to `cuModuleLoadData` / `cuModuleLoadDataEx`.

---

## 8. Recommended Next Steps (Phase 5 — Dynamic)

1. **Windows/x64 emulation:** Run `TexasSolverGpu_131.exe` under Wine or a Windows VM with GPU passthrough.
2. **Handler map:** Instrument `0x140014cf0` and `0x1400099e0` to log `(method_name, handler_VA)` for all ~60 native methods.
3. **PTX capture:** Hook `cuModuleLoadData`/`cuModuleLoadDataEx` to dump PTX blobs for all 40+ kernels (`evaluate_*`, `backward_*`, `forward_*`, `train_*`, `river_*`, `apply_isomorphism`, `gather_cfv`, etc.).
4. **Compact archive:** Trigger `POST /api/export/full-file` with `format=avro`; capture archive bytes; decode with Python `fastavro` using recovered schema; verify Q16 dequantization (`value = q16_raw / 65536.0`).
5. **Kernel configs:** Instrument `cuLaunchKernel`/`cuLaunchKernelEx` to log `(kernel, gridDim, blockDim, sharedMem, stream, args)`; correlate with CFR phases (backward/forward/train/river/finalize).
6. **iter_params:** Hook `cuMemcpyHtoDAsync(d_discount_params_, …)` to capture the 3 discount floats.

---

## 9. Source Code & Author (reconfirmed)

- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h`
- Author: bupticybee; Discord: https://discord.gg/RtyD4vRy2e; GitHub: https://github.com/bupticybee; Page: https://bupticybee.github.io/texassolver_gpu_page/

---

## 10. Binary Anti-Tamper Surface

- `IsDebuggerPresent` (KERNEL32), `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`.
- 5× `cpuid` in `.text` (environment/feature detection, possible anti-VM).
- Stripped binary (no RTTI/debug symbols, `nm` returns "no symbols").
- VRAM gate: "VRAM is below 8 GB; large trees may fail."
- No obvious license/activation mechanism found.
