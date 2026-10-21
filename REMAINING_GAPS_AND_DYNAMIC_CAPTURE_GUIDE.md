# TexasSolverGPU v0.3.0 — Remaining Gaps & Windows+GPU Dynamic Capture Guide

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Static analysis:** 100% COMPLETE
**Dynamic analysis:** BLOCKED — requires Windows/x64 + NVIDIA GPU

---

## 1. What Requires Dynamic Capture (Cannot Be Resolved Statically)

| # | Item | Why Blocked | Priority |
|---|------|-------------|----------|
| 1 | Method name → handler VA mapping | Method names stored in heap at `[handler_obj+0x80]`; creator call sites pass names via registers (rsi/r14/rbx), not direct string loads | **P0 — Critical** |
| 2 | PTX device code (all 45 kernels) | JIT-compiled by CUDA driver at runtime via `cuModuleLoadData`; no embedded cubin | **P0 — Critical** |
| 3 | Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream, args) | **P1 — High** |
| 4 | `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` at code VA `0x24bd10` | **P1 — High** |
| 5 | Compact archive bytes | Written at runtime by `archive.open/node/close` | **P2 — Medium** |
| 6 | Full solve cycle | Requires live solver execution | **P2 — Medium** |
| 7 | Dynamic anti-tamper validation | Requires Windows/x64 + NVIDIA GPU + instrumentation tools | **P3 — Low** |

---

## 2. Required Environment

### 2.1 Hardware/OS

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10/11 x64 |
| GPU | NVIDIA, compute capability 7.5–12.0 (RTX 20-series or newer) |
| VRAM | ≥ 8 GB recommended (VRAM gate check) |
| CUDA Driver | ≥ 13.1 compatible (for 131.exe) or ≥ 12.6 (for legacy_126.exe) |

### 2.2 Options

**Option A: Windows VM with GPU passthrough (recommended)**
- KVM/QEMU + VFIO + NVIDIA GPU passthrough
- Full Windows x64 with direct GPU access
- Best for debugging and instrumentation

**Option B: Bare metal Windows**
- Native Windows installation
- Simplest setup, best performance

**Option C: Wine + NVIDIA driver (NOT RECOMMENDED)**
- WebView2 + CUDA driver under Wine is extremely difficult
- Wine on this Linux host is broken (cannot load kernel32.dll)
- Not viable

### 2.3 Instrumentation Tools

| Tool | Purpose |
|------|---------|
| x64dbg | User-mode debugging, API hooking |
| Ghidra | Decompilation, function matching |
| radare2 | Binary analysis, scripting |
| Proxy DLLs | API-level interception (preferred over debugger due to anti-tamper) |
| WinSock LSP | HTTP traffic interception on 127.0.0.1:6006 |

---

## 3. Anti-Tamper Bypass (Required Before Dynamic Analysis)

| # | Technique | Patch Location | Bypass Method |
|---|-----------|---------------|---------------|
| 1 | `IsDebuggerPresent` | KERNEL32 import; `0x14000b178` (loader) | Patch IAT thunk → return 0; or inline NOP call |
| 2 | 5× `cpuid` | Entry stub `0x140150474`–`0x1401505e8` | NOP each `cpuid` (2 bytes: `0f a2`) |
| 3 | `OutputDebugStringW` | KERNEL32 import | Stub → return immediately |
| 4 | `IsProcessorFeaturePresent` | KERNEL32 import | Stub → return 0 |
| 5 | `TerminateProcess` | KERNEL32 import | NOP call sites with `GetCurrentProcess()` |
| 6 | `GetStartupInfoW` | KERNEL32 import | Stub → return default |
| 7 | `QueryPerformanceCounter` | KERNEL32 import | Stub → return fixed value |
| 8 | Stack cookie | `0x14014fe20` | NOP stack cookie check call |
| 9 | Error handler (int3) | `0x14016d9e0` | NOP `int3` or replace with `ret` |
| 10 | Error handlers | `0x140002970`, `0x140002b80` | NOP calls |
| 11 | VRAM gate | Runtime NVML query | Stub `nvmlDeviceGetMemoryInfo` → return ≥8 GB |
| 12 | CUDA driver version check | Loader `TexasSolverGpu.exe` | Force CUDA 13.1 branch |
| 13 | Singleton mutex | `lock cmpxchg QWORD PTR [0x140012710]` | Write 0 to mutex address |

**Recommended approach:** API-level instrumentation (proxy `nvcuda.dll`, WinSock LSP) rather than user-mode debugger, due to `IsDebuggerPresent` and `OutputDebugStringW` detection.

---

## 4. Dynamic Capture Procedure

### 4.1 Step 1: Environment Setup

1. Install Windows x64 with NVIDIA GPU drivers
2. Install CUDA toolkit 13.1
3. Copy `TexasSolverGpu_131.exe`, `nvcuda.dll` (real), `WebView2Loader.dll` to working directory
4. Replace `nvcuda.dll` with the proxy DLL (see `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp`)
5. Rename real `nvcuda.dll` → `nvcuda_real.dll` (proxy loads it via `LoadLibraryA("nvcuda_real.dll")`)
6. Apply anti-tamper patches (IsDebuggerPresent, cpuid, etc.)

### 4.2 Step 2: Capture CUDA API Calls

The proxy DLL logs all CUDA driver API calls to `%TEMP%\nvcuda_proxy_trace.log`. Key calls to capture:

| API Call | What It Reveals |
|----------|-----------------|
| `cuModuleLoadData` | PTX blob for each kernel (capture the `image` pointer) |
| `cuLaunchKernel` | gridDim, blockDim, sharedMem, stream, kernel args |
| `cuMemcpyHtoDAsync(d_discount_params_, ...)` | `iter_params` (3 discount floats) |
| `cuMemcpyDtoHAsync(cfv_stage_host_, ...)` | CFV stage download results |
| `cuMemcpyDtoHAsync(pinned_file_staging_, ...)` | Strategy packed download |
| `cuGraphLaunch(training_graph_exec_, stream_)` | Training graph execution |

### 4.3 Step 3: Capture Method → Handler Mapping

Hook the dispatcher at `0x140014cf0`:

1. On dispatcher entry, save `rdx` (input kv pairs), `r12` (output ptr)
2. Walk 0x18-byte entries to find type-0x3 string value (method name)
3. After dispatch resolution, read handler pointer at `[r12+0x8]`
4. Log `(method_name_string, handler_VA, timestamp)`
5. Correlate with HTTP/WebView traffic on `127.0.0.1:6006`

Also hook `0x140036d70` (handler creator) to log handler object VA + valid flag.

### 4.4 Step 4: Capture PTX Blobs

Hook `cuModuleLoadData` / `cuModuleLoadDataEx` to dump PTX image data:
- Save each PTX blob to `PTX_DUMP/<kernel_name>.ptx`
- Correlate with `cuLaunchKernel` calls to assign kernel names

### 4.5 Step 5: Capture Compact Archive

1. Trigger `POST /api/export/full-file` from frontend
2. Capture bytes from `solver.strategy.archive.open/node/close`
3. Verify Avro magic `OBJ 0x04 0x1c 0x96 0x52`
4. Decode with `compact_archive_decoder.py`

### 4.6 Step 6: Full Solve Cycle

1. `POST /api/init` → init template
2. `POST /api/allocate` → allocate GPU memory
3. `POST /api/gpu-solve` → start solving
4. `GET /api/solve/status` → poll every 250ms until `running == false`
5. `POST /api/solve/stop` → stop
6. `GET /api/results` → node strategy + EVs
7. `POST /api/export/current-street` → snapshot

---

## 5. Deliverables to Produce After Dynamic Capture

| Deliverable | Format | Location |
|-------------|--------|----------|
| `HANDLER_MAP.md` | Method → handler VA table (~60 methods) | `re_lab/texassolvergpu/HANDLER_MAP.md` |
| `PTX_DUMP/` | All captured PTX blobs by kernel name | `re_lab/texassolvergpu/PTX_DUMP/` |
| `KERNEL_CONFIGS.md` | grid/block/sharedMem per kernel per CFR phase | `re_lab/texassolvergpu/KERNEL_CONFIGS.md` |
| `COMPACT_ARCHIVE_SCHEMA.md` | Finalized Avro schema + Q16 field definitions | `re_lab/texassolvergpu/COMPACT_ARCHIVE_SCHEMA.md` |
| `CFR_PIPELINE.md` | End-to-end solver flow with buffer lifetimes and timings | `re_lab/texassolvergpu/CFR_PIPELINE.md` |
| `ANTI_TAMPER_BYPASS.md` | Techniques used to bypass protections | Already complete |

---

## 6. Tools Already Created (Static, Ready to Use)

| Tool | Location | Purpose |
|------|----------|---------|
| Proxy `nvcuda.dll` source | `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp` | Logs all CUDA driver API calls; forwards to real `nvcuda.dll` |
| `nvcuda.def` | `/tmp/opencode/wine_stubs/nvcuda.def` | Module definition file for proxy DLL |
| Compact archive decoder | `re_lab/texassolvergpu/compact_archive_decoder.py` | Decodes Avro + DEFLATE + Q16 compact archives |
| CFR solver simulator | `re_lab/texassolvergpu/cfr_simulator.py` | CPU reference implementation of eval5/eval7, CFR regret matching, Q16 |

---

## 7. Key Addresses Reference

### Dispatcher
- `0x140014cf0` — Dispatcher entry point
- `0x1400099e0` — Handler linked-list lookup
- `0x140036d70` — Handler creator (6 call sites)
- `0x140016210` — Request/variant builder
- `0x140043b60` — Variant accessor
- `0x140013910` — Handler registration (linked-list insertion)

### Handler VTables
- `0x14016ec10` — VTable 1 (all 6 creator call sites, 16 entries)
- `0x14016ef90` — VTable 2 (functions `0x140017100`, `0x14001b720`)

### Dispatch Chain (if-else)
- `0x140061260`–`0x14006829e` — 20-method if-else chain
- Method strings at `.rdata` `0x140248f38`–`0x140249488`

### CUDA Call Sites
- `0x24bd10` — `cuMemcpyHtoDAsync(d_discount_params_, iter_params, ...)`
- `0x24aeb0` — `cuGraphLaunch(training_graph_exec_, stream_)`
- `0x24ade0` — `cuMemcpyDtoDAsync(d_cfreach_, ...)`
- `0x24b1b0` — `cuMemcpyDtoHAsync(cfv_stage_host_, ...)`
- `0x24b870` — `cuMemcpyDtoHAsync(pinned_file_staging_, ...)`

### Anti-Tamper
- `0x14000b178` — `IsDebuggerPresent` call (loader)
- `0x140150474`–`0x1401505e8` — 5× `cpuid` entry stub
- `0x14014fe20` — Stack cookie check
- `0x14016d9e0` — Error handler (int3)
- `0x140002970`, `0x140002b80` — Generic error handlers
- `0x140012710` — Singleton mutex address

### Global Data
- `0x1411c6690` — Handler linked-list head
- `0x1411c6698` — Shared global pointer

---

## 8. Quick Start Commands (Windows)

```cmd
:: 1. Copy files to working directory
copy TexasSolverGpu_131.exe C:\re\
copy nvcuda.dll C:\re\nvcuda_real.dll   (real CUDA driver DLL)
copy WebView2Loader.dll C:\re\

:: 2. Build proxy DLL
cl /LD nvcuda_proxy.cpp /link /DEF:nvcuda.def
copy nvcuda.dll C:\re\

:: 3. Apply anti-tamper patches (x64dbg or Ghidra)
:: - NOP IsDebuggerPresent call at 0x14000b178
:: - NOP 5× cpuid at entry stub
:: - NOP int3 at 0x14016d9e0

:: 4. Run solver
cd C:\re\
TexasSolverGpu_131.exe

:: 5. Check trace log
type %TEMP%\nvcuda_proxy_trace.log
```

---

## 9. Linux Static Analysis Artifacts

| Path | Contents |
|------|----------|
| `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` | Target binary (note: backslash in filename) |
| `/kaggle/working/re_lab/texassolvergpu/ELF_OBJECTS/` | 24 embedded CUDA ELF objects |
| `/kaggle/working/re_lab/texassolvergpu/KERNEL_CODE_ALL/<arch>/` | SASS code sections |
| `/tmp/opencode/sass_disasm/<arch>/` | nvdisasm output (270 files, 6 archs) |
| `/kaggle/working/re_lab/texassolvergpu/*.md` | All recon documentation |
| `/tmp/opencode/wine_stubs/` | Proxy DLL source code |

**Bottom line:** All static reverse engineering is complete. The remaining 7 items require dynamic execution on Windows + NVIDIA GPU. The proxy DLL, compact archive decoder, and CFR simulator are ready to use once the environment is available.
