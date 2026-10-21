# TEXAS SOLVER GPU v0.3.0 — Handler Function Recon (Static Disassembly)

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static disassembly of `.text` section via objdump (GNU Binutils 2.42)
**Status:** Complete — all handler functions from both vtables disassembled and analyzed

---

## 1. Handler VTable 1 at `0x14016ec10` — 16 Entries

All 6 handler creator call sites (`0x14000634e`, `0x14000649e`, `0x140014933`, `0x1400151f3`, `0x1400328d3`, `0x140033a9a`) use this vtable.

| Entry | VA | Name | Behavior |
|-------|-----|------|----------|
| 0 | `0x14001b660` | `cleanup_destroy` | Destructor. Calls `0x140017070`; if `edx & 1`, calls `0x14014fca4` (free) with size `0x98`. Returns handler obj ptr. |
| 1 | `0x14001d3f0` | `mem_accessor_d8c0` | Loads `[rcx+0x80]` (allocated memory/method name); if non-null, `jmp [0x14016d8c0]` (IAT thunk). Returns null if `[rcx+0x80] == 0`. |
| 2 | `0x14001e280` | `mem_accessor_d8b8` | Same pattern as entry 1, but jumps to IAT `0x14016d8b8`. |
| 3 | `0x140038110` | `substantial_handler` | ~110 bytes. Writes byte to buffer via IAT `0x14016d2b8` or `0x14016da70`. Checks `[rbx+0x68]` flag; if set, calls `0x14016da70` twice with byte value. Buffer I/O with chunk management. |
| 4 | `0x140039980` | `small_accessor` | ~54 bytes. Bit manipulation on WORD values. Calls `0x140021460`. Reads/writes `[rcx+0x38]` and `[rcx+0x50]`. |
| 5 | `0x14014c6aa` | `iat_thunk_d348` | `jmp QWORD PTR [0x14016d348]` — import thunk, not native code. |
| 6 | `0x140045a60` | `handler_45a60` | ~36 bytes. String buffer reader via `[rcx+0x38]` → `[rax]` (buffer ptr). Calls `[rax+0x38]` (vtable method) then `[rax+0x20]`. Returns byte or -1. |
| 7 | `0x140045690` | `large_handler` | ~259 bytes (largest). Complex string/buffer reader. SSO (≤15 bytes inline) vs heap string support. Reads in 0xfff (4096) byte chunks. Calls `0x1400017d0` (variant string copy/allocator), IAT `0x14016d3a0`. Buffer exhaustion handling with `0x140002970` (error). |
| 8 | `0x140046480` | `handler_46480` | ~95 bytes. Buffer read/copy. Takes `(handler, dest, count)`. Chunked reads at 0xfff boundaries. Calls `0x140151238` (memcpy), IAT `0x14016da78`. Returns bytes copied. |
| 9 | `0x1400465e0` | `handler_465e0` | ~63 bytes. Buffer read/copy similar to entry 8. Calls `0x140151238`, IAT `0x14016daa8`. |
| 10 | `0x140043200` | `handler_43200` | ~60 bytes. Calls `0x14001c5b0`, IAT `0x14016dab0`, IAT `0x14016da68`, `0x14001de20`. Stores results at `[rbx]` and `[rbx+0x10]`. Returns handler result struct. |
| 11 | `0x140043480` | `handler_43480` | ~58 bytes. Similar to entry 10. Calls `0x14001c5b0`, IAT `0x14016da88`. Stores results at `[rbx]` and `[rbx+0x10]`. |
| 12 | `0x140043a80` | `handler_initializer` | Handler object constructor helper. Sets `[rbx+0x7c]=1` (valid), `[rbx+0x71]=sil`. Calls IAT `0x14016d400` (init). Calls IAT `0x14016da48` → stores at `[rbx+0x18]`,`[rbx+0x20]`,`[rbx+0x38]`,`[rbx+0x40]`,`[rbx+0x50]`,`[rbx+0x58]`. Sets `[rbx+0x80]=rdi` (allocated memory), `[rbx+0x74]=global 0x1411c6698`, `[rbx+0x68]=0`. |
| 13 | `0x1400450a0` | `handler_450a0` | ~24 bytes. Checks `[rcx+0x80] != 0`. Calls `[rax+0x18]` (vtable method on handler obj), compares to -1. Calls IAT `0x14016da58` with `[rbx+0x80]`. |
| 14 | `0x14002d700` | `handler_2d700` | ~63 bytes. Calls `0x140013910` (handler registration / linked-list insertion). Calls IAT `0x14016d398`, conditionally calls IAT `0x14016d400`. Also contains path separator checking (detects `\` or `/` in strings). |
| 15 | `0x140195f88` | `data_ptr` | **Data pointer** (not code) — points to `.rdata` at `0x140195f88`. |

---

## 2. Handler VTable 2 at `0x14016ef90` — Key Entries

Referenced by functions `0x140017100` and `0x14001b720`.

| Entry | VA | Name | Behavior |
|-------|-----|------|----------|
| 0 | `0x14001b720` | `vtable2_init` | Sets vtable `0x14016ef90` on handler obj, calls `0x14001e0c0`, jumps to IAT `0x14016d410`. Another initializer. |
| 1-2, 5, 7-14 | `0x140014fb35`-`0x140014fb65` | `iat_thunks` | Odd addresses spaced 6 bytes apart — `jmp QWORD PTR [rip+imm32]` import thunks. |
| 3 | `0x1400382c0` | `vtable2_fn_382c0` | ~126 bytes. WORD-sized (UTF-16) string buffer writer. Reads WORD from buffer via IAT `0x14016d428`. Dynamic buffer growth via `0x1400017d0`. Handles `[rcx+0x70]` flags (0x1, 0x2, 0x4). |
| 4 | `0x140039940` | `vtable2_fn_39940` | ~19 bytes. WORD-sized string reader/writer. Increments counter at `[rcx+0x50]`. Reads/writes `[rcx+0x38]` buffer. Returns WORD value. |
| 6 | `0x140045ad0` | `vtable2_fn_45ad0` | ~32 bytes. WORD-sized string reader. Returns WORD from `[rcx+0x38]` buffer via `[rax]`. Handles `[rcx+0x70]` flag 0x4. Returns 0xffff on error. |
| 10 | `0x1400432e0` | `vtable2_fn_432e0` | ~130 bytes. Complex buffer reader with flag-based control flow. Tests `bl & 0x1` and `bl & 0x2` flags against `[rcx+0x70]`. Calls `0x140151238`. Returns result struct at `[r10]`. |
| 11 | `0x140043560` | `vtable2_fn_43560` | ~97 bytes. Complex buffer reader similar to entry 10. Flag-based control flow. Returns result struct at `[r10]`. |
| 15 | `0x140195f860` | `data_ptr` | **Data pointer** (not code). |

---

## 3. IAT Function References Used by Handlers

| IAT VA | Usage | Likely Purpose |
|--------|-------|----------------|
| `0x14016d2b8` | `0x140038110` | Buffer write (byte) |
| `0x14016d330` | `0x1400465e0` | Buffer read (conditional path) |
| `0x14016d338` | `0x140046480` | Buffer read (conditional path) |
| `0x14016d348` | vtable entry 5 | Import thunk |
| `0x14016d398` | `0x14002d700`, creator | Handler validation / ref-count |
| `0x14016d3a0` | `0x140045690` | Buffer operation with flag return |
| `0x14016d3a8` | `0x140038110` | Buffer operation with flag return |
| `0x14016d3f8` | `0x140045690` | Buffer byte read |
| `0x14016d400` | Creator, `0x14002d700` | Handler object init |
| `0x14016d410` | vtable2 entry 0 | Import call after init |
| `0x14016d428` | `0x1400382c0` | WORD buffer read/write |
| `0x14016d58` / `0x14016da58` | `0x1400450a0` | Handler method call |
| `0x14016d68` / `0x14016da68` | `0x140043200` | Handler lookup |
| `0x14016d88` / `0x14016da88` | `0x140043480` | Handler lookup |
| `0x14016da48` | Creator, `0x140043a80` | Handler interface ptr 1 |
| `0x14016da70` | `0x140038110` | Buffer byte write |
| `0x14016da78` | `0x140046480` | Async buffer read |
| `0x14016da80` / similar | various | Buffer operations |
| `0x14016da98` | `0x140045690` | Buffer byte compare/search |
| `0x14016daa0` | `0x140043a80` | Handler validity check |
| `0x14016daa8` | `0x1400465e0` | Async buffer read |
| `0x14016dab0` | `0x140043200` | Handler lookup with params |
| `0x14016d9e0` | `0x140045690` | Error handler (int3 on failure) |
| `0x14016d8c0` | vtable entry 1 | Memory accessor |
| `0x14016d8b8` | vtable entry 2 | Memory accessor |

Other direct calls:
- `0x140017070` — cleanup helper (called by destructor)
- `0x1400017d0` — variant string copy / allocator (called by large handlers)
- `0x140151238` — memcpy / buffer copy (called by buffer handlers)
- `0x140013910` — handler registration / linked-list insertion
- `0x14001c5b0` — handler state check (called by entries 10, 11)
- `0x14001de20` — handler result processing (called by entry 10)
- `0x140021460` — small accessor helper (called by entry 4)
- `0x140002840` — string comparison (called by `0x14002d700`)
- `0x140002970` — error handler (called by large handler on buffer overflow)
- `0x140002b80` — error handler (called by handlers)
- `0x14014fca4` — memory free (called by destructor)
- `0x14014fe20` — stack cookie check (called at end of most handlers)

---

## 4. Handler Object Layout (Confirmed from Disassembly)

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]` | interface_ptr_1 | IAT `0x14016da48` result |
| `[+0x20]` | interface_ptr_1_dup | Same as +0x18 |
| `[+0x38]` | interface_ptr_2 | IAT `0x14016d3d8` result |
| `[+0x40]` | interface_ptr_2_dup | Same as +0x38 |
| `[+0x50]` | interface_ptr_3 | IAT `0x14016d398` result |
| `[+0x58]` | interface_ptr_3_dup | Same as +0x50 |
| `[+0x68]` | validity_flag | IAT `0x14016d398` check result; set to 0 by creator |
| `[+0x70]` | flags_byte | Various flag bits (0x1, 0x2, 0x4 tested by vtable2 handlers) |
| `[+0x71]` | sub_flag | Set by creator to 0; set by initializer to `sil` |
| `[+0x74]` | global_ptr | Global `0x1411c6698` |
| `[+0x7c]` | valid_flag | Set to 1 by creator; set to 1 by initializer |
| `[+0x80]` | allocated_memory | Method name string (heap, runtime only); set by allocator `0x14014fb2f` |

Additional fields seen in handler code:
- `[+0x88]`, `[+0x90]` — buffer size/capacity pair (seen in vtable2 handlers)
- `[+0x70]` — flags (tested as bitfield)

---

## 5. Dispatcher Call Site Analysis

**20+ call sites found** to dispatcher `0x140014cf0`. Pattern at each site:

```
lea rdx, [rip+imm32]    # Load KEY string address from .rdata
lea rcx, [stack_buf]     # Key variant buffer
call 0x140005cd0         # Variant string copy (build key variant)
...                       # (VALUE string pointer loaded from stack buffer)
mov r9b, 0x2             # Flags
mov r8b, 0x1             # Flags
lea rdx, [kv_pair_buf]   # KV pair buffer (key at +0x00, value at +0x10)
lea rcx, [output_buf]    # Output variant struct
call 0x140014cf0         # Dispatcher
```

**Key strings referenced from `.rdata` at call sites:**

| VA | String | Context |
|-----|--------|---------|
| `0x14016eb4c` | `"ok"` + `"result"` + `"error"` | Response envelope keys |
| `0x14016f140` | `"cancelled"` + `"file"` | Status strings |
| `0x14016ee58` | `"path"` + `"/"` + `"\\"` | Path-related |
| `0x14016ffa4` | `"street"` | Current street export |
| `0x14016ffb0` | `"node_count"` | Export progress |
| `0x14016ffc0` | `"bytes_written"` | Export progress |

**Critical finding:** The KEY strings at dispatcher call sites are response envelope keys, not "method". The method name VALUE comes from runtime stack buffers whose contents are determined earlier in the calling function. **Static correlation between dispatcher call sites and specific method names is impossible** — the method name is passed via `[handler_obj+0x80]` (heap) at registration time, not via direct string loads at dispatch time.

---

## 6. Method-Name → Handler-Address Mapping Status

**STATICALLY IMPOSSIBLE.** Confirmed by:

1. Method name strings exist in `.rdata` (all ~60 methods have cross-references) but are used as kv pair VALUES for the dispatcher, not as handler registration names.
2. Handler creator call sites pass method name via registers (rsi/r14/rbx), not direct string loads — none traceable to static strings.
3. Handler objects are heap-allocated by `0x140036d70` at runtime; method names stored at `[handler_obj+0x80]`.
4. Handler objects form a linked list (head at `0x1411c6690`), traversed at runtime by `0x1400099e0`.
5. No static `{method_name_string, handler_fn_ptr}` table exists in `.rdata` or `.data`.

**Required for dynamic recovery:**
- Hook `0x140014cf0` (dispatcher) to log `(method_name_string, handler_VA)` pairs
- Hook `0x1400099e0` (handler lookup) to correlate list traversal with matches
- Read `[handler_obj+0x80]` to extract method name strings at runtime
- Correlate with HTTP traffic on `127.0.0.1:6006`

---

## 7. Dynamic Execution Attempts

| Attempt | Result | Blocker |
|---------|--------|---------|
| Wine (default prefix) | `c0000135` STATUS_DLL_NOT_FOUND | Missing DLL / import resolution failure |
| Wine (WINEARCH=win64, fresh prefix) | `c0000135` STATUS_DLL_NOT_FOUND | Same; no X server for GUI |
| No Windows VM + NVIDIA GPU available | N/A | Required for CUDA context, WebView2, and full execution |

**Conclusion:** All remaining unknowns require dynamic execution on Windows/x64 with NVIDIA GPU:

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Method names in heap at `[handler_obj+0x80]`; no static table |
| PTX device code | JIT-compiled by CUDA driver at runtime via `cuModuleLoadData`; no embedded cubin |
| Kernel launch configurations | Runtime `cuLaunchKernel` args |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, …)` at code VA `0x24bd10` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution |
| Anti-tamper bypass | `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW` |

---

## 8. Related Documents

| Document | Contents |
|----------|----------|
| `HANDLER_VTABLE_RECON.md` | Vtable structure (both vtables, all entry addresses) |
| `HANDLER_FUNCTION_RECON.md` | **This document** — handler function disassembly and behavior |
| `PHASE3_HANDLER_MAP.md` | Native method handler map — dispatch mechanism, method inventory |
| `PHASE4_STATIC_RECON.md` | Extended static recon: dispatcher disassembly, dT table, handler vtable recovery |
| `PHASE5_DYNAMIC_PLAN.md` | Phase 5 dynamic analysis plan |
| `STATIC_EXTENSION_RECON.md` | Static method name strings in `.rdata`, handler creator register tracing |
| `FINAL_RECON_SUMMARY.md` | Overall summary |
| `FINAL_SUMMARY_UPDATED.md` | Updated summary with handler vtable 2 |
| `FINAL_COMPREHENSIVE_RE.md` | Comprehensive reverse engineering report |
| `DEEP_RE_PHASE2.md` | Deep reverse engineering: dispatcher, variant type system, transport protocol |
| `FRONTEND_API_RECON.md` | Frontend API: transport, auth, endpoint map, JSON schemas |
| `CPP_CORE_RECON.md` | C++ data structures: GameState, Node, NodeSnapshot |
| `CUDA_MATH.md` | CUDA kernel mathematics |
| `CUDA_KERNELS_RECON.cpp` | CUDA kernel pseudocode |
| `BINARY_ANALYSIS_RECON.md` | Binary inventory, PE structure, imports, anti-tamper |
| `PHASE3_COMPACT_ARCHIVE.md` | Compact archive format: Avro + DEFLATE, Q16 fields |
| `PHASE3_CFR_LAUNCHERS.md` | CFR kernel launcher inventory, device buffers |
