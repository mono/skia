#!/usr/bin/env python3

import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2] / "third_party/externals/dng_sdk"
patch = Path(__file__).with_name("dng-sdk-1.7.1-2724.patch")

def git_apply(*args, **kwargs):
    return subprocess.run(
        ["git", "apply", *args, str(patch)],
        cwd=root,
        **kwargs,
    )

if git_apply(
    "--reverse",
    "--check",
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
).returncode != 0:
    git_apply("--whitespace=nowarn", check=True)
