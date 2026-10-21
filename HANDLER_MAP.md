# HANDLER_MAP.md
# TexasSolverGPU v0.3.0 — Native Method → Handler VA Mapping (Static Recovery)

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly of the host-side method-dispatch if-else chain
at VA `0x140061260`–`0x14006829e` (the "20-method if-else chain"). Each chain
entry: `lea <method_string>,%rdx → call <string_compare> → test %al,%al → je <handler_target>`.
The `je` target is the concrete handler-entry VA for that method.
**Status:** PARTIAL — 40 of ~60 methods mapped statically (20 `solver.*` via the `0x140061260`–`0x14006829e`
chain + 14 `desktop.*` via the `0x14002a400`–`0x14002c900` chain + 6 `solver.*` via the `0x140052d00`–`0x140053e00`
chain). Remaining methods (`solver.solve.cpu_locked`, `solver.solve.2p.run`, `solver.finalize.compute`,
`solver.finalize.publish`, `solver.layout.validate`, `solver.strategy.archive.*`, `solver.export.*`,
`solver.policy`, `solver.prepare`, `solver.node` base, `desktop.settings.save`, `bridge.ping`, …) use the
variant-based dispatcher `0x140014cf0` or are called directly and require dynamic instrumentation
(see PHASE5_DYNAMIC_PLAN.md §2.2).

Dispatcher helpers recovered:
- `0x140014cf0` — variant-based dispatcher entry (signature `(output*, input_kv_pairs, flags)`)
- `0x140006c30` / `0x140001d40` — string compare helpers used by the if-else chain
- `0x14005cff0` — common handler invoker (called with handler ptr in rcx/r14)
- `0x140002a80` — handler result builder
- `0x1400099e0` — handler linked-list lookup
- `0x140036d70` — handler creator (6 call sites)
- `0x140043b60` — variant accessor
- `0x14016ec10` — Handler VTable 1 (16 entries, all 6 creator call sites)
- `0x14016ef90` — Handler VTable 2
- Global handler-list head: `0x1411c6690`; shared global: `0x1411c6698`

---

## 1. Statically mapped method → dispatch-target (handler-entry VA)

| # | Method | String VVA | Handler-entry VA (dispatch target) | Notes |
|---|--------|-----------|-----------------------------------|-------|
| 1 | `solver.init` | `0x140248f38` | `0x140061732` | |
| 2 | `solver.allocate` | `0x140248f80` | `0x140061cc3` | |
| 3 | `solver.memoryUsage` | `0x140248fc8` | `0x140062f7f` | |
| 4 | `solver.solve.preflight` | `0x140249030` | `0x1400632ab` | |
| 5 | `solver.environment.check` | `0x140249080` | `0x14006330b` | |
| 6 | `solver.solve.start` | `0x1402490a0` | `0x140063bd7` | |
| 7 | `solver.solve.status` | `0x140249170` | `0x1400645bd` | |
| 8 | `solver.solve.stop` | `0x140249198` | `0x14006476a` | |
| 9 | `solver.lock.current.set` | `0x1402491b0` | `0x14006514b` | |
| 10 | `solver.lock.current.get` | `0x140249248` | `0x1400657a7` | |
| 11 | `solver.lock.current.clear` | `0x1402492a0` | `0x140065f1c` | |
| 12 | `solver.lock.list` | `0x1402492c0` | `0x140066295` | |
| 13 | `solver.lock.clearAll` | `0x1402492d8` | `0x1400667d2` | |
| 14 | `solver.history.apply` | `0x1402492f0` | `0x140066b11` | |
| 15 | `solver.node.currentPlayer` | `0x140249348` | `0x140066eb4` | |
| 16 | `solver.node.numActions` | `0x140249368` | `0x140067195` | |
| 17 | `solver.node.currentBoard` | `0x140249380` | `0x140067466` | |
| 18 | `solver.node.actionsAfter` | `0x1402493a0` | `0x1400679f3` | |
| 19 | `solver.cards.possible` | `0x1402493c0` | `0x140067cc4` | |
| 20 | `solver.cards.private` | `0x1402493d8` | `0x14006829e` | |

### 1b. `desktop.*` methods (second if-else chain, VA `0x14002a400`–`0x14002c900`)

| # | Method | String VVA | Handler-entry VA (dispatch target) | Notes |
|---|--------|-----------|-----------------------------------|-------|
| 21 | `desktop.parameters.list` | `0x140170050` | `0x14002a741` | |
| 22 | `desktop.parameters.export` | `0x140170068` | `0x14002a952` | |
| 23 | `desktop.strategy.exportCurrentStreetFile` | `0x140170088` | `0x14002aa82` | |
| 24 | `desktop.strategy.exportFullFileProgress` | `0x1401700b8` | `0x14002abdd` | |
| 25 | `desktop.strategy.importNode` | `0x1401700e0` | `0x14002ad38` | |
| 26 | `desktop.strategy.closeImport` | `0x140170100` | `0x14002b15e` | |
| 27 | `desktop.strategy.importFullFile` | `0x140170168` | `0x14002b7b9` | |
| 28 | `desktop.strategy.exportFullFile` | `0x140170208` | `0x14002bdfc` | |
| 29 | `desktop.parameters.openFolder` | `0x1401702b0` | `0x14002c008` | |
| 30 | `desktop.external.open` | `0x1401702d0` | `0x14002c219` | |
| 31 | `desktop.parameters.load` | `0x1401702e8` | `0x14002c42a` | |
| 32 | `desktop.ranges.list` | `0x140170300` | `0x14002c63b` | |
| 33 | `desktop.ranges.load` | `0x140170318` | `0x14002c84c` | |
| 34 | `desktop.settings.load` | `0x140170330` | `0x14002ca58` | |

### 1c. `solver.*` methods (third if-else chain, VA `0x140052d00`–`0x140053e00`)

| # | Method | String VVA | Handler-entry VA (dispatch target) | Notes |
|---|--------|-----------|-----------------------------------|-------|
| 35 | `solver.lock` | `0x140248aa0` | `0x1400545ec` | base `solver.lock` method |
| 36 | `solver.host.allocate` | `0x140248b00` | `0x14005312d` | |
| 37 | `solver.solve.dispatch` | `0x140248b18` | `0x140053695` | |
| 38 | `solver.solve.cpu_locked.finalize` | `0x140248b48` | `0x1400534f1` | |
| 39 | `solver.solve.2p.construct` | `0x140248ba8` | `0x1400536f1` | |
| 40 | `solver.solve.3p.construct` | `0x140248b70` | `0x140053988` | |

Status strings also compared in this chain (not methods): `solving`→`0x1400534ba`, `finalizing`→`0x140053d00`, `initializing`→`0x140063a34`.

String VVA = virtual address of the method-name string in `.rdata`. Note `solver.solve.cpu_locked`, `solver.solve.2p.run`, `solver.finalize.compute`, `solver.finalize.publish`, `solver.layout.validate` were referenced but their `je` targets did not resolve cleanly in static disassembly; they need dynamic capture.

---

## 2. Not yet mapped (require dynamic capture)

The following methods appear in the binary's string table / `dT` table but were
NOT found in the `0x140061260`–`0x14006829e` if-else chain. Their handler VAs
must be recovered by hooking the dispatcher `0x140014cf0` at runtime:

- `solver.solve.3p.construct` (`0x140248b70`), `solver.solve.3p.run` (`0x140248b90`)
- `solver.solve.2p.construct` (`0x140248ba8`), `solver.solve.2p.run` (`0x140248bc8`)
- `solver.solve.dispatch` (`0x140248b18`), `solver.solve.cpu_locked` (`0x140248b30`), `solver.solve.cpu_locked.finalize` (`0x140248b48`)
- `solver.finalize.compute` (`0x140248be0`), `solver.finalize.publish` (`0x140248bf8`)
- `solver.host.allocate` (`0x140248b00`), `solver.layout.validate` (`0x140248e8`)
- `solver.lock` (`0x140248a0`), `solver.strategy.archive.open/node/close` (`0x16e550`/`0x16e8d0`/`0x16d9b0`)
- `solver.export.currentStreet` (`0x16eb80`), `solver.export.fullFile` (`0x16e2c0`), `solver.export.fullFile.status` (`0x16e8b0`)
- `solver.node` (`0x140249330` region), `solver.policy`, `solver.prepare`, `solver.stop`, `solver.ensureIdle`
- All `desktop.*` methods (`desktop.parameters.*`, `desktop.ranges.*`, `desktop.settings.*`, `desktop.strategy.*`, `desktop.external.open`)

String VVAs for these are listed in DEEP_RE_PHASE2.md §1.5 and REMAINING_GAPS §7.

---

## 3. Status / error strings discovered in the dispatch chain

These constant strings are compared within the handler logic (not method names,
but reveal the solver's error/status vocabulary):

| String VVA | Text | Observed at dispatch target |
|-----------|------|------------------------------|
| `0x14016fb44` | `BUSY` | `0x1400613b2`, `0x1400617f1`, `0x140061e57`, `0x14006303e`, `0x1400635b3`, `0x140064829` |
| `0x140247990` | `BAD_REQUEST` | `0x14006189f`, `0x140061f13`, `0x1400630ec`, `0x140065962`, `0x140066450`, `0x140066cc0`, `0x14006806c` |
| `0x1402484b0` | `require_exact_2p_gpu_layout` | `0x140061995` |
| `0x140248310` | `TREE_TOO_LARGE_FOR_GPU_LAYOUT` | `0x140061971`, `0x14006209c` |
| `0x1402484d0` | `enable_compression` | `0x1400619cd`, `0x1400619f4` |
| `0x14016ee58` | `path` | `0x140061daf`, `0x140068222` |
| `0x140248fe0` | `compress=true` | `0x140061d6a` |
| `0x140248ff0` | `compress=false` | `0x140061da2` |
| `0x14016e2b8` | `true` | `0x140061f7d`, `0x140062aa7` |
| `0x140248600` | `initializing` | `0x140063a34` |
| `0x14016ed80` | `INVALID_ARGUMENT` | `0x140064af9` |
| `0x140248700` | `history` | `0x1400669a1` |
| `0x140248a78` | `terminal` | `0x140066cdc` |
| `0x140248730` | `strategy` | `0x1400650c1`, `0x140064a92`, `0x140064c07`, `0x14006538a`, `0x140065a70` |
| `0x140248798` | `append` | `0x140067755` |
| `0x140249418` | `/api/private-cards/` | `0x140067df8` |
| `0x1402490f0` | `CUDA_VERSION_QUERY_FAILED` | `0x140063591` |

---

## 4. Source paths (from binary strings)

`C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp` (string at `0x14024ae00`)
Related: `gpu_solver_3p.cpp`, `cuda_driver_api.cpp`, `include\poker\cuda_buffer.h`, `include\poker\cuda_host_memory.h`
Author: bupticybee (GitHub: github.com/bupticybee; Discord: discord.gg/RtyD4vRy2e)

---

## 5. How to complete (dynamic)

1. Patch `IsDebuggerPresent` → return 0; NOP the 5× `cpuid` at entry stub `0x140150474`–`0x1401505e8`; NOP `int3` at `0x14016d9e0`.
2. Hook dispatcher `0x140014cf0`: on entry save `rdx` (kv pairs) and `r12` (output ptr); after resolution read handler ptr at `[r12+0x8]`; log `(method_name_string, handler_VA)`.
3. Hook `0x140036d70` (handler creator) to log handler object VA + valid flag.
4. Correlate with HTTP/WebView traffic on `127.0.0.1:6006` to map each `solver.*`/`desktop.*` method to its native handler VA.
5. Produce final `HANDLER_MAP.md` with all ~60 methods.

---

## 6. Verification

- Dispatch chain disassembly range: `0x140061260`–`0x14006829e` (`.text` section, file offset `0x60660`–`0x6869e`).
- Confirmed handler-entry pattern: `solver.init` string `0x140248f38` → `je 0x140061732`; at `0x140061732` the handler loads `solver.allocate` string `0x140248f80` and invokes common handler invoker `0x14005cff0`.
- String VVAs computed from `.rdata` section (file off `0x16be00` = VMA `0x14016d000`): `VVA = 0x14016d000 + (file_off − 0x16be00)`.
