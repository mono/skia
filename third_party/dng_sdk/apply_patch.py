#!/usr/bin/env python3

import hashlib
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from pathlib import Path

URL = "https://download.adobe.com/pub/adobe/dng/dng_sdk_1_7_1_2724_20260908.zip"
SHA256 = "740fbe95c69e09e9cd17654a5e4fef2d7021254b06fd2b8c5557b79a1496b50c"
PREFIX = "dng_sdk_1_7_1/dng_sdk/source/"

skia = Path(__file__).resolve().parents[2]
root = skia / "third_party/externals/dng_sdk"
source = root / "source"
# Port the Android15 fork's Adobe 2502 changes to 2724 first, then apply
# SkiaSharp's decoder adjustments to that baseline.
patches = [
    Path(__file__).with_name("dng-sdk-1.7.1-2724-google.patch"),
    Path(__file__).with_name("dng-sdk-1.7.1-2724-skiasharp.patch"),
]

def git_apply(patch, *args, **kwargs):
    return subprocess.run(["git", "apply", *args, str(patch)], cwd=root, **kwargs)

if any(
    git_apply(patch, "--reverse", "--check", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
    for patch in reversed(patches)
):
    with tempfile.TemporaryDirectory() as temp:
        archive = Path(temp) / Path(URL).name
        urllib.request.urlretrieve(URL, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise SystemExit("Adobe DNG SDK archive checksum mismatch.")

        with zipfile.ZipFile(archive) as zip_file:
            zip_file.extractall(temp, [name for name in zip_file.namelist() if name.startswith(PREFIX)])
        shutil.rmtree(source)
        shutil.copytree(Path(temp) / PREFIX, source)

    for patch in patches:
        git_apply(patch, "--whitespace=nowarn", check=True)
