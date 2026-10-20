# TEXAS SOLVER GPU v0.3.0 — FINAL REVERSE ENGINEERING SUMMARY (Updated)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Analysis host:** Linux (objdump/readelf/strings/python only — no Ghidra/radare2/x64dbg)
**Status:** Static analysis COMPLETE. Dynamic analysis BLOCKED — no Windows/x64 + NVIDIA GPU.

---

## 1. Major New Findings (This Session)

### 1.1 Handler VTable 1 at `0x14016ec10` — STATICALLY RECOVERED

All 6 handler creator call sites use this vtable:

| Call Site | VTable |
|-----------|--------|
| `0x14000634e` | `0x14016ec10` |
| `0x14000649e` | `0x14016ec10` |
| `0x140014933` | `0x14016ec10` |
| `0x1400151f3` | `0x14016ec10` |
| `0x1400328d3` | `0x14016ec10` |
| `0x140033a9a` | `0x14016ec10` |

**16 vtable entries:**

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b660` | Code — cleanup/destroy |
| 1 | `0x14001d3f0` | Code — memory accessor → IAT `0x14016d8c0` |
| 2 | `0x14001e280` | Code — memory accessor → IAT `0x14016d8b8` |
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

### 1.2 Handler VTable 2 at `0x14016ef90` — STATICALLY RECOVERED

Referenced by functions `0x140017100` and `0x14001b720`:

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b720` | Code — sets vtable, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| 1 | `0x140014fb65` | Odd address (6-byte pattern — possible IAT thunk) |
| 2 | `0x140014fb5f` | Odd address (possible IAT thunk) |
| 3 | `0x1400382c0` | Code — real function |
| 4 | `0x140039940` | Code — real function |
| 5 | `0x140014fb59` | Odd address (possible IAT thunk) |
| 6 | `0x140045ad0` | Code — real function |
| 7–14 | `0x140014fb53`…`0x140014fb35` | Odd addresses (possible IAT thunks, 6-byte spacing) |
| 15 | `0x140195f860` | Data pointer |

**~22 total handler function addresses recovered statically.**

### 1.3 Handler Object Layout (Confirmed)

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]`/`[+0x20]` | IAT `0x14016da48` result | Handler interface ptr 1 |
| `[+0x38]`/`[+0x40]` | IAT `0x14016d3d8` result | Handler interface ptr 2 |
| `[+0x50]`/`[+0x58]` | IAT `0x14016d398` result | Handler interface ptr 3 |
| `[+0x68]` | IAT `0x14016d398` check | Validity / ref-count result |
| `[+0x74]` | global `0x1411c6698` | Shared global pointer |
| `[+0x7c]` | valid flag | Set to 1 by creator |
| `[+0x80]` | allocated memory | **Method name string stored here** |

**Method names are in heap memory at `[handler_obj+0x80]` — static method-name → handler-address mapping is IMPOSSIBLE without dynamic execution.**

### 1.4 Dispatcher `0x140014cf0` — 19 Call Sites

| Call Site VA |
|-------------|
| `0x140015660` |
| `0x14001e688` |
| `0x140022c95` |
| `0x140022d03` |
| `0x140022d6b` |
| `0x140022daf` |
| `0x140023650` |
| `0x1400236be` |
| `0x140023726` |
| `0x14002376a` |
| `0x1400243ae` |
| `0x14002443a` |
| `0x1400244e6` |
| `0x140024581` |
| `0x140024630` |
| `0x1400246d4` |
| `0x140024718` |
| `0x1400259e4` |
| `0x140025ab0` |
| `0x140025b09` |

### 1.5 Frontend Blob Extraction

Extracted embedded Vue 3/Vite frontend from `.rdata` at file offset `0x22a000`:

- **Complete `dT` function** (blob offset `0x1325a`): All 38 method→HTTP-path mappings confirmed
- **Complete timeout map** (blob offset `0x14403`): All per-method timeouts confirmed
- `index.html` at blob offset `0x245e90`
- Asset table at blob offset `0x1c5dc`

---

## 2. Documents Updated

| Document | Change |
|----------|--------|
| `HANDLER_VTABLE_RECON.md` | **NEW** — Complete vtable documentation (both vtables, all handler addresses, object layout) |
| `PHASE3_HANDLER_MAP.md` | Added §7 — Handler vtable recovery (static) |
| `PHASE4_STATIC_RECON.md` | Updated §6 — Handler address recovery (partial static) |
| `PHASE5_DYNAMIC_PLAN.md` | Updated — Handler vtable finding noted |
| `FINAL_RECON_SUMMARY.md` | Updated — Handler vtable + 22 handler addresses, bottom line revised |
| `PHASE6_FRONTEND_API_DEEP_DIVE.md` | Already complete (no changes needed) |

All changes committed to git (2 commits):
- `e8d5ac4` — Static handler vtable recovery: 15 handler function addresses at 0x14016ec10
- `5ecf9e6` — Added second handler vtable at 0x14016ef90

---

## 3. What Remains Blocked (Requires Windows + NVIDIA GPU)

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Method names in heap at `[handler_obj+0x80]`, not in `.rdata` |
| PTX device code | JIT-compiled by CUDA driver at runtime (`cuModuleLoadData`), no embedded cubin |
| Kernel launch configs | Runtime `cuLaunchKernel` args |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution |
| Anti-tamper bypass | `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW` |

---

## 4. Key Evidence Files

- `/kaggle/working/re_lab/texassolvergpu/HANDLER_VTABLE_RECON.md` — Vtable details
- `/kaggle/working/re_lab/texassolvergpu/FINAL_RECON_SUMMARY.md` — Overall summary
- `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` — Target binary (PE32+)
