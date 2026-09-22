#!/usr/bin/env python3

import os
from pathlib import Path
import re
import subprocess


SKIA_ROOT = Path(__file__).resolve().parents[2]
DNG_SDK_PATH = SKIA_ROOT / "third_party" / "externals" / "dng_sdk"
ADOBE_PATCH_PATH = Path(__file__).with_name(
    "adobe-dng-sdk-1.7.1-2502-to-2724.patch"
)
SKIA_PATCH_PATH = Path(__file__).with_name(
    "skia-dng-sdk-1.7.1-2724-compat.patch"
)
ADOBE_BASE_FILES = {
    Path(__file__).with_name("adobe-1.7.1-2502-base")
    / "source"
    / "dng_file_stream.cpp": "86b20e2c53644c03a4d5e531f0129e305db2e286",
    Path(__file__).with_name("adobe-1.7.1-2502-base")
    / "source"
    / "dng_jxl.h": "cc17814319d9bb98dd73450b9a48c12fea829c4f",
}
EXPECTED_BASE_REVISION = "1238ed113a6529a5466f9fa683a3bfc3baf7cf2b"
EXPECTED_ADOBE_REVISION = "eb66e5e62169dbd6a958da0a353892f83b00a342"
EXPECTED_PATCHED_REVISION = "90fce837261ce720fb065e2a6494a1699b4cc088"


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


def commit_patch(patch_path, message, three_way=False):
    apply_args = ["apply", "--index", "--whitespace=nowarn"]
    if three_way:
        apply_args.append("--3way")
    apply_args.append(str(patch_path))
    git(*apply_args)

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
        message,
        env=commit_env,
    )


def add_adobe_base_blobs():
    for source_path, expected_hash in ADOBE_BASE_FILES.items():
        blob_hash = git(
            "hash-object",
            "-w",
            str(source_path),
            capture_output=True,
        ).stdout.strip()
        if blob_hash != expected_hash:
            raise RuntimeError(
                f"Adobe base file {source_path.name} has hash {blob_hash}, "
                f"expected {expected_hash}."
            )


def main():
    version = build_version()
    revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()

    if revision == EXPECTED_PATCHED_REVISION:
        ensure_clean()
        if version != "2724":
            raise RuntimeError("The final patched revision is not DNG SDK build 2724.")
        print("Adobe DNG SDK 1.7.1 build 2724 patches are already applied.")
        return

    if revision == EXPECTED_BASE_REVISION:
        if version != "2502":
            raise RuntimeError(
                f"Expected Adobe DNG SDK build 2502, found {version!r}."
            )
        ensure_clean()
        add_adobe_base_blobs()
        commit_patch(
            ADOBE_PATCH_PATH,
            "Apply Adobe DNG SDK 1.7.1 build 2724",
            three_way=True,
        )
        revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()
        if revision != EXPECTED_ADOBE_REVISION:
            raise RuntimeError(
                f"Expected Adobe revision {EXPECTED_ADOBE_REVISION}, "
                f"found {revision}."
            )
        if build_version() != "2724":
            raise RuntimeError("The Adobe patch did not produce build 2724.")

    elif revision != EXPECTED_ADOBE_REVISION:
        raise RuntimeError(
            f"Unexpected DNG SDK revision {revision}; expected the Android base, "
            "the Adobe update, or the final patched revision."
        )

    ensure_clean()
    commit_patch(
        SKIA_PATCH_PATH,
        "Apply Skia DNG SDK compatibility fixes",
    )

    if build_version() != "2724":
        raise RuntimeError("The compatibility patch did not retain build 2724.")

    ensure_clean()
    patched_revision = git("rev-parse", "HEAD", capture_output=True).stdout.strip()
    if patched_revision != EXPECTED_PATCHED_REVISION:
        raise RuntimeError(
            f"Expected patched revision {EXPECTED_PATCHED_REVISION}, "
            f"found {patched_revision}."
        )
    print("Applied Adobe DNG SDK 1.7.1 build 2724 patches.")


if __name__ == "__main__":
    main()
