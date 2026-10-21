# TexasSolverGPU v0.3.0 — Handler Map

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Status:** STATIC PARTIAL — method-name → handler-address mapping NOT possible statically (names stored in heap at `[handler_obj+0x80]`)
**Blocked:** Complete mapping requires dynamic capture (Windows + NVIDIA GPU)

---

## 1. Handler Object Model

All handlers are created by `0x140036d70` (6 call sites). Handler objects use one of two vtables:

| VTable | VA | Entries | Used By |
|--------|-----|---------|---------|
| VTable 1 | `0x14016ec10` | 16 (15 code + 1 data) | All 6 creator call sites |
| VTable 2 | `0x14016ef90` | 16 | Functions `0x140017100`, `0x14001b720` |

### Handler Object Layout (VTable 1)

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` or `0x14016ef90` |
| `[+0x18]`/`[+0x20]` | interface_ptr_1 | IAT `0x14016da48` result |
| `[+0x38]`/`[+0x40]` | interface_ptr_2 | IAT `0x14016d3d8` result |
| `[+0x50]`/`[+0x58]` | interface_ptr_3 | IAT `0x14016d398` result |
| `[+0x68]` | validity_flag | IAT `0x14016d398` check result |
| `[+0x70]` | flags_byte | Flag bits (0x1, 0x2, 0x4 tested by vtable2 handlers) |
| `[+0x71]` | sub_flag | Set by creator to 0; initializer sets to `sil` |
| `[+0x74]` | global_ptr | Global `0x1411c6698` |
| `[+0x7c]` | valid_flag | Set to 1 by creator and initializer |
| `[+0x80]` | allocated_memory | Method name string (heap, runtime only) |
| `[+0x88]`, `[+0x90]` | buffer_size/capacity | Buffer size/capacity pair |

**Critical:** Method name is stored in heap memory at `[handler_obj+0x80]` — set at runtime by the allocator `0x14014fb2f`. No static `{method_name_string, handler_fn_ptr}` table exists.

---

## 2. VTable 1 — Handler Function Addresses (`0x14016ec10`)

| Entry | VA | Type | Role |
|-------|-----|------|------|
| 0 | `0x14001b660` | Code | cleanup/destroy — calls `0x140017070`; if `edx & 1`, frees via `0x14014fca4` (size `0x98`) |
| 1 | `0x14001d3f0` | Code | memory accessor → IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Code | memory accessor → IAT `0x14016d8b8` |
| 3 | `0x140038110` | Code | substantial handler (~0x190 bytes), buffer I/O with chunk management |
| 4 | `0x140039980` | Code | small accessor — bit manipulation on WORD values, calls `0x140021460` |
| 5 | `0x14014c6aa` | IAT thunk | `jmp QWORD PTR [0x14016d348]` |
| 6 | `0x140045a60` | Code | handler — string buffer reader via `[rcx+0x38]` → `[rax]` |
| 7 | `0x140045690` | Code | large handler (~259 bytes), SSO vs heap string support, 0xfff byte chunks |
| 8 | `0x140046480` | Code | handler — buffer read/copy, chunked reads at 0xfff boundaries, calls `0x140151238` |
| 9 | `0x1400465e0` | Code | handler — buffer read/copy, calls `0x140151238` |
| 10 | `0x140043200` | Code | handler — calls `0x14001c5b0`, IAT `0x14016dab0`, `0x14016da68`, `0x14001de20` |
| 11 | `0x140043480` | Code | handler — similar to entry 10, calls `0x14001c5b0`, IAT `0x14001da88` |
| 12 | `0x140043a80` | Code | handler initializer — sets `[rbx+0x7c]=1`, `[rbx+0x71]=sil`, IAT init, interface ptrs |
| 13 | `0x1400450a0` | Code | handler — checks `[rcx+0x80] != 0`, calls `[rax+0x18]`, IAT `0x14016da58` |
| 14 | `0x14002d700` | Code | handler — calls `0x140013910` (handler registration), path separator checking |
| 15 | `0x140195f88` | Data | Data pointer — points to `.rdata` (NOT code) |

**~22 total handler function addresses recovered statically** across both vtables.

---

## 3. VTable 2 — Handler Function Addresses (`0x14016ef90`)

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b720` | Code — sets vtable, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| 1 | `0x140014fb35` | Import thunk (jmp QWORD PTR [rip+imm32]) |
| 2 | `0x140014fb3b` | Import thunk |
| 3 | `0x1400382c0` | Code — WORD-sized UTF-16 string buffer writer |
| 4 | `0x140039940` | Code — WORD-sized string reader/writer |
| 5 | `0x140014fb41` | Import thunk |
| 6 | `0x140045ad0` | Code — WORD-sized string reader |
| 7 | `0x140014fb4d` | Import thunk |
| 8 | `0x140014fb53` | Import thunk |
| 9 | `0x140014fb59` | Import thunk |
| 10 | `0x1400432e0` | Code — complex buffer reader with flag-based control flow |
| 11 | `0x140043560` | Code — complex buffer reader similar to entry 10 |
| 12 | `0x140014fb65` | Import thunk |
| 13 | `0x140014fb6b` | Import thunk |
| 14 | `0x140014fb71` | Import thunk |
| 15 | `0x140195f860` | Data pointer |

---

## 4. Method Dispatch Chain — If-Else Chain (`0x140061260`–`0x14006829e`)

| Order | Code VA | Method String VA | Method String | On Match → Handler VA | Next Check |
|-------|---------|-----------------|---------------|----------------------|------------|
| 1 | `0x140061260` | `0x140248f38` | `solver.init` | `0x14006a680` | `0x140061732` |
| 2 | `0x140061732` | `0x140248f80` | `solver.allocate` | — | `0x140061cc3` |
| 3 | `0x140061cc3` | `0x140248fc8` | `solver.memoryUsage` | — | `0x140062f7f` |
| 4 | `0x140062f7f` | `0x140249030` | `solver.solve.preflight` | — | `0x1400632ab` |
| 5 | `0x1400632ab` | `0x140249080` | `solver.environment.check` | `0x140055840` | `0x14006330b` |
| 6 | `0x14006330b` | `0x1402490a0` | `solver.solve.start` | `0x1400157a0` + `0x14010b9a0` | `0x140063bd7` |
| 7 | `0x140063bd7` | `0x140249170` | `solver.solve.status` | — | `0x1400645bd` |
| 8 | `0x1400645bd` | `0x140249198` | `solver.solve.stop` | — | `0x14006514b` |
| 9 | `0x14006514b` | `0x140249248` | `solver.lock.current.set` | — | `0x1400657a7` |
| 10 | `0x1400657a7` | `0x1402492a0` | `solver.lock.current.get` | — | `0x140065f1c` |
| 11 | `0x140065f1c` | `0x1402492c0` | `solver.lock.current.clear` | — | `0x140066295` |
| 12 | `0x140066295` | `0x1402492d8` | `solver.lock.list` | — | `0x1400667d2` |
| 13 | `0x1400667d2` | `0x1402492f0` | `solver.lock.clearAll` | — | `0x140066b11` |
| 14 | `0x140066b11` | `0x140249348` | `solver.node.currentPlayer` | — | `0x140066eb4` |
| 15 | `0x140066eb4` | `0x140249368` | `solver.node.numActions` | — | `0x140067195` |
| 16 | `0x140067195` | `0x140249380` | `solver.node.currentBoard` | — | `0x140067466` |
| 17 | `0x140067466` | `0x1402493a0` | `solver.node.actionsAfter` | — | `0x1400679f3` |
| 18 | `0x1400679f3` | `0x1402493c0` | `solver.cards.possible` | — | `0x140067cc4` |
| 19 | `0x140067cc4` | `0x1402493d8` | `solver.cards.private` | — | `0x14006829e` |
| 20 | `0x14006829e` | `0x140249488` | `solver.node.results` | — | (end) |

Methods with "—" in the Handler VA column are handled inline by the dispatch chain (no separate handler function).

---

## 5. Additional Native Methods — `.rdata` String Cross-References

| Method String VA | Method String | Code Ref VA(s) | Pattern |
|-----------------|---------------|----------------|---------|
| `0x14016ebb0` | `solver.strategy.archive.close` | `0x140027fae`, `0x14002ae8e`, `0x140044dad`, `0x140060e5a` | lea (4 refs) |
| `0x14016f4c0` | `solver.export.fullFile` | `0x140018d72`, `0x140068569` | movups + lea |
| `0x14016f750` | `solver.strategy.archive.open` | `0x140019a62`, `0x14005f2a8` | movups + lea |
| `0x14016fab0` | `solver.export.currentStreet` | `0x1400238f7`, `0x140069e8c` | movups + lea |
| `0x14016fad0` | `solver.strategy.archive.node` | `0x140027f84`, `0x14002abf9`, `0x1400606c4` | lea (3 refs) |
| `0x14016fd80` | `solver.export.fullFile.status` | `0x140027f5a`, `0x14002aa9e`, `0x14005e911` | lea (3 refs) |
| `0x140170050` | `desktop.parameters.list` | `0x14002a50e` | lea |
| `0x140170068` | `desktop.parameters.export` | `0x14002a741` | lea |
| `0x140170088` | `desktop.strategy.exportCurrentStreetFile` | `0x14002a952` | lea |
| `0x1401700b8` | `desktop.strategy.exportFullFileProgress` | `0x14002aa82` | lea |
| `0x1401700e0` | `desktop.strategy.importNode` | `0x14002abdd` | lea |
| `0x140170100` | `desktop.strategy.closeImport` | `0x14002ad38` | lea |
| `0x140170168` | `desktop.strategy.importFullFile` | `0x14002b15e` | lea |
| `0x140170208` | `desktop.strategy.exportFullFile` | `0x14002b7b9` | lea |
| `0x1401702b0` | `desktop.parameters.openFolder` | `0x14002bdfc` | lea |
| `0x1401702d0` | `desktop.external.open` | `0x14002c008` | lea |
| `0x1401702e8` | `desktop.parameters.load` | `0x14002c219` | lea |
| `0x140170300` | `desktop.ranges.list` | `0x14002c42a` | lea |
| `0x140170318` | `desktop.ranges.load` | `0x14002c63b` | lea |
| `0x140170330` | `desktop.settings.load` | `0x14002c84c` | lea |
| `0x140170348` | `desktop.settings.save` | `0x14002ca58` | lea |

---

## 6. Additional Native Methods — `.rdata` Mid-Section

| Method String VA | Method String | Code Ref VA |
|-----------------|---------------|-------------|
| `0x140248aa0` | `solver.lock` | `0x140052de1` |
| `0x140248ae8` | `solver.layout.validate` | `0x140052f7a`, `0x140052ff6` |
| `0x140248b00` | `solver.host.allocate` | `0x1400530f3` |
| `0x140248b18` | `solver.solve.dispatch` | `0x1400533af` |
| `0x140248b30` | `solver.solve.cpu_locked` | `0x1400533d2` |
| `0x140248b48` | `solver.solve.cpu_locked.finalize` | `0x14005349a`, `0x1400535b3` |
| `0x140248ba8` | `solver.solve.2p.construct` | `0x1400536bb` |
| `0x140248bc8` | `solver.solve.2p.run` | `0x14005376b` |
| `0x140248b70` | `solver.solve.3p.construct` | `0x1400538f0` |
| `0x140248b90` | `solver.solve.3p.run` | `0x14005398e` |
| `0x140248be0` | `solver.finalize.compute` | `0x140053b3c`, `0x140053bc7` |
| `0x140248bf8` | `solver.finalize.publish` | `0x140053d4e`, `0x140053dcf` |

---

## 7. dT Table — HTTP Method/Path → Internal Method (from embedded Vue 3 frontend)

| Method | Path | Internal Method | Timeout |
|--------|------|-----------------|---------|
| POST | /api/init | `solver.init` | 120000 ms |
| POST | /api/allocate | `solver.allocate` | 120000 ms |
| GET | /api/memory-usage | `solver.memoryUsage` | 30000 ms |
| GET | /api/environment/check | `solver.environment.check` | 5000 ms |
| POST | /api/gpu-solve | `solver.solve.start` | 30000 ms |
| POST | /api/solve/preflight | `solver.solve.preflight` | 30000 ms |
| POST | /api/export/current-street | `solver.export.currentStreet` | 120000 ms |
| POST | /api/export/current-street-file | `desktop.strategy.exportCurrentStreetFile` | null |
| POST | /api/export/full-file | `desktop.strategy.exportFullFile` | null |
| GET | /api/export/full-file/progress | `desktop.strategy.exportFullFileProgress` | null |
| POST | /api/import/full-file | `desktop.strategy.importFullFile` | null |
| POST | /api/import/node | `desktop.strategy.importNode` | 30000 ms |
| POST | /api/import/close | `desktop.strategy.closeImport` | null |
| GET | /api/solve/status | `solver.solve.status` | 5000 ms |
| POST | /api/solve/stop | `solver.solve.stop` | 5000 ms |
| POST | /api/apply-history | `solver.history.apply` | 5000 ms |
| GET | /api/results | `solver.node.results` | 5000 ms |
| POST | /api/actions-after | `solver.node.actionsAfter` | 5000 ms |
| GET | /api/current-player | `solver.node.currentPlayer` | 5000 ms |
| GET | /api/current-board | `solver.node.currentBoard` | 5000 ms |
| POST | /api/lock/current | `solver.lock.current.set` | 5000 ms |
| GET | /api/lock/current | `solver.lock.current.get` | 5000 ms |
| POST | /api/lock/unlock | `solver.lock.current.clear` | 5000 ms |
| GET | /api/lock/list | `solver.lock.list` | 5000 ms |
| POST | /api/lock/clear | `solver.lock.clearAll` | 5000 ms |
| GET | /api/possible-cards | `solver.cards.possible` | 5000 ms |
| GET | /api/num-actions | `solver.node.numActions` | 5000 ms |
| POST | /api/parameters/export | `desktop.parameters.export` | 5000 ms |
| POST | /api/parameters/open-folder | `desktop.parameters.openFolder` | 5000 ms |
| POST | /api/external/open | `desktop.external.open` | 5000 ms |
| POST | /api/parameters/load | `desktop.parameters.load` | 5000 ms |
| POST | /api/ranges/load | `desktop.ranges.load` | 5000 ms |
| POST | /api/settings/load | `desktop.settings.load` | 5000 ms |
| POST | /api/settings/save | `desktop.settings.save` | 5000 ms |
| GET | /api/parameters/list?path= | `desktop.parameters.list` | 5000 ms |
| GET | /api/ranges/list?path= | `desktop.ranges.list` | 5000 ms |
| GET | /api/private-cards/<n> | `solver.cards.private` | 5000 ms |

Default timeout = 5000 ms. Base URL: `http://127.0.0.1:6006`.

---

## 8. Internal Solver Methods (called directly, bypassing dT/HTTP)

- `solver.ensureIdle()`
- `solver.policy(postNode, holes)`
- `solver.node(history)`
- `solver.possibleCards()`
- `solver.prepare(payload, settings, onCancelled, onStatus, hooks)`
- `solver.stop()`
- `solver.confirmStopped()`
- `solver.may`
- `solver.confirm`
- `solver.ensure`
- `solver.practice`

---

## 9. Complete Method Name Inventory

All method strings found in `.rdata`:

```
solver.allocate
solver.cards.private
solver.cards.possible
solver.environment.check
solver.export.currentStreet
solver.export.fullFile
solver.export.fullFile.status
solver.finalize.compute
solver.finalize.publish
solver.history.apply
solver.host.allocate
solver.init
solver.layout.validate
solver.lock
solver.lock.clearAll
solver.lock.current.clear
solver.lock.current.get
solver.lock.current.set
solver.lock.list
solver.memoryUsage
solver.node.actionsAfter
solver.node.currentBoard
solver.node.currentPlayer
solver.node.numActions
solver.node.results
solver.node
solver.policy
solver.practice
solver.prepare
solver.solve.2p.construct
solver.solve.2p.run
solver.solve.3p.construct
solver.solve.3p.run
solver.solve.cpu_locked
solver.solve.cpu_locked.finalize
solver.solve.dispatch
solver.solve.preflight
solver.solve.start
solver.solve.status
solver.solve.stop
solver.strategy.archive.close
solver.strategy.archive.node
solver.strategy.archive.open
solver.stop
solver.may
solver.confirm
solver.ensure
desktop.external.open
desktop.parameters.export
desktop.parameters.list
desktop.parameters.load
desktop.parameters.openFolder
desktop.ranges.list
desktop.ranges.load
desktop.settings.load
desktop.settings.save
desktop.strategy.close
desktop.strategy.closeImport
desktop.strategy.export
desktop.strategy.exportCurrentStreetFile
desktop.strategy.exportFullFile
desktop.strategy.exportFullFileProgress
desktop.strategy.import
desktop.strategy.importFullFile
desktop.strategy.importNode
desktop.strategy.closeImport
```

**~60 total methods** (20 in if-else chain + 23+ .rdata cross-refs + internal methods).

---

## 10. Handler → Method Mapping Status

| Mapping | Status | Reason |
|---------|--------|--------|
| Method string → dispatch chain code VA | ✅ Complete | If-else chain at `0x140061260`–`0x14006829e` |
| Dispatch chain code VA → handler VA | ⚠️ Partial | 6 methods have handler VAs; 14 handled inline |
| Method string → handler VA (direct) | ❌ Blocked | No static table exists |
| Handler object → method name | ❌ Blocked | Name stored in heap at `[handler_obj+0x80]`, set at runtime |
| Handler creator → handler VA | ⚠️ Partial | Creator `0x140036d70` known, but method name passed via register |

**Required for complete mapping:** Dynamic instrumentation of dispatcher `0x140014cf0` and handler creator `0x140036d70` on Windows + NVIDIA GPU.

---

## 11. Key Addresses Reference

| Address | Description |
|---------|-------------|
| `0x140014cf0` | Dispatcher entry point |
| `0x1400099e0` | Handler linked-list lookup |
| `0x140036d70` | Handler creator (6 call sites) |
| `0x140016210` | Request/variant builder |
| `0x140043b60` | Variant accessor |
| `0x140013910` | Handler registration (linked-list insertion) |
| `0x14016ec10` | VTable 1 (16 entries) |
| `0x14016ef90` | VTable 2 (16 entries) |
| `0x1411c6690` | Handler linked-list head (global) |
| `0x1411c6698` | Shared global pointer |
| `0x140061260`–`0x14006829e` | If-else dispatch chain |
