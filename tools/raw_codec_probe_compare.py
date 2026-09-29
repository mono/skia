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
import math
from pathlib import Path
import subprocess
import tempfile
import time


PUBLIC_REQUEST_MATRIX = (
    {"name": "native-rgba", "color_type": "rgba", "scale": 1.0,
     "color_space": "inherit", "row_padding": 0, "repeat": 1},
    {"name": "srgb-padded-repeated", "color_type": "rgba", "scale": 1.0,
     "color_space": "srgb", "row_padding": 13, "repeat": 2},
    {"name": "linear-padded-repeated", "color_type": "rgba", "scale": 1.0,
     "color_space": "linear", "row_padding": 7, "repeat": 2},
    {"name": "bgra-padded", "color_type": "bgra", "scale": 1.0,
     "color_space": "inherit", "row_padding": 11, "repeat": 2},
    {"name": "rgb565-padded", "color_type": "rgb565", "scale": 1.0,
     "color_space": "inherit", "row_padding": 9, "repeat": 2},
    {"name": "f16-padded", "color_type": "f16", "scale": 1.0,
     "color_space": "inherit", "row_padding": 16, "repeat": 2},
    {"name": "half-scale", "color_type": "rgba", "scale": 0.5,
     "color_space": "inherit", "row_padding": 11, "repeat": 2},
    {"name": "memory-stream", "color_type": "rgba", "scale": 1.0,
     "color_space": "inherit", "row_padding": 11, "repeat": 2,
     "stream_mode": "memory"},
    {"name": "nonseekable-stream", "color_type": "rgba", "scale": 1.0,
     "color_space": "inherit", "row_padding": 11, "repeat": 2,
     "stream_mode": "nonseekable"},
    {"name": "short-read-stream", "color_type": "rgba", "scale": 1.0,
     "color_space": "inherit", "row_padding": 11, "repeat": 2,
     "stream_mode": "short-read"},
)


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def probe(executable, route, source, output, timeout, stage=None, color_type="rgba",
          scale=1.0, color_space="inherit", row_padding=0, repeat=1,
          stream_mode="file", subset="none", frame_index=0):
    start = time.monotonic()
    extended = stage is None and (
        scale != 1.0 or color_space != "inherit" or row_padding != 0
        or repeat != 1 or stream_mode != "file"
        or subset != "none" or frame_index != 0
    )
    try:
        command = [str(executable), route, str(source), str(output)]
        if stage is None and (color_type != "rgba" or extended):
            command.append(color_type)
        if extended:
            if scale != 1.0:
                command.append(f"--scale={scale}")
            if color_space != "inherit":
                command.append(f"--color-space={color_space}")
            if row_padding != 0:
                command.append(f"--row-padding={row_padding}")
            if repeat != 1:
                command.append(f"--repeat={repeat}")
            if stream_mode != "file":
                command.append(f"--stream={stream_mode}")
            if subset != "none":
                command.append(f"--subset={subset}")
            if frame_index != 0:
                command.append(f"--frame-index={frame_index}")
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
    if output.is_file() and (not data["created"] or data.get("decode_result") != "success"):
        result["process"] = "invalid_pixels"
        result["error"] = "pixel bytes written despite codec rejection"
        return result
    if extended and data["created"]:
        fields = ("source_width", "source_height", "scaled_width", "scaled_height")
        if (any(not isinstance(data.get(key), int) or data[key] <= 0 for key in fields)
                or data.get("scaled_width") != data.get("width")
                or data.get("scaled_height") != data.get("height")
                or not isinstance(data.get("requested_scale"), (int, float))
                or not math.isclose(data["requested_scale"], scale, rel_tol=1e-6)
                or data.get("requested_color_space") != color_space
                or not isinstance(data.get("destination_has_color_space"), bool)
                or data.get("stream_mode") != stream_mode
                or data.get("subset_mode") != subset
                or data.get("frame_index") != frame_index
                or data.get("row_padding") != row_padding
                or data.get("repeats") != repeat
                or data.get("repeats_consistent") is not True
                or data.get("padding_preserved") is not True):
            result["process"] = "invalid_output"
            result["error"] = "invalid SkCodec request metadata or repeated/padded output"
            return result
        if subset != "none":
            valid = data.get("subset_supported")
            rect = data.get("subset_rect")
            if not isinstance(valid, bool) or (
                valid and (
                    not isinstance(rect, list) or len(rect) != 4
                    or any(type(position) is not int for position in rect)
                    or data["width"] != rect[2] - rect[0]
                    or data["height"] != rect[3] - rect[1]
                )
            ) or (not valid and (rect is not None or data.get("decode_result") == "success")):
                result["process"] = "invalid_output"
                result["error"] = "inconsistent subset support or dimensions"
                return result
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
    parser.add_argument("--scale", type=float, default=1.0,
                        help="public SkCodec getScaledDimensions request in (0,1]")
    parser.add_argument("--color-space", choices=("inherit", "srgb", "linear"),
                        default="inherit", help="requested destination color space")
    parser.add_argument("--row-padding", type=int, default=0,
                        help="sentinel bytes after each active destination row")
    parser.add_argument("--repeat", type=int, default=1,
                        help="decode repeatedly through the same public codec")
    parser.add_argument("--stream", choices=("file", "memory", "nonseekable", "short-read"),
                        default="file", help="public input stream behavior")
    parser.add_argument("--subset", choices=("none", "center"), default="none",
                        help="request a centered subset through SkCodec options")
    parser.add_argument("--frame-index", type=int, default=0,
                        help="requested SkCodec frame index")
    parser.add_argument("--public-matrix", action="store_true",
                        help="compare common SkCodec output requests (use same-arch binaries "
                             "for F16); reports each request separately")
    parser.add_argument("--expect-request", action="append", default=[],
                        metavar="INPUT:REQUEST:rejection|decode_rejection",
                        help="explicit reference outcome for one --case/--public-matrix "
                             "request; INPUT must match the --case path exactly")
    parser.add_argument("--case", type=Path, action="append", default=[])
    parser.add_argument("--reject-case", type=Path, action="append", default=[])
    parser.add_argument("--decode-reject-case", type=Path, action="append", default=[])
    args = parser.parse_args()
    if not args.case and not args.reject_case and not args.decode_reject_case:
        parser.error("at least one case is required")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not math.isfinite(args.scale) or not 0 < args.scale <= 1:
        parser.error("--scale must be finite and in (0,1]")
    if args.row_padding < 0 or args.repeat <= 0 or not 0 <= args.frame_index <= 2**31 - 1:
        parser.error("row padding must be nonnegative, repeat positive, "
                     "and frame index within SkCodec's int range")
    if args.subset != "none" and (args.scale != 1.0 or args.frame_index != 0):
        parser.error("--subset cannot be combined with scaling or frame selection")
    if args.stage is not None and (
        args.color_type != "rgba" or args.scale != 1.0 or
        args.color_space != "inherit" or args.row_padding != 0 or
        args.repeat != 1 or args.stream != "file" or
        args.subset != "none" or args.frame_index != 0
    ):
        parser.error("destination and repeated/scale options apply only to final SkCodec pixels")
    if args.public_matrix and (
        args.stage is not None or args.reference_route != "public"
        or args.candidate_route != "public"
        or args.color_type != "rgba" or args.scale != 1.0
        or args.color_space != "inherit" or args.row_padding != 0
        or args.repeat != 1 or args.stream != "file"
        or args.subset != "none" or args.frame_index != 0
        or args.reject_case or args.decode_reject_case
    ):
        parser.error("--public-matrix needs both public routes and success cases, "
                     "without individual request options")
    if args.expect_request and not args.public_matrix:
        parser.error("--expect-request requires --public-matrix")
    overrides = {}
    valid_inputs = {str(source) for source in args.case}
    valid_requests = {request["name"] for request in PUBLIC_REQUEST_MATRIX}
    for item in args.expect_request:
        parts = item.rsplit(":", 2)
        if (len(parts) != 3 or parts[0] not in valid_inputs
                or parts[1] not in valid_requests
                or parts[2] not in ("rejection", "decode_rejection")
                or (parts[0], parts[1]) in overrides):
            parser.error(f"invalid or duplicate --expect-request: {item}")
        overrides[(parts[0], parts[1])] = parts[2]
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
    requests = PUBLIC_REQUEST_MATRIX if args.public_matrix else ({
        "color_type": args.color_type,
        "scale": args.scale,
        "color_space": args.color_space,
        "row_padding": args.row_padding,
        "repeat": args.repeat,
        "stream_mode": args.stream,
        "subset": args.subset,
        "frame_index": args.frame_index,
    },)
    if args.public_matrix:
        report["schema_version"] = 2
        report["request_mode"] = "public_matrix"
    if args.stage is None and (
        args.scale != 1.0 or args.color_space != "inherit" or
        args.row_padding != 0 or args.repeat != 1 or args.stream != "file"
        or args.subset != "none" or args.frame_index != 0
    ):
        report["request"] = {
            "destination": args.color_type,
            "scale": args.scale,
            "color_space": args.color_space,
            "row_padding": args.row_padding,
            "repeat": args.repeat,
            "stream_mode": args.stream,
            "subset": args.subset,
            "frame_index": args.frame_index,
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
            input_sha256 = digest(source)
            for request_index, request in enumerate(requests):
                full_request = {
                    "stream_mode": "file", "subset": "none", "frame_index": 0,
                    **request,
                }
                request_expected = overrides.get((str(source), request.get("name")), expected)
                options = {key: value for key, value in full_request.items() if key != "name"}
                before = probe(args.reference, reference_route, source,
                               temp / f"{number}-{request_index}-reference.raw",
                               args.timeout, args.stage, **options)
                after = probe(args.candidate, candidate_route, source,
                              temp / f"{number}-{request_index}-candidate.raw",
                              args.timeout, args.stage, **options)
                case = {
                    "input": str(source),
                    "input_sha256": input_sha256,
                    "expected": request_expected,
                    "reference": before,
                    "candidate": after,
                    "comparison": compare(before, after, request_expected),
                }
                if args.public_matrix:
                    case["request"] = full_request
                report["cases"].append(case)

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
