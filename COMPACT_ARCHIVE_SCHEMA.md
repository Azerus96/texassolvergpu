# TexasSolverGPU v0.3.0 — Compact Archive / Avro Schema

**Target:** `TexasSolverGpu_131.exe` (PE32+ GUI, ImageBase `0x140000000`)
**Status:** Statically reconstructed from binary strings, `compact_archive_decoder.py`, and `PHASE3_COMPACT_ARCHIVE.md`
**Blocked:** Actual archive bytes require dynamic capture (Windows + NVIDIA GPU)

---

## 1. Format Overview

Compact strategy archives are **Apache Avro object container files** with the **DEFLATE codec**, written by the native methods `solver.strategy.archive.open/node/close`. Q16 fixed-point encoding is used for probabilities and EVs.

| Property | Value |
|----------|-------|
| Container | Apache Avro Object Container File |
| Magic | `OBJ 0x04 0x1c 0x96 0x52` (`"Obj\x04"`) |
| Codec | `deflate` (raw DEFLATE, zlib window -15) |
| Sync marker | 16 bytes (random per file) |
| Native entry points | `solver.strategy.archive.open`, `solver.strategy.archive.node`, `solver.strategy.archive.close` |
| Variants | `compact_v2`, `compact_H9`, `compact_size`, `compact_regrets_enabled` |
| Async lifecycle | `avro_busy` → `avro_done` / `avro_cancelled` |
| Decoder tool | `re_lab/texassolvergpu/compact_archive_decoder.py` |

---

## 2. Avro File Header

| Offset | Field | Type | Description |
|--------|-------|------|-------------|
| 0x00 | Magic | 4 bytes | `Obj\x04` (Avro version 4) |
| 0x04 | Metadata count | Avro long | Number of key/value metadata entries |
| 0x04+N | Metadata | key/value pairs | `avro.codec` = `"deflate"`, plus schema |
| ... | Schema | Avro string | JSON schema string (long-prefixed UTF-8) |
| ... | Sync marker | 16 bytes | Random sync marker for block framing |

---

## 3. Block Framing

Each data block:
| Field | Type | Description |
|-------|------|-------------|
| num_records | Avro long | Number of records in block (0 = end of file) |
| block_bytes | Avro long | Compressed size of block data in bytes |
| block_data | `block_bytes` bytes | DEFLATE-compressed serialized records |
| sync_marker | 16 bytes | File sync marker |

After decompression, each record is serialized according to the Avro schema (see §4).

---

## 4. Record Layout

| Field | Avro Type | Description |
|-------|-----------|-------------|
| version | string | Archive format version string |
| num_players | int | Number of players (2 for HU, 3 for 3-max, 6 for 6-max) |
| num_actions | int | Number of actions at this node |
| num_combos | int | Number of hand combos (should be 1326 = C(52,2)) |
| board | array<int> | Board card indices (0-51), 0 cards preflop, 3 flop, 4 turn, 5 river |
| paid | array<int> | Chips paid by each player (length = num_players) |
| pot | int | Total pot size in chips |
| reach_probs_q16 | array<int16> | Q16 reach probabilities per combo (length = num_combos) |
| strategy_probs_q16 | array<array<int16>> | Q16 strategy probabilities per action per combo [num_actions x num_combos], row-major |
| action_evs_q16 | array<array<int16>> \| null | Q16 action EVs per action per combo, or null if not computed |
| compact_regrets_enabled | boolean | Whether regret data is included |
| regret_data | bytes \| null | Opaque regret descriptor / hand map (present only if regrets_enabled) |
| chance_representatives | bytes | Empty or 52 bytes (chance card mapping for 3p games) |
| history | string | Action sequence history (e.g., "0:C/1:R100") |
| scale | float | Scale factor for Q16 dequantization (must be nonzero if reach != 0) |

### Q16 Fixed-Point

All Q16 fields are **int16 little-endian** values. Dequantization:

```
value = q16_raw / 65536.0
```

Special value `-32768` (0x8000) indicates an invalid/uninitialized Q16 value.

---

## 5. Validation Rules

| Check | Error Message |
|-------|--------------|
| `scale == 0.0` AND any reach != 0 | "Compact archive has nonzero reach values with a zero scale" |
| Any Q16 value == -32768 | "Compact strategy archive contains invalid -32768 q16" |
| `compact_regrets_enabled == true` AND `num_players != 2` | "Compact regret layout only supports two-player games" |
| Strategy row sum != 1.0 (tolerance 0.01) | "Invalid compact Avro strategy row sum" |
| Chance mapping invalid | "Compact archive chance mapping is invalid" |

---

## 6. Schema (JSON)

```json
{
  "type": "record",
  "name": "CompactStrategyRecord",
  "fields": [
    {"name": "version", "type": "string"},
    {"name": "num_players", "type": "int"},
    {"name": "num_actions", "type": "int"},
    {"name": "num_combos", "type": "int"},
    {"name": "board", "type": {"type": "array", "items": "int"}},
    {"name": "paid", "type": {"type": "array", "items": "int"}},
    {"name": "pot", "type": "int"},
    {"name": "reach_probs_q16", "type": {"type": "array", "items": {"type": "bytes", "logicalType": "q16"}}},
    {"name": "strategy_probs_q16", "type": {"type": "array", "items": {"type": "array", "items": {"type": "bytes", "logicalType": "q16"}}}},
    {"name": "action_evs_q16", "type": ["null", {"type": "array", "items": {"type": "array", "items": {"type": "bytes", "logicalType": "q16"}}}}],
    {"name": "compact_regrets_enabled", "type": "boolean"},
    {"name": "regret_data", "type": ["null", "bytes"]},
    {"name": "chance_representatives", "type": "bytes"},
    {"name": "history", "type": "string"},
    {"name": "scale", "type": "float"}
  ]
}
```

> Note: Q16 fields are stored as raw int16 LE bytes within the Avro binary serialization; the logical type `q16` is a documentation annotation, not a native Avro logical type.

---

## 7. Native Method Integration

| Method | Role |
|--------|------|
| `solver.strategy.archive.open` | Initialize archive writing, set up Avro container |
| `solver.strategy.archive.node` | Write a single node record (strategy snapshot) |
| `solver.strategy.archive.close` | Finalize and flush archive |

Async lifecycle tracked via state flags: `avro_busy` → `avro_done` / `avro_cancelled`.

---

## 8. Decoder Usage

```bash
python3 compact_archive_decoder.py <archive.avro> [output.json]
```

The decoder:
1. Parses the Avro file header (magic, metadata, schema, sync marker)
2. Reads blocks (num_records + block_bytes + DEFLATE data + sync marker)
3. Decompresses each block with raw DEFLATE
4. Parses each record according to the known layout (§4)
5. Dequantizes Q16 fields: `value = q16_raw / 65536.0`
6. Runs validation checks (§5)

**Limitation:** The decoder's record parser is hand-written based on the statically reconstructed layout. The exact Avro binary encoding (union tags, array framing) may require adjustment once real archive bytes are captured dynamically.

---

## 9. Blocked Items (Require Dynamic Capture)

| Item | Why |
|------|-----|
| Actual Avro schema JSON string | Embedded in binary but not extracted; written at runtime by `archive.open` |
| Real archive bytes | Generated by `archive.open/node/close` during live solver execution |
| `regret_data` binary format | Opaque; structure unknown without live capture |
| `chance_representatives` content (3p) | 52-byte mapping; exact encoding unknown |
| Validation of Q16 matrix framing | Row/column ordering in Avro binary needs live verification |

---

## 10. Tool Location

- Decoder: `/kaggle/working/re_lab/texassolvergpu/compact_archive_decoder.py`
- Schema doc: `/kaggle/working/re_lab/texassolvergpu/COMPACT_ARCHIVE_SCHEMA.md` (this file)
- Prior recon: `PHASE3_COMPACT_ARCHIVE.md`
