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
patch = Path(__file__).with_name("dng-sdk-1.7.1-2724-adaptations.patch")
archive = Path.home() / ".skiasharp/cache/dng_sdk" / Path(URL).name

def git_apply(*args, **kwargs):
    return subprocess.run(["git", "apply", *args, str(patch)], cwd=root, **kwargs)

def digest():
    return hashlib.sha256(archive.read_bytes()).hexdigest()

if git_apply("--reverse", "--check", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists() or digest() != SHA256:
        urllib.request.urlretrieve(URL, archive)
    if digest() != SHA256:
        raise SystemExit("Adobe DNG SDK archive checksum mismatch.")

    with tempfile.TemporaryDirectory() as temp:
        with zipfile.ZipFile(archive) as zip_file:
            zip_file.extractall(temp, [name for name in zip_file.namelist() if name.startswith(PREFIX)])
        shutil.rmtree(source)
        shutil.copytree(Path(temp) / PREFIX, source)

    git_apply("--whitespace=nowarn", check=True)
