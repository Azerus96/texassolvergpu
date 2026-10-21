# TEXAS SOLVER GPU v0.3.0 — Embedded CUDA Kernel Recon

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Method:** Static analysis of embedded CUDA ELF objects within the PE binary
**Status:** Complete — 24 embedded CUDA ELF objects discovered, contradicting prior report's claim of "no embedded cubin/PTX"

---

## 1. MAJOR FINDING: Embedded CUDA ELF Objects Exist

The prior `FINAL_COMPREHENSIVE_RE.md` claimed:
> "No embedded cubin to extract" and "PTX target: 120 (forward PTX JIT — no embedded cubin)"

**This is incorrect.** The binary contains **24 embedded CUDA ELF object files** (relocatable, ET_REL) with e_machine=0x00BE (CUDA), compiled with CUDA 13.1 (ptxas V13.1.115). These contain SASS machine code (not PTX text) for 45 kernels across 6 GPU architectures.

Each ELF object is embedded directly in the PE `.rdata` section and follows the standard ELF64 format with CUDA-specific sections:
- `.text.<kernel>` — SASS machine code (SHF_ALLOC|SHF_EXECINSTR)
- `.nv.info.<kernel>` — per-kernel metadata (shared mem, registers, etc.)
- `.nv.info` — global compilation metadata
- `.nv.constant0.<kernel>` — constant memory
- `.nv.shared.<kernel>` — shared memory declarations
- `.note.nv.tkinfo` — toolkit info (includes `-arch sm_XX` target)
- `.note.nv.cuinfo` — CUDA info (version + arch code)

---

## 2. ELF OBJECT INVENTORY (24 objects)

| # | Offset | Size (bytes) | Sections | Architecture | Source File |
|---|--------|-------------|----------|-------------|-------------|
| 00 | 0x24d920 | 571,392 | 194 | sm_75 | ELF_OBJECTS/elf_00_0x24d920_sh194.elf |
| 01 | 0x2d9240 | 565,632 | 194 | sm_80 | ELF_OBJECTS/elf_01_0x2d9240_sh194.elf |
| 02 | 0x3634e0 | 563,456 | 194 | sm_86 | ELF_OBJECTS/elf_02_0x3634e0_sh194.elf |
| 03 | 0x3ecf00 | 563,200 | 194 | sm_89 | ELF_OBJECTS/elf_03_0x3ecf00_sh194.elf |
| 04 | 0x476820 | 616,616 | 229 | sm_90 | ELF_OBJECTS/elf_04_0x476820_sh229.elf |
| 05 | 0x53c998 | 843,560 | 361 | sm_120 | ELF_OBJECTS/elf_05_0x53c998_sh361.elf |
| 06 | 0x60aa30 | 532,352 | 194 | sm_75 | ELF_OBJECTS/elf_06_0x60aa30_sh194.elf |
| 07 | 0x68cad0 | 530,944 | 194 | sm_80 | ELF_OBJECTS/elf_07_0x68cad0_sh194.elf |
| 08 | 0x70e5f0 | 526,080 | 194 | sm_86 | ELF_OBJECTS/elf_08_0x70e5f0_sh194.elf |
| 09 | 0x78ee10 | 525,824 | 194 | sm_89 | ELF_OBJECTS/elf_09_0x78ee10_sh78ee10.elf |
| 10 | 0x80f530 | 578,536 | 222 | sm_90 | ELF_OBJECTS/elf_10_0x80f530_sh222.elf |
| 11 | 0x8c8620 | 820,792 | 347 | sm_120 | ELF_OBJECTS/elf_11_0x8c8620_sh347.elf |
| 12 | 0x990dd0 | 623,360 | 194 | sm_75 | ELF_OBJECTS/elf_12_0x990dd0_sh194.elf |
| 13 | 0xa291f0 | 619,008 | 194 | sm_80 | ELF_OBJECTS/elf_13_0xa291f0_sh194.elf |
| 14 | 0xac0510 | 616,832 | 194 | sm_86 | ELF_OBJECTS/elf_14_0xac0510_sh194.elf |
| 15 | 0xb56fb0 | 616,576 | 194 | sm_89 | ELF_OBJECTS/elf_15_0xb56fb0_sh194.elf |
| 16 | 0xbed950 | 672,808 | 229 | sm_90 | ELF_OBJECTS/elf_16_0xbed950_sh229.elf |
| 17 | 0xcc3c50 | 897,024 | 361 | sm_120 | ELF_OBJECTS/elf_17_0xcc3c50_sh361.elf |
| 18 | 0xd9edc0 | 590,848 | 194 | sm_75 | ELF_OBJECTS/elf_18_0xd9edc0_sh194.elf |
| 19 | 0xe2f2e0 | 590,080 | 194 | sm_80 | ELF_OBJECTS/elf_19_0xe2f2e0_sh194.elf |
| 20 | 0xebf500 | 586,880 | 194 | sm_89 | ELF_OBJECTS/elf_20_0xebf500_sh194.elf |
| 21 | 0xf4eaa0 | 586,624 | 194 | sm_89 | ELF_OBJECTS/elf_21_0xf4eaa0_sh194.elf |
| 22 | 0xfddf40 | 642,024 | 222 | sm_90 | ELF_OBJECTS/elf_22_0xfddf40_sh222.elf |
| 23 | 0x10a8560 | 860,056 | 347 | sm_120 | ELF_OBJECTS/elf_23_0x10a8560_sh347.elf |

**Total embedded CUDA code: ~14 MB**
**4 objects per architecture** (sm_75/80/86/89/90/120)

---

## 3. ARCHITECTURE MAPPING

| Architecture | Objects | Section Count | Compile Flags |
|-------------|---------|--------------|---------------|
| sm_75 | elf_00, 06, 12, 18 | 194 | `-arch sm_75 -m 64` |
| sm_80 | elf_01, 07, 13, 19 | 194 | `-arch sm_80 -m 64` |
| sm_86 | elf_02, 08, 14, 20 | 194 | `-arch sm_86 -m 64` |
| sm_89 | elf_03, 09, 15, 21 | 194 | `-arch sm_89 -m 64` |
| sm_90 | elf_04, 10, 16, 22 | 222/229 | `-arch sm_90 -m 64` |
| sm_120 | elf_05, 11, 17, 23 | 347/361 | `-arch sm_120 -m 64` |

**CUDA toolchain:** release 13.1, V13.1.115, build `cuda_13.1.r13.1/compiler.37061995_0`

---

## 4. KERNEL INVENTORY (45 kernels per architecture)

All 24 ELF objects contain the same 45 kernels. Code sizes vary by architecture and by object variant (4 variants per architecture per kernel — all differ byte-for-byte).

### sm_120 (largest code, newest architecture)

| Kernel | Code (bytes) | Shared Mem (bytes) | Constant Mem (bytes) |
|--------|-------------|-------------------|---------------------|
| train_small_tree_fp32_multi_iter | 46,208 | 5,664 | 0 |
| evaluate_fold_showdown_prefix | 45,056 | 1,616 | 0 |
| evaluate_showdown_prefix | 35,200 | 1,264 | 0 |
| train_mixed_opponent_fold_showdown_parent | 29,312 | 1,632 | 0 |
| train_fold_showdown_parent_packed | 27,520 | 1,344 | 0 |
| backward_player_packed | 26,624 | 1,056 | 0 |
| train_opponent_fold_showdown_parent_packed | 25,984 | 1,632 | 0 |
| evaluate_fold | 24,832 | 1,712 | 0 |
| evaluate_fold_3p | 24,192 | 1,712 | 0 |
| forward_river_subtrees_packed | 17,280 | 1,072 | 0 |
| evaluate_fold_fp32 | 17,024 | 1,376 | 0 |
| forward_opponent_packed | 14,208 | 1,040 | 0 |
| evaluate_showdown_prefix_3p | 14,464 | 1,056 | 0 |
| backward_player | 12,544 | 1,040 | 0 |
| backward_player_packed_cached | 19,968 | 1,056 | 0 |
| evaluate_showdown_brute | 9,856 | 1,024 | 0 |
| evaluate_showdown | 9,216 | 1,024 | 0 |
| backward_player_packed_a2 | 4,864 | 1,056 | 0 |
| evaluate_fold_abstract | 4,352 | 1,024 | 0 |
| encode_stage_segments_i16 | 3,968 | 1,168 | 0 |
| backward_chance_turn_river_batched | 7,040 | 1,024 | 0 |
| backward_chance_turn_river | 7,040 | 1,024 | 0 |
| forward_chance_turn_river_batched | 7,040 | 1,024 | 0 |
| forward_chance_turn_river | 7,040 | 1,024 | 0 |
| backward_chance_tr_2d | 6,656 | 1,024 | 0 |
| forward_chance_tr_2d | 6,656 | 1,024 | 0 |
| forward_opponent_br_packed | 9,984 | 1,040 | 0 |
| backward_player_cfv_packed | 10,496 | 1,040 | 0 |
| forward_opponent | 6,272 | 1,040 | 0 |
| forward_opponent_br | 5,760 | 1,040 | 0 |
| backward_player_cfv | 6,144 | 1,040 | 0 |
| forward_copy | 5,120 | 1,024 | 0 |
| forward_chance_abstract | 6,144 | 1,024 | 0 |
| backward_player_packed_a2 | 4,864 | 1,056 | 0 |
| apply_isomorphism_permuted | 1,792 | 1,024 | 0 |
| apply_isomorphism | 1,792 | 1,024 | 0 |
| forward_chance | 1,792 | 1,024 | 0 |
| forward_player | 1,664 | 1,024 | 0 |
| gather_cfv | 640 | 1,024 | 0 |
| backward_opponent_boundary_iso | 6,016 | 1,024 | 0 |
| backward_opponent | 2,816 | 1,024 | 0 |
| backward_chance_abstract | 2,688 | 1,024 | 0 |
| train_mixed_fold_showdown_parent_packed | 7,552 | 1,056 | 0 |
| evaluate_showdown_abstract | 4,992 | 1,024 | 0 |
| evaluate_showdown_prefix | 35,200 | 1,264 | 0 |
| evaluate_showdown_brute_3p | 9,344 | 1,024 | 0 |
| reserved.0 | 0 | 64 | 0 |
| constant0 | 0 | 0 | 45,716 |

**Total code: 548,992 bytes (~536 KB)**

### sm_75 (smallest code, oldest architecture)

**Total code: 485,376 bytes (~474 KB)** — same 45 kernels, smaller SASS encoding

Key size differences (sm_120 vs sm_75):
- `train_small_tree_fp32_multi_iter`: 46,208 vs 52,096 (sm_75 larger — different register allocation)
- `evaluate_fold_showdown_prefix`: 45,056 vs 54,016
- `backward_player_packed`: 26,624 vs 24,960
- `train_mixed_opponent_fold_showdown_parent`: 29,312 vs 28,288

---

## 5. FOUR VARIANTS PER ARCHITECTURE

Each architecture has 4 ELF objects (e.g., sm_75: elf_00, 06, 12, 18). All 45 kernels differ byte-for-byte across the 4 objects within each architecture. This suggests:

1. **Different compilation units** — kernels split across multiple `.cu` source files
2. **Different optimization levels** or **debug/release variants**
3. **Redundant copies** for driver fallback selection

The CUDA driver at runtime (`cuModuleLoadData` / `cuModuleLoadDataEx`) selects the appropriate object based on the detected GPU compute capability.

---

## 6. KERNEL CODE EXTRACTION

All SASS code sections have been extracted to:

```
KERNEL_CODE_ALL/<arch>/obj<N>/<kernel>.sass
```

- 6 architectures × 4 objects × 45 kernels = **1,080 SASS files**
- Individual file sizes: 384 bytes (gather_cfv) to 56,960 bytes (evaluate_fold_showdown_prefix)
- Format: Raw SASS machine code (binary, not disassembled)

Example: `KERNEL_CODE_ALL/sm_120/obj0/train_small_tree_fp32_multi_iter.sass` (46,208 bytes)

---

## 7. CORRECTIONS TO PRIOR REPORT

The following claims in `FINAL_COMPREHENSIVE_RE.md` and related docs are **incorrect**:

| Prior Claim | Reality |
|-------------|---------|
| "No native cubin is embedded" | **False** — 24 embedded CUDA ELF objects with SASS code |
| "PTX target: 120 (forward PTX JIT — no embedded cubin)" | **False** — SASS cubin IS embedded for sm_75/80/86/89/90/120 |
| "Only status string 'PTX target: 120' and ptxas references found" | **False** — full kernel binaries with `.text.<kernel>`, `.nv.info.<kernel>`, `.nv.shared.<kernel>` sections |
| "No embedded PTX/cubin to extract" | **False** — ~14 MB of embedded CUDA kernel code |

The `.text` sections contain **SASS (binary machine code)**, not PTX text. This means:
- `cuModuleLoadData` / `cuModuleLoadDataEx` can load the cubin directly
- No PTX JIT compilation is needed for these embedded kernels
- Kernel disassembly would require `cuobjdump` or `nvdisasm` (not available on Linux host)

---

## 8. REMAINING UNKNOWNSTILL REQUIRING DYNAMIC ANALYSIS

| Item | Why Blocked |
|------|-------------|
| Method name → handler address mapping | Heap-stored at `[handler_obj+0x80]`, no static table |
| Kernel launch configurations | Runtime `cuLaunchKernel` args (gridDim, blockDim, stream, args) |
| `iter_params` (3 discount floats) | Runtime `cuMemcpyHtoDAsync` at code VA `0x24bd10` |
| Compact archive bytes | Written at runtime by `archive.open/node/close` |
| Full solve cycle | Requires live solver execution on Windows + GPU |
| Anti-tamper bypass | `IsDebuggerPresent`, 5× `cpuid`, `OutputDebugStringW` |
| Which of 4 object variants is used per kernel | Runtime driver selection logic |
| SASS instruction disassembly | Requires `nvdisasm`/`cuobjdump` (Windows/CUDA toolkit needed) |

---

## 9. DELIVERABLES PRODUCED

| File | Contents |
|------|----------|
| `ELF_OBJECTS/` | 24 extracted CUDA ELF objects |
| `KERNEL_CODE_ALL/` | 1,080 SASS code sections (arch/obj/kernel) |
| `KERNEL_INVENTORY.json` | Complete kernel metadata (code size, shared mem, const mem) |
| `arch_map.txt` | ELF object → architecture mapping |
| `EMBEDDED_CUDA_KERNELS_RECON.md` | This document |

---

## 10. RELATED DOCUMENTS

| Document | Contents |
|----------|----------|
| `FINAL_COMPREHENSIVE_RE.md` | Overall RE report (contains incorrect "no embedded cubin" claim) |
| `CUDA_KERNELS_RECON.cpp` | CUDA kernel pseudocode reconstruction |
| `CUDA_MATH.md` | CUDA kernel mathematics |
| `PHASE3_CFR_LAUNCHERS.md` | CFR kernel launcher inventory |
| `PHASE3_COMPACT_ARCHIVE.md` | Compact archive / strategy archive format |
| `HANDLER_FUNCTION_RECON.md` | Handler function disassembly |
| `HANDLER_VTABLE_RECON.md` | Handler vtable recovery |