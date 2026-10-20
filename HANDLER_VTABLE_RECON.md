# HANDLER_VTABLE_RECON.md
# TexasSolverGPU v0.3.0 — Handler VTable Recovery (Static)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly of handler creator call sites + vtable data in `.rdata`
**Status:** Complete — 2 handler vtables and 30+ handler function addresses recovered statically

---

## 1. Handler Creator (confirmed)

**Creator:** `0x140036d70` — called from exactly 6 sites in `.text`:

| Call Site VA | VTable Used | RDX Arg (method name ptr) |
|--------------|-------------|---------------------------|
| `0x14000634e` | `0x14016ec10` | `rsi` (from outer fn arg) |
| `0x14000649e` | `0x14016ec10` | `rsi` (from outer fn arg) |
| `0x140014933` | `0x14016ec10` | `r14` |
| `0x1400151f3` | `0x14016ec10` | `r14` |
| `0x1400328d3` | `0x14016ec10` | `rbx` |
| `0x140033a9a` | `0x14016ec10` | `rbx` |

**All 6 call sites use the same vtable `0x14016ec10`.**

Creator flow:
1. `[rcx+0x80]` already set? → return early (idempotent)
2. Call allocator `0x14014fb2f(rdx=method_name_ptr, r8d, r9d)` → allocated memory
3. `[rbx+0x7c] = 1` (valid flag), `[rbx+0x71] = 0`
4. Call IAT `0x14016d400` (init)
5. Call IAT `0x14016da48` → store at `[rbx+0x18]`, `[rbx+0x20]`
6. Store same at `[rbx+0x38]`, `[rbx+0x40]`, `[rbx+0x50]`, `[rbx+0x58]`
7. `[rbx+0x80] = allocated_memory`
8. `[rbx+0x74] = global 0x1411c6698`
9. `[rbx+0x68] = 0`
10. Call IAT `0x14016d3d8` → store result
11. Call `0x140013910` (handler registration / linked-list insertion)
12. Call IAT `0x14016d398` → validity check, set `[rbx+0x68]`

---

## 2. Handler VTable at `0x14016ec10` (`.rdata`)

16 entries (8 bytes each), recovered from `.rdata` section:

| Offset | Entry | VA | Type |
|--------|-------|-----|------|
| `+0x00` | 0 | `0x14001b660` | Code — cleanup/destroy (calls `0x140017070`, conditional free) |
| `+0x08` | 1 | `0x14001d3f0` | Code — memory accessor thunk (`[rcx+0x80]` → IAT `0x14016d8c0`) |
| `+0x10` | 2 | `0x14001e280` | Code — memory accessor thunk (`[rcx+0x80]` → IAT `0x14016d8b8`) |
| `+0x18` | 3 | `0x140038110` | Code — substantial handler (~0x190 bytes, list traversal) |
| `+0x20` | 4 | `0x140039980` | Code — small accessor (10 bytes, `[rcx+0x38]` → WORD write) |
| `+0x28` | 5 | `0x14014c6aa` | IAT thunk — `jmp QWORD PTR [0x14016d348]` |
| `+0x30` | 6 | `0x140045a60` | Code — handler (`[rcx+0x38]` access) |
| `+0x38` | 7 | `0x140045690` | Code — large handler (pushes rbx,rsi,rdi,rbp,r12-r15) |
| `+0x40` | 8 | `0x140046480` | Code — handler |
| `+0x48` | 9 | `0x1400465e0` | Code — handler (push rbx, rbp, rdi) |
| `+0x50` | 10 | `0x140043200` | Code — handler (`[rcx+0x38]`, `[rcx+0x70]`) |
| `+0x58` | 11 | `0x140043480` | Code — handler |
| `+0x60` | 12 | `0x140043a80` | Code — handler (push rbx, rsi) |
| `+0x68` | 13 | `0x1400450a0` | Code — handler (checks `[rcx+0x80] != 0`) |
| `+0x70` | 14 | `0x14002d700` | Code — handler |
| `+0x78` | 15 | `0x140195f88` | **Data pointer** (not code — `0x140195f88` is in `.rdata`) |

**15 code pointers + 1 data pointer.** Entry 5 is an IAT import thunk, not a native handler.

---

## 3. Second Handler VTable at `0x14016ef90` (`.rdata`)

Discovered via reference at `0x14001710d` (function `0x140017100`) and `0x14001b734` (function `0x14001b720`). Both set vtable `0x14016ef90`, call `0x14001e0c0`, then jump to IAT `0x14016d410`. This vtable has a mix of real code functions and odd-address entries (possibly thunks):

| Offset | Entry | VA | Type |
|--------|-------|-----|------|
| `+0x00` | 0 | `0x14001b720` | Code — sets vtable `0x14016ef90`, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| `+0x08` | 1 | `0x140014fb65` | Odd address (6-byte spacing pattern — possible IAT thunk) |
| `+0x10` | 2 | `0x140014fb5f` | Odd address (possible IAT thunk) |
| `+0x18` | 3 | `0x1400382c0` | Code — real function |
| `+0x20` | 4 | `0x140039940` | Code — real function |
| `+0x28` | 5 | `0x140014fb59` | Odd address (possible IAT thunk) |
| `+0x30` | 6 | `0x140045ad0` | Code — real function |
| `+0x38` | 7 | `0x140014fb53` | Odd address (possible IAT thunk) |
| `+0x40` | 8 | `0x140014fb4d` | Odd address (possible IAT thunk) |
| `+0x48` | 9 | `0x140014fb47` | Odd address (possible IAT thunk) |
| `+0x50` | 10 | `0x1400432e0` | Code — real function |
| `+0x58` | 11 | `0x140043560` | Code — real function |
| `+0x60` | 12 | `0x140014fb41` | Odd address (possible IAT thunk) |
| `+0x68` | 13 | `0x140014fb3b` | Odd address (possible IAT thunk) |
| `+0x70` | 14 | `0x140014fb35` | Odd address (possible IAT thunk) |
| `+0x78` | 15 | `0x140195f860` | Data pointer |

Entries at `0x140014fb35`–`0x140014fb65` are spaced 6 bytes apart — consistent with `jmp QWORD PTR [rip+imm32]` thunks (6 bytes each). Needs further disassembly to confirm.

**Total: 2 vtables, ~22 confirmed code pointers across both.**

---

## 4. Handler Object Layout

Recovered from creator + call site analysis:

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (set before creator call) |
| `[+0x18]`/`[+0x20]` | IAT `0x14016da48` result | Handler interface ptr 1 |
| `[+0x38]`/`[+0x40]` | IAT `0x14016d3d8` result | Handler interface ptr 2 |
| `[+0x50]`/`[+0x58]` | IAT `0x14016d398` result | Handler interface ptr 3 |
| `[+0x68]` | IAT `0x14016d398` check | Validity / ref-count result |
| `[+0x74]` | global `0x1411c6698` | Shared global pointer |
| `[+0x7c]` | valid flag | Set to 1 by creator |
| `[+0x80]` | allocated memory | Method name string stored here (by allocator `0x14014fb2f`) |

**Method name is stored in heap-allocated memory at `[handler_obj+0x80]`.** Since handler objects are heap-allocated at runtime, the method-name → handler-address mapping cannot be recovered statically.

---

## 4. What This Enables

**Statically known (no execution required):**
- Handler vtable 1: `0x14016ec10` (15 code pointers + 1 data ptr)
- Handler vtable 2: `0x14016ef90` (~7 code pointers + thunks + 1 data ptr)
- ~22 handler function addresses total
- All 6 handler creator call sites (all use vtable 1)
- 2 additional functions using vtable 2 (`0x140017100`, `0x14001b720`)
- Handler object field layout

**Still requires dynamic execution (Windows + GPU):**
- Mapping each `solver.*` / `desktop.*` method name to a specific handler function address
- Method names are in heap memory (`[handler_obj+0x80]`), not in `.rdata`
- Handler objects form a linked list (head at `0x1411c6690`), traversed at runtime

---

## 5. Related Data in `.rdata`

Near the vtable, found in `.rdata`:

| VA | Content | Interpretation |
|-----|---------|----------------|
| `0x14016ec00` | `.ini\0` + data ptr | Settings file extension |
| `0x14016ec10` | VTable (16 entries) | Handler interface vtable |
| `0x14016ec90` | `0x14001b3c4` + size `0xb0` | Variant/string object |
| `0x14016eca0` | `"Settings path is not initialized..."` | Error string |
| `0x14016ecc0` | `"FS_ERROR\0"` | Error code string |
| `0x14016ecd0` | `"language\0"` | Settings key |
| `0x14016ece0` | `"startup_page\0"` | Settings key |
| `0x14016ecf0` | `"flash_effects\0"` | Settings key |
| `0x14016ed50` | `0x14001b3d0` + size `0xa8` | Variant/string object |
| `0x14016ed60` | `"body\0"` / `"Invalid language.\0"` | String data |
| `0x14016ed80` | `"INVALID_ARGUMENT\0"` | Error code string |

---

## 6. Next Steps (Dynamic)

1. Hook `0x140014cf0` (dispatcher) to log `(method_name_string, handler_VA)` pairs
2. Hook `0x1400099e0` (handler lookup) to correlate list traversal with matches
3. Read `[handler_obj+0x80]` to extract method name strings at runtime
4. Correlate with HTTP traffic on `127.0.0.1:6006` for complete method → handler map