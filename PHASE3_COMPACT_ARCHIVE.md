# PHASE3_COMPACT_ARCHIVE.md
# TexasSolverGPU v0.3.0 — Phase 3: Compact Archive / Strategy Archive Format

**Target:** `re_lab/target_bin/TexasSolverGPU-0.3.0-win64/TexasSolverGpu_131.exe` (PE32+ GUI, 18.9 MB)
**Status:** Static strings/recon only. No dynamic capture (no Windows/x64 emulator, no GPU on host).
**Source of truth:** `strings -n 5` of `TexasSolverGpu_131.exe` (`all_strings.txt`, 32,289 strings), cross-referenced with Phase-1/2 docs (BINARY_ANALYSIS_RECON.md §6, DEEP_RE_PHASE2.md §6, CPP_CORE_RECON.md §4).

---

## 1. High-level conclusion

The "compact archive" (a.k.a. **strategy archive**) is **Apache Avro-based with DEFLATE compression**, serializing Q16 fixed-point solver outputs (reach probs, strategy probs, action EVs) plus compact regret maps. It is **not** a plain binary blob — it is an Avro container with schema + codec metadata, written/read asynchronously (`avro_busy` / `avro_done` / `avro_cancelled`).

Native entry points:
- `solver.strategy.archive.open`
- `solver.strategy.archive.node`
- `solver.strategy.archive.close`

Related frontend: `desktop.strategy.exportFullFile` / `exportFullFileProgress` / `importFullFile`; `solver.export.fullFile`, `solver.export.fullFile.status`; `export_format_avro_recommended`.

---

## 2. Avro schema / codec evidence (from string table)

| String | Interpretation |
|--------|----------------|
| `avro.apache.org` | Avro spec namespace/reference |
| `Avro codec`, `avro.codec`, `avro_codec` | Per-archive codec selection (DEFLATE expected) |
| `Avro codec must be a string.` | Validation error |
| `Avro DEFLATE block`, `Avro DEFLATE reader`, `Avro DEFLATE stream` | DEFLATE is the compression codec |
| `Avro DEFLATE block count overflow`, `Avro DEFLATE block has trailing bytes`, `Avro DEFLATE pipeline accounting` | DEFLATE stream framing & bounds checks |
| `Avro file is missing schema or codec metadata` | Schema + codec stored in archive header/metadata |
| `Avro archive is open.` / `Avro archive opened` | Archive lifecycle state |
| `Avro archives can be used for Play.` | Archive also feeds the play/drill mode |
| `Avro array is too large for ` | Array length bounds |
| `Avro block exceeds safety limit`, `Avro block exceeds the output buffer limit`, `Avro block size must be greater than zero` | Block framing validation |
| `Avro chance_representatives must be empty or 52 bytes` | Chance card representatives (52 = full deck) |
| `Avro could not enter an action child`, `Avro could not enter a representative chance child` | Avro recursive descent into action/chance subrecords |
| `Invalid compact Avro action`, `Invalid compact Avro reach q16 length`, `Invalid compact Avro strategy dimensions`, `Invalid compact Avro strategy q16 dimensions`, `Invalid compact Avro strategy row sum` | Per-field Q16/dimension validation |
| `Compact Avro chance mapping is invalid`, `Compact Avro traversal reached an invalid history` | Chance-mapping & history-traversal validation |
| `Compressed Avro block exceeds the output buffer limit` | Compressed-block output buffering |
| `Decoded Apache Avro block exceeds safety limit` | Decompression-side bounds |
| `Compact strategy archive contains an invalid scale`, `Compact strategy archive metadata is invalid` | Scale + metadata validation |

Archive variants referenced elsewhere: `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled`. New constants: `COMPACT_BACKWARD_CACHE`, `COMPACT_INVALID_REGRETS`, `COMPACT_REGRET_ALIGNMENT`.

---

## 3. Q16 fixed-point fields (serialized in the archive)

| Field | Meaning |
|-------|---------|
| `reach_probs_q16` | Per-combo reach probability, Q16 (16.16) |
| `strategy_probs_q16` | Per-(action × combo) strategy probability, Q16 |
| `action_evs_q16` | Per-(action × combo) action EV, Q16 |

Q16 validation errors:
- `Compact strategy archive contains invalid -32768 q16` (sentinel / out-of-range)
- `Compact strategy archive contains invalid ` (truncated — q16 dimension/length)
- `Invalid compact Avro reach q16 length`
- `Invalid compact Avro strategy q16 dimensions`

Regret-related (compact regret):
- `compact regret descriptor`, `compact regret hand map`, `compact regret layout`
- `compact regret descriptor exceeds uint32 range`
- `compact regret hand map exceeds descriptor range`
- `compact regret layout only supports two-player games`
- `COMPACT_INVALID_REGRETS`

---

## 4. Compact archive layout (reconstructed)

```
[Avro file header]
  magic: OBJ 0x04 0x1c 0x96 0x52 (standard Avro)
  meta: {avro.schema, avro.codec="deflate", ...}
[Avro data blocks]
  block: [rowCount (long)] [blockBytes (long)] [compressed bytes] [sync marker]
  codec = deflate per avro.codec
[records]
  strategy archive record:
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

Constraints from validation strings:
- Row sums of `strategy_probs_q16` must be valid (`Invalid compact Avro strategy row sum`) → each action row sums to ~1.0 across combos.
- `strategy dimensions` / `q16 dimensions` must match `[num_actions x num_combos]`.
- `reach q16 length` must equal `num_combos`.
- `scale` present in metadata and must be valid (`contains an invalid scale`).
- Regret layout only supports 2-player games (`compact regret layout only supports two-player games`).
- Chance reps either absent or exactly 52 bytes (full deck).

---

## 5. Capacity / overflow validation errors (native strings)

| Error | What it guards |
|-------|----------------|
| `compact regret descriptor exceeds uint32 range` | regret descriptor fits u32 |
| `compact regret hand map exceeds descriptor range` | hand map within descriptor |
| `packed strategy offset exceeds int32 range` | strategy offsets fit i32 |
| `persistent river storage offset exceeds int32 range` | river storage offsets fit i32 |
| `GPU exact-layout capacity exceeded: this tree needs …` | exact GPU layout budget |
| `GPU node/schedule capacity exceeded: this tree has …` / `…reachable tree has …` | node + schedule budget |
| `GPU hand-index capacity exceeded for player …` | hand-index table per player |
| `GPU workspace-row capacity exceeded: this schedule needs …` | workspace rows |
| `one upper/river-batch slice exceeds the signed int32 entry-offset limit` | batch slice entry offsets fit i32 |
| `GPU isomorphism schedule capacity exceeded at depth …` / `while building a depth slice: entry offsets and counts must fit signed int32` | isomorphism schedule per depth |
| `GPU isomorphism permutation capacity exceeded for player …` / `too many 2P isomorphism permutations` | iso permutations |
| `GPU NodeLock capacity exceeded: the concatenated lock table would contain more than …` | NodeLock table |
| `GPU NodeLock capacity exceeded while building the lock table` | lock-table build |
| `CFV staging plan exceeds scratch capacity` | CFV staging scratch |
| `Compressed CFV segment exceeds storage` | compressed CFV segment |
| `GPU gather destination exceeds storage` | gather destination |
| `Apache Avro block exceeds the output buffer limit` / `Avro record exceeds the output buffer limit` / `Compressed Avro block exceeds the output buffer limit` | Avro output buffering |
| `Apache Avro DEFLATE block count overflow` / `Avro encode batch count overflow` / `Avro record count overflow` | Avro counters |
| `Archive history exceeds safety limit` | history length |
| `backed solver storage size overflow` / `…plus safety reserve overflow` | backed (file-mapped) storage |
| `backed storage file exceeds Windows int64 size limit` | file size limit |
| `batch slice exceeds the signed int32 entry` | batch slice entries |
| `bit CFV staging offset overflow` / `bit GPU offset overflow` | bit-offset fields |

---

## 6. Async archive lifecycle

States: `avro_busy` → `avro_done` / `avro_cancelled`. Export paths:
- `export_format_avro_recommended` (Avro recommended over raw JSON)
- `export_full_busy` / `export_cancel` / `export_cancelled` / `export_failed`
- Progress via `desktop.strategy.exportFullFileProgress` → `/api/export/full-file/progress`

Import path: `desktop.strategy.importFullFile` → `/api/import/full-file`; single-node `desktop.strategy.importNode` → `/api/import/node`; `desktop.strategy.closeImport` → `/api/import/close`.

---

## 7. Recommended next steps (dynamic)

1. Run the solver under a Windows/x64 emulator (or instrument `nvcuda.dll`/`WinSock`) and trigger `POST /api/export/full-file`; capture the bytes written for `solver.strategy.archive.open/node/close` and confirm the Avro magic + deflate blocks.
2. Decode `compact_v2`/`compact_H9` from a real export using Python `fastavro` with the recovered schema; verify Q16 dequantization: `value = q16_raw / 65536.0`.
3. Confirm `chance_representatives` (empty or 52 bytes) maps to board/chance nodes in 3p games.
4. Correlate `exported_at` / `parameter_schema_version:2` (from FRONTEND_API_RECON.md §4.8) with the Avro metadata `avro.schema`.
