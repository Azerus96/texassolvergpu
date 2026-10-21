# TexasSolverGPU v0.3.0 — CUDA Driver API Call Sites & Source Variable Names

**Source:** `all_strings.txt` (embedded frontend + binary strings, 32,289 lines)
**Target:** `TexasSolverGpu_131.exe`

---

## 1. CUDA Driver API Call Sites with C++ Source Variable Names

These strings were extracted from the binary's `.rdata` section. They contain actual C++ source code variable names and member access patterns, providing ground truth for the device buffer inventory.

### Stream & Event Creation
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| — | `cuEventCreate(&event_, flags)` | Generic event creation |
| — | `cuStreamCreate(&stream_, CU_STREAM_DEFAULT)` | Main training stream |
| — | `cuStreamCreate(&stream2_, CU_STREAM_DEFAULT)` | Secondary stream |
| — | `cuStreamCreate(&stream_dl_, CU_STREAM_DEFAULT)` | Download stream |
| — | `cuStreamCreate(&stream_file_, CU_STREAM_DEFAULT)` | File I/O stream |
| — | `cuEventCreate(&event_fwd_done_, CU_EVENT_DISABLE_TIMING)` | Forward done event |
| — | `cuEventCreate(&event_show_done_, CU_EVENT_DISABLE_TIMING)` | Show done event |
| — | `cuEventCreate(&event_cfv_stage_ready_, CU_EVENT_DISABLE_TIMING)` | CFV stage ready event |
| — | `cuEventCreate(&event_cfv_stage_free_, CU_EVENT_DISABLE_TIMING)` | CFV stage free event |

### Module Loading
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| — | `cuModuleLoadData failed: ` | Module load error message |

### Device-to-Device Copies
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24ade0` | `cuMemcpyDtoDAsync( d_cfreach_.dev_ptr(), d_init_weights_[opponent].dev_ptr(), static_cast<size_t>(nho) * sizeof(float), stream_)` | CFV reach copy from init weights |
| — | `cuMemcpyDtoDAsync(d_cfreach_.dev_ptr(), d_init_weights_[opponent].dev_ptr(), nho * sizeof(float), reinterpret_cast<CUstream>(stream_))` | CFV reach copy (alt form) |
| — | `cuMemcpyDtoDAsync(reinterpret_cast<CUdeviceptr>(d_cfreach0_.ptr()), reinterpret_cast<CUdeviceptr>(d_init_weights_[opp0].ptr()), num_hands_[opp0] * sizeof(float), stream_)` | CFV0 copy (opponent 0) |

### Graph Execution
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24aeb0` | `cuGraphLaunch(training_graph_exec_, stream_)` | Training graph launch |

### Device-to-Host Copies (CFV Staging)
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24b1b0` | `cuMemcpyDtoHAsync( cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), chunk_elements * sizeof(int16_t), stream_dl_)` | CFV stage download (i16) |
| — | `cuMemcpyDtoHAsync( cfv_stage_host_, reinterpret_cast<CUdeviceptr>(cfv_stage_device_), chunk_elements * sizeof(float), stream_dl_)` | CFV stage download (float) |
| — | `cuMemcpyDtoHAsync( pinned_cfv_stage_scales_, d_cfv_stage_scales_.dev_ptr(), static_cast<size_t>(num_segments) * sizeof(float), stream_dl_)` | CFV scales download |

### Device-to-Host Copies (Gather Staging)
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| — | `cuMemcpyDtoHAsync( dst + dst_float, d_gather_staging_.dev_ptr() + src_float * sizeof(float), run_floats * sizeof(float), reinterpret_cast<CUstream>(stream_))` | Gather staging download (float) |
| — | `cuMemcpyDtoHAsync( pinned_gather_staging_, d_gather_i16_.dev_ptr(), chunk_floats * sizeof(int16_t), reinterpret_cast<CUstream>(stream_))` | Gather i16 download |
| — | `cuMemcpyDtoHAsync( pinned_gather_scales_, d_gather_scales_.dev_ptr(), static_cast<size_t>(chunk) * sizeof(float), reinterpret_cast<CUstream>(stream_))` | Gather scales download |
| — | `cuMemcpyDtoHAsync( pinned_gather_staging_, d_gather_staging_.dev_ptr(), chunk_floats * sizeof(float), reinterpret_cast<CUstream>(stream_))` | Gather staging download |

### Device-to-Host Copies (Strategy Packed)
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24b870` | `cuMemcpyDtoHAsync( pinned_file_staging_, d_strategy_packed_.dev_ptr() + offset, chunk, stream_file_)` | Strategy packed download |

### Host-to-Device Copies
| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24bd10` | `cuMemcpyHtoDAsync( d_discount_params_.dev_ptr(), iter_params, 3 * sizeof(float), reinterpret_cast<CUstream>(stream_))` | Discount params upload (iter_params = 3 floats) |
| — | `cuMemAlloc(&ptr_, count_ * sizeof(T))` | Templated memory allocation |
| — | `cuMemcpyHtoD(ptr_, data.data(), count_ * sizeof(T))` | Templated HtoD copy |
| — | `cuMemAlloc(&ptr_, bytes)` | Raw memory allocation |
| — | `cuMemcpyDtoHAsync(dst, ptr_, count * sizeof(T), stream)` | Templated DtoH copy |
| — | `cuMemcpyDtoH(dst, ptr_, count * sizeof(T))` | Synchronous DtoH copy |
| — | `cuMemcpyHtoD(ptr_, data, count * sizeof(T))` | Raw HtoD copy |
| — | `cuMemcpyDtoH(root_cfv.data(), d_cfreach_.dev_ptr(), static_cast<size_t>(nhp) * sizeof(float))` | Root CFV download |

---

## 2. Device Buffer Inventory (with Source Variable Names)

| Buffer Variable | Type | Purpose |
|-----------------|------|---------|
| `d_cfv_` | device buffer | Counterfactual values |
| `d_cfreach_` | device buffer | Counterfactual reach (current player) |
| `d_cfreach0_` | device buffer | Counterfactual reach (opponent 0) |
| `d_cfreach1_` | device buffer | Counterfactual reach (opponent 1) |
| `d_init_weights_[opponent]` | device buffer array | Initial weights per opponent |
| `d_init_weights_[opp0]` | device buffer | Initial weights (opponent 0) |
| `d_discount_params_` | device buffer | Discount parameters (3 floats: iter_params) |
| `d_strategy_packed_` | device buffer | Packed strategy output |
| `d_cfv_stage_i16_` | device buffer | CFV staging (int16) |
| `d_cfv_stage_scales_` | device buffer | CFV staging scales (float) |
| `cfv_stage_device_` | CUdeviceptr | CFV stage device pointer |
| `d_gather_staging_` | device buffer | Gather staging (float) |
| `d_gather_i16_` | device buffer | Gather staging (int16) |
| `d_gather_scales_` | device buffer | Gather scales (float) |
| `cfv_stage_host_` | host-pinned | CFV staging host buffer |
| `pinned_cfv_stage_scales_` | host-pinned | CFV scales host buffer |
| `pinned_gather_staging_` | host-pinned | Gather staging host buffer |
| `pinned_gather_scales_` | host-pinned | Gather scales host buffer |
| `pinned_file_staging_` | host-pinned | File staging host buffer |
| `root_cfv` | host vector | Root CFV (host-side) |

### Scalar Variables
| Variable | Type | Purpose |
|----------|------|---------|
| `iter_params` | float[3] | Discount parameters (uploaded via cuMemcpyHtoDAsync) |
| `nho` | size_t | Number of hands (opponent) |
| `nhp` | size_t | Number of hands (player) |
| `num_hands_[opp0]` | size_t | Number of hands (opponent 0) |
| `num_segments` | size_t | Number of CFV stage segments |
| `chunk_elements` | size_t | Chunk element count |
| `chunk_floats` | size_t | Chunk float count |
| `chunk` | size_t | Chunk size |
| `offset` | size_t | Strategy packed offset |
| `dst_float` | size_t | Destination float offset |
| `src_float` | size_t | Source float offset |
| `run_floats` | size_t | Run float count |
| `flags` | unsigned | Event creation flags |

---

## 3. NVML Usage

| Function | Purpose |
|----------|---------|
| `nvmlInit_v2` | Initialize NVML |
| `nvmlShutdown` | Shutdown NVML |
| `nvmlDeviceGetMemoryInfo` | Query GPU memory info (VRAM gate check) |
| `nvmlDeviceGetHandleByPciBusId_v2` | Get device handle by PCI bus ID |

### Error Strings
- `nvmlInit_v2 failed with status ` — NVML init failure
- `nvmlShutdown failed with status ` — NVML shutdown failure

---

## 4. CUDA Driver API Function Addresses (from .idata)

| VA | Function |
|----|----------|
| `0x117e2f8` | `cuModuleLoadData` |
| `0x117e310` | `cuModuleLoadDataEx` |
| `0x1180010` | `cuLaunchKernel` |
| `0x1180038` | `cuLaunchKernelEx` |
| `0x117ebc0` | `cuMemcpyHtoDAsync` |
| `0x117ebf8` | `cuMemcpyDtoHAsync` |
| `0x1180e30` | `cuOccupancyAvailableDynamicSMemPerBlock` |
| `0x1180b90` | `cuGraphLaunch` |

---

## 5. Complete CUDA Driver API Call Site Inventory

### 5.1 CUDA Driver Initialization

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x248c70` | `cuInit(0)` | CUDA driver initialization |
| `0x248c80` | `cuDeviceGetCount(&count)` | Query device count |
| `0x248cb0` | `cuDeviceGet(&device, ordinal)` | Get device handle |
| `0x248dc0` | `cuDeviceGet(&cuda_device, ordinal)` | Get CUDA device handle |
| `0x248df0` | `cuDeviceGetPCIBusId(pci_bus_id.data(), ..., cuda_device)` | PCI bus ID for NVML |
| `0x248fb8` | `cuDeviceGet(&dev, 0)` | Get device 0 |
| `0x248fd0` | `cuDeviceGetName(name.data(), ..., dev)` | Device name query |
| `0x249058` | `cuDeviceGet(&device_, ordinal_)` | Store device handle |

### 5.2 Stream & Event Creation

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x249c70` | `cuStreamCreate(&stream_, CU_STREAM_DEFAULT)` | Main training stream |
| `0x249ca0` | `cuStreamCreate(&stream2_, CU_STREAM_DEFAULT)` | Secondary stream |
| `0x249cd0` | `cuStreamCreate(&stream_dl_, CU_STREAM_DEFAULT)` | Download stream |
| `0x249d00` | `cuStreamCreate(&stream_file_, CU_STREAM_DEFAULT)` | File I/O stream |
| `0x2490c8` | `cuEventCreate(&event_, flags)` | Generic event creation |
| `0x249d38` | `cuEventCreate(&event_fwd_done_, CU_EVENT_DISABLE_TIMING)` | Forward done event |
| `0x249d78` | `cuEventCreate(&event_show_done_, CU_EVENT_DISABLE_TIMING)` | Show done event |
| `0x249dc0` | `cuEventCreate(&event_cfv_stage_ready_, CU_EVENT_DISABLE_TIMING)` | CFV stage ready event |
| `0x249e00` | `cuEventCreate(&event_cfv_stage_free_, CU_EVENT_DISABLE_TIMING)` | CFV stage free event |

### 5.3 Device-to-Device Copies

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24ade0` | `cuMemcpyDtoDAsync(d_cfreach_.dev_ptr(), d_init_weights_[opponent].dev_ptr(), nho * sizeof(float), stream_)` | CFV reach copy from init weights |
| — | `cuMemcpyDtoDAsync(reinterpret_cast<CUdeviceptr>(d_cfreach0_.ptr()), ..., num_hands_[opp0] * sizeof(float), stream_)` | CFV0 copy (opponent 0) |

### 5.4 Graph Execution

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24aeb0` | `cuGraphLaunch(training_graph_exec_, stream_)` | Training graph launch |

### 5.5 Device-to-Host Copies (CFV Staging)

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24b1b0` | `cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), chunk_elements * sizeof(int16_t), stream_dl_)` | CFV stage download (i16) |
| `0x24b220` | `cuMemcpyDtoHAsync(pinned_cfv_stage_scales_, d_cfv_stage_scales_.dev_ptr(), num_segments * sizeof(float), stream_dl_)` | CFV scales download |
| `0x24b2f0` | `cuMemcpyDtoHAsync(cfv_stage_host_, reinterpret_cast<CUdeviceptr>(cfv_stage_device_), chunk_elements * sizeof(float), stream_dl_)` | CFV stage download (float) |

### 5.6 Device-to-Host Copies (Gather Staging)

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24b570` | `cuMemcpyDtoHAsync(dst + dst_float, d_gather_staging_.dev_ptr() + src_float * sizeof(float), run_floats * sizeof(float), stream_)` | Gather staging download (float) |
| `0x24b610` | `cuMemcpyDtoHAsync(pinned_gather_staging_, d_gather_i16_.dev_ptr(), chunk_floats * sizeof(int16_t), stream_)` | Gather i16 download |
| `0x24b6a0` | `cuMemcpyDtoHAsync(pinned_gather_scales_, d_gather_scales_.dev_ptr(), chunk * sizeof(float), stream_)` | Gather scales download |
| `0x24b740` | `cuMemcpyDtoHAsync(pinned_gather_staging_, d_gather_staging_.dev_ptr(), chunk_floats * sizeof(float), stream_)` | Gather staging download |

### 5.7 Device-to-Host Copies (Strategy Packed & Compact Archive)

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24b870` | `cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_.dev_ptr() + offset, chunk, stream_file_)` | Strategy packed download |
| `0x24ce50` | `cuMemcpyDtoHAsync(cg.host_compact.data(), reinterpret_cast<CUdeviceptr>(cg.d_compact.ptr()), compact_size * sizeof(float), stream_)` | Compact archive download |
| `0x24cee0` | `cuMemcpyDtoHAsync(ip_gather_.host_compact.data(), reinterpret_cast<CUdeviceptr>(ip_gather_.d_compact.ptr()), compact_size * sizeof(float), stream_)` | Compact archive download (ip_gather) |

### 5.8 Host-to-Device Copies

| Code VA | API Call | Source Context |
|---------|----------|----------------|
| `0x24bd10` | `cuMemcpyHtoDAsync(d_discount_params_.dev_ptr(), iter_params, 3 * sizeof(float), stream_)` | Discount params upload (iter_params = 3 floats) |
| `0x24d040` | `cuMemcpyHtoDAsync(reinterpret_cast<CUdeviceptr>(d_discount_params_.ptr()), pinned_discount_params_, 3 * sizeof(float), stream_)` | Discount params upload (alt path) |
| — | `cuMemAlloc(&ptr_, count_ * sizeof(T))` | Templated memory allocation |
| — | `cuMemcpyHtoD(ptr_, data.data(), count_ * sizeof(T))` | Templated HtoD copy |
| — | `cuMemAlloc(&ptr_, bytes)` | Raw memory allocation |
| — | `cuMemcpyDtoHAsync(dst, ptr_, count * sizeof(T), stream)` | Templated DtoH copy |
| — | `cuMemcpyDtoH(dst, ptr_, count * sizeof(T))` | Synchronous DtoH copy |
| — | `cuMemcpyHtoD(ptr_, data, count * sizeof(T))` | Raw HtoD copy |
| — | `cuMemcpyDtoH(root_cfv.data(), d_cfreach_.dev_ptr(), nhp * sizeof(float))` | Root CFV download |

### 5.9 Module Loading & Error Strings

| Code VA | String | Context |
|---------|--------|---------|
| `0x24a2f0` | `cuModuleLoadData failed:` | Module load error message |
| `0x24aef8` | `cuMemFreeHost(cfv_stage_host_)` | CFV staging host free |

### 5.10 CUDA Driver API Function Pointer Addresses (from `.rdata`)

These are the addresses where resolved function pointers are stored (loaded via `GetProcAddress`):

| VA | Function |
|----|----------|
| `0x117dd80` | `cuInit` |
| `0x117dd88` | `cuDeviceGet` |
| `0x117dd98` | `cuDeviceGetCount` |
| `0x117ddb0` | `cuDeviceGetName` |
| `0x117de50` | `cuDeviceGetAttribute` |
| `0x117e2f8` | `cuModuleLoadData` |
| `0x117e310` | `cuModuleLoadDataEx` |
| `0x117e668` | `cuMemAlloc_v2` |
| `0x117e678` | `cuMemAlloc` |
| `0x117e6b0` | `cuMemFree_v2` |
| `0x117e6c0` | `cuMemFree` |
| `0x117e700` | `cuMemAllocHost_v2` |
| `0x117e718` | `cuMemAllocHost` |
| `0x117e728` | `cuMemFreeHost` |
| `0x117f768` | `cuStreamCreate` |
| `0x117f778` | `cuStreamCreateWithPriority` |
| `0x117fc08` | `cuEventCreate` |
| `0x117ff58` | `cuFuncGetAttribute` |
| `0x1180010` | `cuLaunchKernel` |
| `0x1180038` | `cuLaunchKernelEx` |
| `0x1180e30` | `cuOccupancyAvailableDynamicSMemPerBlock` |
| `0x1180b90` | `cuGraphLaunch` |

**Note:** All CUDA Driver API functions are dynamically loaded via `LoadLibraryW("nvcuda.dll")` + `GetProcAddress`. There are no traditional IAT entries — function pointers are stored in global variables at the addresses above and called indirectly.

## 6. Key Observations

1. **4 CUDA streams** are created: `stream_`, `stream2_`, `stream_dl_`, `stream_file_` — matching the FINAL_RECON_STATE.md documentation.

2. **4 CUDA events** with `CU_EVENT_DISABLE_TIMING`: `event_fwd_done_`, `event_show_done_`, `event_cfv_stage_ready_`, `event_cfv_stage_free_` — matching FINAL_RECON_STATE.md.

3. **`iter_params`** is a 3-float array uploaded via `cuMemcpyHtoDAsync(d_discount_params_.dev_ptr(), iter_params, 3 * sizeof(float), stream_)` at code VA `0x24bd10` — this is the discount parameter array used in CFR training. A second upload path exists at `0x24d040`.

4. **`d_cfreach_`** is populated from `d_init_weights_[opponent]` via `cuMemcpyDtoDAsync` — this confirms the CFV reach buffer is initialized from opponent-specific initial weights.

5. **`training_graph_exec_`** is launched via `cuGraphLaunch` at code VA `0x24aeb0` — this is the CUDA graph that encapsulates the CFR training iteration.

6. **Two-stage CFV download**: CFV data is downloaded both as `int16_t` (`d_cfv_stage_i16_`) and `float` (`cfv_stage_device_`) — supporting the Q16 fixed-point format with separate scale factors.

7. **`d_strategy_packed_`** is downloaded to `pinned_file_staging_` via `stream_file_` — this is the final packed strategy output for archive export.

8. **Compact archive data** (`cg.host_compact`, `ip_gather_.host_compact`) is downloaded via `cuMemcpyDtoHAsync` at VAs `0x24ce50` and `0x24cee0` — these are the compact archive byte export paths.

9. **`cuModuleLoadData`** is used (not `cuModuleLoadFatBinary`) — PTX is JIT-compiled at runtime, confirming no embedded cubin. The 24 ELF objects contain SASS machine code, not PTX source.

10. **`cuLaunchKernel`** function pointer at `0x1180010` — no direct call sites found statically (indirect call through function pointer variable). Kernel launch configurations require dynamic capture via hooking `cuLaunchKernel`.
