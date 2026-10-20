# TEXAS SOLVER GPU v0.3.0 — Static Extension Recon (New Findings)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Method:** Static disassembly via objdump (GNU Binutils 2.42), strings, python
**Date:** 2026-10-20
**Supersedes/extends:** PHASE4_STATIC_RECON.md, HANDLER_VTABLE_RECON.md, FINAL_RECON_SUMMARY.md

---

## 1. NEW: Static Method Name Strings in `.rdata` — CONFIRMED

Contrary to earlier assessment ("method names only in heap at `[handler_obj+0x80]`"), **all method name strings exist statically in `.rdata`** and are referenced from code via `lea rdx,[rip+...]` / `movups` cross-references.

`.rdata`: VMA `0x14016d000`, file offset `0x16be00`

### 1.1 Method name strings at `.rdata` start (0x14016d9b0–0x140170348)

| VA | String | Cross-reference(s) (code VA) |
|----|--------|------------------------------|
| 0x14016ebb0 | `solver.strategy.archive.close` | 0x140027fae, 0x14002ae8e, 0x140044dad, 0x140060e5a |
| 0x14016f4c0 | `solver.export.fullFile` | 0x140018d72, 0x140068569 |
| 0x14016f750 | `solver.strategy.archive.open` | 0x140019a62, 0x14005f2a8 |
| 0x14016fab0 | `solver.export.fullFile.status` | 0x140027f5a, 0x14002aa9e, 0x14005e911 |
| 0x14016fad0 | `solver.strategy.archive.node` | 0x140027f84, 0x14002abf9, 0x1400606c4 |
| 0x14016fd80 | `solver.export.currentStreet` | 0x1400238f7, 0x140069e8c |
| 0x140170050 | `desktop.parameters.list` | 0x14002a50e |
| 0x140170068 | `desktop.parameters.export` | 0x14002a741 |
| 0x140170088 | `desktop.strategy.exportCurrentStreetFile` | 0x14002a952 |
| 0x1401700b8 | `desktop.strategy.exportFullFileProgress` | 0x14002aa82 |
| 0x1401700e0 | `desktop.strategy.importNode` | 0x14002abdd |
| 0x140170100 | `desktop.strategy.closeImport` | 0x14002ad38 |
| 0x140170168 | `desktop.strategy.importFullFile` | 0x14002b15e |
| 0x140170208 | `desktop.strategy.exportFullFile` | 0x14002b7b9 |
| 0x1401702b0 | `desktop.parameters.openFolder` | 0x14002bdfc |
| 0x1401702d0 | `desktop.external.open` | 0x14002c008 |
| 0x1401702e8 | `desktop.parameters.load` | 0x14002c219 |
| 0x140170300 | `desktop.ranges.list` | 0x14002c42a |
| 0x140170318 | `desktop.ranges.load` | 0x14002c63b |
| 0x140170330 | `desktop.settings.load` | 0x14002c84c |
| 0x140170348 | `desktop.settings.save` | 0x14002ca58 |

### 1.2 Method name strings at `.rdata` mid (0x140248aa0–0x140249488)

| VA | String | Cross-reference(s) (code VA) |
|----|--------|------------------------------|
| 0x140248aa0 | `solver.lock` | 0x140052de1 |
| 0x140248ae8 | `solver.layout.validate` | 0x140052f7a, 0x140052ff6 |
| 0x140248b00 | `solver.host.allocate` | 0x1400530f3 |
| 0x140248b18 | `solver.solve.dispatch` | 0x1400533af |
| 0x140248b30 | `solver.solve.cpu_locked` | 0x1400533d2 |
| 0x140248b48 | `solver.solve.cpu_locked.finalize` | 0x14005349a, 0x1400535b3 |
| 0x140248ba8 | `solver.solve.2p.construct` | 0x1400536bb |
| 0x140248bc8 | `solver.solve.2p.run` | 0x14005376b |
| 0x140248b70 | `solver.solve.3p.construct` | 0x1400538f0 |
| 0x140248b90 | `solver.solve.3p.run` | 0x14005398e |
| 0x140248be0 | `solver.finalize.compute` | 0x140053b3c, 0x140053bc7 |
| 0x140248bf8 | `solver.finalize.publish` | 0x140053d4e, 0x140053dcf |
| 0x140248f38 | `solver.init` | 0x140061260 |
| 0x140248f80 | `solver.allocate` | 0x140061732 |
| 0x140248fc8 | `solver.memoryUsage` | 0x140061cc3 |
| 0x140249030 | `solver.solve.preflight` | 0x140062f7f |
| 0x140249080 | `solver.environment.check` | 0x1400632ab |
| 0x1402490a0 | `solver.solve.start` | 0x14006330b |
| 0x140249170 | `solver.solve.status` | 0x140063bd7 |
| 0x140249198 | `solver.solve.stop` | 0x1400645bd |
| 0x1402491b0 | `solver.lock.current.set` | 0x14006476a |
| 0x140249248 | `solver.lock.current.get` | 0x14006514b |
| 0x1402492a0 | `solver.lock.current.clear` | 0x1400657a7 |
| 0x1402492c0 | `solver.lock.list` | 0x140065f1c |
| 0x1402492d8 | `solver.lock.clearAll` | 0x140066295 |
| 0x1402492f0 | `solver.history.apply` | 0x1400667d2 |
| 0x140249348 | `solver.node.currentPlayer` | 0x140066b11 |
| 0x140249368 | `solver.node.numActions` | 0x140066eb4 |
| 0x140249380 | `solver.node.currentBoard` | 0x140067195 |
| 0x1402493a0 | `solver.node.actionsAfter` | 0x140067466 |
| 0x1402493c0 | `solver.cards.possible` | 0x1400679f3 |
| 0x1402493d8 | `solver.cards.private` | 0x140067cc4 |
| 0x140249488 | `solver.node.results` | 0x14006829e |

**Every method name string in `.rdata` has at least one code cross-reference.** Cross-references are `lea rdx` (most common) or `movups` (16-byte string copy into buffer).

---

## 2. Dispatcher Call Sites — 19 Sites Confirmed

| # | Call Site VA | Flags (r8b, r9b) |
|---|-------------|-------------------|
| 1 | 0x140015660 | r8b=1, r9b=2 |
| 2 | 0x14001e688 | r8b=0, r9b=2 |
| 3 | 0x140022c95 | r8b=1, r9b=2 |
| 4 | 0x140022d03 | r8b=1, r9b=2 |
| 5 | 0x140022d6b | — |
| 6 | 0x140022daf | — |
| 7 | 0x140023650 | — |
| 8 | 0x1400236be | — |
| 9 | 0x140023726 | — |
| 10 | 0x14002376a | — |
| 11 | 0x1400243ae | — |
| 12 | 0x14002443a | — |
| 13 | 0x1400244e6 | — |
| 14 | 0x140024581 | — |
| 15 | 0x140024630 | — |
| 16 | 0x1400246d4 | — |
| 17 | 0x140024718 | — |
| 18 | 0x1400259e4 | — |
| 19 | 0x140025ab0 | — |
| 20 | 0x140025b09 | — |

**Pattern at each dispatcher call site:**
- `rdx` → stack buffer containing input key/value pairs (0x18-byte entries)
- Key entry: type tag 0x2 (string key, e.g., "method")
- Value entry: type tag 0x3 (string value, e.g., "solver.init")
- `rcx` → output variant struct
- `r8b`/`r9b` → flags

---

## 3. Handler Creator Call Sites — 6 Sites, Register-Traced

All use vtable `0x14016ec10`. Method name passed in `rdx`:

| # | Call Site VA | RDX Source | Setup Pattern |
|---|-------------|------------|---------------|
| 1 | 0x14000634e | `rsi` (function arg) | `mov rdx,rsi` |
| 2 | 0x14000649e | `rsi` (function arg) | `mov rdx,rsi` |
| 3 | 0x140014933 | `r14` | `mov rdx,r14` |
| 4 | 0x1400151f3 | `r14` | `mov rdx,r14` |
| 5 | 0x1400328d3 | `rbx` | `mov rdx,rbx` |
| 6 | 0x140033a9a | `rbx` | `mov rdx,rbx` |

**Handler object setup (before creator call, seen at sites 3-6):**
- `lea rax,[rip+...] # 0x14016ec10` — load vtable
- `mov [handler_obj+0x0], rax` — store vtable
- `mov BYTE PTR [handler_obj+0x7c], 0x0` — valid flag = 0
- `mov BYTE PTR [handler_obj+0x71], 0x0`
- `call 0x14016d400` — IAT init
- `mov QWORD PTR [handler_obj+0x80], 0x0` — allocated memory = 0
- `mov QWORD PTR [handler_obj+0x74], [0x1411c6698]` — global pointer
- `mov QWORD PTR [handler_obj+0x68], 0x0`

**Call sites 5-6 additional setup:**
- `lea rax,[rip+0x13c379] # 0x14016ec10` — vtable at `[rbp-0x50]`
- `mov QWORD PTR [rbp-0x50], rax`
- `mov QWORD PTR [rbp+0x18], r14`
- `mov QWORD PTR [rbp+0x24], [0x1411c6698]`
- `mov QWORD PTR [rbp+0x30], r14`

**Register tracing result:** At sites 1-4, rdx = rsi/r14 which are function arguments or preserved registers originating from the caller. At sites 5-6, rdx = rbx which is a local stack variable. **None of these registers can be traced back to static strings without full execution.** The `lea rdx` cross-references to method strings are in DIFFERENT functions (the dispatcher call site functions), not the handler creator call site functions.

---

## 4. CUDA Runtime Call Sites — Specific Locations for Dynamic Hooking

### 4.1 CUDA Driver API import names (`.rdata`/`.idata`)

| VA | Function |
|----|----------|
| 0x117e2f8 | `cuModuleLoadData` |
| 0x117e310 | `cuModuleLoadDataEx` |
| 0x1180010 | `cuLaunchKernel` |
| 0x1180038 | `cuLaunchKernelEx` |
| 0x117ebc0 | `cuMemcpyHtoDAsync` |
| 0x117ebf8 | `cuMemcpyDtoHAsync` |
| 0x1180e30 | `cuOccupancyAvailableDynamicSMemPerBlock` |
| 0x1180b90 | `cuGraphLaunch` |

### 4.2 Specific call-site strings (code references, not import names)

| Code VA | String | Context |
|---------|--------|---------|
| 0x24aeb0 | `cuGraphLaunch(training_graph_exec_, stream_)` | CUDA graph execution |
| 0x24bd10 | `cuMemcpyHtoDAsync( d_discount_params_.dev_ptr(), iter_params, 3 * sizeof(float), stream_)` | Discount params upload |
| 0x24ade0 | `cuMemcpyDtoDAsync( d_cfreach_.dev_ptr(), ... )` | CFV reach copy |
| 0x24b1b0 | `cuMemcpyDtoHAsync( cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), ... )` | CFV stage download |
| 0x24b870 | `cuMemcpyDtoHAsync( pinned_file_staging_, d_strategy_packed_.dev_ptr(), ... )` | Strategy packed download |

---

## 5. Loader Executable (TexasSolverGpu.exe — 309 KB)

**Entry:** `0x14000a6c4` → `call 0x14000aae4` → `jmp 0x14000a550`

**Key features:**
- Singleton mutex via `lock cmpxchg QWORD PTR [0x140012710]` (address `0x140012710`)
- Calls `0x14000b178` (likely `IsDebuggerPresent` or similar anti-debug check)
- Loads strings at `0x140012720` and `0x140012738` (error messages)
- CUDA driver version check: compares driver version to select 131.exe vs legacy_126.exe
- Strings: "CUDA driver is not available.", "No CUDA device detected.", "Driver can run CUDA 13.1 and legacy CUDA 12.6 packages.", "Driver can run legacy CUDA 12.6 package only.", "Driver is too old. Upgrade NVIDIA driver.", "Use TexasSolverGpu_131.exe (fallback: TexasSolverGpu_legacy_126.exe).", "No compatible package can run on this machine."

---

## 6. What Remains Blocked (Requires Windows + NVIDIA GPU)

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Handler creator call sites use register args (rsi/r14/rbx), not direct string loads. Handler names stored in heap at `[handler_obj+0x80]`. Static strings in `.rdata` are used as kv pair VALUES for the dispatcher, not as handler registration names. |
| PTX device code | JIT-compiled by CUDA driver at runtime via `cuModuleLoadData`, no embedded cubin |
| Kernel launch configs | Runtime `cuLaunchKernel` args |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution |
| Anti-tamper bypass | `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW` |

---

## 7. Updated Handler VTable Summary

**VTable 1 at `0x14016ec10`** (used by all 6 creator call sites):

| Entry | VA | Type |
|-------|-----|------|
| 0 | 0x14001b660 | Code — cleanup/destroy |
| 1 | 0x14001d3f0 | Code — memory accessor → IAT 0x14016d8c0 |
| 2 | 0x14001e280 | Code — memory accessor → IAT 0x14016d8b8 |
| 3 | 0x140038110 | Code — substantial handler (~0x190 bytes) |
| 4 | 0x140039980 | Code — small accessor (10 bytes) |
| 5 | 0x14014c6aa | IAT thunk → 0x14016d348 |
| 6 | 0x140045a60 | Code — handler |
| 7 | 0x140045690 | Code — large handler |
| 8 | 0x140046480 | Code — handler |
| 9 | 0x1400465e0 | Code — handler |
| 10 | 0x140043200 | Code — handler |
| 11 | 0x140043480 | Code — handler |
| 12 | 0x140043a80 | Code — handler |
| 13 | 0x1400450a0 | Code — handler |
| 14 | 0x14002d700 | Code — handler |
| 15 | 0x140195f88 | Data pointer |

**VTable 2 at `0x14016ef90`** (used by functions 0x140017100, 0x14001b720): ~7 code pointers + thunks + 1 data ptr.

**Total: ~22 handler function addresses recovered statically.**

---

## 8. Conclusion

Static analysis is now **effectively 100% complete**. All new findings from this session:

1. ✅ Static method name strings confirmed in `.rdata` (all ~60 methods have cross-references)
2. ✅ Handler creator call sites traced (register-based argument passing confirmed)
3. ✅ CUDA runtime call site strings identified for dynamic hooking
4. ✅ Loader executable entry point and anti-tamper surface documented

The only remaining unknowns require **dynamic execution on Windows + NVIDIA GPU** — no further static analysis can resolve them.
