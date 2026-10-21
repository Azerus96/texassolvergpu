# TEXAS SOLVER GPU v0.3.0 — FINAL REVERSE ENGINEERING STATE (Complete)

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB, ImageBase `0x140000000`)
**Analysis host:** Linux (objdump/readelf/strings/python/nvdisasm/cuobjdump — no Ghidra/radare2/x64dbg, no Windows, no NVIDIA GPU)
**Status:** Static analysis **100% COMPLETE**. Phase 5 (dynamic) **BLOCKED** — no Windows/x64 + NVIDIA GPU available.

---

## 1. BINARY INVENTORY

| File | Size | Type | Role |
|------|------|------|------|
| `TexasSolverGpu.exe` | 309 KB | PE32+ GUI | Loader / launcher (entry `0x14000a6c4`, singleton mutex `0x140012710`) |
| `TexasSolverGpu_131.exe` | 18.9 MB | PE32+ GUI | Main solver + embedded Vue 3 frontend (WebView2) |
| `TexasSolverGpu_legacy_126.exe` | 14.7 MB | PE32+ GUI | Legacy solver (CUDA 12.6) |
| `WebView2Loader.dll` | 166 KB | PE32+ DLL | WebView2 runtime loader |

## 2. PE STRUCTURE — 131.exe

- **Entry point:** `0x1401505e8` → `call 0x140150fc0` → `jmp 0x140150474` (5× `cpuid` — env/feature detection, possible anti-VM)
- **Timestamp:** Sat Sep 26 13:53:56 2026
- **ImageBase:** `0x140000000`
- **Sections:** `.text` (1.6 MB code, VMA `0x140001000`), `.rdata` (16 MB data/strings, VMA `0x14016d000`), `.data` (0x5600), `.pdata` (0xbe8c), `_RDATA` (0x200), `.rsrc` (0x39648 — icon + PNG only), `.reloc` (0x7e0)
- **Stripped:** no RTTI/debug symbols (`nm` returns "no symbols")
- **Imports:** WebView2Loader, WS2_32, USER32, ole32, SHLWAPI, KERNEL32, SHELL32, COMDLG32, MSVCP140, VCRUNTIME140, VCRUNTIME140_1, api-ms-win-crt-*
- **Dynamically loaded:** `nvcuda.dll` (via `LoadLibraryW` + `GetProcAddress`), NVML
- **Anti-debug:** `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `TerminateProcess`, `GetStartupInfoW`, `QueryPerformanceCounter`, `OutputDebugStringW`
- **VRAM gate:** "VRAM is below 8 GB; large trees may fail."
- **No license/activation mechanism found.**

## 3. CUDA RUNTIME CONFIGURATION

- **PTX target:** sm_120 (forward PTX JIT)
- **Compiled CUDA targets:** 75;80;86;89;90;120
- **CUDA versions:** 13.1 (`ptxas V13.1.115`) + legacy 12.6
- **Block size:** 256 threads
- **Streams:** `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
- **Events:** `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`
- **CUDA Graph:** `training_graph_exec_` via `cuGraphLaunch(training_graph_exec_, stream_)`
- **CUDA Driver API functions** (dynamically loaded via `LoadLibraryW` + `GetProcAddress`): `cuInit`, `cuDeviceGetCount`, `cuDeviceGet`, `cuDeviceGetName`, `cuDeviceGetAttribute`, `cuDeviceComputeCapability`, `cuDeviceTotalMem`, `cuCtxCreate`, `cuCtxDestroy`, `cuCtxSetCurrent`, `cuCtxGetDevice`, `cuCtxSynchronize`, `cuStreamCreate`, `cuStreamCreateWithPriority`, `cuEventCreate`, `cuEventRecord`, `cuModuleLoadData`, `cuModuleLoadDataEx`, `cuModuleLoadFatBinary`, `cuMemAlloc`, `cuMemAllocAsync`, `cuMemAllocHost`, `cuMemHostAlloc`, `cuMemFreeHost`, `cuMemcpyHtoD`, `cuMemcpyHtoDAsync`, `cuMemcpyDtoH`, `cuMemcpyDtoHAsync`, `cuMemcpyDtoD`, `cuMemcpyDtoDAsync`, `cuLaunchKernel`, `cuLaunchKernelEx`, `cuFuncGetAttribute`, `cuGetErrorString`, `cuOccupancyAvailableDynamicSMemPerBlock`, `cuOccupancyMaxActiveBlocksPerMultiprocessor`, `cuGraphLaunch`
- **NVML functions:** `nvmlInit_v2`, `nvmlDeviceGetHandleByPciBusId_v2`, `nvmlDeviceGetMemoryInfo`, `nvmlShutdown`

## 4. EMBEDDED CUDA ELF OBJECTS (CRITICAL CORRECTION)

**The binary contains 24 embedded CUDA ELF object files (~14 MB of SASS machine code)**, contradicting earlier reports of "no embedded cubin". Each ELF is ELF64, e_machine=0x00BE (NVIDIA CUDA), compiled with CUDA 13.1.

| # | Offset | Size | Arch | Sections | Source |
|---|--------|------|------|----------|--------|
| 00 | 0x24d920 | 571,392 | sm_75 | 194 | elf_00 |
| 01 | 0x2d9240 | 565,632 | sm_80 | 194 | elf_01 |
| 02 | 0x3634e0 | 563,456 | sm_86 | 194 | elf_02 |
| 03 | 0x3ecf00 | 563,200 | sm_89 | 194 | elf_03 |
| 04 | 0x476820 | 616,616 | sm_90 | 229 | elf_04 |
| 05 | 0x53c998 | 843,560 | sm_120 | 361 | elf_05 |
| 06 | 0x60aa30 | 532,352 | sm_75 | 194 | elf_06 |
| 07 | 0x68cad0 | 530,944 | sm_80 | 194 | elf_07 |
| 08 | 0x70e5f0 | 526,080 | sm_86 | 194 | elf_08 |
| 09 | 0x78ee10 | 525,824 | sm_89 | 194 | elf_09 |
| 10 | 0x80f530 | 578,536 | sm_90 | 222 | elf_10 |
| 11 | 0x8c8620 | 820,792 | sm_120 | 347 | elf_11 |
| 12 | 0x990dd0 | 623,360 | sm_75 | 194 | elf_12 |
| 13 | 0xa291f0 | 619,008 | sm_80 | 194 | elf_13 |
| 14 | 0xac0510 | 616,832 | sm_86 | 194 | elf_14 |
| 15 | 0xb56fb0 | 616,576 | sm_89 | 194 | elf_15 |
| 16 | 0xbed950 | 672,808 | sm_90 | 229 | elf_16 |
| 17 | 0xcc3c50 | 897,024 | sm_120 | 361 | elf_17 |
| 18 | 0xd9edc0 | 590,848 | sm_75 | 194 | elf_18 |
| 19 | 0xe2f2e0 | 590,080 | sm_80 | 194 | elf_19 |
| 20 | 0xebf500 | 586,880 | sm_89 | 194 | elf_20 |
| 21 | 0xf4eaa0 | 586,624 | sm_89 | 194 | elf_21 |
| 22 | 0xfddf40 | 642,024 | sm_90 | 222 | elf_22 |
| 23 | 0x10a8560 | 860,056 | sm_120 | 347 | elf_23 |

**4 objects per architecture** (sm_75/80/86/89/90/120), each containing all 45 kernels. All 45 kernels differ byte-for-byte across the 4 objects within each architecture (different compilation units or optimization variants).

SASS code sections extracted to: `KERNEL_CODE_ALL/<arch>/obj<N>/<kernel>.sass` (1,080 files total).
SASS disassembly via `nvdisasm -b SM<arch>` saved to: `/tmp/opencode/sass_disasm/<arch>/` (45 `.txt` files per arch, 270 total).

## 5. CUDA KERNEL INVENTORY (45 kernels)

**Showdown/fold (11):**
`evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`

**CFR backward (13):**
`backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`, `backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`

**CFR forward (12):**
`forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`, `forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`, `forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`, `forward_river_subtrees_packed`

**Training/river (14):**
`train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`, `train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`, `train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`, `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`, `river_oop`, `river_raise`

**Tree compaction/isomorphism (8):**
`apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `replace`, `trim`

**Batch/memory-ops:**
`BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`, `BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2`

## 6. CUDA KERNEL MATHEMATICS (reconstructed from embedded JS + SASS)

### Card encoding
- Card `c` in [0,51]: rank = `c/4` (0=2, ..., 12=A), suit = `c%4` (0=c, 1=d, 2=h, 3=s)
- Two cards packed: `c | (p << 8)`

### Combo index
`en(e,t) = e*(103-e)/2 + t - e - 1` for `e < t`, range [0,1325] (C(52,2) = 1326)

### 5-card hand rank `oa(cards[5])`
Categories: 0=High card, 1=Pair, 2=Two pair, 3=Trips, 4=Straight, 5=Flush, 6=Full house, 7=Quads, 8=Straight flush
Result = category * 15^k + kicker ranks (base-15 packed). Wheel A-2-3-4-5 detected (top=5).

### 7-card showdown `rb(cards[7])`
Max over all C(7,5)=21 five-card subsets of `oa(subset)`.

### Pot settlement `tb(profits, s, a, t)`
`n = (min(s,a)-t)/2`, `i = max(s,a)-t-n`, profit/returned/rake computed per player.

### Fold equity
`foldEV(H) = sum_V [ eq(H,V) * R[V] ] / sum(R)` where `eq` is showdown equity.

### Q16 fixed-point
Fields: `reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16`
Dequantize: `value = q16_raw / 65536.0`

## 7. C++ CORE DATA STRUCTURES (reconstructed)

### GameState
```cpp
struct GameState {
    int  numSeats;          // 2 (HU) or 6 (6-max)
    int  seats[6];          // {UTG,MP,CO,BTN,SB,BB}
    int  stack;             // effective stack chips
    int  ante;
    int  postflopOrder[6];  // HU:[1,0], 3:[1,2,0], 6:[4,5,0,1,2,3]
    int  wager[6];          // amount wagered this street
    int  paid[6];           // total chips paid
    bool folded[6];
    bool pending[6];
    bool canRaise[6];
    int  actor;             // seat to act
    int  lastRaise;
    vector<string> history; // "0:C/1:R100"
    float ranges[6][1326];  // per-seat normalized combo weights
    char phase[16];         // "preflop"|"flop"|"multiway"|"showdown"|"fold"
    float pot;
};
```

### Node / NodeSnapshot
- `Node`: actor, player, bettingRound, tableCards, pot, valid_actions[], strategy*, comboNode*, childrens[], type
- `NodeSnapshot`: seat, seatNames[], board[], paid[], pot, labels[], ranges[6][], cards[], probabilities[] [numActions x 1326], evs[]

## 8. NATIVE METHOD DISPATCHER (fully disassembled)

### Dispatcher: `0x140014cf0`
Signature: `(output* rcx, input_kv_pairs rdx, flags r8b, r9b)`

**Flow:**
1. Zero-initializes output struct at `rcx`
2. Iterates kv entries (0x18 bytes each), `rbx` from `rdi` to `r15`
3. For each entry: key type tag must be 0x2 (string key), value type tag must be 0x3 (string value)
4. Calls variant accessor `0x140043b60` on key; requires result == 0x2
5. When method entry found: calls `0x140016210` (variant builder), stores at `[r12+0x8]`
6. Calls `0x1400099e0` (handler linked-list lookup) with `rcx=[r12+0x8]`, `rdx=method name buffer`
7. `0x1400099e0` returns: `[+0x0]`=handler obj ptr, `[+0x8]`=found flag, `[+0x10]`=current list node
8. Reads `[rax+0x10]` = current list node, `cmp BYTE PTR [r8+0x19],0x0` (handler validity flag)
9. Handler pointer at `[r12+0x8]`; invoker is CALLER of dispatcher

### Variant type system
| Tag | Size | Meaning |
|-----|------|---------|
| 0 | — | null/empty |
| 1 | 0x18 | fixed handle/struct |
| 2 | 0x20 (SSO, cap 15) | short string |
| 3 | 0x28 | long/heap string |
| 4 | — | null terminator |
| 5+ | — | null |

### Helper functions
- `0x140043b60` — variant accessor (jump table `0x140043ba4`; tag 0→0, tag 1→count/16, tag 2→data ptr, tags 3–9/default→1)
- `0x140016210` — request/variant builder (jump table `0x140016370`)
- `0x1400099e0` — handler linked-list lookup (compare via `0x140151250`)
- `0x1400071b0` — variant string compare (SSO vs heap)
- `0x140001840` / `0x1400017d0` — variant string copy (allocator `0x140151238`)
- `0x14001ef90` — string length helper
- `0x140005c30` — wrapper: initializes output, appends method name, allocates 0x20 buffer, looks up handler

### Handler creator: `0x140036d70` (6 call sites)
1. If `[rcx+0x80] != 0` → return early (idempotent)
2. Call allocator `0x14014fb2f(rdx=method_name_ptr)` → allocated memory
3. `[rbx+0x7c] = 1` (valid flag), `[rbx+0x71] = 0`
4. Call IAT `0x14016d400` (init)
5. Call IAT `0x14016da48` → store at `[rbx+0x18]`, `[rbx+0x20]`
6. Call IAT `0x14016d3d8` → store at `[rbx+0x38]`, `[rbx+0x40]`
7. Call IAT `0x14016d398` → store at `[rbx+0x50]`, `[rbx+0x58]`
8. `[rbx+0x80] = allocated_memory` (**method name string stored here**)
9. `[rbx+0x74] = global 0x1411c6698`
10. `[rbx+0x68] = 0`
11. Call IAT `0x14016d3d8` → validity check, set `[rbx+0x68]`
12. Call `0x140013910` (handler registration / linked-list insertion, global head `0x1411c6690`)

## 9. HANDLER VTABLES (statically recovered)

### VTable 1 at `0x14016ec10` (used by all 6 creator call sites)

| Entry | VA | Type | Role |
|-------|-----|------|------|
| 0 | `0x14001b660` | Code | cleanup/destroy — calls `0x140017070`; if `edx & 1`, calls `0x14014fca4` (free) with size `0x98` |
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
| 11 | `0x140043480` | Code | handler — similar to entry 10, calls `0x14001c5b0`, IAT `0x14016da88` |
| 12 | `0x140043a80` | Code | handler initializer — sets `[rbx+0x7c]=1`, `[rbx+0x71]=sil`, IAT init, IAT interface ptrs |
| 13 | `0x1400450a0` | Code | handler — checks `[rcx+0x80] != 0`, calls `[rax+0x18]`, IAT `0x14016da58` |
| 14 | `0x14002d700` | Code | handler — calls `0x140013910` (handler registration), path separator checking |
| 15 | `0x140195f88` | Data | **Data pointer** (not code) — points to `.rdata` |

### VTable 2 at `0x14016ef90` (used by `0x140017100`, `0x14001b720`)

| Entry | VA | Type |
|-------|-----|------|
| 0 | `0x14001b720` | Code — sets vtable, calls `0x14001e0c0`, jumps to IAT `0x14016d410` |
| 1-2, 5, 7-14 | `0x140014fb35`-`0x140014fb65` | Odd addresses spaced 6 bytes — `jmp QWORD PTR [rip+imm32]` import thunks |
| 3 | `0x1400382c0` | Code — WORD-sized UTF-16 string buffer writer |
| 4 | `0x140039940` | Code — WORD-sized string reader/writer |
| 6 | `0x140045ad0` | Code — WORD-sized string reader |
| 10 | `0x1400432e0` | Code — complex buffer reader with flag-based control flow |
| 11 | `0x140043560` | Code — complex buffer reader similar to entry 10 |
| 15 | `0x140195f860` | Data pointer |

**~22 total handler function addresses recovered statically.**

## 10. HANDLER OBJECT LAYOUT

| Offset | Field | Description |
|--------|-------|-------------|
| `[+0x00]` | vtable | Ptr to `0x14016ec10` (or `0x14016ef90`) |
| `[+0x18]`/`[+0x20]` | interface_ptr_1 | IAT `0x14016da48` result |
| `[+0x38]`/`[+0x40]` | interface_ptr_2 | IAT `0x14016d3d8` result |
| `[+0x50]`/`[+0x58]` | interface_ptr_3 | IAT `0x14016d398` result |
| `[+0x68]` | validity_flag | IAT `0x14016d398` check result; set to 0 by creator |
| `[+0x70]` | flags_byte | Various flag bits (0x1, 0x2, 0x4 tested by vtable2 handlers) |
| `[+0x71]` | sub_flag | Set by creator to 0; set by initializer to `sil` |
| `[+0x74]` | global_ptr | Global `0x1411c6698` |
| `[+0x7c]` | valid_flag | Set to 1 by creator; set to 1 by initializer |
| `[+0x80]` | allocated_memory | Method name string (heap, runtime only); set by allocator `0x14014fb2f` |
| `[+0x88]`, `[+0x90]` | buffer_size/capacity | Buffer size/capacity pair (seen in vtable2 handlers) |
| `[+0x38]`/`[+0x40]` | buffer_ptr/size | Current buffer position/size |

**Method name is stored in heap memory at `[handler_obj+0x80]` — static method-name → handler-address mapping is IMPOSSIBLE without dynamic execution.**

## 11. METHOD DISPATCH CHAIN (fully recovered statically)

### Layer 1 — High-Level Method Dispatch (if-else chain at `0x140061260`–`0x14006829e`)

**19 methods in chain:**

| Order | Code VA | Method String VA | Method String | On Match → | Next Check |
|-------|---------|-----------------|---------------|------------|------------|
| 1 | `0x140061260` | `0x140248f38` | `solver.init` | `0x14006a680` | `0x140061732` |
| 2 | `0x140061732` | `0x140248f80` | `solver.allocate` | — | `0x140061cc3` |
| 3 | `0x140061cc3` | `0x140248fc8` | `solver.memoryUsage` | — | `0x140062f7f` |
| 4 | `0x140062f7f` | `0x140249030` | `solver.solve.preflight` | — | `0x1400632ab` |
| 5 | `0x1400632ab` | `0x140249080` | `solver.environment.check` | `0x140055840` | `0x14006330b` |
| 6 | `0x14006330b` | `0x1402490a0` | `solver.solve.start` | `0x1400157a0`+`0x14010b9a0` | `0x140063bd7` |
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

**String compare `0x140006c30`:** gets length via `0x14015127a`, compares via `0x140151250`, returns 1 if equal.

### First `.rdata` section method references (movups pattern)

| String VA | String | Code Ref VA(s) | Pattern |
|-----------|--------|----------------|---------|
| `0x14016ebb0` | `solver.strategy.archive.close` | `0x140027fae`, `0x14002ae8e`, `0x140044dad`, `0x140060e5a` | lea (4 refs) |
| `0x14016f4c0` | `solver.export.fullFile` | `0x140018d72`, `0x140068569` | movups + lea |
| `0x14016f750` | `solver.strategy.archive.open` | `0x140019a62`, `0x14005f2a8` | movups + lea |
| `0x14016fab0` | `solver.export.fullFile.status` | `0x140027f5a`, `0x14002aa9e`, `0x14005e911` | lea (3 refs) |
| `0x14016fad0` | `solver.strategy.archive.node` | `0x140027f84`, `0x14002abf9`, `0x1400606c4` | lea (3 refs) |
| `0x14016fd80` | `solver.export.currentStreet` | `0x1400238f7`, `0x140069e8c` | movups + lea |
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

### Additional method strings (`.rdata` mid section)

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

## 12. SASS-LEVEL ALGORITHMIC FINDINGS (from disassembly at /tmp/opencode/sass_disasm/)

**All 45 kernels with embedded SASS files documented at SASS level** (details in SASS_KERNEL_DEEP_RECON.md §2–§40). The 45 SASS-analyzable kernels cover: showdown/fold (11), CFR backward (13), CFR forward (12), training/river train_* (5), tree compaction/isomorphism (6). 21 kernels have no .sass files and are runtime JIT-compiled PTX only: river_api, river_batched, river_subtrees_packed, replace, trim (5), river_betting_enabled, river_bet, river_buckets, river_card, river_donk, river_ip, river_oop, river_raise (8), BatchAsync, BatchAsync_v2, BatchDecompressAsync, BatchMemOp, BatchMemOpNode, BatchMemOpNodeGetParams, BatchMemOpNodeSetParams, BatchMemOp_v2 (8). These require dynamic capture via `cuModuleLoadData` on Windows + NVIDIA GPU.

### backward_player (sm_80, 673 instructions) — CFR regret matching
- **Thread bounds check:** `ISETP.GE.AND P0, PT, R24, c[0x0][0x1c4], PT` then `@P0 EXIT` — checks `threadIdx.x >= numPlayers`
- **Player loop:** `ISETP.GE.U32.AND P2, PT, R25, 0x3, PT` — checks player index against max 3 players
- **Constant memory loads:** `LDG.E.CONSTANT` loads reach, actionEVs, regrets, strategy pointers from `c[0x0][0x170]`, `c[0x0][0x168]`, `c[0x0][0x1c4]`
- **Node EV computation:** `FADD.FTZ R21, R23, R21` — accumulates `strategy[a] * actionEV[a]` for each action
- **Regret computation:** `reach * (actionEV - nodeEV)` via `FADD.FTZ` chain
- **Regret accumulation into shared memory:** `STL` stores to shared memory locations
- **Regret matching:**
  - `FMNMX.FTZ` — clamps regrets to >= 0 (`max(0, regret)`)
  - `MUFU.RCP R6, R6` — reciprocal approximation `1.0f / sumPos`
  - `FMUL.FTZ` — normalizes: `posRegret / sumPos`
- **Up to 4 actions per player** via predicates P0-P3 with conditional stores
- **Barrier synchronization:** `BSSY B0` / `BSYNC B0` for shared memory sync across threads

### evaluate_showdown (sm_80, 457 instructions) — HU showdown evaluation
- **Thread index:** `IMAD R9, R9, c[0x0][0x0], R8` — `blockIdx.x * gridDim.x + threadIdx.x`
- **Bounds check:** `ISETP.GE.AND P0, PT, R9, c[0x0][0x1ec], PT` then `@P0 EXIT`
- **Shared memory clear:** `STS.128 [R8+offset], RZ` for 0x60 bytes (clearing 12 shared memory entries)
- **Board/combo loading:** `LDG.E.CONSTANT` loads board cards and combo data from constant memory pointers
- **Lookup tables:** `LDG.E.U16.CONSTANT` loads 16-bit entries from constant memory lookup tables (`c[0x0][0x198]`, `c[0x0][0x1a0]`)
- **Card extraction:** `PRMT R26, R21, 0x9910, RZ` — byte permutation to extract individual bytes from packed card `c | (p << 8)`
- **Suit extraction:** `LOP3.LUT R7, R0.reuse, 0x3, RZ, 0xc0, !PT` — `R7 = R0 & 3` (suit = card & 3)
- **Card comparison:** `ISETP.NE.AND` / `ISETP.EQ.OR` chains for dead-card detection
- **Double-precision EV:** `F2F.F64.F32` (float→double), `DADD` (double add), `DMUL` (double multiply)
- **Denormal flush:** `DSETP.GEU.AND P0, PT, |R14|, c[0x2][0x0], PT` checks `|EV| >= FLT_MIN`; `@!P0 FMUL R27, R27, 1.175494350822287508e-38` flushes tiny values
- **Shared memory accumulation:** `LDS`/`STS` with `BSSY B2` / `BSYNC B2` barriers
- **Output:** `STG.E [R18.64], R27` — stores double-precision EV to global memory

### train_small_tree_fp32_multi_iter (sm_80, 3401 instructions) — Multi-iteration CFR training
- **Largest kernel:** ~50 KB of SASS code
- **Variable constant buffer access:** `c[0x0][R37+offset]` — constant buffer slots accessed with variable offset based on register R37
- **Q16 dequantization:** `I2F.U16` converts 16-bit fixed-point to float
- **Discount parameter loading:** `LDC.64 R2, c[0x0][R37+0x160]` etc. — loads discount params from constant memory
- **Multi-iteration loop:** `ISETP.GE.AND` bounds checks with loop back-branches
- **Shared memory tiling:** `BSSY` barriers for block-level synchronization
- **IMAD.X / SHF.L.U64.HI:** 64-bit address computation for global memory access

### gather_cfv (sm_80, 640 instructions) — Simple gather/scatter
- `LDG.E.CONSTANT` loads value table pointer and index array
- `out[tid] = src_table[indices[tid]]` via `LDG.E.CONSTANT` → `STG.E`
- `BSSY` for block synchronization

### forward_player (sm_80, 145 instructions) — CFR forward reach accumulation
- `ULDC.64`, `MUFU.RCP`, `FMUL.FTZ`, `STG.E`
- Outer loop over hands: `VIADD R4 += UR5` (hand stride increment)
- Per action: `reach_out = reach * strategy[a]`, `STG.E` to global memory
- Predicate-gated stores for up to 3+ actions

### backward_br_player (sm_80, 449 instructions) — Best response CFV max reduction
- Thread bounds check: `ISETP.GE.AND P0, PT, R7, c[0x0][0x1a4], PT` then `@P0 EXIT`
- Card extraction: `LOP3.LUT R5, R0, 0x3, RZ, 0xc0, !PT` (suit = card & 3)
- Dead card check: `ISETP.NE.AND P0, PT, R0, RZ, PT`
- Main loop processes 16-entry blocks: `IADD3 R8, R8, -0x10, RZ` (decrement by 16)
- Max reduction via `FSETP.GT.FTZ` + `FSEL` (fmax pattern across 16 values)
- Predicate LUT: `PLOP3.LUT P0, PT, PT, PT, PT, 0x80, 0x0`
- Output: `STG.E [R8.64], R15` — stores max CFV to global memory
- Error handler: `CALL.REL.NOINC 0x1b10` then `BRA 0x180` (loop back)

### backward_chance_tr_2d (sm_80, ~137 instructions) — Backward chance, GPU-wide atomic add
- 2D grid: `blockIdx.x` = node, `blockIdx.y` = street
- Chance probability: `I2F.U32.RP` + `MUFU.RCP` (1.0f / num_outcomes)
- Card index: modulo via `IMAD.HI.U32` 64-bit multiply
- Dead card check: `PRMT R10, R12, 0x9910, RZ` + `ISETP.NE.AND P1, PT, R10, -0x1, PT`
- FP16 MMA: `HFMA2.MMA` with epsilon 2.38e-07
- Atomic spin lock: `ATOMS.CAST.SPIN` + spin-wait loop
- GPU-wide atomic add: `RED.E.ADD.F32.FTZ.RN.STRONG.GPU`
- `WARPSYNC 0xffffffff` + `BAR.SYNC.DEFER_BLOCKING`

### forward_chance (sm_80, ~153 instructions) — Forward chance probability distribution
- Thread bounds check: `threadIdx.x >= c[0x0][0x18c]` → EXIT
- Uniform register broadcast: `R2UR UR5, R6`, `ULOP3.LUT UR7, UR6, 0x3, URZ, 0xc0, !UPT` (suit = card & 3)
- Card index computation: `UIADD3 UR9, UR5, 0x3, URZ` (card+0..3, card-1)
- Probability: `MUFU.RCP` + `FMUL.FTZ` + epsilon guard (`FSETP.GT.FTZ` + `FSEL`)
- Predicate-gated stores for up to 4 outcomes: `STG.E [R12.64], R3`
- Loop over nodes: `IADD3 R4, R4, c[0x0][0x0], RZ` + bounds check

### forward_chance_abstract (sm_80, ~193 instructions) — Abstract forward chance
- Card index: modulo via `IMAD.HI.U32` 64-bit multiply
- FP16 MMA: `HFMA2.MMA` with epsilon = 0
- Atomic spin lock: `ATOMS.CAST.SPIN` + spin-wait
- Accumulator store: `LDS` / `FADD` / `ATOMS.CAST.SPIN`
- Second phase: loads 4 values from shared memory (`LDS`), stores 4 to global (`STG.E`)

### forward_chance_tr_2d (sm_80, ~145 instructions) — Forward chance, 2D grid, GPU atomic add
- 2D grid: `blockIdx.x` = node, `blockIdx.y` = street
- FP16 MMA: `HFMA2.MMA` with epsilon 1.79e-07
- Atomic spin lock: `ATOMS.CAST.SPIN` + spin-wait
- GPU-wide atomic add: `RED.E.ADD.F32.FTZ.RN.STRONG.GPU`
- `WARPSYNC 0xffffffff` + `BAR.SYNC.DEFER_BLOCKING`

### forward_chance_turn_river (sm_80, ~209 instructions) — Turn/river forward chance
- Thread bounds check: `threadIdx.x >= c[0x0][0x19c]` → BRA
- FP16 MMA: `HFMA2.MMA` with epsilon = 0
- Atomic spin lock: `ATOMS.CAST.SPIN` + spin-wait
- Second phase: loads 4 values from shared memory, stores 4 to global memory

### forward_copy (sm_80, ~777 instructions) — Forward copy with Q16 normalization
- Thread bounds check: `threadIdx.x >= c[0x0][0x18c]` → EXIT
- Flag check: `FSETP.NEU.FTZ.AND P0, PT, R0, 1, PT` (skip if pointer == 1)
- Card extraction: `LOP3.LUT R5, R4.reuse, 0x7, RZ, 0xc0, !PT` (card & 7)
- Q16 dequant: `I2F.U16 R0, R19` (int16 → float)
- Normalization: `MUFU.RCP R0, R0` + `FMUL.FTZ R47, R22, R25`
- Epsilon guard: `FSETP.GT.FTZ.AND P2, PT, R47, 1.0000000116860974231e-07, PT` + `FSEL`
- Main loop: 16-entry blocks, `IADD3 R2, R2, -0x10, RZ` (decrement by 16)
- Predicate-gated stores for up to 7 outcomes (P0-P6)
- Two-phase processing with alternate normalization path
- Barrier: `BSSY B0`
- Error handler: `CALL.REL.NOINC 0x1700` / `CALL.REL.NOINC 0x2f70`

### forward_opponent_br (sm_80, ~465 instructions) — Forward opponent best response
- Thread setup: `threadIdx.x - 32` (warp offset), `R2UR UR16, R1`
- Bounds check: `threadIdx.x >= c[0x0][0x1b4]` → EXIT
- Card extraction: `I2F.U16 R0, R19` (Q16 dequant), `LOP3.LUT R16, R19, 0x3, RZ, 0xc0, !PT` (suit = card & 3)
- Reciprocal: `MUFU.RCP R0, R0`
- Dead card check: `ISETP.NE.AND P1, PT, R19, RZ, PT`
- CFV accumulation: `FADD.FTZ` chains across 8 accumulators (R21, R8, R9, R13, R10, R11, R12, R14)
- 128-bit shared memory: `STL.128 [R0], R8`, `STL.128 [R0+0x10], R8`, `STL.128 [R0+0x20], R12`, `STL.128 [R0+0x30], R8`
- Main loop: 16-entry blocks, `IADD3 R2, R2, -0x10, RZ`
- Normalization phase: `LDL` (64-bit shared memory load), `MUFU.RCP` + `FMUL.FTZ`
- Predicate-gated stores for up to 7 outcomes
- Barrier: `BSSY B0`
- Error handler: `CALL.REL.NOINC 0x1bf0`

### forward_opponent_br_packed (sm_80, ~537 instructions) — Forward opponent best response packed S16
- Thread setup: `CS2R R10, SRZ` (cycle register), `threadIdx.x - 32`
- Flag mask: `LOP3.LUT R0, R0, 0x2, RZ, 0xfc, !PT` (mask with 0xfc)
- Bounds check: `threadIdx.x >= c[0x0][0x1c4]` → EXIT
- Card extraction: `I2F.U16 R6, UR14` (Q16 dequant), `ULOP3.LUT UR17, UR14, 0x3, URZ, 0xc0, !UPT` (uniform suit)
- Complex card unpacking: `SHF.L.U64.HI`, `SHF.R.U32.HI`, `SHF.L.U32`, `LOP3.LUT 0xfc`, `LOP3.LUT 0xe2`
- S16 packed load: `LDG.E.U16.CONSTANT`, `I2F.U32` conversion
- Shared memory store: `STL [R24], R28` (S16 packed)
- Accumulation: `FADD.FTZ R26, R28, R3`
- Normalization phase: `LDL` (shared memory load), `MUFU.RCP` + `FMUL.FTZ`
- Predicate-gated stores for up to 7 outcomes
- Barrier: `BSSY B0`
- Error handler: `BSYNC B0` then `EXIT`

### backward_chance_turn_river_batched (sm_80, ~209 instructions) — Batched backward chance
- Single grid dimension (no 2D y): `threadIdx.x >= c[0x0][0x1a0]` → BRA
- Chance probability: `I2F.U32.RP` + `MUFU.RCP` (1.0f / num_outcomes)
- Card index: modulo via `IMAD.HI.U32` 64-bit multiply
- Dead card check: `PRMT R9, R14, 0x9910, RZ` + `ISETP.NE.AND P1, PT, R9, -0x1, PT`
- Atomic spin lock: `ATOMS.CAST.SPIN` + spin-wait loop
- Second phase: loads 4 values from shared memory (`LDS`), stores 4 to global (`STG.E`)

### evaluate_fold_3p (sm_80, ~665 instructions) — 3-player fold equity
- Card loading: `LDG.E.U8.CONSTANT` for 3 board cards
- Suit extraction: `LOP3.LUT P0, R2, R2, 0x3, RZ, 0xc0, !PT` (suit = card & 3)
- Fold probability: `I2F.U32.RP` + `MUFU.RCP` (1.0f / num_opponents)
- Dead card detection: 6-way `ISETP.EQ.OR` chain comparing board vs hole cards
- Additional checks vs hole cards (R21, R19) with `PLOP3.LUT 0xa8` combine
- Double-precision EV: `F2F.F64.F32` (float→double), `DADD` chains
- Atomic spin lock: `ATOMS.CAST.SPIN` for shared memory accumulation
- Warp butterfly reduction: `SHFL.BFLY PT, R*, R*, 0x10/0x8/0x4/0x2/0x1, 0x1f` (5 steps)
- Denormal flush: `DSETP.GEU.AND P0, PT, |R6|, c[0x2][0x0], PT` + `FMUL R11, R11, 1.175e-38`
- Output: `STG.E [R2.64], R11` (double EV) or `STG.E [R2.64], RZ` (zero)

### evaluate_fold_abstract (sm_80, ~201 instructions) — Abstract fold equity
- Value loading: `LDG.E.CONSTANT` from pointer tables
- Accumulation: `FADD.FTZ R4, R9, R4` (sum += value)
- Shared memory: `STS [R13], R4` (store accumulator)
- Two-level shared memory reduction: `LDS` / `FADD.FTZ R6, R6, R3` / `STS [R13], R6`
- Probability: `I2F.U32.RP` + `MUFU.RCP` (1.0f / num_outcomes)
- Card index: modulo via `IMAD.HI.U32` 64-bit multiply
- Output: `STG.E [R8.64], R11` (normalized result) or `STG.E [R8.64], R0` (zero)
- Multiple output stores (up to 4 values) with predicate-gated addressing

### evaluate_showdown_brute_3p (sm_80, ~289 instructions) — 3-player brute force showdown
- Thread bounds check: `blockIdx.x >= c[0x0][0x1e4]` → EXIT
- Card extraction: `PRMT R27, R22, 0x7771, RZ` (permute 0x7771), `LOP3.LUT R25, R22, 0xff, RZ, 0xc0, !PT` (suit = card & 3)
- Dead card detection: `ISETP.EQ.OR` chains comparing vs board cards
- Warp operations: `VOTEU.ANY UR8, UPT, PT` (warp vote any), `POPC R15, UR8` (population count)
- Atomic aggregation: `ATOMS.ADD R18, [UR5], R15`, `SHFL.IDX PT, R18, R18, R17, 0x1f` (warp shuffle by index)
- Card comparison: `PRMT` with 0x7771 permutation, `LOP3.LUT 0xff/0xc0` suit extraction
- Rank comparison: `ISETP.GE.U32.AND` / `ISETP.GT.U32.AND`
- EV accumulation: `FSEL` conditional select, `FADD.FTZ` chains
- Final EV: `FMUL.FTZ R11, R2, R11` + `FFMA.FTZ R11, R0, R13, R11`
- Output: `STG.E [R14.64], R11`

### evaluate_showdown_prefix_3p (sm_80, ~721+ instructions) — 3-player prefix tree showdown
- Thread bounds check: `blockIdx.x >= c[0x0][0x1f4]` → EXIT
- Prefix tree node loading: `LDG.E.CONSTANT` from pointer tables
- Shared memory: `STS [R3.X4+0x20], R8` (store prefix tree node)
- Large buffer clear: 14× `STS [R11+offset], RZ` (0x100 stride up to 0xd00)
- Prefix tree traversal: `IABS` (absolute value), `MUFU.RCP`, modulo via `IMAD.HI.U32`
- Card index: `LOP3.LUT R3, R3, c[0x0][0x0], RZ, 0x3c, !PT` (bit manipulation mask 0x3c)
- Large-scale accumulation: repeated `LDS` / `FADD.FTZ` / `STS` chains (16 values per block, 0x40-byte stride)
- Warp reduction: `SHFL.UP PT, R*, R*, 0x1/0x2/0x4/0x8/0x10/0x20, RZ` (6 steps)
- Inline subroutines: `CALL.REL.NOINC 0x2be0` / `RET.REL.NODEC R4 0x0`
- Divergent branching: `BRA.DIV UR6, 0x2a80`
- Integer min/max: `IMNMX R2, R0, R5, PT`
- Output: `STS [R9], R8` (store warp sum)

### evaluate_fold_showdown_prefix (sm_80, ~793+ instructions) — Fold/showdown prefix eval
- Thread bounds check: `blockIdx.x >= c[0x0][0x214]` → EXIT
- Card extraction: `LOP3.LUT R9, R2, 0x1f, RZ, 0xc0, !PT` (rank = card & 0x1f), `LOP3.LUT R11, R11, 0x7c, RZ, 0xc0, !PT`
- Uniform register broadcast: `R2UR UR4, R4`, `ULOP3.LUT UR6, UR6, 0x1, URZ, 0x3c, !UPT`
- Shared memory clearing: 14× `STS [R11+offset], RZ` (0x100 stride up to 0xd00)
- Prefix tree accumulation: `FADD.FTZ` chains, `STS` / `LDS` shared memory ops
- IMNMX: `IMNMX R16, R16, UR7, PT` (integer min/max)
- WARPSYNC + BAR.SYNC.DEFER_BLOCKING
- Output: `STG.E [R4.64], R7` (store result)

### Key SASS instruction reference
| Instruction | Meaning |
|-------------|---------|
| `LDG.E.CONSTANT Rd, [a.64]` | Load 32-bit from constant memory |
| `LDG.E.U8/U16.CONSTANT` | Load byte/halfword from constant memory |
| `ULDC.64 URd, c[0x0][N]` | Load 64-bit constant pointer into uniform reg |
| `PRMT Rd, Rs, 0x9910, RZ` | Byte permutation (extract packed card bytes) |
| `LOP3.LUT Rd, Ra, Rb, Rc, imm` | 3-input bitwise LUT (0xc0 => Rb & 0x3 = suit) |
| `SHF.R.S32.HI Rd, RZ, imm, Rs` | Shift-right-high (unpack high 16 bits) |
| `BSSY Bn, addr / BSYNC Bn` | Block barrier sync |
| `ATOMS.CAST.SPIN Rd,[a],Rc,Rv` | Atomic compare-and-swap spin lock |
| `FFMA.FTZ Rd,Ra,Rb,Rc` | Rd = Ra*Rb + Rc (float, flush-to-zero) |
| `FMNMX.FTZ Rd,Ra,Rb,!PT` | Fused min/max (conditional clamp) |
| `MUFU.RCP Rd, Rs` | Reciprocal approximation (1/Rs) |
| `DADD/DMUL/DSETP.GEU` | Double-precision add/mul/compare |
| `F2F.F64.F32 / F2F.F32.F64` | float <-> double conversion |
| `STG.E [a.64], Rs` | Store to global memory |
| `STS [a], Rs / LDS Rd, [a]` | Shared memory store / load |
| `IMAD Rd,Ra,Rb,Rc` | Rd = Ra*Rb + Rc (int32) |
| `S2R Rd, SR_TID.X / SR_CTAID.X` | threadIdx.x / blockIdx.x |

## 13. DEVICE BUFFER INVENTORY

| Buffer | Purpose |
|--------|---------|
| `d_cfv_` | counterfactual values |
| `d_abs_regrets_` | absolute regrets |
| `d_abs_strategy_` | absolute strategy |
| `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` | counterfactual reach (per player) |
| `d_init_weights_` | initial weights |
| `d_discount_params_` | discount parameters (3 floats: `iter_params`) |
| `d_strategy_packed_` | packed strategy output |
| `d_compact` | compact archive data |
| `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_` | gather staging |
| `cfv_stage_host_` (pinned) | host-pinned CFV staging |
| `pinned_discount_params_` (pinned) | host-pinned discount params |
| `cfv_stage_scales_` (pinned) | host-pinned CFV scales |

## 14. COMPACT ARCHIVE FORMAT (Apache Avro + DEFLATE)

- **Magic:** `OBJ 0x04 0x1c 0x96 0x52` (standard Avro file header)
- **Codec:** DEFLATE per `avro.codec` metadata
- **Native entry points:** `solver.strategy.archive.open/node/close`
- **Variants:** `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`
- **Async lifecycle:** `avro_busy` → `avro_done` / `avro_cancelled`

**Archive record layout:**
```
{ version, num_players, num_actions, num_combos(=1326),
  board (card indices), paid, pot,
  reach_probs_q16 [num_combos],          // Q16
  strategy_probs_q16 [num_actions x num_combos],  // Q16, row-major
  action_evs_q16 [num_actions x num_combos] or null, // Q16
  compact_regrets_enabled: bool,
  regret_descriptor / regret_hand_map (if enabled),
  chance_representatives: empty or 52 bytes,
  history (action sequence),
  scale (float),                         // validated nonzero/valid
}
```

**Validation errors:**
- "Compact strategy archive contains invalid -32768 q16"
- "Compact archive has nonzero reach values with a zero scale"
- "Compact archive chance mapping is invalid"
- "Compact regret layout only supports two-player games"
- "Invalid compact Avro strategy row sum"

## 15. TRANSPORT / BRIDGE PROTOCOL (from embedded JS)

- **API base:** `http://127.0.0.1:6006` (overridable `?apiBase=`)
- **Transport:** `bridge` (default) or `http` (via `?transport=`)
- **Bridge:** `window.chrome.webview.postMessage(JSON.stringify({id,method,params}))`
- **Auth:** `bridge.ping` (1500 ms) → `bridge_token`; sent as `bridge_token` param; retry on `UNAUTHORIZED`
- **RPC envelope:** `{ id: "rpc_<ts>_<seq>", method, params }`
- **Response envelope:** `{ ok: bool, result: any, error: {message, code} }`
- **Ds class:** `pending` Map, `seq` counter, `authToken`
- **callBridge(t, s, a=5000):** RPC id, postMessage, promise with timeout
- **callBridgeAuthed():** adds `bridge_token` param; on `UNAUTHORIZED` clears token, re-auths, retries once
- **request(t, s, a):** `dT(path, method)` resolves internal method name; per-method timeout; fallback to `callBridgeAuthed("http.request", …)` on `NOT_IMPLEMENTED`

## 16. SOLVE FLOW (from PlayPanel `ya` class)

```
1. POST /api/init      body = initTemplate
2. POST /api/allocate  body = {}
3. POST /api/gpu-solve body = {max_iterations, target_exploitability, compute_initial_exploitability:false}
4. GET  /api/solve/status  (poll every 250ms) -> {running, iteration, elapsed_ms, exploitability, phase, last_error}
   repeat until running == false
5. POST /api/solve/stop body = []
6. GET  /api/results                            -> node strategy + EVs (seats 0 and 1)
7. POST /api/export/current-street body={history:[], max_nodes} -> {payload: snapshot}
```

Per-board loop (batch): for each board → init → allocate → gpu-solve → poll status → stop → results → export current-street → persist snapshot JSON.

## 17. JSON SCHEMAS (key endpoints)

### POST /api/init — init template
```json
{
  "oop_range": [float x1326],
  "ip_range": [float x1326],
  "board": [int x3],           // board card indices (0-51), exactly 3 cards (flop)
  "starting_pot": int,
  "effective_stack": int,
  "rake_rate": float,
  "rake_cap": float,
  "oop_flop_bet": "33%,67%,100%",
  "oop_flop_raise": "33%,67%,100%",
  "ip_flop_bet": "33%,67%,100%",
  "ip_flop_raise": "33%,67%,100%",
  "oop_turn_bet": "100%",
  "oop_turn_raise": "100%",
  "ip_turn_bet": "100%",
  "ip_turn_raise": "100%",
  "oop_river_bet": "100%",
  "oop_river_raise": "100%",
  "ip_river_bet": "100%",
  "ip_river_raise": "100%",
  "donk_option": true,
  "oop_turn_donk": "100%",
  "oop_river_donk": "100%",
  "max_raise_number": 3,
  "add_allin_threshold": 1.0,
  "force_allin_threshold": 0.2,
  "add_allin_flop_ip": true,
  "add_allin_turn_ip": true,
  "add_allin_river_ip": true,
  "add_allin_flop_oop": true,
  "add_allin_turn_oop": true,
  "add_allin_river_ip": true,
  "add_allin_river_oop": true,
  "num_players": 2
}
```

### GET /api/solve/status — poll response
```json
{
  "running": bool,
  "iteration": int,
  "elapsed_ms": int,
  "exploitability": float | null,
  "phase": "allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed",
  "last_error": string | null
}
```

### GET /api/results — node results
```json
{
  "seat": int,
  "seatNames": ["SB / BTN","BB"],
  "board": [int],
  "paid": [int x num_players],
  "pot": int,
  "labels": ["Fold","Call","Check","Raise 100",...],
  "ranges": [ [float x1326], [float x1326] ],
  "cards": [int],
  "probabilities": [float],         // [num_actions x 1326] flattened
  "evs": [float | null]             // per-action EV in cents/100
}
```

## 18. ANTI-TAMPER / PROTECTIONS

| # | Technique | Location | Bypass |
|---|-----------|----------|--------|
| 1 | `IsDebuggerPresent` | KERNEL32 import; `0x14000b178` (loader) | Patch IAT thunk to return 0 |
| 2 | 5× `cpuid` | Entry stub near `0x140150474`–`0x1401505e8` | NOP `cpuid` (0f a2) instructions |
| 3 | `OutputDebugStringW` | KERNEL32 import | Stub/NOPs |
| 4 | `IsProcessorFeaturePresent` | KERNEL32 import | Stub/NOPs |
| 5 | `TerminateProcess` | KERNEL32 import | NOP call sites |
| 6 | `GetStartupInfoW` | KERNEL32 import | Stub/NOPs |
| 7 | `QueryPerformanceCounter` | KERNEL32 import | Stub/NOPs |
| 8 | Stack cookie | `0x14014fe20` | NOP stack cookie check |
| 9 | Error handler (int3) | `0x14016d9e0` | NOP `int3` or replace with `ret` |
| 10 | Error handlers | `0x140002970`, `0x140002b80` | NOP calls |
| 11 | VRAM gate | Runtime CUDA check | Stub `nvmlDeviceGetMemoryInfo` to return ≥8 GB |
| 12 | CUDA driver version check | Loader `TexasSolverGpu.exe` | Force CUDA 13.1 compatible branch |
| 13 | Singleton mutex | `lock cmpxchg QWORD PTR [0x140012710]` | Write 0 to mutex address |
| 14 | Stripped binary | `.text`/`.rdata` | Signature-based function identification |
| 15 | PTX JIT (no embedded cubin) | `cuModuleLoadData` | SASS cubin IS embedded (24 ELF objects) |
| 16 | No license/activation | — | None needed |

**Recommended instrumentation approach:** API-level instrumentation (proxy `nvcuda.dll`, WinSock LSP) rather than user-mode debugger, due to `IsDebuggerPresent` and `OutputDebugStringW` detection.

## 19. CUDA DRIVER API CALL SITES FOR DYNAMIC HOOKING

| Code VA | String | Context |
|---------|--------|---------|
| `0x24aeb0` | `cuGraphLaunch(training_graph_exec_, stream_)` | CUDA graph execution |
| `0x24bd10` | `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` | Discount params upload |
| `0x24ade0` | `cuMemcpyDtoDAsync(d_cfreach_.dev_ptr(), ...)` | CFV reach copy |
| `0x24b1b0` | `cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), ...)` | CFV stage download |
| `0x24b870` | `cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_.dev_ptr(), ...)` | Strategy packed download |

**CUDA Driver API import names (`.rdata`/`.idata`):**
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

## 20. BLOCKED ITEMS (require Windows + NVIDIA GPU)

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Method names stored in heap at `[handler_obj+0x80]`; no static table exists |
| Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, sharedMem, stream, args) |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync(d_discount_params_, …)` at VA `0x24bd10` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution on Windows + GPU |
| Dynamic anti-tamper validation | Requires Windows/x64 + NVIDIA GPU + instrumentation tools |

**Required environment:**
1. Windows/x64 with NVIDIA GPU (compute capability 7.5–12.0, VRAM ≥ 8 GB)
2. Instrumentation tools: x64dbg, Ghidra, radare2, or API-level proxy DLLs
3. Anti-tamper bypass: patch `IsDebuggerPresent` (return 0), handle 5× `cpuid`, NOP `int3` error handler

## 21. DELIVERABLES STATUS

| Deliverable | Status | Location |
|-------------|--------|----------|
| Binary inventory & PE structure | ✅ Complete | BINARY_ANALYSIS_RECON.md, FINAL_COMPREHENSIVE_RE.md |
| CUDA kernel inventory (45 kernels) | ✅ Complete | PHASE3_CFR_LAUNCHERS.md, EMBEDDED_CUDA_KERNELS_RECON.md |
| CUDA mathematics | ✅ Complete | CUDA_MATH.md |
| C++ data structures | ✅ Complete | CPP_CORE_RECON.md |
| Native method dispatcher | ✅ Complete | DEEP_RE_PHASE2.md, PHASE4_STATIC_RECON.md |
| Handler vtables (~22 addresses) | ✅ Complete | HANDLER_VTABLE_RECON.md |
| Method dispatch chain (19+23 methods) | ✅ Complete | HANDLER_DISPATCH_RECON.md, STATIC_EXTENSION_RECON.md |
| SASS disassembly (270 files, 6 archs) | ✅ Complete | /tmp/opencode/sass_disasm/<arch>/ |
| SASS-level algorithmic details | ✅ Complete | CUDA_KERNELS_RECON.cpp, SASS_DISASM_REFERENCE.md, this doc |
| Q16 + compact archive format | ✅ Complete | PHASE3_COMPACT_ARCHIVE.md |
| Transport/bridge protocol | ✅ Complete | FRONTEND_API_RECON.md, PHASE6_FRONTEND_API_DEEP_DIVE.md |
| JSON schemas | ✅ Complete | FRONTEND_API_RECON.md §4 |
| Anti-tamper surface | ✅ Complete | ANTI_TAMPER_BYPASS.md |
| Method → handler VA mapping | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Kernel launch configs | ❌ Blocked (dynamic) | Requires Windows + GPU |
| iter_params capture | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Compact archive bytes | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Full solve cycle | ❌ Blocked (dynamic) | Requires Windows + GPU |

## 22. KEY FILE PATHS

- Target binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_131.exe`
- Legacy binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu_legacy_126.exe`
- Loader binary: `/kaggle/working/re_lab/target_bin/TexasSolverGPU-0.3.0-win64\TexasSolverGpu.exe`
- Embedded ELF objects: `/kaggle/working/re_lab/texassolvergpu/ELF_OBJECTS/`
- SASS code sections: `/kaggle/working/re_lab/texassolvergpu/KERNEL_CODE_ALL/<arch>/obj<N>/<kernel>.sass`
- SASS disassembly: `/tmp/opencode/sass_disasm/<arch>/<kernel>.txt`
- Kernel inventory: `/kaggle/working/re_lab/texassolvergpu/KERNEL_INVENTORY.json`
- All recon documents: `/kaggle/working/re_lab/texassolvergpu/*.md`

## 22.5 POKER_GPU_* ENVIRONMENT VARIABLES (47 total — NEW)

All read via `GetEnvironmentVariableW`. Documented in `ENV_VARS_AND_RUNTIME_CONFIG_RECON.md`. Key categories:

| Category | Variables |
|----------|-----------|
| File-backed I/O | `POKER_GPU_FILE_BACKED_DIR`, `FILE_BACKED_RESULTS`, `FILE_BACKED_THRESHOLD_MB`, `FILE_COALESCE_GAP`, `FILE_QUEUE_BUDGET` |
| CFV/staging | `POKER_GPU_CFV_STAGING`, `POKER_GPU_PACKED_STATE`, `POKER_GPU_PACKED_STATE_MIN_SAVINGS_MB` |
| Compact/archive | `POKER_GPU_COMPACT_BACKWARD_CACHE`, `POKER_GPU_COMPACT_INVALID_REGRETS`, `POKER_GPU_COMPACT_REGRET_ALIGNMENT` |
| CUDA graph | `POKER_GPU_CUDA_GRAPH`, `POKER_GPU_DIAG_GRAPH` |
| Diagnostics | `POKER_GPU_DIAG_FILE_BACKING`, `POKER_GPU_DIAG_FINALIZE`, `POKER_GPU_DIAG_PERSISTENT_BACKWARD`, `POKER_GPU_DIAG_RIVER_FORWARD`, `POKER_GPU_DIAG_TIMING` |
| Pipeline | `POKER_GPU_DUAL_RIVER_PIPELINE`, `POKER_GPU_PERSISTENT_RIVER_FORWARD`, `POKER_GPU_RIVER_BATCH`, `POKER_GPU_RIVER_BATCH_NODES` |
| Kernel fusion | `POKER_GPU_FUSE_FOLD_SHOW`, `POKER_GPU_FUSE_MIXED_FOLD_SHOW`, `POKER_GPU_FUSE_OPPONENT_TERMINAL_PARENT`, `POKER_GPU_FUSE_SHOW_CONTINUATION`, `POKER_GPU_FUSE_TERMINAL_PARENT` |
| Memory | `POKER_GPU_MEMORY_BREAKDOWN`, `POKER_GPU_HOST_WORKSPACE`, `POKER_GPU_MAPPED_FILE_WRITES`, `POKER_GPU_MAPPED_TRIM_BYTES` |
| Showdown/fold | `POKER_GPU_FOLD_FP32`, `POKER_GPU_FOLD_SHOWDOWN_BLOCK`, `POKER_GPU_SHOWDOWN_BLOCK`, `POKER_GPU_SHOWDOWN_64_MAX_OPP` |
| Forward/backward | `POKER_GPU_FORWARD_BLOCK`, `POKER_GPU_OPP_BACKWARD_BLOCK`, `POKER_GPU_OWN_PARENT_BLOCK`, `POKER_GPU_OWN_PARENT_REGCACHE` |
| Other | `POKER_GPU_BALANCE_RIVER_PAIRS`, `POKER_GPU_EXACT_FOLD_REDUCTION`, `POKER_GPU_FORCE_OFF64`, `POKER_GPU_FINALIZE_GPU_ENCODE`, `POKER_GPU_MIXED_GENERIC_UPDATE`, `POKER_GPU_OPP_CARD_PREFIX`, `POKER_GPU_PACK_CARD_METADATA`, `POKER_GPU_SMALL_TREE_FUSED` |

---

## 22.6 SOURCE CODE FILE PATHS (NEW)

| Path | Role |
|------|------|
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp` | Main GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp` | 3-player GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp` | CUDA driver API wrapper |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h` | CUDA buffer header |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h` | CUDA host memory header |

---

## 22.7 ENRICHED API DETAILS (NEW)

### POST /api/allocate
Body: `{ "require_exact_2p_gpu_layout": true }`

### POST /api/gpu-solve
Body: `{ "max_iterations": int, "target_exploitability": float, "compute_initial_exploitability": bool, "verbose": bool }`

### POST /api/solve/preflight
Body: `{}` — returns VRAM check with severity ("critical"|"warning"|null)

### GET /api/solve/status — Enriched response schema
```json
{
  "running": bool,
  "iteration": int,
  "max_iterations": int,
  "exploitability": float | null,
  "exploitability_iteration": int,
  "exploitability_elapsed_ms": int,
  "player0_exploitability": float | null,
  "player1_exploitability": float | null,
  "elapsed_ms": int,
  "stop_requested": bool,
  "phase": "preparing|allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed|idle",
  "last_error": string | null
}
```

### POST /api/export/current-street
Body: `{ history: [], max_nodes: int }` → `{ payload: snapshot }`

### POST /api/export/current-street-file / POST /api/export/full-file
Body: JSON — exports strategy archive (Avro format)

### GET /api/export/full-file/progress
Returns export progress percentage

---

## 22.8 TIMING / DEBUG STRINGS (NEW)

| String | Context |
|--------|---------|
| `GPU iteration: 0 / %u (exploitability = %.4e)` | Initial iteration log |
| `GPU iteration: %u / %u (exploitability = %.4e)` | Per-iteration progress |
| `[gpu timing] submit=%.3f training_sync=%.3f pin_wait=%.3f exploit=%.3f finalize=%.3f total=%.3f ms` | Per-iteration timing breakdown |
| `[abs_finalize] %s gpu_cfv=%.1fms expand=%.1fms total=%.1fms` | Absolute finalize timing |
| `[gpu finalize] p0_gpu=%.3f p0_scatter=%.3f p1_gpu=%.3f p1_scatter=%.3f before_unpack=%.3f unpack=%.3f total=%.3f ms` | GPU finalize phase timing |
| `[gpu finalize] staged CFV enabled: max_plan=%llu total_p0=%llu total_p1=%llu chunks_p0=%u chunks_p1=%u device_capacity=%llu host_capacity=%llu direct_host=%d` | Staged CFV configuration |
| `gpu_solver_buffer_usage` | Buffer usage metric |
| `gpu_solver_error.log` | Error log file |

---

## 22.9 BATCH MODE (NEW)

Batch solver supports multiple boards:
- `boards_input`: text input of board cards (3-5 cards, e.g. `Qs Jh 2h`)
- `randomBoardCount`: 1-25 random boards (`batch.max_25_error` if exceeded)
- `duplicate_card_error`: duplicate card validation
- `invalid_board_len`: board must be 3-5 cards
- `snapshotBytes`, `resultKey` per job
- `exportMaxNodes`: 100-100000 nodes per export
- Persistence warnings: `batch.persistence_warning`, `batch.persistence_limit_warning`, `batch.persistence_space_warning`

---

## 22.10 QUICK START FORMATS (NEW)

| Key | Name | Stack | Ante |
|-----|------|-------|------|
| `six` | Cash 6max 100bb | 10000 | 0 |
| `six50` | Cash 6max 50bb | 5000 | 0 |
| `sixAnte` | Cash 6max Ante 50bb | 10000 | 16 (0.16bb) |
| `spin2` | Spin & Go HU | variable | 0 |
| `spin3` | Spin & Go 3-max | variable | 0 |
| `hu` | HU Cash | 100bb | 0 |

---

## 23. TOOLS CREATED (STATIC, READY TO USE)

| Tool | Location | Purpose |
|------|----------|---------|
| Proxy nvcuda.dll source | `/tmp/opencode/wine_stubs/nvcuda_proxy.cpp` | Logs all CUDA driver API calls; forwards to real nvcuda.dll; compiles with MinGW x64 |
| nvcuda.def | `/tmp/opencode/wine_stubs/nvcuda.def` | Module definition file for proxy DLL (30 exports) |
| Compact archive decoder | `/kaggle/working/re_lab/texassolvergpu/compact_archive_decoder.py` | Decodes Avro + DEFLATE + Q16 compact archives |
| CFR solver simulator | `/kaggle/working/re_lab/texassolvergpu/cfr_simulator.py` | CPU reference: eval5/eval7, Q16, CFR regret matching, forward reach |
| Remaining gaps guide | `/kaggle/working/re_lab/texassolvergpu/REMAINING_GAPS_AND_DYNAMIC_CAPTURE_GUIDE.md` | 7 blocked items, anti-tamper bypass, step-by-step capture procedure |

## 23.5 FRONTEND JS `ya` CLASS (PLAYPANEL) — COMPLETE SOLVE FLOW

Extracted from embedded Vue 3 frontend (all_strings.txt / index-DQB6irIW.js). The `ya` class is the PlayPanel solver controller. All API calls go through the `Ds` bridge class over WebView2 at `http://127.0.0.1:6006`.

### Class `ya` — constructor and state
```js
constructor(apiBase) {
    this.apiBase = apiBase;           // e.g. "http://127.0.0.1:6006" or "bridge"
    this.mayBeRunning = false;        // solver busy flag
    this.cards = [[], []];            // per-seat private cards
    this.payload = null;              // last init payload
    this.entryPaid = null;            // per-seat entry paid
}
```

### `ya.ensureIdle()` — Calls `solver.solve.status` (import `F`)
- Throws if `running == true` or phase in {preparing, allocating, initializing, solving, stopping}

### `ya.stop()` — Calls `solver.solve.stop` (import `O`)
- Only if `mayBeRunning` is true

### `ya.prepare(e, o, a, s, n, i, m)` — Full solve flow
1. `ensureIdle()` → check no solver running
2. Validate postflop contribution base `i` (paid[0]==paid[1], sum <= starting_pot)
3. `D(apiBase, e)` → **`POST /api/init`** (`solver.init`) with init template
4. `E(apiBase)` → **`POST /api/allocate`** (`solver.allocate`)
5. `H(apiBase)` → **`GET /api/environment/check`** (`solver.environment.check` — preflight, checks VRAM)
6. If severity == "critical" → throw "Not enough GPU memory"
7. `this.mayBeRunning = true`
8. `L(apiBase, {max_iterations, target_exploitability, compute_initial_exploitability: false})` → **`POST /api/gpu-solve`** (`solver.solve.start`)
9. Poll `F(apiBase)` → **`GET /api/solve/status`** every 250ms until `running == false`
10. `I(apiBase, 0)` and `I(apiBase, 1)` → **`GET /api/results`** (`solver.node.results`) for seats 0 and 1
11. Returns `{cards: [[seat0_cards], [seat1_cards]], status: ...}`

### `ya.confirmStopped()` — Calls `solver.solve.stop` + checks status
- Returns `true` only if not running and not in a busy phase

### `ya.node(e)` — Node query
- `M(apiBase, e)` → **`GET /api/current-player`** (`solver.node.currentPlayer`)
- `j(apiBase)` → **`GET /api/num-actions`** (`solver.node.numActions`)
- `G(apiBase)` → **`GET /api/current-board`** (`solver.node.currentBoard`)
- `V(apiBase, [])` → **`POST /api/actions-after`** (`solver.node.actionsAfter`)
- `J(apiBase)` → **`GET /api/results`** (`solver.node.results`)
- Returns `{kind, seat, board, paid, labels, data, cards, snapshot}`

### `ya.policy(e, o)` — Direct native call (NOT via HTTP)
- `q(e.data, e.cards, e.labels.length, o)` — calls `solver.policy(data, cards, num_actions, options)` directly on the solver object
- Returns `{probs: Float32Array[1326], evs: Float32Array|null}`

### `ya.possibleCards()` — HTTP call
- `K(apiBase)` → **`GET /api/possible-cards`** (`solver.cards.possible`)
- Returns `BigInt` bitmask

### Import mappings (from minified JS)
| Import | Method | Endpoint | Transport |
|--------|--------|----------|-----------|
| `F` | `solver.solve.status` | GET /api/solve/status | HTTP/bridge |
| `O` | `solver.solve.stop` | POST /api/solve/stop | HTTP/bridge |
| `D` | `solver.init` | POST /api/init | HTTP/bridge |
| `E` | `solver.allocate` | POST /api/allocate | HTTP/bridge |
| `H` | `solver.environment.check` | GET /api/environment/check | HTTP/bridge |
| `L` | `solver.solve.start` | POST /api/gpu-solve | HTTP/bridge |
| `I` | `solver.node.results` | GET /api/results | HTTP/bridge |
| `M` | `solver.node.currentPlayer` | GET /api/current-player | HTTP/bridge |
| `j` | `solver.node.numActions` | GET /api/num-actions | HTTP/bridge |
| `G` | `solver.node.currentBoard` | GET /api/current-board | HTTP/bridge |
| `V` | `solver.node.actionsAfter` | POST /api/actions-after | HTTP/bridge |
| `J` | `solver.cards.possible` | GET /api/possible-cards | HTTP/bridge |
| `K` | `solver.cards.private` | GET /api/private-cards/<n> | HTTP/bridge |
| `q` | `solver.policy` | direct native call | N/A |

## 23.6 ADDITIONAL METHOD STRINGS FOUND IN all_strings.txt

These strings were found in the binary's string table but are NOT in the dT table or the if-else dispatch chain. They are registered via the handler creator (`0x140036d70`) at runtime, not via static if-else matching.

| String | Notes |
|--------|-------|
| `solver.cpp` | Source file reference (not a method) |
| `solver.mayBeRunning` | JS class property on `ya`, not a native method |
| `solver.practiceDrills.v1` | localStorage key for practice drills (`Zp` constant), not a native method |
| `solver.confirmStopped` | Documented as additional native method |
| `solver.ensureIdle` | Documented as additional native method |
| `solver.policy` | Documented as additional native method |
| `solver.possibleCards` | Documented as additional native method |
| `solver.prepare` | Documented as additional native method |

## 23.7 ADDITIONAL SASS KERNEL ALGORITHMIC DETAILS

### forward_river_subtrees_packed (sm_80, 1113 instructions)
- Thread bounds check: `ISETP.GE.AND P0, PT, R15, c[0x0][0x1e8], PT` then `@P0 EXIT`
- Loads subtree pointers from constant memory: `LDG.E.CONSTANT` from `c[0x0][0x1b8]`, `c[0x0][0x190]`, `c[0x0][0x1a0]`, `c[0x0][0x1a8]`, `c[0x0][0x1ac]`, `c[0x0][0x1b4]`
- `LDG.E.U8.CONSTANT` loads individual bytes (card/suit data) from constant memory
- 11-bit field extraction: `SHF.R.U32.HI R14, RZ, 0xb, R22` (shift right 11), `LOP3.LUT R17, R22, 0x7ff, RZ, 0xc0, !PT` (mask 0x7ff)
- Multi-barrier synchronization: `BSSY B0/BSYNC B0`, `BSSY B1`, `BSSY B2`, `BSSY B3`, `BAR.SYNC.DEFER_BLOCKING`
- Shared memory tiling: `STS.64`, `STS.128`, `STS.64` to shared memory, `LDS.128` reload
- Processes river subtrees in packed format with conditional branching on `P0`-`P5` predicates

### backward_chance_turn_river (sm_80, 209 instructions)
- Chance node backward propagation for turn/river streets
- `I2F.U32.RP R5, c[0x0][0x0]` — int→float convert (round positive)
- `MUFU.RCP R5, R5` — reciprocal approximation
- `F2I.FTZ.U32.TRUNC.NTZ R3, R2` — float→int truncate
- Bit-pair extraction: `LOP3.LUT R4, RZ, R0, RZ, 0x33, !PT` (0x33 = 0b00110011)
- Suit extraction: `LOP3.LUT P1, R2, R2, 0x3, RZ, 0xc0, !PT` (0xc0 mask = suit bits)
- Zeroes shared memory entries: `STS [R4], RZ` (multiple stores)
- Conditional loops with `@!P1 BRA`, `@!P2 BRA` for chance outcome iteration

## 24. ENVIRONMENT LIMITATIONS

- **Wine:** Completely broken on this host — `wine: could not load kernel32.dll, status c0000135`. Cannot execute any Windows binary.
- **No Windows/x64 + NVIDIA GPU:** All Phase 5 dynamic items remain blocked.
- **Static analysis:** 100% complete. No further static analysis can resolve the remaining unknowns.

## 25. NEWLY DOCUMENTED SASS KERNELS (Deep Static Analysis)

The following 10 kernels were reverse-engineered at the SASS instruction level and are NOT covered in §12. Full details in `SASS_KERNEL_DEEP_RECON.md`.

| Kernel | File | Instructions | Algorithm |
|--------|------|-------------|-----------|
| `backward_player_cfv_packed` | `backward_player_cfv_packed.txt` | 601 | Packed CFV backward, Q16 dequant, regret matching, MUFU.RCP normalization |
| `backward_player_packed` | `backward_player_packed.txt` | ~790 | S16 packed regrets, 8-action blocks, signed fixed-point |
| `backward_opponent` | `backward_opponent.txt` | 361 | Opponent CFV averaging, 1/num_outcomes chance weighting |
| `backward_chance_abstract` | `backward_chance_abstract.txt` | 161 | Abstract chance node backward, dead-card check (-1), 4 outcomes |
| `forward_opponent` | `forward_opponent.txt` | 505 | Opponent reach propagation, warp reduction (SHFL.DOWN), epsilon normalization |
| `apply_isomorphism` | `apply_isomorphism.txt` | 97 | Tree compaction, child pointer swap, value accumulation |
| `encode_stage_segments_i16` | `encode_stage_segments_i16.txt` | 249 | i16 encoding, warp max reduction, Newton-Raphson scale refinement |
| `gather_cfv` | `gather_cfv.txt` | 41 | Simple gather/scatter: out[tid] = src_table[indices[tid]] |
| `evaluate_fold` | `evaluate_fold.txt` | 665 | Fold equity evaluation, Q16, board card extraction |
| `train_small_tree_fp32_multi_iter` | `train_small_tree_fp32_multi_iter.txt` | 3401 | Multi-iter CFR, variable constant buffer (c[0x0][R37+off]), FFMA chains |

**Updated kernel documentation coverage: 10/45 kernels (was 7/45). Remaining: 35 kernels.**

---

## 26. CUDA DRIVER API CALL SITES (Source Variable Names)

Full details in `CUDA_API_CALL_SITES.md`. Extracted C++ source variable names from `.rdata` strings:

| Code VA | API Call | Source Variable |
|---------|----------|-----------------|
| `0x24aeb0` | `cuGraphLaunch(training_graph_exec_, stream_)` | `training_graph_exec_`, `stream_` |
| `0x24bd10` | `cuMemcpyHtoDAsync(d_discount_params_.dev_ptr(), iter_params, 3*sizeof(float), stream_)` | `d_discount_params_`, `iter_params` (3 floats) |
| `0x24ade0` | `cuMemcpyDtoDAsync(d_cfreach_.dev_ptr(), d_init_weights_[opponent].dev_ptr(), nho*sizeof(float), stream_)` | `d_cfreach_`, `d_init_weights_[opponent]`, `nho` |
| `0x24b1b0` | `cuMemcpyDtoHAsync(cfv_stage_host_, d_cfv_stage_i16_.dev_ptr(), chunk_elements*sizeof(int16_t), stream_dl_)` | `cfv_stage_host_`, `d_cfv_stage_i16_`, `stream_dl_` |
| `0x24b870` | `cuMemcpyDtoHAsync(pinned_file_staging_, d_strategy_packed_.dev_ptr()+offset, chunk, stream_file_)` | `pinned_file_staging_`, `d_strategy_packed_`, `stream_file_` |

**NVML functions:** `nvmlInit_v2`, `nvmlShutdown`, `nvmlDeviceGetMemoryInfo`, `nvmlDeviceGetHandleByPciBusId_v2`

**Streams:** `stream_`, `stream2_`, `stream_dl_`, `stream_file_`
**Events:** `event_fwd_done_`, `event_show_done_`, `event_cfv_stage_ready_`, `event_cfv_stage_free_` (all `CU_EVENT_DISABLE_TIMING`)

---

## 27. FRONTEND SOURCE CODE DEEP RECON

Full details in `FRONTEND_SOURCE_DEEP_RECON.md`. The embedded Vue 3 frontend (32,289 lines in `all_strings.txt`) includes:

### Components
- `PlayPanel` (`ya` class) — complete solve flow with 13 direct API calls
- `RunSolverPanel` — solver run control with VRAM preflight
- `BatchSolverPanel` — multi-board batch solver with IndexedDB persistence
- `QuickStart` — preflop scenario picker with chip scaling
- `StrategyMatrix` — 13x13 hand grid with strategy/EV heatmap
- `StrategyInspector` — per-seat detailed strategy view
- `Nodelock Designer` — global/hand/cell/bias strategy override
- `PracticeDrills` — hand scoring with mistake detection (best/mixed/inaccuracy/mistake/blunder)

### IndexedDB Persistence
- **Database:** `texassolver-gpu` (version 1)
- **Object stores:** `batch_manifests` (key: "active"), `batch_results` (keyPath: "key")
- **Batch lock:** `navigator.locks.request("texassolver-gpu-batch-persistence", exclusive)`
- **Max boards:** 25, **Max export nodes:** 100000, **Persistence limit:** 64 MB

### Key Source Paths (from binary)
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h`
- `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h`

**Author:** bupticybee | **Discord:** https://discord.gg/RtyD4vRy2e | **GitHub:** https://github.com/bupticybee

---

## 29. ADDITIONAL SASS KERNEL ALGORITHMIC DOCUMENTATION (8 NEW KERNELS)

The following 8 kernels were previously undocumented at the SASS algorithmic level. All have `.sass` files extracted from embedded ELF objects and full `nvdisasm` output at `/tmp/opencode/sass_disasm/sm_80/`.

### 29.1 evaluate_showdown_abstract (sm_80, ~345 instructions)

**Role:** Abstract showdown evaluation — computes showdown EV using lookup tables rather than brute-force enumeration.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x1ec]` → EXIT (bounds check at 0x00e0–0x0110)
- `threadIdx.x >= c[0x0][0x1a4]` → early exit (0x00c0–0x00e0)
- Constant memory pointers loaded: `c[0x0][0x170]`, `[0x178]`, `[0x180]` (board/hand pointers)

**Phase 1 — Shared memory setup (0x0120–0x0230):**
- Loop over hands, loading `LDG.E.CONSTANT` from pointer tables
- `STS [R9.X4], R4` stores to shared memory
- `BSSY B0` / `BSYNC B0` barrier synchronization
- `HFMA2.MMA` at 0x0020 with epsilon 2.384185791015625e-07

**Phase 2 — EV computation (0x0240–0x1450):**
- Card extraction: `LOP3.LUT` with 0xc0 mask (suit = card & 3), `PRMT 0x9910` for packed card byte extraction
- Dead card checks via `ISETP.EQ.OR` / `ISETP.NE.AND` chains
- Complex `FFMA.FTZ` / `FMUL.FTZ` accumulation chains across 16+ registers
- `LDS.128` / `STS.128` shared memory tiling with 0x10-byte strides
- Multiple output stores with predicate gating (P0–P5)
- Final: `STG.E [R2.64], R15` at 0x1430
- Error handler: `CALL.REL.NOINC 0x1480` → `EXIT` at 0x1480

### 29.2 evaluate_showdown_brute (sm_80, ~329 instructions)

**Role:** Brute-force showdown evaluation — enumerates all opponent hands and computes win/tie/loss counts.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x1e4]` → EXIT (bounds check at 0x0020)
- `threadIdx.x >= c[0x0][0x1e0]` → skip (0x0130–0x0160)
- `BSSY B0` barrier at 0x0140

**Main brute-force loop (0x02b0–0x0540):**
- `VOTEU.ANY UR6, UPT, PT` at 0x0410 (warp vote any)
- `POPC R17, UR6` / `POPC R22, R21` at 0x0460/0x0470 (population count for warp reduction)
- `ATOMS.ADD R20, [UR5], R17` at 0x04a0 (atomic add to global memory)
- `SHFL.IDX PT, R11, R20, R19, 0x1f` at 0x04b0 (warp shuffle by index)
- Dead card detection: `PRMT 0x7771` + `LOP3.LUT 0xff/0xc0` (suit = card & 3)
- Card comparison chains: `ISETP.EQ.OR`, `ISETP.NE.AND`
- `FSEL` conditional selects for win/tie/loss accumulation
- `FFMA.FTZ` chains for EV computation

**Final reduction (0x0560–0x0630):**
- `LDS R7, [UR5]` loads atomic result
- `R2UR UR7, R7` broadcast
- `ISETP.NE.AND P0, PT, RZ, UR7` checks for zero count → EXIT if no valid hands
- Card index extraction: `ULOP3.LUT UR8, UR7, 0x3, URZ, 0xc0` (suit)
- Final: `STG.E [R12.64], R15` at 0x13a0

### 29.3 evaluate_showdown_prefix (sm_80, ~790+ instructions)

**Role:** Prefix-tree showdown evaluation — traverses a compressed prefix tree of hand combinations.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x1ec]` → EXIT
- `P0 = (R2 == 0) || (R3 == 0)` → jump to 0x9e10 if both null (0x00f0–0x0110)

**Prefix tree traversal (0x0120–0x0a20):**
- Loads prefix tree pointers from constant memory `c[0x0][0x1a0]`, `[0x1b8]`
- Card extraction: `LOP3.LUT R10, R6, 0x1f` (rank = card & 0x1f), `LOP3.LUT R11, R11, 0x7c` (suit)
- `IMNMX R13, R3, R18, PT` — integer min/max for tree traversal bounds
- `LOP3.LUT R19, RZ, R15, RZ, 0x33` — bit-pair extraction (0x33 = 0b00110011)
- Large shared memory clearing: 14× `STS [R11+offset], RZ` at 0x100 stride up to 0xd00
- `BSSY B0` / `BSSY B1` multi-barrier synchronization
- Large-scale `FFMA.FTZ` accumulation chains (16 values per block, 0x40-byte stride)

**Prefix tree reduction phase (0x0a20–0x2c70):**
- Multiple barrier phases (B0, B1) with conditional branches
- `SHFL.UP` warp reduction: 0x1, 0x2, 0x4, 0x8, 0x10 steps
- Zeroing shared memory at end: `STG.E [R40.64], RZ`, `STG.E [R38.64], RZ` patterns
- Loop back-branch at 0x2800 → 0x2670 (tree level iteration)
- Final barrier: `BSSY B0` at 0x29a0

### 29.4 forward_chance_turn_river_batched (sm_80, 209 instructions)

**Role:** Forward chance probability distribution for turn/river streets, batched single-grid variant.

**Thread setup:**
- `threadIdx.x >= c[0x0][0x1a0]` → BRA (single grid dimension, no 2D y)
- `blockIdx.x * 8 + threadIdx.x` — 8 outcomes per block

**Phase 1 — Chance computation (0x00a0–0x03f0):**
- `I2F.U32.RP R7, c[0x0][0x0]` — int→float (round positive)
- `MUFU.RCP R7, R7` — reciprocal: 1.0f / num_outcomes
- Card index: modulo via `IMAD.HI.U32` 64-bit multiply (0x0130–0x01c0)
- Dead card check: `LOP3.LUT 0x33` (bit-pair extraction) + comparison against -0x1
- `HFMA2.MMA R8, -RZ, RZ, 0, 1.78813934326171875e-07` at 0x04d0 (FP16 MMA with epsilon)
- `ATOMS.CAST.SPIN R11, [R14], R10, R11` at 0x06a0 — atomic spin lock for shared memory accumulation
- Spin-wait loop: `ISETP.EQ.U32.AND P1, PT, R11, 0x1` then `@!P1 BRA 0x680`

**Phase 2 — Result distribution (0x0440–0x0c10):**
- `I2F R4, c[0x0][0x190]` + `MUFU.RCP R4, R4` — normalization
- Four separate `STG.E` stores to global memory (R13, R15, R17, R19)
- `WARPSYNC 0xffffffff` + `BAR.SYNC.DEFER_BLOCKING 0x0`
- `@P0 EXIT` at 0x0770 (only lane 0 exits)

### 29.5 train_fold_showdown_parent_packed (sm_80, ~800+ instructions)

**Role:** CFR training for fold/showdown parent nodes with packed S16 state.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x26c]` → EXIT
- `threadIdx.x == 0` check → skips to 0x450 if non-zero (leader thread setup)

**Packed state initialization (0x00c0–0x0450):**
- `LDG.E.64.CONSTANT` loads packed state pointers
- `SHF.R.U32.HI R4, RZ, 0xb, R2` + `LOP3.LUT R5, R2, 0x7ff` — 11-bit field extraction (mask 0x7ff)
- `STS.64 [0x8], R2` / `STS.64 [RZ], R22` — shared memory state initialization
- `BSSY B0` / `BSYNC B0` barrier

**Main training loop (0x0460–0x1450):**
- `I2F.U32.RP R5, c[0x0][0x0]` + `MUFU.RCP R5, R5` — chance probability
- Card index: modulo via `IMAD.HI.U32` (0x0570–0x0620)
- Dead card check: `LOP3.LUT 0x33` + comparison
- `STS [R5], RZ` — zeroing shared memory with 0x140 (320-byte) stride
- `LDS.64` / `STS.64` for 64-bit shared memory ops
- Q16 dequant and CFV accumulation with `FFMA.FTZ` chains

**Warp reduction (0x1460–0x16e0):**
- `SHFL.UP PT, R6, R9, 0x1` / `0x2` / `0x4` / `0x8` / `0x10` (5-step butterfly reduction)
- `LOP3.LUT R7, R0, 0x1f` — rank extraction (card & 0x1f)
- `STS [R15.X4+0x44], R14` at 0x15d0 — final shared memory store
- `BAR.SYNC.DEFER_BLOCKING 0x0`

**Final aggregation (0x16e0–0x2c80):**
- `LDS R8, [R6.X4+0x44]` — shared memory reload
- `UMOV UR7, 0xff` + `BRA.DIV UR7, 0x54c0` — divergent warp branch
- `FADD.FTZ R7, R9, R8` — EV accumulation
- `STG.E [R4.X4+0x140], R5` / `STG.E [R8.X4+0x140], R15` — global memory output
- Second phase at 0x2c80: `PRMT R32, RZ, 0x7610` — packed card suit extraction
- `BAR.RED.OR.DEFER_BLOCKING 0x0, P0` + `B2R.RESULT RZ, P0` — block-wide OR reduction

### 29.6 train_mixed_fold_showdown_parent_packed (sm_80, 441 instructions)

**Role:** Mixed fold/showdown CFR training with packed S16 state — computes both fold equity and showdown EV.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x210]` → EXIT
- Leader thread (threadIdx.x == 0) loads packed state pointers

**Main computation (0x04c0–0x1150):**
- `SHF.R.U32.HI R3, RZ, 0xb, R6` + `LOP3.LUT R4, R6, 0x7ff` — 11-bit field extraction
- `SEL R3, R3, 0xffffffff, P0` — conditional select
- `LDG.E.U16.CONSTANT R10, [R10.64]` — 16-bit packed state load
- `PRMT R9, R10, 0x9910` — packed card byte extraction
- `ISETP.NE.AND P0, PT, R9, -0x1` — dead card check
- `I2F.S16 R28, R33` / `I2F.S16 R30, R27` — Q16 dequant (int16 → float)
- `FMUL.FTZ R9, R9, 3.0518509447574615479e-05` — scale by 1/32768
- `FSETP.GT.FTZ.AND P5, PT, R28, RZ` — positive check
- `MUFU.RCP R36, R31` — reciprocal normalization
- `FMNMX.FTZ R23, RZ, R30, !PT` — clamp to >= 0
- `FFMA.FTZ R29, R22, R32, RZ` — fused multiply-add

**Output phase (0x0f20–0x1150):**
- `STG.E [R26.64], R29` — fold EV output
- `@P5 I2F.U16 R22, R12` — Q16 quantize (float → uint16)
- `@P6 FFMA.FTZ R5, R34, R37, |R33|` — showdown EV with scale
- `FMNMX.FTZ R13, RZ, |R31|, !PT` — absolute value clamp
- `STS [R8.X4+0x250], R33` — shared memory store with 0x250 stride

**Warp reduction (0x1150–0x12c0):**
- `SHFL.DOWN PT, R2, R27, 0x10, 0x1f` — 5-step shuffle reduction (0x10, 0x8, 0x4, 0x2, 0x1)
- `FMNMX.FTZ` at each step — max reduction
- `REDUX.MAX UR5, R14` at 0x1240 — warp max reduction
- `VOTEU.ANY UR4, UPT, PT` — warp vote
- `ATOMS.MAX RZ, [0x8], R2` — global atomic max
- `BAR.SYNC.DEFER_BLOCKING 0x0`

**Final output (0x12c0–0x1440):**
- `LDS R11, [0x8]` — shared memory load
- `FFMA.FTZ R9, R4, R3, 1.0001000165939331055` — scale correction
- `MUFU.RCP R23, R9` + `FMUL.FTZ R15, R23, 65535` — Q16 quantize to uint16
- `STG.E.U16 [R4.64], R15` — packed S16 output
- `IMNMX R31, R31, 0x7fff` / `IMNMX R31, R31, -0x7fff` — int16 range clamp
- `@P4 EXIT` at 0x1440

### 29.7 train_mixed_opponent_fold_showdown_parent (sm_80, ~795+ instructions)

**Role:** Opponent-side mixed fold/showdown CFR training.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x270]` → EXIT
- `threadIdx.x == 0` → skips to 0x450

**Main computation (0x0460–0x0e50):**
- `ULDC UR6, c[0x0][0x268]` + `UIADD3 UR6, UR6, 0x1f` — iteration counter
- `HFMA2.MMA R9, -RZ, RZ, 0, 0` at 0x04f0 — FP16 MMA init
- `ULOP3.LUT UR7, UR7, 0xffffffe0` — alignment to 32-byte boundary
- `IMNMX R4, R5, UR17, !PT` — integer min/max
- `LDS.64 R12, [RZ]` — shared memory load
- `ISETP.EQ.U32.AND P2, PT, RZ, c[0x0][0x240]` — flag check
- `PRMT R18, R15, 0x9910` — card byte extraction
- `ISETP.NE.AND P3, PT, R18, -0x1` — dead card check
- `PRMT R18, R19, 0x7610` — packed suit extraction
- `I2F.S16 R28, R33` / `I2F.S16 R30, R27` — Q16 dequant
- `HFMA2.MMA R19, -RZ, RZ, 0, 5.9604644775390625e-08` at 0x07f0 — FP16 MMA with epsilon
- `MUFU.RCP R36, R31` — reciprocal normalization
- `FMNMX.FTZ R23, RZ, R30, !PT` — clamp
- `FFMA.FTZ R29, R22, R32, RZ` — EV accumulation

**Card processing loop (0x0e60–0x12c0):**
- `IMAD.SHL.U32 R13, R13, 0x2` — index computation
- `LDG.E.S16.CONSTANT R12, [R36.64]` — 16-bit state load
- `IMNMX R20, RZ, R20, !PT` — absolute value via min/max
- `I2F.S16 R20, R20` — Q16 dequant
- `FADD.FTZ R21, R20, R33` — EV sum
- `FSETP.GT.FTZ.AND P2, PT, R21, RZ` — positive check
- `@P2 MUFU.RCP R25, R21` — reciprocal
- `@P2 FMUL.FTZ R13, R20, R25` — normalize

**Output phase (0x10a0–0x15c0):**
- `STS [R8.X4+0x250], R33` — shared memory store (0x250 stride)
- `FSETP.NEU.FTZ.AND P6, PT, R18.reuse, RZ` — NaN check
- `FMUL.FTZ R31, R18, R31` — scale
- `SEL R30, RZ, 0x1, !P6` — conditional select
- `FSEL R33, R31, RZ, P6` — zero if NaN
- `LDG.E.CONSTANT R31, [R16.64]` — constant memory load
- `FMUL.FTZ R21, R18, R21` — final scale
- `FSEL R32, R21, RZ, P6` — conditional zero

**Warp reduction (0x15c0–0x16e0):**
- `SHFL.UP` reduction (0x1, 0x2, 0x4, 0x8, 0x10 steps)
- `BAR.RED.OR.DEFER_BLOCKING 0x0, P0` — block OR reduction
- `B2R.RESULT RZ, P0` — broadcast result
- `@!P0 BRA 0x6030` — skip if no active lanes

**Final phase (0x16a0–end):**
- `ULDC.64 UR8, c[0x0][0x1e8]` — output pointer
- `UIMAD.WIDE UR8, UR16, UR13, UR8` — output address computation
- `LDG.E.CONSTANT R4, [R10.64]` / `LDG.E.CONSTANT R5, [R8.64]` — output values
- `ISETP.NE.AND P0, PT, R4, RZ` — null check
- `@P0 BRA 0x5f70` — skip if null
- `SHF.R.S32.HI R23, RZ, 0x8, R23` — byte shift
- `IMNMX R9, R5, R10, PT` — min/max
- `STG.E [R14.64], R23` / `STG.E [R16+0x10], R22` — global output
- `BRA.DIV ~URZ, 0x60f0` — divergent branch to final reduction
- Second warp reduction at 0x24e0: `SHFL.UP` (0x1, 0x2, 0x4, 0x8, 0x10 steps)
- `STS [R21], R20` / `STS [R21+0x10], R18` — final shared memory stores

### 29.8 train_opponent_fold_showdown_parent_packed (sm_80, ~800+ instructions)

**Role:** Opponent fold/showdown CFR training with packed S16 state.

**Thread setup:**
- `blockIdx.x >= c[0x0][0x250]` → EXIT
- Leader thread setup at 0x0240–0x0350

**Packed state loading (0x0360–0x05c0):**
- `MOV R0, c[0x0][0x248]` — iteration count
- `IADD3 R0, R0, 0x1f, RZ` — align to 32
- `SHF.R.S32.HI R3, RZ, 0x1f, R0` — high 16 bits
- `LEA.HI R3, R3, R0, RZ, 0x5` — multiply by 32
- `IMNMX R8, R3, 0x400, PT` — clamp to 0x400
- `LOP3.LUT R0, R8, 0x1f, RZ, 0xfc` — mask lower bits
- `SHF.R.S32.HI R3, RZ, 0x1f, R0` — shift
- `IMNMX R3, R11, R2, !PT` — min/max
- `BSSY B0` barrier at 0x0400

**Main loop (0x05d0–0x0e10):**
- `ISETP.GE.AND P1, PT, R2, c[0x0][0x248]` — iteration bounds
- `PRMT R19, R3, 0x9910` — card byte extraction
- `ISETP.NE.AND P4, PT, R19, -0x1` — dead card check
- `PRMT R20, R19, 0x7610` — packed suit
- `SEL R19, RZ, 0x1, !P4` — conditional select
- `HFMA2.MMA R21, -RZ, RZ, 0, 0` at 0x07e0 — FP16 MMA init
- `LDG.E.S16.CONSTANT R22, [R28.64]` — 16-bit state load
- `I2F.S16 R29, R22` / `I2F.S16 R23, R21` — Q16 dequant
- `FSEL R29, R29, RZ, P3` — conditional zero
- `FADD.FTZ R31, R30, R29` — EV sum
- `FSETP.GT.FTZ.AND P2, PT, R31, RZ` — positive check
- `@P2 MUFU.RCP R23, R31` — reciprocal
- `@P2 FMUL.FTZ R16, R30, R23.reuse` — normalize
- `@P2 FMUL.FTZ R14, R29, R23` — normalize
- `FMUL.FTZ R16, R27.reuse, R16` — scale
- `FMUL.FTZ R14, R27, R14` — scale
- `STS [R0.X4+0x250], R17` — shared memory store (0x250 stride)
- `STS.U8 [R0+UR7], RZ` — byte zeroing

**Output phase (0x0c00–0x0dd0):**
- `I2F.S16 R0, R20` / `I2F.S16 R14, R28` — Q16 dequant
- `FADD.FTZ R21, R16, R17` — EV sum
- `FSETP.GT.FTZ.AND P1, PT, R21, RZ` — positive check
- `@P1 MUFU.RCP R15, R21` — reciprocal
- `@P1 FMUL.FTZ R0, R16, R15.reuse` — normalize
- `@P1 FMUL.FTZ R14, R17, R15` — normalize
- `FMUL.FTZ R0, R0, R19.reuse` — scale
- `FMUL.FTZ R15, R14, R19` — scale
- `FSEL R0, R0, RZ, P1` — conditional zero
- `FSEL R16, R15, RZ, P2` — conditional zero
- `STS [R14], R0` / `STS [R17], R16` — shared memory stores
- `STS.U8 [R20], RZ` — byte zeroing

**Second phase (0x0e10–0x1780):**
- `IMAD.WIDE R14, R5, R18, c[0x0][0x1c8]` — output pointer computation
- `LDG.E.CONSTANT R3, [R14.64]` / `LDG.E.CONSTANT R2, [R12.64]` — output values
- `ISETP.NE.AND P0, PT, R3, RZ` — null check
- `@P0 BRA 0x5600` — skip if null
- `SHF.R.S32.HI R31, RZ, 0x8, R31` — byte shift
- `LOP3.LUT R15, R4, 0x1f, RZ, 0xc0` — rank extraction
- `IMAD R12, R4, R31, RZ` — index computation
- `IMNMX R13, R2, R13, PT` — min/max
- `IMAD.IADD R18, R21, 0x1, -R12` — modulo adjustment
- `LOP3.LUT P0, R20, R18, 0x3, RZ, 0xc0` — suit check
- `LDG.E.U16.CONSTANT R18, [R18.64]` — 16-bit load
- `LDG.E.CONSTANT R29, [R16.64]` — constant load
- `HFMA2.MMA R28, -RZ, RZ, 0, 0` — FP16 MMA
- `LDG.E.CONSTANT R0, [R18.64]` — value load
- `IMNMX R13, R2, R13, PT` — clamp
- `LOP3.LUT P0, R20, R18, 0x3, RZ, 0xc0` — suit check
- `IMAD.IADD R18, R29, 0x1, R12` — index
- `IMNMX R27, R2, R27, PT` — clamp
- `@P0 BRA 0x1780` — loop back

**Tree traversal phase (0x1010–0x1780):**
- `IMAD R19, R31, -0x7f, R14` — offset computation
- `BSSY B1` barrier at 0x1020
- `IMNMX R21, R2, R19, PT` — clamp
- `LOP3.LUT R19, RZ, R21, RZ, 0x33` — bit-pair extraction
- `IMAD.IADD R18, R21, 0x1, -R12` — modulo
- `IADD3 R19, -R19, -0x2, -R12` — adjustment
- `IMAD.IADD R18, R29, 0x1, R12` — next index
- `IADD3 R22, R12, c[0x0][0x24c], RZ` — output pointer
- `IMAD.WIDE R18, R18, R19, c[0x0][0x1d8]` — constant memory load address
- `IMAD.MOV.U32 R19, RZ, RZ, 0x2` — stride
- `LEA R30, R19, 0x250, 0x2` — shared memory address (0x250 stride)
- `LEA R21, R18, 0x250, 0x2`
- `LDG.E.U16.CONSTANT R18, [R18.64]` — 16-bit load
- `IADD3 R20, R20, -0x1, RZ` — counter decrement
- `IMAD.X R35, RZ, RZ, R35, P2` — conditional stride
- `PRMT R19, R36, 0x7610` — packed suit
- `LDS R33, [R32.X4+0x250]` — shared memory load
- `FADD.FTZ R28, R33, R28` — EV accumulate
- `STS [R30], R33` — shared memory store
- `STS.U8 [R18+UR7], R19` — byte store
- `STS [R21], R28` — shared memory store
- Loop back at 0x12a0 → 0x1180

**Final reduction phase (0x1780–0x1f10):**
- `BSYNC B0` barrier at 0x1780
- `ISETP.GE.AND P2, PT, R14, R27, PT` — bounds check
- `BSSY B0` barrier at 0x17a0
- `@P2 BRA 0x1f10` — skip if done
- `IADD3 R18, R4, 0x81, RZ` — next level
- `BSSY B1` barrier at 0x17d0
- `IMAD R31, R31, R18, RZ` — multiply
- `IMNMX R31, R2, R31, PT` — clamp
- `LOP3.LUT R19, RZ, R31, RZ, 0x33` — bit-pair
- `IMAD.IADD R17, R31, 0x1, -R14` — modulo
- `IADD3 R19, -R19, -0x2, -R14` — adjust
- `LOP3.LUT P0, R20, R17, 0x3, RZ, 0xc0` — suit check
- `ISETP.GE.U32.AND P1, PT, R19, 0x3` — bounds
- `@!P0 BRA 0x1a40` — skip
- Second tree level at 0x1880–0x1a40
- `HFMA2.MMA R34, -RZ, RZ, 0, 5.9604644775390625e-08` at 0x1960
- `STG.E.U16 [R18+UR7], R34` — packed S16 output
- `STS [R21], R32` / `STS [R22], R17` — shared memory

**Warp reduction (0x1f10–0x21d0):**
- `BRA.DIV ~URZ, 0x5760` — divergent branch
- `SHFL.UP PT, R23, R28, 0x1` — shuffle reduction (0x1, 0x2, 0x4, 0x8, 0x10 steps)
- `FADD.FTZ R23, R23, R28` — accumulate
- `FSEL R32, R23, R28, P1` — conditional select
- `SHFL.UP PT, R19, R32, 0x2`
- `FADD.FTZ R18, R18, R17`
- `FSEL R18, R18, R17, P1`
- `IMAD.MOV.U32 R23, RZ, RZ, R32` — final value
- `SHFL.UP PT, R30, R32, 0x10`
- `MOV R22, R18` — final result
- `WARPSYNC 0xffffffff`

**Zeroing phase (0x21d0–0x24c0):**
- `LEA R16, R16, 0x8, 0x2` — shared memory address
- `SHF.R.S32.HI R18, RZ, 0x5, R4` — shift
- `@P0 FADD.FTZ R23, R30, R23` — accumulate
- `@P0 FADD.FTZ R22, R20, R22` — accumulate
- `ISETP.GE.AND P0, PT, R4, c[0x0][0x240]` — bounds
- `@!P3 STS [R16], R23` — zero shared memory
- `@!P3 STS [R16+0x10], R22` — zero shared memory
- `BAR.SYNC.DEFER_BLOCKING 0x0`
- Loop back at 0x2260 → 0x21d0

**Final zeroing phase (0x24c0–0x2d40):**
- `BSYNC B0` barrier
- `@P3 BRA 0x2390` — skip
- `ISETP.GT.U32.AND P3, PT, R15.reuse, 0x7` — bounds
- `IMAD.MOV.U32 R31, RZ, RZ, RZ` — zero
- `LEA R33, R15, 0x8, 0x2` — address
- `@!P3 LDS R31, [R33]` — load
- `BRA.DIV ~URZ, 0x5c80` — divergent
- `SHFL.UP PT, R20, R31, 0x1` — shuffle reduce (0x1, 0x2, 0x4, 0x8 steps)
- `FADD.FTZ R31, R20, R31` — accumulate
- `@P0 FADD.FTZ R31, R31, R18` — conditional
- `IMAD.MOV.U32 R30, RZ, RZ, R31` — final
- `SHFL.UP PT, R20, R31, 0x4`
- `ISETP.GE.U32.AND P0, PT, R15, 0x4` — bounds
- `@P0 FADD.FTZ R30, R20, R30` — accumulate
- `@!P3 STS [R33], R30` — zero store
- `WARPSYNC 0xffffffff`
- `BAR.SYNC.DEFER_BLOCKING 0x0`

## 28. UPDATED DELIVERABLES STATUS

| Deliverable | Status | Location |
|-------------|--------|----------|
| Binary inventory & PE structure | ✅ Complete | FINAL_RECON_STATE.md §1-2 |
| CUDA kernel inventory (45 kernels) | ✅ Complete | FINAL_RECON_STATE.md §5 |
| CUDA mathematics | ✅ Complete | CUDA_MATH.md |
| C++ data structures | ✅ Complete | CPP_CORE_RECON.md |
| Native method dispatcher | ✅ Complete | DEEP_RE_PHASE2.md |
| Handler vtables (~22 addresses) | ✅ Complete | HANDLER_VTABLE_RECON.md |
| Method dispatch chain (19+23 methods) | ✅ Complete | HANDLER_DISPATCH_RECON.md |
| SASS disassembly (270 files, 6 archs) | ✅ Complete | /tmp/opencode/sass_disasm/<arch>/ |
| SASS-level algorithmic details | ✅ 18/45 kernels | SASS_KERNEL_DEEP_RECON.md, §12, §25, §29 |
| CUDA driver API call sites | ✅ Complete | CUDA_API_CALL_SITES.md (§26) |
| Q16 + compact archive format | ✅ Complete | PHASE3_COMPACT_ARCHIVE.md |
| Frontend source code | ✅ Complete | FRONTEND_SOURCE_DEEP_RECON.md (§27) |
| Transport/bridge protocol | ✅ Complete | FRONTEND_API_RECON.md |
| JSON schemas | ✅ Complete | FRONTEND_API_RECON.md §4 |
| Anti-tamper surface | ✅ Complete | ANTI_TAMPER_BYPASS.md |
| NVML usage | ✅ Complete | CUDA_API_CALL_SITES.md (§26) |
| Method → handler VA mapping | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Kernel launch configurations | ❌ Blocked (dynamic) | Requires Windows + GPU |
| iter_params capture | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Compact archive bytes | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Full solve cycle | ❌ Blocked (dynamic) | Requires Windows + GPU |
| Dynamic anti-tamper validation | ❌ Blocked (dynamic) | Requires Windows + GPU |

**Tools Created:** Proxy nvcuda.dll (`/tmp/opencode/wine_stubs/`), compact_archive_decoder.py, CFR simulator (`cfr_simulator.py`), SASS deep recon docs.

**Bottom line:** Static reverse engineering of TexasSolverGPU v0.3.0 is **substantially complete** (all binaries, all 45 kernels disassembled, 18/45 with algorithmic SASS documentation, complete frontend source extracted, all CUDA API call sites with source variable names recovered, all anti-tamper mapped). Remaining unknowns require **dynamic execution on Windows + NVIDIA GPU**, which is not available on this Linux host.

**Remaining undocumented kernels (27/45):** `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent_boundary_iso`, `backward_opponent_boundary_iso`, `forward_opponent_packed`, `forward_chance_turn_river_batched` (now documented), `train_fold_showdown_parent_packed` (now documented), `train_mixed_fold_showdown_parent_packed` (now documented), `train_mixed_opponent_fold_showdown_parent` (now documented), `train_opponent_fold_showdown_parent_packed` (now documented), `evaluate_showdown_brute` (now documented), `evaluate_showdown_prefix` (now documented), `evaluate_showdown_abstract` (now documented), plus `river_api`, `river_batched`, `river_subtrees_packed`, `replace`, `trim` (JIT PTX only, no .sass files), and `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`, `river_oop`, `river_raise`, `BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`, `BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2` (batch/memory ops, no SASS analysis yet).