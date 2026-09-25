#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Record the pinned Adobe ACR3 table as attributed, test-only Rust source.

The input is Adobe DNG SDK 1.7.1.2724
source/dng_render.cpp::dng_tone_curve_acr3_default::Evaluate::kTable.
Copyright 2006-2023 Adobe Systems Incorporated. All Rights Reserved.
The original LICENSE.source_code and LICENSE.technology are preserved in
experimental/rust_raw/licenses; the generated source retains attribution.
"""

import argparse
import hashlib
from pathlib import Path
import re
import struct


SOURCE = Path("third_party/externals/dng_sdk/source/dng_render.cpp")
OUTPUT = Path("experimental/rust_raw/ffi/acr3_default.rs")
EXPECTED_TABLE_SHA256 = "1ae4726011b5bf18a806c1c029a68fe3c56c30b0c9f289d95f23073a86c6fa27"


def table_values(source):
    start = source.index("real64 dng_tone_curve_acr3_default::Evaluate (real64 x) const")
    table = source.index("static const real32 kTable []", start)
    end = source.index("};", table)
    values = re.findall(r"(?<![\w.])\d+\.\d+f", source[table:end])
    if len(values) != 1025 or values[0] != "0.00000f" or values[-1] != "1.00000f":
        raise ValueError("unexpected pinned SDK ACR3 table shape")
    original = b"".join(struct.pack("<f", float(value[:-1])) for value in values)
    if hashlib.sha256(original).hexdigest() != EXPECTED_TABLE_SHA256:
        raise ValueError("pinned Adobe ACR3 table changed")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    try:
        values = table_values(args.source.read_text(encoding="utf-8"))
    except ValueError as error:
        parser.error(str(error))
    lines = [
        "// Derived from Adobe DNG SDK 1.7.1.2724,",
        "// source/dng_render.cpp::dng_tone_curve_acr3_default::Evaluate::kTable.",
        "// Copyright 2006-2023 Adobe Systems Incorporated. All Rights Reserved.",
        "// See experimental/rust_raw/licenses/LICENSE.adobe-dng-sdk and PROVENANCE.md.",
        "",
        "#[rustfmt::skip]",
        "pub(super) const SDK_ACR3_DEFAULT: [f32; 1025] = [",
    ]
    for index in range(0, len(values), 4):
        lines.append("    " + " ".join(value[:-1] + "," for value in values[index:index + 4]))
    lines.append("];")
    result = "\n".join(lines) + "\n"
    if not args.output.is_file() or args.output.read_text(encoding="utf-8") != result:
        args.output.write_text(result, encoding="utf-8")
    print(f"SDK ACR3 samples: {len(values)}; table SHA-256: {EXPECTED_TABLE_SHA256}")


if __name__ == "__main__":
    main()
