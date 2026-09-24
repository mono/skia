#!/usr/bin/env python3
#
# Copyright 2026 Google LLC.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest.mock import patch
import subprocess
from pathlib import Path
import tempfile

from raw_codec_probe_compare import compare, probe


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


if __name__ == "__main__":
    unittest.main()
