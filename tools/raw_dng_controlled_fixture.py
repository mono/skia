#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate checked tone/black variants from Skia's existing RAW test DNG."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


def controlled_dng(source, tone="identity", black="none"):
    if tone not in ("identity", "sdk-default"):
        raise ValueError("unknown controlled tone mode")
    if black not in ("none", "auto"):
        raise ValueError("unknown controlled black mode")
    data = bytearray(source)
    if data[:4] != b"II\x2a\0" or len(data) < 14:
        raise ValueError("expected Skia's little-endian classic TIFF DNG")
    first = struct.unpack_from("<I", data, 4)[0]
    if first > len(data) - 2:
        raise ValueError("root IFD is outside the file")
    count = struct.unpack_from("<H", data, first)[0]
    size = 2 + count * 12 + 4
    extra_tags = (tone == "identity") + (black == "none")
    if count > 0xFFFF - extra_tags or size > len(data) - first:
        raise ValueError("root IFD is truncated or too large")
    entries = [
        bytes(data[first + 2 + i * 12:first + 2 + (i + 1) * 12])
        for i in range(count)
    ]
    ids = [struct.unpack_from("<H", entry)[0] for entry in entries]
    if sorted(ids) != ids or len(set(ids)) != len(ids) or 50940 in ids or 51110 in ids:
        raise ValueError("expected sorted tags without profile tone or black-render metadata")
    if extra_tags == 0:
        return data
    if len(data) & 1:
        data.append(0)
    extra_size = 12 * extra_tags + (16 if tone == "identity" else 0)
    if len(data) > 0xFFFFFFFF - size - extra_size:
        raise ValueError("fixture would exceed classic TIFF offsets")
    new_ifd = len(data)
    if tone == "identity":
        entries.append(struct.pack("<HHII", 50940, 11, 4, 0))
    if black == "none":
        entries.append(struct.pack("<HHII", 51110, 4, 1, 1))
    entries.sort(key=lambda entry: struct.unpack_from("<H", entry)[0])
    data.extend(struct.pack("<H", len(entries)))
    for entry in entries:
        data.extend(entry)
    data.extend(struct.pack("<I", 0))
    if tone == "identity":
        tone_entry = new_ifd + 2 + 12 * next(
            index for index, entry in enumerate(entries)
            if struct.unpack_from("<H", entry)[0] == 50940
        )
        struct.pack_into("<I", data, tone_entry + 8, len(data))
        data.extend(struct.pack("<ffff", 0.0, 0.0, 1.0, 1.0))
    struct.pack_into("<I", data, 4, new_ifd)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="resources/images/sample_1mp.dng")
    parser.add_argument("output", type=Path, help="generated test DNG outside the source tree")
    parser.add_argument("--tone", choices=("identity", "sdk-default"), default="identity")
    parser.add_argument("--black", choices=("none", "auto"), default="none")
    args = parser.parse_args()
    source = args.source.read_bytes()
    output = controlled_dng(source, args.tone, args.black)
    args.output.write_bytes(output)
    print(json.dumps({
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "output_sha256": hashlib.sha256(output).hexdigest(),
        "output_bytes": len(output),
        "tone": args.tone,
        "black": args.black,
    }))


if __name__ == "__main__":
    main()
