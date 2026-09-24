#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Compare independently run RAW decoders without hiding unsupported inputs."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def probe(executable, route, source, output, timeout, stage=None, color_type="rgba"):
    start = time.monotonic()
    try:
        command = [str(executable), route, str(source), str(output)]
        if stage is None and color_type != "rgba":
            command.append(color_type)
        process = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return {"process": "timeout", "seconds": round(time.monotonic() - start, 3)}

    result = {
        "process": ("exited" if process.returncode == 0 else
                    "signalled" if process.returncode < 0 else "failed"),
        "exit_code": process.returncode,
        "seconds": round(time.monotonic() - start, 3),
    }
    if process.returncode != 0:
        result["stderr"] = process.stderr[-2048:]
        return result
    try:
        data = json.loads(process.stdout)
        if not isinstance(data, dict):
            raise ValueError("missing codec result")
        if stage is not None:
            if data.get("stage") != stage:
                raise ValueError("stage does not match the request")
            data["created"] = data.get("created", True)
            if data["created"]:
                data["create_result"] = "success"
                data["decode_result"] = "success"
            else:
                if not isinstance(data.get("status"), str):
                    raise ValueError("missing stage rejection status")
                data["create_result"] = data["status"]
        if not isinstance(data.get("created"), bool) or not isinstance(
            data.get("create_result"), str
        ):
            raise ValueError("missing decoder status")
    except ValueError as error:
        result["process"] = "invalid_output"
        result["error"] = str(error)
        return result

    result["codec"] = data
    if data["created"] and data.get("decode_result") == "success":
        if not all(isinstance(data.get(key), int) and data[key] > 0
                   for key in ("width", "height")):
            result["process"] = "invalid_output"
            result["error"] = "missing or invalid dimensions"
            return result
        if not output.is_file():
            result["process"] = "missing_pixels"
            return result
        if stage is None:
            bytes_per_pixel = {
                "rgba": 4, "bgra": 4, "rgb565": 2,
                "gray8": 1, "f16": 8, "rgba1010102": 4,
            }[color_type]
            if data.get("destination", "rgba") != color_type:
                result["process"] = "invalid_output"
                result["error"] = "destination format does not match the request"
                return result
            expected_size = data["width"] * data["height"] * bytes_per_pixel
        else:
            fields = ("planes", "bytes_per_sample", "row_bytes", "total_bytes")
            if (any(not isinstance(data.get(key), int) or data[key] <= 0 for key in fields)
                    or data["row_bytes"] !=
                    data["width"] * data["planes"] * data["bytes_per_sample"]
                    or data["total_bytes"] != data["row_bytes"] * data["height"]):
                result["process"] = "invalid_output"
                result["error"] = "invalid stage geometry"
                return result
            expected_size = data["total_bytes"]
        if output.stat().st_size != expected_size:
            result["process"] = "invalid_pixels"
            result["actual_bytes"] = output.stat().st_size
            result["expected_bytes"] = expected_size
            return result
        result["pixel_bytes"] = expected_size
        result["pixels_sha256"] = digest(output)
    return result


def compare(reference, candidate, expected):
    for outcome in (reference, candidate):
        if outcome["process"] != "exited":
            return "process_failure"
    before = reference["codec"]
    after = candidate["codec"]
    if expected == "decode_rejection":
        if not before["created"] or before.get("decode_result") in (None, "success"):
            return "invalid_reference_case"
        if not after["created"]:
            return "missing_candidate_support"
        if after.get("decode_result") == "success":
            return "unexpected_acceptance"
        if not isinstance(after.get("decode_result"), str):
            return "invalid_output"
        if before["decode_result"] != after["decode_result"]:
            return "rejection_mismatch"
        if {key: value for key, value in before.items() if key != "route"} != {
            key: value for key, value in after.items() if key != "route"
        }:
            return "metadata_mismatch"
        return "matched_decode_rejection"
    if expected == "success":
        if not before["created"] or before.get("decode_result") != "success":
            return "invalid_reference_case"
        if not after["created"] or after.get("decode_result") != "success":
            return "missing_candidate_support"
        if before != after:
            # The route field is intentionally different between the programs.
            if {k: v for k, v in before.items() if k != "route"} != {
                k: v for k, v in after.items() if k != "route"
            }:
                return "metadata_mismatch"
        if reference["pixels_sha256"] != candidate["pixels_sha256"]:
            return "pixel_mismatch"
        return "matched"
    if before["created"] or after["created"]:
        return "unexpected_acceptance"
    if not all("create_result" in result for result in (before, after)):
        return "invalid_output"
    if before["create_result"] != after["create_result"]:
        return "rejection_mismatch"
    return "matched_rejection"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--reference-route", choices=("legacy", "public"), default="legacy")
    parser.add_argument("--candidate-route", choices=("rust", "public"), default="rust")
    parser.add_argument("--stage", type=int, choices=(1, 2, 3),
                        help="compare stage bytes with dng_stage_oracle and raw_rust_stage_probe")
    parser.add_argument("--color-type", choices=("rgba", "bgra", "rgb565",
                                                 "gray8", "f16", "rgba1010102"),
                        default="rgba", help="destination format for full SkCodec pixels")
    parser.add_argument("--case", type=Path, action="append", default=[])
    parser.add_argument("--reject-case", type=Path, action="append", default=[])
    parser.add_argument("--decode-reject-case", type=Path, action="append", default=[])
    args = parser.parse_args()
    if not args.case and not args.reject_case and not args.decode_reject_case:
        parser.error("at least one case is required")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.stage is not None and args.color_type != "rgba":
        parser.error("--color-type applies only to final SkCodec pixels")
    for executable in (args.reference, args.candidate):
        if not executable.is_file():
            parser.error(f"missing executable: {executable}")

    reference_route = str(args.stage) if args.stage else args.reference_route
    candidate_route = str(args.stage) if args.stage else args.candidate_route
    report = {
        "schema_version": 1,
        "reference_sha256": digest(args.reference),
        "candidate_sha256": digest(args.candidate),
        "reference_route": reference_route,
        "candidate_route": candidate_route,
        "cases": [],
    }
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for number, (source, expected) in enumerate(
            [(source, "success") for source in args.case]
            + [(source, "rejection") for source in args.reject_case]
            + [(source, "decode_rejection") for source in args.decode_reject_case]
        ):
            if not source.is_file():
                parser.error(f"missing input: {source}")
            before = probe(args.reference, reference_route, source,
                           temp / f"{number}-reference.raw", args.timeout, args.stage,
                           args.color_type)
            after = probe(args.candidate, candidate_route, source,
                          temp / f"{number}-candidate.raw", args.timeout, args.stage,
                          args.color_type)
            report["cases"].append({
                "input": str(source),
                "input_sha256": digest(source),
                "expected": expected,
                "reference": before,
                "candidate": after,
                "comparison": compare(before, after, expected),
            })

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    counts = {}
    for case in report["cases"]:
        counts[case["comparison"]] = counts.get(case["comparison"], 0) + 1
    print(json.dumps(counts, sort_keys=True))
    return 0 if set(counts).issubset({
        "matched", "matched_rejection", "matched_decode_rejection"
    }) else 1


if __name__ == "__main__":
    raise SystemExit(main())
