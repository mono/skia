#!/usr/bin/env python3
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[2] / "third_party/externals/dng_sdk"
patch = Path(__file__).with_name("dng-sdk-1.7.1-2724.patch")
def check(*args): return subprocess.run(["git", "apply", *args, str(patch)], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
if check("--check"):
    subprocess.check_call(["git", "apply", "--whitespace=nowarn", str(patch)], cwd=root)
elif not check("--reverse", "--check"):
    raise SystemExit("DNG SDK patch cannot be applied or verified.")
