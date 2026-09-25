#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Compare controlled DNG color through Rust Stage 3/4 against a separate SDK process."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from raw_dng_controlled_fixture import controlled_dng


def digest(data):
    return hashlib.sha256(data).hexdigest()


def capture(executable, arguments, output, timeout, stage=None):
    try:
        process = subprocess.run(
            [str(executable), *map(str, arguments), str(output)],
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return {"process": "timeout"}
    result = {"process": "exited" if process.returncode == 0 else "failed",
              "exit_code": process.returncode}
    if process.returncode != 0:
        result["stderr"] = process.stderr[-2048:]
        return result
    if stage is not None:
        try:
            metadata = json.loads(process.stdout)
            if not isinstance(metadata, dict) or (stage != "public"
                                                   and metadata.get("stage") != stage):
                raise ValueError("incorrect reference metadata")
            if metadata.get("created", True) is not True:
                raise ValueError("decoder rejected a required stage")
            result["metadata"] = metadata
        except ValueError as error:
            result["process"] = "invalid_metadata"
            result["error"] = str(error)
            return result
    if not output.is_file():
        result["process"] = "missing_output"
        return result
    data = output.read_bytes()
    result["bytes"] = len(data)
    result["sha256"] = digest(data)
    return result


def validate_stage(result, stage, sample_bytes):
    if result["process"] != "exited":
        return False
    expected = {"stage": stage, "width": 600, "height": 338, "planes": 3,
                "pixel_type": 3 if stage == 3 else 1,
                "bytes_per_sample": sample_bytes, "main_ifd_index": 1,
                "row_bytes": 600 * 3 * sample_bytes,
                "total_bytes": 600 * 338 * 3 * sample_bytes}
    return (all(type(result["metadata"].get(key)) is type(value)
                and result["metadata"][key] == value
                for key, value in expected.items())
            and result["bytes"] == expected["total_bytes"])


def validate_public(result):
    if result["process"] != "exited":
        return False
    expected = {"created": True, "create_result": "success", "decode_result": "success",
                "width": 600, "height": 338, "encoded_format": 10, "origin": 1,
                "alpha_type": 1, "color_type": 4, "destination": "rgba",
                "has_color_space": True}
    return (all(type(result["metadata"].get(key)) is type(value)
                and result["metadata"][key] == value
                for key, value in expected.items())
            and result["bytes"] == 600 * 338 * 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path("resources/images/sample_1mp.dng"))
    parser.add_argument("--reference", type=Path, required=True,
                        help="Adobe-enabled dng_stage_oracle executable")
    parser.add_argument("--candidate-stage", type=Path, required=True,
                        help="SDK-free raw_rust_stage_probe executable")
    parser.add_argument("--candidate-color", type=Path, required=True,
                        help="test-only Rust color_probe executable")
    parser.add_argument("--reference-public", type=Path,
                        help="optional Adobe-enabled raw_codec_probe public route (full-size RGBA)")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--tone", choices=("identity", "sdk-default"), default="identity")
    parser.add_argument("--black", choices=("none", "auto"), default="none")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    for executable in (args.reference, args.candidate_stage, args.candidate_color,
                       args.reference_public):
        if executable is None:
            continue
        if not executable.is_file():
            parser.error(f"missing executable: {executable}")
    if not args.source.is_file():
        parser.error(f"missing Skia test DNG: {args.source}")
    try:
        source = args.source.read_bytes()
        controlled = controlled_dng(source, args.tone, args.black)
    except ValueError as error:
        parser.error(f"invalid source test DNG: {error}")

    report = {"schema_version": 1, "tone_mode": args.tone, "black_mode": args.black,
              "source_sha256": digest(source),
              "public_reference_compared": args.reference_public is not None,
              "rust_public_skcodec_compared": False,
              "input_sha256": digest(controlled), "cases": {}}
    with tempfile.TemporaryDirectory() as directory:
        scratch = Path(directory)
        source_file = scratch / "controlled.dng"
        source_file.write_bytes(controlled)
        paths = {
            "reference_stage3": scratch / "reference-stage3.u16",
            "candidate_stage3": scratch / "candidate-stage3.u16",
            "reference_stage4": scratch / "reference-stage4.rgb",
            "candidate_stage4": scratch / "candidate-stage4.rgb",
            "reference_public": scratch / "reference-public.rgba",
        }
        cases = report["cases"]
        cases["reference_stage3"] = capture(
            args.reference, (3, source_file), paths["reference_stage3"], args.timeout, 3)
        cases["candidate_stage3"] = capture(
            args.candidate_stage, (3, source_file), paths["candidate_stage3"], args.timeout, 3)
        cases["reference_stage4"] = capture(
            args.reference, (4, source_file), paths["reference_stage4"], args.timeout, 4)
        if args.reference_public:
            cases["reference_public"] = capture(
                args.reference_public, ("public", source_file),
                paths["reference_public"], args.timeout, "public")
        if validate_stage(cases["candidate_stage3"], 3, 2):
            color_options = [source_file, paths["candidate_stage3"]]
            if args.tone == "sdk-default":
                color_options.append("--tone=sdk-default")
            if args.black == "auto":
                color_options.append("--black=auto")
            cases["candidate_stage4"] = capture(
                args.candidate_color, color_options,
                paths["candidate_stage4"], args.timeout)
        else:
            cases["candidate_stage4"] = {"process": "blocked_by_stage3"}

        if not (validate_stage(cases["reference_stage3"], 3, 2)
                and validate_stage(cases["reference_stage4"], 4, 1)):
            outcome = "invalid_reference"
        elif args.reference_public and not validate_public(cases["reference_public"]):
            outcome = "invalid_reference_public"
        elif not validate_stage(cases["candidate_stage3"], 3, 2):
            outcome = "invalid_candidate_stage3"
        elif cases["candidate_stage4"]["process"] != "exited" or (
            cases["candidate_stage4"]["bytes"] != cases["reference_stage4"]["bytes"]
        ):
            outcome = "invalid_candidate_stage4"
        else:
            stage3_ref = paths["reference_stage3"].read_bytes()
            stage3_candidate = paths["candidate_stage3"].read_bytes()
            stage4_ref = paths["reference_stage4"].read_bytes()
            stage4_candidate = paths["candidate_stage4"].read_bytes()
            report["stage3_different_bytes"] = sum(
                a != b for a, b in zip(stage3_ref, stage3_candidate)
            )
            report["stage4_different_channels"] = sum(
                a != b for a, b in zip(stage4_ref, stage4_candidate)
            )
            if args.reference_public:
                packed = bytearray(len(stage4_candidate) // 3 * 4)
                for channel in range(3):
                    packed[channel::4] = stage4_candidate[channel::3]
                packed[3::4] = b"\xff" * (len(packed) // 4)
                public_ref = paths["reference_public"].read_bytes()
                report["packed_candidate_rgba_sha256"] = digest(packed)
                report["public_full_size_different_bytes"] = sum(
                    a != b for a, b in zip(public_ref, packed)
                )
            outcome = ("matched" if (stage3_ref == stage3_candidate
                                     and stage4_ref == stage4_candidate
                                     and (not args.reference_public
                                          or public_ref == packed))
                       else "pixel_mismatch")
    report["comparison"] = outcome
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"comparison": outcome,
                      "stage3_different_bytes": report.get("stage3_different_bytes"),
                      "stage4_different_channels": report.get("stage4_different_channels"),
                      "public_full_size_different_bytes": report.get(
                          "public_full_size_different_bytes")}))
    return 0 if outcome == "matched" else 1


if __name__ == "__main__":
    raise SystemExit(main())
