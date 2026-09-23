#!/usr/bin/env python3
"""Assemble a single-arch Apple .framework bundle around a GN/ninja-linked dylib.

Run as a post-link GN `action` (see the skiasharp_build template in
//gn/BUILDCONFIG.gn): it turns the bare lib<Name>.dylib ninja produced into a
ready-to-use lib<Name>.framework in the out dir, so the managed/packaging layer
only has to lipo the per-arch frameworks together and code-sign - it no longer
owns the bundle shape, Info.plist or install_name.

A framework is just a Mach-O dylib + Info.plist + code signature in a bundle
directory. The dylib's install_name is already framework-relative (set at link
time by GN). This script lays out the bundle, copies the dylib in as the
(extension-less) framework binary, thins off GN's force-added arm64e slice, and
reproduces the Info.plist that Xcode's PROCESS_INFOPLIST build phase emits -
including the build-provenance keys (DT*/BuildMachineOSBuild) that clang/ld never
emit but that Apple's App Store / notarization asset validation expects on
embedded frameworks.

Everything here uses first-party Apple command-line tools only:
  - lipo        : thin GN's extra arm64e slice
  - xcrun       : SDK version/build (DTSDKName/DTSDKBuild/DTPlatformVersion)
  - xcodebuild  : Xcode version/build (DTXcode/DTXcodeBuild)
  - sw_vers     : build machine OS build (BuildMachineOSBuild)
There is no public Apple CLI that auto-injects the DT* keys (Xcode does it via an
internal, unexported build tool), so the values are queried from the tools above
exactly as Xcode does. The plist itself is written with the standard-library
plistlib. Code signing is intentionally NOT done here: the packaging layer lipos
additional arch slices into the binary afterwards (which would invalidate a
signature), so it signs last.

GN's `.gn` pins `script_executable = "python3"` (Skia's global convention), which
is why this is a Python script rather than a shell script.
"""

import argparse
import os
import plistlib
import shutil
import subprocess
import sys


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, help="the GN-linked dylib")
    parser.add_argument("--output", required=True, help="the lib<Name>.framework directory")
    parser.add_argument("--name", required=True, help="framework name incl. lib prefix, e.g. libSkiaSharp")
    parser.add_argument("--os", required=True, help="ios | tvos | maccatalyst")
    parser.add_argument("--arch", required=True, help="mach-o arch to keep: arm64 | x86_64")
    parser.add_argument("--sim", default="0", help="1 when building for a simulator SDK")
    parser.add_argument("--min-os", dest="min_os", default="", help="deployment target for the plist min-version key")
    args = parser.parse_args()

    is_sim = args.sim == "1"

    # Derive the bundle's platform metadata from the SDK this slice was built for. A
    # single GN invocation targets exactly one OS + device/simulator SDK, so these are
    # unambiguous here; the packaging layer later picks the correct per-SDK framework
    # as the base when fusing device + simulator slices. "versioned" (the macOS-style
    # Versions/A layout) and the Catalyst-specific plist keys follow directly from the
    # OS, so they are not passed in separately.
    versioned = False
    catalyst = False
    plist_min = args.min_os
    if args.os == "ios":
        sdk = "iphonesimulator" if is_sim else "iphoneos"
        supported = "iPhoneSimulator" if is_sim else "iPhoneOS"
        device_family = [1, 2]
    elif args.os == "tvos":
        sdk = "appletvsimulator" if is_sim else "appletvos"
        supported = "AppleTVSimulator" if is_sim else "AppleTVOS"
        device_family = [3]
    elif args.os == "maccatalyst":
        sdk = "macosx"
        supported = "MacOSX"
        device_family = [2]
        versioned = True
        catalyst = True
        # Mac Catalyst records the macOS-equivalent minimum (LSMinimumSystemVersion),
        # not the iOS deployment target the compiler uses.
        plist_min = "10.15"
    else:
        sys.exit(f"unknown os: {args.os}")

    identifier = f"com.microsoft.{args.name}"

    # --- bundle layout -------------------------------------------------------
    if os.path.islink(args.output) or os.path.isfile(args.output):
        os.remove(args.output)
    elif os.path.isdir(args.output):
        shutil.rmtree(args.output)

    if versioned:
        bin_dir = os.path.join(args.output, "Versions", "A")
        res_dir = os.path.join(args.output, "Versions", "A", "Resources")
    else:
        bin_dir = args.output
        res_dir = args.output
    os.makedirs(bin_dir, exist_ok=True)
    os.makedirs(res_dir, exist_ok=True)

    framework_binary = os.path.join(bin_dir, args.name)
    shutil.copyfile(args.binary, framework_binary)
    shutil.copymode(args.binary, framework_binary)

    # GN's is_ios config force-adds an "arm64e" slice for non-simulator arm64 builds;
    # the shipped frameworks carry only the plain requested arch, so thin to it.
    archs = run(["lipo", "-archs", framework_binary]).split()
    if len(archs) > 1:
        thin_tmp = framework_binary + ".thin"
        run(["lipo", framework_binary, "-thin", args.arch, "-output", thin_tmp])
        os.replace(thin_tmp, framework_binary)

    if versioned:
        os.symlink("A", os.path.join(args.output, "Versions", "Current"))
        os.symlink(os.path.join("Versions", "Current", args.name), os.path.join(args.output, args.name))
        os.symlink(os.path.join("Versions", "Current", "Resources"), os.path.join(args.output, "Resources"))

    # --- Info.plist ----------------------------------------------------------
    # Apple toolchain provenance, queried the same way Xcode populates these keys.
    sdk_version = run(["xcrun", "--sdk", sdk, "--show-sdk-version"]).strip()
    sdk_build = run(["xcrun", "--sdk", sdk, "--show-sdk-build-version"]).strip()
    build_machine = run(["sw_vers", "-buildVersion"]).strip()

    # xcodebuild prints e.g. "Xcode 26.3" then "Build version 17C529".
    xcodebuild_lines = run(["xcodebuild", "-version"]).splitlines()
    xcode_version = xcodebuild_lines[0].replace("Xcode ", "").strip()
    dtxcode_build = xcodebuild_lines[1].replace("Build version ", "").strip()
    # Xcode encodes its version as MMmp (15.4.0 -> 1540, 26.3.0 -> 2630), zero-padded
    # to at least four characters.
    parts = (xcode_version.split(".") + ["0", "0", "0"])[:3]
    xc_major, xc_minor, xc_patch = (int(p) for p in parts)
    dtxcode = f"{xc_major * 100 + xc_minor * 10 + xc_patch:04d}"

    info = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": args.name,
        "CFBundleIdentifier": identifier,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": args.name,
        "CFBundlePackageType": "FMWK",
        "CFBundleShortVersionString": "1.0",
        "CFBundleSignature": "????",
        "CFBundleSupportedPlatforms": [supported],
        "CFBundleVersion": "1",
        # Build-provenance keys (what Xcode's PROCESS_INFOPLIST injects).
        "BuildMachineOSBuild": build_machine,
        "DTCompiler": "com.apple.compilers.llvm.clang.1_0",
        "DTPlatformBuild": sdk_build,
        "DTPlatformName": sdk,
        "DTPlatformVersion": sdk_version,
        "DTSDKBuild": sdk_build,
        "DTSDKName": f"{sdk}{sdk_version}",
        "DTXcode": dtxcode,
        "DTXcodeBuild": dtxcode_build,
        "UIDeviceFamily": device_family,
    }
    if catalyst:
        info["LSMinimumSystemVersion"] = plist_min
    else:
        info["MinimumOSVersion"] = plist_min

    with open(os.path.join(res_dir, "Info.plist"), "wb") as f:
        plistlib.dump(info, f, fmt=plistlib.FMT_BINARY)


if __name__ == "__main__":
    main()
