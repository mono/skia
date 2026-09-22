#!/usr/bin/env python3

from pathlib import Path
import re
import subprocess


SKIA_ROOT = Path(__file__).resolve().parents[2]
DNG_SDK_PATH = SKIA_ROOT / "third_party" / "externals" / "dng_sdk"
PATCH_PATH = Path(__file__).with_name("dng-sdk-1.7.1-2724.patch")
EXPECTED_BASE_REVISION = "1238ed113a6529a5466f9fa683a3bfc3baf7cf2b"


def git(*args, capture_output=False):
    return subprocess.run(
        ["git", *args],
        cwd=DNG_SDK_PATH,
        check=True,
        text=True,
        capture_output=capture_output,
    )


def build_version():
    flags = (DNG_SDK_PATH / "source" / "dng_flags.h").read_text()
    match = re.search(
        r"^#define kDNGSDK_BuildVersion (\d+)\s*$",
        flags,
        re.MULTILINE,
    )
    return match.group(1) if match else ""


def main():
    version = build_version()

    if version == "2724":
        git("apply", "--reverse", "--check", str(PATCH_PATH))
        print("Adobe DNG SDK 1.7.1 build 2724 patch is already applied.")
        return

    if version != "2502":
        raise RuntimeError(
            f"Expected Adobe DNG SDK build 2502 before patching, found {version!r}."
        )

    revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()
    if revision != EXPECTED_BASE_REVISION:
        raise RuntimeError(
            f"Expected DNG SDK revision {EXPECTED_BASE_REVISION}, found {revision}."
        )

    if git("status", "--porcelain", capture_output=True).stdout:
        raise RuntimeError("The DNG SDK checkout contains local changes.")

    git("apply", "--check", str(PATCH_PATH))
    git("apply", "--whitespace=nowarn", str(PATCH_PATH))

    if build_version() != "2724":
        raise RuntimeError("The DNG SDK patch did not produce build 2724.")

    print("Applied Adobe DNG SDK 1.7.1 build 2724 patch.")


if __name__ == "__main__":
    main()
