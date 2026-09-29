#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from raw_codec_probe_compare import PUBLIC_REQUEST_MATRIX, compare, main, probe


class RawCodecProbeCompareTest(unittest.TestCase):
    def test_valid_pixel_parity_requires_both_codecs(self):
        reference = {
            "process": "exited",
            "codec": {"route": "legacy", "created": True,
                      "create_result": "success", "decode_result": "success",
                      "width": 3, "height": 2, "origin": 1},
            "pixels_sha256": "same",
        }
        candidate = {
            "process": "exited",
            "codec": {**reference["codec"], "route": "rust"},
            "pixels_sha256": "same",
        }
        self.assertEqual(compare(reference, candidate, "success"), "matched")
        candidate["codec"] = {"created": False, "create_result": "unimplemented"}
        self.assertEqual(
            compare(reference, candidate, "success"), "missing_candidate_support"
        )

    def test_pixel_and_metadata_differences_do_not_pass(self):
        reference = {
            "process": "exited",
            "codec": {"route": "legacy", "created": True,
                      "decode_result": "success", "origin": 1},
            "pixels_sha256": "old",
        }
        candidate = {
            "process": "exited",
            "codec": {**reference["codec"], "route": "rust"},
            "pixels_sha256": "new",
        }
        self.assertEqual(compare(reference, candidate, "success"), "pixel_mismatch")
        candidate["pixels_sha256"] = "old"
        candidate["codec"]["origin"] = 2
        self.assertEqual(compare(reference, candidate, "success"), "metadata_mismatch")

    def test_rejections_must_be_explicit_and_equal(self):
        reference = {
            "process": "exited",
            "codec": {"created": False, "create_result": "invalid input"},
        }
        candidate = {
            "process": "exited",
            "codec": {"created": False, "create_result": "invalid input"},
        }
        self.assertEqual(
            compare(reference, candidate, "rejection"), "matched_rejection"
        )
        candidate["codec"]["create_result"] = "unimplemented"
        self.assertEqual(
            compare(reference, candidate, "rejection"), "rejection_mismatch"
        )
        reference["process"] = "failed"
        self.assertEqual(
            compare(reference, candidate, "rejection"), "process_failure"
        )

    def test_decode_rejections_distinguish_creation_from_pixels(self):
        reference = {
            "process": "exited",
            "codec": {"route": "public", "created": True,
                      "create_result": "success", "decode_result": "invalid conversion",
                      "width": 256, "height": 256},
        }
        candidate = {
            "process": "exited",
            "codec": {**reference["codec"], "route": "rust"},
        }
        self.assertEqual(
            compare(reference, candidate, "decode_rejection"),
            "matched_decode_rejection",
        )
        candidate["codec"]["decode_result"] = "invalid input"
        self.assertEqual(
            compare(reference, candidate, "decode_rejection"), "rejection_mismatch"
        )
        candidate["codec"]["decode_result"] = "success"
        self.assertEqual(
            compare(reference, candidate, "decode_rejection"), "unexpected_acceptance"
        )
        candidate["codec"]["created"] = False
        self.assertEqual(
            compare(reference, candidate, "decode_rejection"), "missing_candidate_support"
        )

    def test_stage_probe_checks_byte_count_and_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "stage.raw"
            output.write_bytes(b"\x00\x01")
            valid = ('{"stage":1,"width":2,"height":1,"planes":1,'
                     '"bytes_per_sample":1,"row_bytes":2,"total_bytes":2}')
            with patch("raw_codec_probe_compare.subprocess.run") as run:
                run.return_value = subprocess.CompletedProcess([], 0, valid, "")
                result = probe(Path("reference"), "1", Path("input"), output, 5, 1)
                self.assertEqual(result["process"], "exited")
                self.assertEqual(result["codec"]["decode_result"], "success")
                self.assertEqual(result["pixel_bytes"], 2)
                run.return_value = subprocess.CompletedProcess(
                    [], 0, valid.replace('"stage":1', '"stage":3'), ""
                )
                stage3 = probe(Path("reference"), "3", Path("input"), output, 5, 3)
                self.assertEqual(stage3["process"], "exited")
                self.assertEqual(stage3["codec"]["stage"], 3)
                run.return_value = subprocess.CompletedProcess(
                    [], 0, '{"stage":1,"width":2,"height":1,"total_bytes":2}', ""
                )
                self.assertEqual(
                    probe(Path("reference"), "1", Path("input"), output, 5, 1)["process"],
                    "invalid_output",
                )
                run.return_value = subprocess.CompletedProcess(
                    [], 0, '{"stage":1,"created":false}', ""
                )
                self.assertEqual(
                    probe(Path("reference"), "1", Path("input"), output, 5, 1)["process"],
                    "invalid_output",
                )

    def test_full_probe_checks_destination_shape_and_format(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "pixels.raw"
            output.write_bytes(bytes(4))
            valid = ('{"created":true,"create_result":"success",'
                     '"decode_result":"success","width":2,"height":1,'
                     '"destination":"rgb565"}')
            with patch("raw_codec_probe_compare.subprocess.run") as run:
                run.return_value = subprocess.CompletedProcess([], 0, valid, "")
                result = probe(Path("reference"), "public", Path("input"),
                               output, 5, color_type="rgb565")
                self.assertEqual(result["process"], "exited")
                self.assertEqual(result["pixel_bytes"], 4)
                self.assertEqual(run.call_args.args[0][-1], "rgb565")
                output.write_bytes(bytes(8))
                self.assertEqual(
                    probe(Path("reference"), "public", Path("input"), output,
                          5, color_type="rgb565")["process"], "invalid_pixels"
                )
                run.return_value = subprocess.CompletedProcess(
                    [], 0, valid.replace("rgb565", "bgra"), ""
                )
                self.assertEqual(
                    probe(Path("reference"), "public", Path("input"), output,
                          5, color_type="rgb565")["process"], "invalid_output"
                )
                run.return_value = subprocess.CompletedProcess(
                    [], 0, valid.replace("rgb565", "f16"), ""
                )
                output.write_bytes(bytes(16))
                self.assertEqual(
                    probe(Path("reference"), "public", Path("input"), output,
                          5, color_type="f16")["pixel_bytes"], 16
                )

    def test_public_request_matrix_checks_scale_padding_repeat_and_color_space(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "pixels.raw"
            output.write_bytes(bytes(4))
            metadata = {
                "created": True, "create_result": "success", "decode_result": "success",
                "width": 2, "height": 1, "source_width": 4, "source_height": 2,
                "scaled_width": 2, "scaled_height": 1, "requested_scale": 0.5,
                "requested_color_space": "linear", "destination_has_color_space": True,
                "row_padding": 7, "repeats": 2, "repeats_consistent": True,
                "padding_preserved": True, "destination": "rgb565",
                "stream_mode": "short-read", "subset_mode": "none",
                "frame_index": 0,
            }
            with patch("raw_codec_probe_compare.subprocess.run") as run:
                run.return_value = subprocess.CompletedProcess(
                    [], 0, json.dumps(metadata), ""
                )
                options = {
                    "color_type": "rgb565", "scale": 0.5, "color_space": "linear",
                    "row_padding": 7, "repeat": 2, "stream_mode": "short-read",
                }
                result = probe(Path("reference"), "public", Path("input"), output,
                               5, **options)
                self.assertEqual(result["pixel_bytes"], 4)
                self.assertEqual(run.call_args.args[0][-6:], [
                    "rgb565", "--scale=0.5", "--color-space=linear",
                    "--row-padding=7", "--repeat=2", "--stream=short-read",
                ])
                for invalid in (
                    {"padding_preserved": False},
                    {"repeats_consistent": False},
                    {"scaled_width": 3},
                    {"requested_color_space": "srgb"},
                    {"stream_mode": "memory"},
                    {"frame_index": 1},
                ):
                    run.return_value = subprocess.CompletedProcess(
                        [], 0, json.dumps({**metadata, **invalid}), ""
                    )
                    self.assertEqual(
                        probe(Path("reference"), "public", Path("input"),
                              output, 5, **options)["process"],
                        "invalid_output",
                    )

    def test_subset_and_frame_request_results_remain_distinct(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "pixels.raw"
            metadata = {
                "created": True, "create_result": "success",
                "decode_result": "unimplemented", "width": 4, "height": 2,
                "source_width": 4, "source_height": 2,
                "scaled_width": 4, "scaled_height": 2, "requested_scale": 1.0,
                "requested_color_space": "inherit", "destination_has_color_space": False,
                "row_padding": 0, "repeats": 1, "repeats_consistent": True,
                "padding_preserved": True, "destination": "rgba",
                "stream_mode": "file", "subset_mode": "center",
                "subset_supported": False, "frame_index": 0,
            }
            with patch("raw_codec_probe_compare.subprocess.run") as run:
                run.return_value = subprocess.CompletedProcess(
                    [], 0, json.dumps(metadata), ""
                )
                result = probe(Path("reference"), "public", Path("input"),
                               output, 5, subset="center")
                self.assertEqual(result["codec"]["decode_result"], "unimplemented")
                self.assertEqual(run.call_args.args[0][-2:], ["rgba", "--subset=center"])

                run.return_value = subprocess.CompletedProcess(
                    [], 0, json.dumps({**metadata, "subset_supported": True}), ""
                )
                self.assertEqual(
                    probe(Path("reference"), "public", Path("input"), output,
                          5, subset="center")["process"], "invalid_output"
                )
                run.return_value = subprocess.CompletedProcess(
                    [], 0, json.dumps({
                        **metadata, "decode_result": "incomplete input",
                        "subset_mode": "none", "frame_index": 1,
                    }), ""
                )
                result = probe(Path("reference"), "public", Path("input"),
                               output, 5, frame_index=1)
                self.assertEqual(result["codec"]["decode_result"], "incomplete input")
                self.assertEqual(run.call_args.args[0][-2:], ["rgba", "--frame-index=1"])

                output.write_bytes(b"unexpected")
                self.assertEqual(
                    probe(Path("reference"), "public", Path("input"), output,
                          5, frame_index=1)["process"], "invalid_pixels"
                )

    def test_public_scale_metadata_mismatch_does_not_count_as_pixel_parity(self):
        reference = {
            "process": "exited",
            "codec": {
                "route": "public", "created": True, "create_result": "success",
                "decode_result": "success", "width": 256, "height": 256,
                "scaled_width": 256, "scaled_height": 256,
            },
            "pixels_sha256": "same",
        }
        candidate = {
            "process": "exited",
            "codec": {**reference["codec"], "scaled_width": 128, "route": "public"},
            "pixels_sha256": "same",
        }
        self.assertEqual(compare(reference, candidate, "success"), "metadata_mismatch")

    def test_public_matrix_reports_each_request_and_rejects_missing_support(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reference = root / "reference"
            candidate = root / "candidate"
            source = root / "input.dng"
            report = root / "report.json"
            for path in (reference, candidate, source):
                path.write_bytes(b"test")
            args = [
                "raw_codec_probe_compare.py", "--reference", str(reference),
                "--candidate", str(candidate), "--report", str(report),
                "--reference-route", "public", "--candidate-route", "public",
                "--public-matrix", "--case", str(source),
            ]
            unsupported_f16 = False
            reference_rejects_f16 = False

            def fake_probe(executable, route, source, output, timeout, stage, **options):
                if options["color_type"] == "f16" and (
                    (unsupported_f16 and executable == candidate)
                    or (reference_rejects_f16 and executable == reference)
                ):
                    return {
                        "process": "exited",
                        "codec": {"created": False, "create_result": "unimplemented"},
                    }
                return {
                    "process": "exited",
                    "codec": {
                        "route": route, "created": True,
                        "create_result": "success", "decode_result": "success",
                        "width": 2, "height": 1, "destination": options["color_type"],
                        "row_padding": options["row_padding"],
                        "requested_scale": options["scale"],
                    },
                    "pixels_sha256": "same",
                }

            with patch("sys.argv", args), patch(
                "raw_codec_probe_compare.probe", side_effect=fake_probe
            ), redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
                cases = json.loads(report.read_text())["cases"]
                self.assertEqual(len(cases), len(PUBLIC_REQUEST_MATRIX))
                self.assertEqual([case["request"]["name"] for case in cases],
                                 [request["name"] for request in PUBLIC_REQUEST_MATRIX])
                self.assertTrue(all(case["comparison"] == "matched" for case in cases))
                self.assertTrue(all(
                    {"stream_mode", "subset", "frame_index"} <= case["request"].keys()
                    for case in cases
                ))

                unsupported_f16 = True
                self.assertEqual(main(), 1)
                cases = json.loads(report.read_text())["cases"]
                self.assertEqual(
                    [case["comparison"] for case in cases].count("missing_candidate_support"), 1
                )

                reference_rejects_f16 = True
                with patch("sys.argv", args + [
                    "--expect-request", f"{source}:f16-padded:rejection",
                ]):
                    self.assertEqual(main(), 0)
                    cases = json.loads(report.read_text())["cases"]
                    rejected = next(case for case in cases if case["request"]["name"] == "f16-padded")
                    self.assertEqual(rejected["expected"], "rejection")
                    self.assertEqual(rejected["comparison"], "matched_rejection")

            with patch("sys.argv", args + ["--stage", "1"]), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 2)
            with patch("sys.argv", args + [
                "--expect-request", f"{source}:typo:rejection",
            ]), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
