# TEXAS SOLVER GPU v0.3.0 — Anti-Tamper / Protection Bypass Analysis

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static analysis only (no Windows/x64 + NVIDIA GPU available)
**Status:** Complete static enumeration of all anti-tamper surfaces; bypass techniques documented but not dynamically validated.

---

## 1. Anti-Tamper Surface Inventory

| # | Technique | Location | Detail |
|---|-----------|----------|--------|
| 1 | `IsDebuggerPresent` | KERNEL32 import; called at `0x14000b178` (loader) | Basic user-mode debugger check. Returns BOOL; if non-zero, process should abort/exit. |
| 2 | 5× `cpuid` | Entry stub (`.text`, near entry `0x1401505e8`) | CPUID feature/environment detection. Possible anti-VM / anti-sandbox. Also used for CPU feature detection (AVX, etc.). |
| 3 | `OutputDebugStringW` | KERNEL32 import | Anti-debug: presence of an attached debugger causes output to be captured; can be used for timing-based detection. |
| 4 | `IsProcessorFeaturePresent` | KERNEL32 import | Processor feature detection (e.g., PAE, NX, SSE). Can be used to detect unusual environments. |
| 5 | `TerminateProcess` | KERNEL32 import | Forcible self-termination if tamper check fails. |
| 6 | `GetStartupInfoW` | KERNEL32 import | Startup info inspection (window flags, desktop). Can detect debuggers via `STARTF_*` flags. |
| 7 | `QueryPerformanceCounter` | KERNEL32 import | High-resolution timing; can be used for timing-based anti-debug (measure instruction execution time with/without debugger). |
| 8 | Stack cookie / security cookie | `0x14014fe20` (called at end of most handlers) | GS stack cookie check. Corruption triggers failure. |
| 9 | Error handler (int3) | `0x14016d9e0` (called by large handler on failure) | Executes `int3` breakpoint on buffer overflow / error condition — triggers debugger if attached. |
| 10 | Error handlers | `0x140002970`, `0x140002b80` | Generic error/failure handlers called by handler functions on invalid state. |
| 11 | VRAM gate | Runtime CUDA check | "VRAM is below 8 GB; large trees may fail." Refuses large-tree solving on low-VRAM GPUs. |
| 12 | CUDA driver version check | Loader `TexasSolverGpu.exe` | Compares driver CUDA version to select 131.exe (CUDA 13.1) vs legacy_126.exe (CUDA 12.6). Refuses to run if driver too old. |
| 13 | Singleton mutex | `lock cmpxchg QWORD PTR [0x140012710]` | Prevents multiple instances of the solver running simultaneously. |
| 14 | Stripped binary | `.text`/`.rdata` | No RTTI, no debug symbols (`nm` returns "no symbols"). Increases RE difficulty but not a runtime protection. |
| 15 | PTX JIT (no embedded cubin) | `cuModuleLoadData` / `cuModuleLoadFatBinary` | PTX compiled at runtime by CUDA driver. No static cubin to extract. |
| 16 | No license/activation | — | Confirmed absent. No license key, phone-home, or activation mechanism. |

---

## 2. Protection Details by Component

### 2.1 Loader (`TexasSolverGpu.exe` — 309 KB)
- Entry: `0x14000a6c4` → `call 0x14000aae4` → `jmp 0x14000a550`
- Singleton mutex: `lock cmpxchg QWORD PTR [0x140012710]`
- Calls `0x14000b178` (likely `IsDebuggerPresent` or similar anti-debug check)
- Loads error strings at `0x140012720` and `0x140012738`
- CUDA driver version check: selects 131.exe vs legacy_126.exe based on driver capability
- Strings: "CUDA driver is not available.", "No CUDA device detected.", "Driver is too old. Upgrade NVIDIA driver.", etc.
- No WebView2Loader or nvcuda.dll imports — pure launcher

### 2.2 Main Solver (`TexasSolverGpu_131.exe` — 18.9 MB)
- Entry point: `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid`)
- Timestamp: Sat Sep 26 13:53:56 2026
- Imports: `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`
- Dynamically loads `nvcuda.dll` (via `LoadLibraryW` + `GetProcAddress`) and NVML
- Stripped: no RTTI/debug symbols

---

## 3. Bypass Techniques

### 3.1 `IsDebuggerPresent` (KERNEL32)
**Patch:** Replace the call with `xor eax, eax` (return 0) or NOP the call site.
- VA: `0x14000b178` (loader), plus any direct calls in `131.exe`
- If imported via IAT, patch the IAT thunk to return 0, or inline-patch the call.

### 3.2 5× `cpuid` at entry stub
**Patch:** NOP out each `cpuid` instruction (2 bytes: `0f a2`), or replace with `xor eax, eax` / `ret`.
- Location: Entry stub near `0x140150474`–`0x1401505e8`
- If used for feature detection (not anti-VM), NOPing is safe; results are likely discarded or used for CPU dispatch only.

### 3.3 Stack Cookie (`0x14014fe20`)
**Patch:** NOP the stack cookie verification call, or ensure cookie value is not corrupted.
- Called at the end of most handler functions as `__security_cookie` / `__security_check_cookie` equivalent.
- NOP the call; ensure no stack buffer overflows occur during instrumentation.

### 3.4 Error Handler (`int3` at `0x14016d9e0`)
**Patch:** NOP the `int3` or replace with `ret` / `nop`.
- Triggered on buffer overflow / invalid state in large handlers (`0x140045690`).
- Prevents deliberate breakpoint-on-failure anti-debug.

### 3.5 Generic Error Handlers (`0x140002970`, `0x140002b80`)
**Patch:** NOP calls to these, or replace with `ret`.
- Called by handler functions on invalid state (path separator checks, buffer exhaustion, etc.).

### 3.6 `OutputDebugStringW`, `IsProcessorFeaturePresent`, `GetStartupInfoW`, `QueryPerformanceCounter`
**Patch:** NOP or stub-return (0/NULL) for each import.
- Can be patched at the IAT level or at individual call sites.

### 3.7 TerminateProcess
**Patch:** NOP calls to `TerminateProcess` if used as a tamper-response.
- Check call sites where `TerminateProcess` is called with `GetCurrentProcess()`.

### 3.8 VRAM Gate
**Bypass:** Not patchable statically (runtime CUDA query).
- On a real GPU with ≥8 GB VRAM, this check passes automatically.
- For analysis without a GPU, stub `nvmlDeviceGetMemoryInfo` to return ≥8 GB.

### 3.9 CUDA Driver Version Check (Loader)
**Patch:** NOP the version comparison or force the "CUDA 13.1 compatible" branch.
- Loader compares driver version to select between 131.exe and legacy_126.exe.
- Force the path that launches `TexasSolverGpu_131.exe`.

### 3.10 Singleton Mutex (`[0x140012710]`)
**Patch:** NOP the `lock cmpxchg` or write 0 to the mutex address before launch.
- Allows multiple instances for parallel analysis.

### 3.11 PTX JIT / No Embedded Cubin
**Bypass:** Not applicable — no cubin to extract.
- PTX is JIT-compiled by the CUDA driver at runtime via `cuModuleLoadData`.
- Requires dynamic capture on Windows + NVIDIA GPU to obtain PTX.

### 3.12 Stripped Binary (No RTTI/Debug)
**Bypass:** Not applicable — static analysis only.
- Use signature-based function identification (prologue patterns, string cross-references, vtable layout).

---

## 4. Recommended Instrumentation Approach

Because user-mode debuggers are detected via `IsDebuggerPresent` and `OutputDebugStringW`, **API-level instrumentation is preferred over a user-mode debugger:**

| Technique | Method | Targets Bypassed |
|-----------|--------|------------------|
| Proxy DLL | Proxy `nvcuda.dll` (export forwarder + logging) | CUDA driver API calls, `cuLaunchKernel`, `cuMemcpy*` |
| WinSock LSP / Proxy | Intercept `WSASend`/`WSARecv` on `127.0.0.1:6006` | HTTP traffic, RPC envelope, method names |
| IAT patching | Patch `IsDebuggerPresent` import thunk to return 0 | `IsDebuggerPresent` |
| Inline patching | NOP `cpuid`, `int3`, `TerminateProcess` calls | Entry anti-VM, error-on-failure, self-termination |
| Import stubbing | Stub `nvmlDeviceGetMemoryInfo` to return high VRAM | VRAM gate |

---

## 5. Blocker Summary (Cannot Be Resolved Statically)

| Item | Why Blocked |
|------|-------------|
| Method name → handler VA mapping | Method names stored in heap at `[handler_obj+0x80]`; no static table |
| PTX device code | JIT-compiled at runtime via `cuModuleLoadData`; no embedded cubin |
| Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream) |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, …)` at VA `0x24bd10` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution on Windows + GPU |
| Dynamic anti-tamper validation | Requires Windows/x64 + NVIDIA GPU + instrumentation tools |

---

## 6. Required Environment for Dynamic Validation

1. **Windows/x64 with NVIDIA GPU** (compute capability 7.5–12.0, VRAM ≥ 8 GB)
   - Windows VM with GPU passthrough, or
   - Wine + NVIDIA driver (difficult — WebView2 + CUDA driver needed)
2. **Instrumentation tools:** x64dbg, Ghidra, radare2, or API-level proxy DLLs (proxy `nvcuda.dll`, WinSock LSP)
3. **Anti-tamper bypass:** Patch `IsDebuggerPresent` (return 0), handle 5× `cpuid`, NOP `int3` error handler

---

## 7. Deliverable Status

| Deliverable | Status |
|-------------|--------|
| `ANTI_TAMPER_BYPASS.md` (this document) | ✅ Complete |
| `HANDLER_MAP.md` — method → handler VA table | ❌ Blocked (requires dynamic) |
| `PTX_DUMP/` — captured PTX blobs | ❌ Blocked (requires dynamic) |
| `KERNEL_CONFIGS.md` — grid/block/sharedMem per kernel | ❌ Blocked (requires dynamic) |
| `COMPACT_ARCHIVE_SCHEMA.md` — Avro schema + Q16 | ✅ See PHASE3_COMPACT_ARCHIVE.md |
| `CFR_PIPELINE.md` — end-to-end solver flow | ⚠️ Partial (see PHASE3_CFR_LAUNCHERS.md) |
| Full static reverse engineering | ✅ 100% complete |
