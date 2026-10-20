# PHASE3_CFR_LAUNCHERS.md
# TexasSolverGPU v0.3.0 — Phase 3: CFR Kernel Launcher Inventory & Buffer Lifetimes

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64/TexasSolverGpu_131.exe`
**Status:** Static symbol-name inventory only. Grid/block configs, shared-memory sizes, and exact buffer lifetimes require dynamic capture (PTX JIT via `cuModuleLoadData`, `cuLaunchKernel` args).

Source: DEEP_RE_PHASE2.md §5, BINARY_ANALYSIS_RECON.md §5-§6, FRONTEND_API_RECON.md §6, CUDA_MATH.md, all_strings.txt.

---

## 1. CUDA runtime configuration (from strings/imports)

- PTX target: **sm_120** (forward PTX JIT; no embedded cubin). `cuobjdump -sass` → "does not contain device code".
- Compiled CUDA targets: `75;80;86;89;90;120`.
- CUDA 13.1 (`Build cuda_13.1.r13.1/compiler.37061995_0`, `ptxas V13.1.115`) + legacy 12.6 (`TexasSolverGpu_legacy_126.exe`).
- Block size: **256 threads**.
- Streams: `stream_`, `stream2_`, `stream_dl_`, `stream_file_`.
- Events: `event_cfv_stage_free_`, `event_cfv_stage_ready_`, `event_fwd_done_`, `event_show_done_`, `event_`.
- `cuGraphLaunch(training_graph_exec_, stream_)` — CUDA Graph for the training loop.
- VRAM gate: "VRAM is below 8 GB; large trees may fail."
- `cuOccupancyAvailableDynamicSMemPerBlock`, `cuOccupancyMaxActiveBlocksPerMultiprocessor` used for tuning.

---

## 2. Complete kernel / launcher symbol inventory

### 2.1 Showdown / fold evaluation (10)
`evaluate_showdown`, `evaluate_showdown_brute`, `evaluate_showdown_prefix`, `evaluate_showdown_abstract`, `evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold`, `evaluate_fold_fp32`, `evaluate_fold_3p`, `evaluate_fold_showdown_prefix`, `evaluate_fold_abstract`.

### 2.2 CFR backward pass (~13)
`backward_player`, `backward_player_cfv`, `backward_player_cfv_packed`, `backward_player_packed`, `backward_player_packed_a2`, `backward_player_packed_cached`, `backward_opponent`, `backward_opponent_boundary_iso`, `backward_br_player`, `backward_chance_abstract`, `backward_chance_tr_2d`, `backward_chance_turn_river`, `backward_chance_turn_river_batched`.

### 2.3 CFR forward pass (~12)
`forward_player`, `forward_opponent`, `forward_opponent_packed`, `forward_opponent_br`, `forward_opponent_br_packed`, `forward_chance`, `forward_chance_abstract`, `forward_chance_tr_2d`, `forward_chance_turn_river`, `forward_chance_turn_river_batched`, `forward_copy`, `forward_river_subtrees_packed`.

### 2.4 Training / river (~14)
`train_fold_showdown_parent_packed`, `train_mixed_fold_showdown_parent_packed`, `train_mixed_opponent_fold_showdown_parent`, `train_opponent_fold_showdown_parent_packed`, `train_small_tree_fp32_multi_iter`, `river_api`, `river_batched`, `river_subtrees_packed`, `river_betting_enabled`, `river_bet`, `river_buckets`, `river_card`, `river_donk`, `river_ip`, `river_oop`, `river_raise`.

### 2.5 Tree compaction / isomorphism / misc (~8)
`apply_isomorphism`, `apply_isomorphism_permuted`, `encode_stage_segments_i16`, `gather_cfv`, `replace`, `trim`.

> Supersedes Phase-1 table (which listed only the 10 `evaluate_*` kernels). Full CFR pipeline now enumerated: backward/forward/train/river/isomorphism.

---

## 3. Device buffer inventory (device pointers)

| Buffer | Role |
|--------|------|
| `d_cfv_` | counterfactual values |
| `d_abs_regrets_` | absolute regrets |
| `d_abs_strategy_` | absolute strategy |
| `d_cfreach_` / `d_cfreach0_` / `d_cfreach1_` | counterfactual reach (per player) |
| `d_init_weights_` | initial weights |
| `d_discount_params_` | 3 floats (`iter_params`), copied H→D async |
| `d_strategy_packed_` | packed strategy output |
| `d_compact` | compact archive data |
| `d_gather_staging_`, `d_gather_scales_`, `d_gather_i16_` | gather staging |

---

## 4. Host-pinned / CFV staging

| Item | Detail |
|------|--------|
| `cfv_stage_host_` | `CU_MEMHOSTALLOC_PORTABLE \| CU_MEMHOSTALLOC_DEVICEMAP` |
| `pinned_discount_params_` | pinned `iter_params` |
| `cfv_stage_scales_` | CFV stage scales |
| `pinned_gather_staging_`, `gather_staging_capacity_` | `cuMemHostAlloc … CU_MEMHOSTALLOC_PORTABLE` |
| `cfv_stage_device_`, `cfv_stage_host_` | CFV stage device/host pointers |
| `event_cfv_stage_free_`, `event_cfv_stage_ready_` | `cuEventCreate(…, CU_EVENT_DISABLE_TIMING)` |

---

## 5. CFV staging + training graph lifecycle (from timing-log strings)

- `[gpu timing] submit=%.3f training_sync=%.3f pin_wait=%.3f exploit=%.3f finalize=%.3f total=%.3f ms`
- `[gpu finalize] p0_gpu=%.3f p0_scatter=%.3f p1_gpu=%.3f p1_scatter=%.3f before_unpack=%.3f unpack=%.3f total=%.3f ms`
- `[gpu] persistent backward eligibility: roots=%zu/%zu nodes=%zu`
- `[gpu] river forward a2: player=%zu roots=%zu/%zu nodes=%zu/%zu opponent_nodes=%zu/%zu max_nodes=%zu`
- `[gpu finalize] staged CFV enabled: max_plan=%llu total_p0=%llu total_p1=%llu chunks_p0=%u chunks_p1=%u device_capacity=%llu host_capacity=%llu direct_host=%d`

Inferred lifecycle:
1. `cuMemcpyHtoDAsync(d_discount_params_, iter_params, 3*sizeof(float), stream_)` — discount params.
2. Backward pass: `backward_player_cfv*` / `backward_opponent*` / `backward_chance_*` kernels write `d_cfv_`, `d_abs_regrets_`; `persistent backward eligibility` tracks roots/nodes.
3. CFV staged to host via `cfv_stage_host_` / `cfv_stage_device_` with `event_cfv_stage_ready_` / `event_cfv_stage_free_`.
4. Forward pass: `forward_player*` / `forward_opponent*` / `forward_chance_*`; `river forward a2` for river subtrees.
5. Training: `train_*` kernels; launched via `cuGraphLaunch(training_graph_exec_, stream_)`.
6. Finalize: pack Q16 strategy/EVs into `d_strategy_packed_` / `d_compact`; `unpack` stage; write compact Avro archive.

---

## 6. Batch / memory-op CUDA graph nodes (new, Phase 3)

`BatchAsync`, `BatchAsync_v2`, `BatchDecompressAsync`, `BatchMemOp`, `BatchMemOpNode`, `BatchMemOpNodeGetParams`, `BatchMemOpNodeSetParams`, `BatchMemOp_v2`, `BATCH_NODES`, `batch_manifests`, `batch_results`, `batch_rows`, `batch_solver`, `batching_enabled`, `river_batching_enabled`.

Inferred: the solver batches multiple boards/nodes into a single CUDA graph (`BatchMemOpNode` get/set params) with async + decompress variants (`BatchAsync_v2`, `BatchDecompressAsync`). `batch_manifests` describes the batch; `batch_results` collects per-node outputs. `river_batching_enabled` gates river forward batching.

---

## 7. Q16 fixed-point fields (device ↔ host)

`reach_probs_q16`, `strategy_probs_q16`, `action_evs_q16`. Serialized into the compact Avro archive (see PHASE3_COMPACT_ARCHIVE.md). Dequantize: `value = q16_raw / 65536.0`.

---

## 8. Buffer lifetime summary (reconstructed)

| Phase | Buffers touched | Kernels |
|-------|----------------|---------|
| Allocate | `d_cfv_`, `d_abs_regrets_`, `d_abs_strategy_`, `d_cfreach*`, `d_init_weights_`, `d_strategy_packed_`, `d_compact`, staging | `solver.allocate` |
| Discount params | `d_discount_params_` (pinned H→D) | `cuMemcpyHtoDAsync` |
| Backward | `d_cfv_`, `d_abs_regrets_`, `d_cfreach*` | `backward_player_cfv*`, `backward_opponent*`, `backward_chance_*` |
| CFV stage | `cfv_stage_*`, `event_cfv_stage_*` | gather/scatter to host |
| Forward | `d_cfv_`, `d_abs_strategy_` | `forward_player*`, `forward_opponent*`, `forward_chance_*`, `river_*` |
| Train | `d_abs_regrets_`, `d_abs_strategy_` | `train_*`, `cuGraphLaunch(training_graph_exec_)` |
| Finalize/pack | `d_strategy_packed_`, `d_compact` | pack Q16 → compact archive |

---

## 9. Recommended dynamic capture

1. Instrument `cuLaunchKernel` / `cuLaunchKernelEx` to log `(kernelName, gridDim, blockDim, sharedMem, stream, args)` for every launch; capture the PTX blobs passed to `cuModuleLoadData`/`cuModuleLoadDataEx`.
2. Hook `cuMemcpyHtoDAsync(d_discount_params_, …)` to capture `iter_params` (3 floats).
3. Correlate kernel launches with `solver.solve.start` / status polling to assign each kernel to a CFR phase (backward/forward/train/river/finalize).
4. Measure grid/block for `backward_player_cfv_packed`, `forward_player`, `train_small_tree_fp32_multi_iter`, `river_batched`, `apply_isomorphism`, `gather_cfv`; derive shared memory from `cuOccupancyAvailableDynamicSMemPerBlock`.
