# HANDLER_MAP.md
# TexasSolverGPU v0.3.0 — Native Method → Handler VA Mapping (Static Recovery)

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly of the host-side method-dispatch if-else chains
plus method-string cross-references from `.rdata` (method_vmas.txt).
**Status:** PARTIAL — 46 of ~60 methods mapped statically. Remaining methods
require dynamic instrumentation (see REMAINING_GAPS_AND_DYNAMIC_CAPTURE_GUIDE.md).

Dispatcher helpers recovered:
- `0x140014cf0` — variant-based dispatcher entry (signature `(output*, input_kv_pairs, flags)`)
- `0x140006c30` / `0x140001d40` — string compare helpers used by the if-else chains
- `0x14005cff0` — common handler invoker (called with handler ptr in rcx/r14)
- `0x140002a80` — handler result builder
- `0x1400099e0` — handler linked-list lookup
- `0x140036d70` — handler creator (6 call sites)
- `0x140043b60` — variant accessor
- `0x14016ec10` — Handler VTable 1 (16 entries, all 6 creator call sites)
- `0x14016ef90` — Handler VTable 2
- Global handler-list head: `0x1411c6690`; shared global: `0x1411c6698`

---

## 1. `solver.*` methods — first if-else chain (VA `0x140061260`–`0x14006829e`)

| # | Method | String VVA | Handler-entry VA | Notes |
|---|--------|-----------|-----------------|-------|
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

---

## 2. `desktop.*` methods — second if-else chain (VA `0x14002a400`–`0x14002c900`)

| # | Method | String VVA | Handler-entry VA | Notes |
|---|--------|-----------|-----------------|-------|
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
| 35 | `desktop.settings.save` | `0x140170348` | `0x14002ca58` | |

---

## 3. `solver.*` methods — third if-else chain (VA `0x140052d00`–`0x140053e00`)

| # | Method | String VVA | Handler-entry VA | Notes |
|---|--------|-----------|-----------------|-------|
| 36 | `solver.lock` | `0x140248aa0` | `0x1400545ec` | base `solver.lock` method |
| 37 | `solver.layout.validate` | `0x140248ae8` | — | je target unresolved statically |
| 38 | `solver.host.allocate` | `0x140248b00` | `0x14005312d` | |
| 39 | `solver.solve.dispatch` | `0x140248b18` | `0x140053695` | |
| 40 | `solver.solve.cpu_locked` | `0x140248b30` | — | je target unresolved statically |
| 41 | `solver.solve.cpu_locked.finalize` | `0x140248b48` | `0x1400534f1` | |
| 42 | `solver.solve.2p.construct` | `0x140248ba8` | `0x1400536f1` | |
| 43 | `solver.solve.2p.run` | `0x140248bc8` | — | je target unresolved statically |
| 44 | `solver.solve.3p.construct` | `0x140248b70` | `0x140053988` | |
| 45 | `solver.solve.3p.run` | `0x140248b90` | — | je target unresolved statically |
| 46 | `solver.finalize.compute` | `0x140248be0` | — | je target unresolved statically |
| 47 | `solver.finalize.publish` | `0x140248bf8` | — | je target unresolved statically |

Status strings also compared in chain 3: `solving`→`0x1400534ba`, `finalizing`→`0x140053d00`, `initializing`→`0x140063a34`.

---

## 4. Additional method strings in `.rdata` (not in any if-else chain)

These methods are registered via the handler creator (`0x140036d70`) at runtime,
not via static if-else matching. Their string VVAs are known but handler VAs
require dynamic capture.

| Method | String VVA | Notes |
|--------|-----------|-------|
| `solver.solve.status` | `0x140249170` | already mapped in chain 1 |
| `solver.node.results` | `0x140249488` | not in chain 1 (chain 1 ends at cards.private) |
| `solver.export.currentStreet` | `0x16eb80` | frontend calls via HTTP |
| `solver.export.fullFile` | `0x16e2c0` | frontend calls via HTTP |
| `solver.export.fullFile.status` | `0x16e8b0` | frontend calls via HTTP |
| `solver.strategy.archive.open` | `0x16e550` | |
| `solver.strategy.archive.node` | `0x16e8d0` | |
| `solver.strategy.archive.close` | `0x16d9b0` | |
| `solver.strategy.archive` (base) | `0x16ebb0` | |
| `bridge.ping` | — | auth handshake method |
| `solver.policy` | — | direct native call (not via HTTP/dispatcher) |
| `solver.prepare` | — | direct native call (not via HTTP/dispatcher) |
| `solver.confirmStopped` | — | direct native call |
| `solver.ensureIdle` | — | direct native call |

---

## 5. Not yet mapped (require dynamic capture)

The following methods have known string VVAs but UNKNOWN handler VAs:

| # | Method | String VVA | Why blocked |
|---|--------|-----------|-------------|
| — | `solver.layout.validate` | `0x140248ae8` | chain 3 je target unresolved |
| — | `solver.solve.cpu_locked` | `0x140248b30` | chain 3 je target unresolved |
| — | `solver.solve.2p.run` | `0x140248bc8` | chain 3 je target unresolved |
| — | `solver.solve.3p.run` | `0x140248b90` | chain 3 je target unresolved |
| — | `solver.finalize.compute` | `0x140248be0` | chain 3 je target unresolved |
| — | `solver.finalize.publish` | `0x140248bf8` | chain 3 je target unresolved |
| — | `solver.node.results` | `0x140249488` | not in any if-else chain |
| — | `solver.strategy.archive.open` | `0x16e550` | handler creator only |
| — | `solver.strategy.archive.node` | `0x16e8d0` | handler creator only |
| — | `solver.strategy.archive.close` | `0x16d9b0` | handler creator only |
| — | `solver.export.currentStreet` | `0x16eb80` | handler creator only |
| — | `solver.export.fullFile` | `0x16e2c0` | handler creator only |
| — | `solver.export.fullFile.status` | `0x16e8b0` | handler creator only |
| — | `bridge.ping` | — | auth handshake |
| — | `solver.policy` | — | direct native call |
| — | `solver.prepare` | — | direct native call |

---

## 6. Status / error strings discovered in dispatch chains

| String VVA | Text | Observed at |
|-----------|------|-------------|
| `0x14016fb44` | `BUSY` | multiple dispatch targets |
| `0x140247990` | `BAD_REQUEST` | multiple dispatch targets |
| `0x1402484b0` | `require_exact_2p_gpu_layout` | `0x140061995` |
| `0x140248310` | `TREE_TOO_LARGE_FOR_GPU_LAYOUT` | `0x140061971`, `0x14006209c` |
| `0x1402484d0` | `enable_compression` | `0x1400619cd`, `0x1400619f4` |
| `0x140248fe0` | `compress=true` | `0x140061d6a` |
| `0x140248ff0` | `compress=false` | `0x140061da2` |
| `0x140248700` | `initializing` | `0x140063a34` |
| `0x140248730` | `strategy` | multiple |
| `0x140249418` | `/api/private-cards/` | `0x140067df8` |
| `0x1402490f0` | `CUDA_VERSION_QUERY_FAILED` | `0x140063591` |

---

## 7. Source paths (from binary strings)

`C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp` (string at `0x14024ae00`)
Related: `gpu_solver_3p.cpp`, `cuda_driver_api.cpp`, `include\poker\cuda_buffer.h`, `include\poker\cuda_host_memory.h`
Author: bupticybee (GitHub: github.com/bupticybee; Discord: discord.gg/RtyD4vRy2e)

---

## 8. How to complete (dynamic)

1. Patch `IsDebuggerPresent` → return 0; NOP the 5× `cpuid` at entry stub `0x140150474`–`0x1401505e8`; NOP `int3` at `0x14016d9e0`.
2. Hook dispatcher `0x140014cf0`: on entry save `rdx` (kv pairs) and `r12` (output ptr); after resolution read handler ptr at `[r12+0x8]`; log `(method_name_string, handler_VA)`.
3. Hook `0x140036d70` (handler creator) to log handler object VA + valid flag.
4. Correlate with HTTP/WebView traffic on `127.0.0.1:6006` to map each `solver.*`/`desktop.*` method to its native handler VA.
5. Produce final `HANDLER_MAP.md` with all ~60 methods.
