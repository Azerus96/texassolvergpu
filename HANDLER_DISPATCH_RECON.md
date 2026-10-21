# TEXAS SOLVER GPU v0.3.0 — Handler Dispatch Recon (Static)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly via objdump (GNU Binutils 2.42)
**Date:** 2026-10-20
**Status:** Complete — static method dispatch chain fully recovered

---

## 1. Two-Layer Dispatch Architecture

TexasSolverGPU uses **two independent dispatch layers**:

### Layer 1 — High-Level Method Dispatch (Static if-else chain)
A large function at `0x140061260`–`0x14006829e` implements a sequential if-else chain that compares the incoming method name against known method strings. Each comparison uses:

1. `lea rdx,[rip+imm32]` — load address of method name string from `.rdata`
2. `mov rcx,rbx` — `rbx` holds the incoming method name variant
3. `call 0x140006c30` — string comparison function (returns 1 if equal, 0 if not)
4. `test al,al` / `je <next_method>` — if not matching, fall through to next check
5. On match: execute method-specific inline code block

**String compare function `0x140006c30`:**
- Gets string lengths via `0x14015127a` (handles SSO vs heap strings)
- Compares lengths; if unequal, return 0
- If equal, calls `0x140151250` (byte-by-byte comparison)
- Returns 1 if strings match, 0 otherwise

### Layer 2 — Low-Level Handler Dispatcher (`0x140014cf0`)
Generic kv-pair-based dispatcher that looks up handler objects from a linked list. Used internally by Layer 1 code blocks and by handler infrastructure. See `HANDLER_VTABLE_RECON.md` for details.

---

## 2. Static Method Dispatch Chain — Complete Map

**Dispatch chain location:** `0x140061260` – `0x14006829e` (`.text` section)
**Method strings:** `.rdata` VMA `0x140248aa0`–`0x140249488` (file offset `0x1188aa0`–`0x1189488`)

### 2.1 Chain Entries (in dispatch order)

| Order | Code VA | Method String VA | Method String | On Match → | Next Check (je target) |
|-------|---------|-----------------|---------------|------------|------------------------|
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
| 20 | `0x14006829e` | `0x140249488` | `solver.node.results` | — | (end of chain) |

### 2.2 Notable Dispatch Branch Details

**`solver.init` (0x140061260 → 0x14006a680):**
- Allocates `0xeff0` bytes stack frame
- Calls `0x14011ede0` twice (sub-initialization)
- Loads string at `0x140247fa0`, calls `0x14000cf10`
- Calls `0x140006e50` to get kv pair value
- Calls variant accessor `0x140043b60`, compares result with `0x52e` (1326 = C(52,2) combo count)

**`solver.environment.check` (0x1400632ab → 0x140055840):**
- Calls `0x140055840` directly

**`solver.solve.start` (0x14006330b → 0x1400157a0 → 0x14010b9a0):**
- Loads `[r15] + 0x4ee0`, calls `0x1400157a0`
- Calls `0x14010b9a0`, stores result in `r12d`

---

## 3. First `.rdata` Section Method References (movups Pattern)

**Method strings:** `.rdata` VMA `0x14016d9b0`–`0x140170348`

Some methods use a SIMD-based comparison pattern (`movups xmm0,[method_string]`) instead of `lea`+`call 0x140006c30`. These appear at different code locations, suggesting separate dispatch functions.

| String VA | String | Code Ref VA(s) | Pattern |
|-----------|--------|----------------|---------|
| `0x14016ebb0` | `solver.strategy.archive.close` | `0x140027fae`, `0x14002ae8e`, `0x140044dad`, `0x140060e5a` | lea (4 refs) |
| `0x14016f4c0` | `solver.export.fullFile` | `0x140018d72` (movups), `0x140068569` | movups + lea |
| `0x14016f750` | `solver.strategy.archive.open` | `0x140019a62` (movups), `0x14005f2a8` | movups + lea |
| `0x14016fab0` | `solver.export.fullFile.status` | `0x140027f5a`, `0x14002aa9e`, `0x14005e911` | lea (3 refs) |
| `0x14016fad0` | `solver.strategy.archive.node` | `0x140027f84`, `0x14002abf9`, `0x1400606c4` | lea (3 refs) |
| `0x14016fd80` | `solver.export.currentStreet` | `0x1400238f7` (movups), `0x140069e8c` | movups + lea |
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

## 4. Additional Method Strings (`.rdata` mid section)

| String VA | String | Code Ref VA |
|-----------|--------|-------------|
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

## 5. Handler VTable → Dispatch Relationship

### 5.1 VTable 1 at `0x14016ec10` (15 entries)

| Entry | VA | Function | Role |
|-------|-----|----------|------|
| 0 | `0x14001b660` | Cleanup/destroy | Calls `0x140017070`, conditionally frees 0x98 bytes |
| 1 | `0x14001d3f0` | Memory read accessor | Gets `[rcx+0x80]`, jumps through IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Memory write accessor | Gets `[rcx+0x80]`, jumps through IAT `0x14016d8b8` |
| 3 | `0x140038110` | Byte input handler | Writes byte to buffer, IAT calls, string ops |
| 4 | `0x140039980` | WORD accessor | Writes WORD to `[rcx+0x38]`, compares with r9w |
| 5 | `0x14014c6aa` | IAT thunk | `jmp QWORD PTR [0x14016d348]` |
| 6 | `0x140045a60` | Byte read handler | Reads byte from buffer or calls vtable methods |
| 7 | `0x140045690` | Large handler | String ops, memory allocation (~0x380 bytes) |
| 8 | `0x140046480` | Data read/copy | Copies from internal buffer, chunked IAT reads (0xfff chunks) |
| 9 | `0x1400465e0` | Data write handler | Copies to internal buffer, chunked IAT writes |
| 10 | `0x140043200` | Getter/lookup | Retrieves values, calls IAT methods, returns objects |
| 11 | `0x140043480` | Getter variant | Similar to entry 10, retrieves from buffer |
| 12 | `0x140043a80` | Handler initializer | Sets up handler object with IAT interface pointers, valid flag=1 |
| 13 | `0x1400450a0` | Count/size handler | Calls `[rax+0x18]`, IAT `0x14016da58`, returns count |
| 14 | `0x14002d700` | Registration handler | Calls `0x140013910` (handler registration), IAT `0x14016d398`, `0x14016d400` |
| 15 | `0x140195f88` | Data pointer | Not code |

### 5.2 VTable 2 at `0x14016ef90` (used by `0x140017100`, `0x14001b720`)

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b720` | Code — sets vtable, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| 1-2 | `0x140014fb65`, `0x140014fb5f` | Possible IAT thunks (6-byte spacing) |
| 3 | `0x1400382c0` | Code — real function |
| 4 | `0x140039940` | Code — real function |
| 5 | `0x140014fb59` | Possible IAT thunk |
| 6 | `0x140045ad0` | Code — real function |
| 7-14 | `0x140014fb53`…`0x140014fb35` | Possible IAT thunks (6-byte spaced) |
| 15 | `0x140195f860` | Data pointer |

---

## 6. Handler Object Layout

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]`/`[+0x20]` | IAT `0x14016da48` result | Handler interface ptr 1 |
| `[+0x38]`/`[+0x40]` | IAT `0x14016d3d8` result | Handler interface ptr 2 |
| `[+0x50]`/`[+0x58]` | IAT `0x14016d398` result | Handler interface ptr 3 |
| `[+0x68]` | IAT `0x14016d398` check | Validity / ref-count result |
| `[+0x70]` | flags byte | Bit 0x4 = read-only?, bit 0x2 = write-enabled? |
| `[+0x74]` | global `0x1411c6698` | Shared global pointer |
| `[+0x7c]` | valid flag | Set to 1 by creator/initializer |
| `[+0x80]` | allocated memory | Method name string (heap, runtime only) |
| `[+0x88]`/`[+0x90]` | buffer start/end | Read/write buffer bounds |
| `[+0x38]`/`[+0x40]` | buffer ptr/size | Current buffer position/size |

---

## 7. Key Helper Functions

| VA | Name | Description |
|-----|------|-------------|
| `0x140006c30` | String compare | Compares two variant strings (SSO/heap-aware), returns 1 if equal |
| `0x14015127a` | String length | Gets length of variant string |
| `0x140151250` | Byte compare | Byte-by-byte string comparison |
| `0x140043b60` | Variant accessor | Jump table dispatch on type tag (0→0, 1→count/16, 2→data ptr, 3-9→1) |
| `0x140016210` | Variant builder | Type dispatch on `dl`, jump table `0x140016370` |
| `0x1400099e0` | Handler lookup | Linked-list handler lookup by name |
| `0x140013910` | Handler registration | Linked-list insertion, global head `0x1411c6690` |
| `0x140036d70` | Handler creator | Creates handler object, calls vtable entry 12 |
| `0x140151238` | Memory copy | memcpy-like function used by handlers 8 and 9 |
| `0x14001ef90` | String length | String length helper |
| `0x1400071b0` | Variant string compare | SSO vs heap string compare |

---

## 8. What Is Statically Known vs. Requires Dynamic

### Statically Known (no execution required):
- Complete method dispatch chain (all 19 methods in chain + 23 additional method string refs)
- All method name strings in `.rdata` with code cross-references
- Both handler vtables with all 30+ handler function addresses
- Complete disassembly of all handler functions
- Handler object field layout
- All helper functions and their roles
- Dispatch chain code block VA for each method

### Requires Dynamic Execution (Windows + GPU):
- Method name → handler vtable entry mapping (handler objects created at runtime, method names in heap at `[handler_obj+0x80]`)
- Which specific handler function each method ultimately invokes
- PTX device code (JIT-compiled at runtime)
- Kernel launch configurations
- Compact archive bytes
- Full solve cycle execution

---

## 9. Dispatch Chain Entry Points Summary

The dispatch chain begins at `0x140061260` and sequentially checks methods. The function containing this chain appears to be the main RPC request handler. Each matched branch calls method-specific inline code (not handler vtable entries directly), which may internally use the Layer 2 dispatcher or handler vtable functions.

**Chain start:** `0x140061260` (checks `solver.init`)
**Chain end:** `0x14006829e` (checks `solver.node.results`)
**Total methods in chain:** 19
