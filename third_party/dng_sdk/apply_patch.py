#!/usr/bin/env python3

import os
from pathlib import Path
import re
import subprocess


SKIA_ROOT = Path(__file__).resolve().parents[2]
DNG_SDK_PATH = SKIA_ROOT / "third_party" / "externals" / "dng_sdk"
PATCH_PATH = Path(__file__).with_name("dng-sdk-1.7.1-2724.patch")
EXPECTED_BASE_REVISION = "1238ed113a6529a5466f9fa683a3bfc3baf7cf2b"
EXPECTED_PATCHED_REVISION = "6d4305596597681f0babcb36047453567f710402"


def git(*args, capture_output=False, env=None):
    return subprocess.run(
        ["git", *args],
        cwd=DNG_SDK_PATH,
        check=True,
        env=env,
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


def ensure_clean():
    status = git("status", "--porcelain", capture_output=True).stdout
    if status:
        raise RuntimeError("The DNG SDK checkout contains local changes.")


def main():
    version = build_version()
    revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()

    if version == "2724":
        ensure_clean()
        if revision != EXPECTED_PATCHED_REVISION:
            raise RuntimeError(
                "DNG SDK build 2724 does not match the expected patched revision."
            )
        print("Adobe DNG SDK 1.7.1 build 2724 patch is already applied.")
        return

    if version != "2502":
        raise RuntimeError(
            f"Expected Adobe DNG SDK build 2502 before patching, found {version!r}."
        )

    if revision != EXPECTED_BASE_REVISION:
        raise RuntimeError(
            f"Expected DNG SDK revision {EXPECTED_BASE_REVISION}, found {revision}."
        )

    ensure_clean()
    git("apply", "--index", "--whitespace=nowarn", str(PATCH_PATH))

    commit_env = os.environ.copy()
    commit_env.update(
        {
            "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z",
            "GIT_AUTHOR_EMAIL": "dependency-sync@skia.org",
            "GIT_AUTHOR_NAME": "Skia Dependency Sync",
            "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z",
            "GIT_COMMITTER_EMAIL": "dependency-sync@skia.org",
            "GIT_COMMITTER_NAME": "Skia Dependency Sync",
        }
    )
    git(
        "commit",
        "--quiet",
        "--no-gpg-sign",
        "-m",
        "Apply Skia dependency patches",
        env=commit_env,
    )

    if build_version() != "2724":
        raise RuntimeError("The DNG SDK patch did not produce build 2724.")

    ensure_clean()
    patched_revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()
    if patched_revision != EXPECTED_PATCHED_REVISION:
        raise RuntimeError(
            f"Expected patched revision {EXPECTED_PATCHED_REVISION}, "
            f"found {patched_revision}."
        )
    print("Applied Adobe DNG SDK 1.7.1 build 2724 patch.")


if __name__ == "__main__":
    main()
