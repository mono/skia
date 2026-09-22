#!/usr/bin/env python3

import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2] / "third_party/externals/dng_sdk"
patch = Path(__file__).with_name("dng-sdk-1.7.1-2724.patch")

if subprocess.run(
    ["git", "apply", "--reverse", "--check", str(patch)],
    cwd=root,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
).returncode != 0:
    subprocess.check_call(
        ["git", "apply", "--whitespace=nowarn", str(patch)],
        cwd=root,
    )
