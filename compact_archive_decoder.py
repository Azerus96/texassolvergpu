#!/usr/bin/env python3
"""
TexasSolverGPU v0.3.0 — Compact Archive / Avro + Q16 Decoder

Decodes compact strategy archives written by solver.strategy.archive.open/node/close.
Format: Apache Avro object container file + DEFLATE codec + Q16 fixed-point fields.

Usage:
    python3 compact_archive_decoder.py <archive.avro> [output.json]
"""

import sys
import json
import struct
import zlib

# Avro magic: "Obj" + version + codec + meta
AVRO_MAGIC = b'Obj\x04'
AVRO_SYNC_SIZE = 16

# Q16 dequantization
Q16_SCALE = 65536.0


def read_block_header(f):
    """Read an Avro block header: (num_records, block_bytes)."""
    # block header is a long (num_records) followed by a long (block size)
    num_records = _read_long(f)
    block_size = _read_long(f)
    return num_records, block_size


def _read_long(f):
    """Read a Avro variable-length long (zigzag + base128)."""
    result = 0
    shift = 0
    while True:
        byte = f.read(1)
        if not byte:
            raise EOFError("unexpected EOF reading long")
        b = byte[0]
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    # zigzag decode
    return (result >> 1) ^ -(result & 1)


def read_avro_schema(f):
    """Read an Avro schema string (prefixed with a long length)."""
    n = _read_long(f)
    schema_bytes = f.read(n)
    return schema_bytes.decode('utf-8')


def parse_avro_header(f):
    """Parse the Avro file header. Returns (schema, codec, meta, sync_marker)."""
    magic = f.read(4)
    if magic != AVRO_MAGIC:
        raise ValueError(f"bad Avro magic: {magic!r}")
    meta_len = _read_long(f)
    meta = {}
    for _ in range(meta_len):
        key_len = _read_long(f)
        key = f.read(key_len).decode('utf-8')
        val_len = _read_long(f)
        val = f.read(val_len).decode('utf-8')
        meta[key] = val
    schema = read_avro_schema(f)
    sync = f.read(AVRO_SYNC_SIZE)
    if len(sync) != AVRO_SYNC_SIZE:
        raise ValueError("unexpected EOF reading sync marker")
    return schema, meta, sync


def inflate_deflate(data):
    """DEFLATE-decompress (raw deflate, no zlib wrapper)."""
    return zlib.decompress(data, -15)


def read_q16_array(f, count):
    """Read count Q16 (int16 little-endian) values."""
    raw = f.read(count * 2)
    if len(raw) != count * 2:
        raise EOFError(f"short read: expected {count*2} bytes for Q16 array, got {len(raw)}")
    vals = struct.unpack('<%dh' % count, raw)
    return vals


def decode_archive(path):
    """Decode a compact archive file. Returns a dict with all fields."""
    with open(path, 'rb') as f:
        schema, meta, sync = parse_avro_header(f)
        codec = meta.get('avro.codec', 'null')
        if codec not in ('null', 'deflate'):
            raise ValueError(f"unsupported Avro codec: {codec}")

        print(f"Schema: {schema[:200]}...")
        print(f"Codec: {codec}")
        print(f"Meta: {meta}")

        records = []
        while True:
            try:
                num_records, block_size = read_block_header(f)
            except EOFError:
                break
            if num_records == 0:
                break

            block_data = f.read(block_size)
            if len(block_data) != block_size:
                raise EOFError(f"short read block: expected {block_size}, got {len(block_data)}")

            # Check sync marker after block
            next_sync = f.read(AVRO_SYNC_SIZE)
            if len(next_sync) != AVRO_SYNC_SIZE:
                # Might be EOF
                f.seek(-len(next_sync), 1)
                break

            if codec == 'deflate':
                block_data = inflate_deflate(block_data)

            # Parse each record within the block
            # Each record is a single datum per Avro spec - here it's a complex record
            # We'll parse manually based on known layout
            offset = 0
            for i in range(num_records):
                rec, offset = _parse_record(block_data, offset)
                records.append(rec)

    return {
        'schema': schema,
        'codec': codec,
        'meta': meta,
        'records': records,
    }


def _parse_record(data, offset):
    """Parse a single archive record from raw bytes starting at offset."""
    start = offset

    # version: string (long-prefixed UTF-8)
    version, offset = _read_avro_string(data, offset)

    # num_players: int
    num_players, offset = _read_avro_int(data, offset)

    # num_actions: int
    num_actions, offset = _read_avro_int(data, offset)

    # num_combos: int (should be 1326)
    num_combos, offset = _read_avro_int(data, offset)

    # board: array of int (card indices)
    board, offset = _read_avro_int_array(data, offset)

    # paid: array of int (per player)
    paid, offset = _read_avro_int_array(data, offset)

    # pot: int
    pot, offset = _read_avro_int(data, offset)

    # reach_probs_q16: array of int16 (num_combos) — length-prefixed Avro array
    reach_len, offset = _read_avro_long(data, offset)
    reach_q16, offset = _read_avro_q16_array(data, offset, reach_len)

    # strategy_probs_q16: array of array of int16 (num_actions x num_combos)
    # Outer length prefix = num_rows, then per-row: length prefix + int16 values
    strategy_nrows, offset = _read_avro_long(data, offset)
    strategy_q16, offset = _read_avro_q16_matrix(data, offset, strategy_nrows, num_combos)

    # action_evs_q16: array of array of int16 or null
    evs_q16, offset = _read_avro_q16_matrix_nullable(data, offset, num_actions, num_combos)

    # compact_regrets_enabled: boolean
    regrets_enabled, offset = _read_avro_bool(data, offset)

    # regret_descriptor / regret_hand_map (if enabled) - opaque
    regret_data = None
    if regrets_enabled:
        regret_data, offset = _read_avro_bytes(data, offset)

    # chance_representatives: empty or 52 bytes
    chance_reps, offset = _read_avro_bytes(data, offset)

    # history: string
    history, offset = _read_avro_string(data, offset)

    # scale: float
    scale, offset = _read_avro_float(data, offset)

    # Validate Q16 values
    invalid_q16 = [v for v in reach_q16 if v == -32768]
    for row in strategy_q16:
        invalid_q16.extend([v for v in row if v == -32768])
    if evs_q16:
        for row in evs_q16:
            invalid_q16.extend([v for v in row if v == -32768])

    record = {
        'version': version,
        'num_players': num_players,
        'num_actions': num_actions,
        'num_combos': num_combos,
        'board': board,
        'paid': paid,
        'pot': pot,
        'reach_probs_q16': reach_q16,
        'reach_probs': [v / Q16_SCALE for v in reach_q16],
        'strategy_probs_q16': strategy_q16,
        'strategy_probs': [[v / Q16_SCALE for v in row] for row in strategy_q16],
        'action_evs_q16': evs_q16,
        'action_evs': [[v / Q16_SCALE for v in row] for row in evs_q16] if evs_q16 else None,
        'compact_regrets_enabled': regrets_enabled,
        'regret_data': regret_data,
        'chance_representatives': chance_reps,
        'history': history,
        'scale': scale,
        'invalid_q16_count': len(invalid_q16),
    }

    # Validation
    if scale == 0.0 and any(v != 0 for v in reach_q16):
        record['validation_error'] = "nonzero reach values with a zero scale"
    if len(invalid_q16) > 0:
        record['validation_error'] = f"contains {len(invalid_q16)} invalid -32768 q16 values"
    if regrets_enabled and num_players != 2:
        record['validation_error'] = "regret layout only supports two-player games"

    return record, offset


def _read_avro_string(data, offset):
    """Read an Avro string: long length + UTF-8 bytes."""
    length, offset = _read_avro_long(data, offset)
    s = data[offset:offset + length].decode('utf-8')
    return s, offset + length


def _read_avro_int(data, offset):
    """Read an Avro int (zigzag + base128)."""
    return _read_avro_long(data, offset)


def _read_avro_long(data, offset):
    """Read an Avro variable-length long (zigzag + base128)."""
    result = 0
    shift = 0
    while True:
        b = data[offset]
        offset += 1
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    return (result >> 1) ^ -(result & 1), offset


def _read_avro_bool(data, offset):
    """Read an Avro boolean (1 byte)."""
    return data[offset] != 0, offset + 1


def _read_avro_float(data, offset):
    """Read an Avro float (4 bytes, little-endian IEEE 754)."""
    val, = struct.unpack_from('<f', data, offset)
    return val, offset + 4


def _read_avro_int_array(data, offset):
    """Read an Avro array of ints."""
    length, offset = _read_avro_long(data, offset)
    arr = []
    for _ in range(length):
        val, offset = _read_avro_long(data, offset)
        arr.append(val)
    return arr, offset


def _read_avro_q16_array(data, offset, count):
    """Read count Q16 values (int16 LE, 2 bytes each)."""
    vals = []
    for _ in range(count):
        val, = struct.unpack_from('<h', data, offset)
        vals.append(val)
        offset += 2
    return vals, offset


def _read_avro_q16_matrix(data, offset, rows, cols):
    """Read a rows x cols matrix of Q16 values. Each row is length-prefixed."""
    matrix = []
    for _ in range(rows):
        row_len, offset = _read_avro_long(data, offset)
        row, offset = _read_avro_q16_array(data, offset, row_len)
        matrix.append(row)
    return matrix, offset


def _read_avro_q16_matrix_nullable(data, offset, rows, cols):
    """Read a nullable Q16 matrix (null or rows x cols)."""
    # Avro union: null (tag 0) or matrix (tag 1)
    if offset >= len(data):
        return None, offset
    tag = data[offset]
    if tag == 0:
        return None, offset + 1
    # tag == 1: read outer length prefix, then the matrix
    offset += 1
    nrows, offset = _read_avro_long(data, offset)
    matrix = []
    for _ in range(nrows):
        row_len, offset = _read_avro_long(data, offset)
        row, offset = _read_avro_q16_array(data, offset, row_len)
        matrix.append(row)
    return matrix, offset


def _read_avro_bytes(data, offset):
    """Read an Avro bytes field (long length + raw bytes)."""
    length, offset = _read_avro_long(data, offset)
    val = data[offset:offset + length]
    return val, offset + length


def validate_record(rec):
    """Run archive validation checks and return list of errors."""
    errors = []
    if rec.get('validation_error'):
        errors.append(rec['validation_error'])
    # Row sum check: each strategy row should sum to ~1.0 (within tolerance)
    for i, row in enumerate(rec['strategy_probs']):
        row_sum = sum(row)
        if abs(row_sum - 1.0) > 0.01:
            errors.append(f"strategy row {i} sum={row_sum:.6f} != 1.0")
    return errors


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <archive.avro> [output.json]")
        sys.exit(1)

    path = sys.argv[1]
    result = decode_archive(path)

    print(f"\nRecords decoded: {len(result['records'])}")
    for i, rec in enumerate(result['records']):
        print(f"\n--- Record {i} ---")
        print(f"  version: {rec['version']}")
        print(f"  num_players: {rec['num_players']}")
        print(f"  num_actions: {rec['num_actions']}")
        print(f"  num_combos: {rec['num_combos']}")
        print(f"  board: {rec['board']}")
        print(f"  paid: {rec['paid']}")
        print(f"  pot: {rec['pot']}")
        print(f"  scale: {rec['scale']}")
        print(f"  history: {rec['history']}")
        print(f"  compact_regrets_enabled: {rec['compact_regrets_enabled']}")
        print(f"  reach_probs (first 5): {rec['reach_probs'][:5]}")
        print(f"  strategy_probs row 0 (first 5): {rec['strategy_probs'][0][:5] if rec['strategy_probs'] else None}")
        print(f"  action_evs row 0 (first 5): {rec['action_evs'][0][:5] if rec['action_evs'] else None}")
        print(f"  invalid_q16_count: {rec['invalid_q16_count']}")
        errs = validate_record(rec)
        if errs:
            print(f"  VALIDATION ERRORS: {errs}")

    if len(sys.argv) > 2:
        with open(sys.argv[2], 'w') as f:
            json.dump(result, f, indent=2)
        print(f"\nOutput written to {sys.argv[2]}")


if __name__ == '__main__':
    main()
