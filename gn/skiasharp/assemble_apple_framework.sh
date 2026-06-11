#!/bin/bash
# Assemble a single-arch Apple .framework bundle around a GN/ninja-linked dynamic
# library, as a post-link step of the GN build (see the skiasharp_build template in
# //gn/BUILDCONFIG.gn and skiasharp_apple_framework). This makes the GN out dir
# contain a ready-to-use lib<Name>.framework instead of a bare dylib, so the
# managed/packaging layer only has to lipo the per-arch frameworks together and
# code-sign - it no longer owns the bundle shape, Info.plist or install_name.
#
# A framework is just a Mach-O dylib + Info.plist + code signature in a bundle
# directory. The dylib's install_name is already framework-relative (set at link
# time by GN). This script lays out the bundle, copies the dylib in as the
# (extension-less) framework binary, thins off GN's force-added arm64e slice, and
# reproduces the Info.plist that Xcode's PROCESS_INFOPLIST build phase emitted -
# including the build-provenance keys (DT*/BuildMachineOSBuild) that clang/ld never
# emit but that Apple's App Store / notarization asset validation expects on
# embedded frameworks.
#
# Everything here uses first-party Apple command-line tools only:
#   - lipo        : thin GN's extra arm64e slice
#   - plutil      : create/populate the (binary1) Info.plist
#   - xcrun       : SDK version/build (DTSDKName/DTSDKBuild/DTPlatformVersion)
#   - xcodebuild  : Xcode version/build (DTXcode/DTXcodeBuild)
#   - sw_vers     : build machine OS build (BuildMachineOSBuild)
# There is no public Apple CLI that auto-injects the DT* keys (Xcode does it via an
# internal, unexported build tool), so the values are queried from the tools above
# exactly as Xcode does. Code signing is intentionally NOT done here: the packaging
# layer lipos additional arch slices into the binary afterwards (which would
# invalidate a signature), so it signs last.

set -euo pipefail

BINARY=""        # input: the GN-linked dylib
OUTPUT=""        # output: the lib<Name>.framework directory
NAME=""          # framework name incl. lib prefix, e.g. libSkiaSharp
OS=""            # ios | tvos | maccatalyst
ARCH=""          # mach-o arch to keep: arm64 | x86_64
SIM=0            # 1 when building for a simulator SDK
MIN_OS=""        # deployment target used for the plist min-version key

while [[ $# -gt 0 ]]; do
    case "$1" in
        --binary) BINARY="$2"; shift 2;;
        --output) OUTPUT="$2"; shift 2;;
        --name) NAME="$2"; shift 2;;
        --os) OS="$2"; shift 2;;
        --arch) ARCH="$2"; shift 2;;
        --sim) SIM="$2"; shift 2;;
        --min-os) MIN_OS="$2"; shift 2;;
        *) echo "unknown argument: $1" >&2; exit 1;;
    esac
done

# Derive the bundle's platform metadata from the SDK this slice was built for. A
# single GN invocation targets exactly one OS + device/simulator SDK, so these are
# unambiguous here; the packaging layer later picks the correct per-SDK framework as
# the base when fusing device + simulator slices.
VERSIONED=0
CATALYST=0
PLIST_MIN="$MIN_OS"
case "$OS" in
    ios)
        if [[ "$SIM" == "1" ]]; then SDK="iphonesimulator"; SUPPORTED="iPhoneSimulator"; else SDK="iphoneos"; SUPPORTED="iPhoneOS"; fi
        DEVICE_FAMILY=(1 2);;
    tvos)
        if [[ "$SIM" == "1" ]]; then SDK="appletvsimulator"; SUPPORTED="AppleTVSimulator"; else SDK="appletvos"; SUPPORTED="AppleTVOS"; fi
        DEVICE_FAMILY=(3);;
    maccatalyst)
        SDK="macosx"; SUPPORTED="MacOSX"; DEVICE_FAMILY=(2); VERSIONED=1; CATALYST=1
        # Mac Catalyst records the macOS-equivalent minimum (LSMinimumSystemVersion),
        # not the iOS deployment target the compiler uses.
        PLIST_MIN="10.15";;
    *) echo "unknown os: $OS" >&2; exit 1;;
esac

IDENTIFIER="com.microsoft.$NAME"

# --- bundle layout -----------------------------------------------------------
rm -rf "$OUTPUT"
if [[ "$VERSIONED" == "1" ]]; then
    BIN_DIR="$OUTPUT/Versions/A"
    RES_DIR="$OUTPUT/Versions/A/Resources"
else
    BIN_DIR="$OUTPUT"
    RES_DIR="$OUTPUT"
fi
mkdir -p "$BIN_DIR" "$RES_DIR"

FRAMEWORK_BINARY="$BIN_DIR/$NAME"
cp "$BINARY" "$FRAMEWORK_BINARY"

# GN's is_ios config force-adds an "arm64e" slice for non-simulator arm64 builds; the
# shipped frameworks carry only the plain requested arch, so thin to it.
if [[ "$(lipo -archs "$FRAMEWORK_BINARY" | wc -w | tr -d ' ')" -gt 1 ]]; then
    THIN_TMP="$FRAMEWORK_BINARY.thin"
    lipo "$FRAMEWORK_BINARY" -thin "$ARCH" -output "$THIN_TMP"
    mv -f "$THIN_TMP" "$FRAMEWORK_BINARY"
fi

if [[ "$VERSIONED" == "1" ]]; then
    ln -sfh A "$OUTPUT/Versions/Current"
    ln -sfh "Versions/Current/$NAME" "$OUTPUT/$NAME"
    ln -sfh "Versions/Current/Resources" "$OUTPUT/Resources"
fi

# --- Info.plist --------------------------------------------------------------
PLIST="$RES_DIR/Info.plist"

# Apple toolchain provenance, queried the same way Xcode populates these keys.
SDK_VERSION="$(xcrun --sdk "$SDK" --show-sdk-version)"
SDK_BUILD="$(xcrun --sdk "$SDK" --show-sdk-build-version)"
BUILD_MACHINE="$(sw_vers -buildVersion)"

# xcodebuild prints e.g. "Xcode 26.3" then "Build version 17C529".
XCODE_VERSION="$(xcodebuild -version | sed -n '1s/Xcode //p')"
DTXCODE_BUILD="$(xcodebuild -version | sed -n '2s/Build version //p')"
# Xcode encodes its version as MMmp (15.4.0 -> 1540, 26.3.0 -> 2630), zero-padded to
# at least four characters.
IFS='.' read -ra _xc <<< "$XCODE_VERSION"
XC_MAJOR="${_xc[0]:-0}"; XC_MINOR="${_xc[1]:-0}"; XC_PATCH="${_xc[2]:-0}"
DTXCODE="$(printf '%04d' $(( 10#$XC_MAJOR * 100 + 10#$XC_MINOR * 10 + 10#$XC_PATCH )))"

rm -f "$PLIST"
plutil -create binary1 "$PLIST"

plutil -insert CFBundleDevelopmentRegion -string "en" "$PLIST"
plutil -insert CFBundleExecutable -string "$NAME" "$PLIST"
plutil -insert CFBundleIdentifier -string "$IDENTIFIER" "$PLIST"
plutil -insert CFBundleInfoDictionaryVersion -string "6.0" "$PLIST"
plutil -insert CFBundleName -string "$NAME" "$PLIST"
plutil -insert CFBundlePackageType -string "FMWK" "$PLIST"
plutil -insert CFBundleShortVersionString -string "1.0" "$PLIST"
plutil -insert CFBundleSignature -string "????" "$PLIST"
plutil -insert CFBundleSupportedPlatforms -json "[\"$SUPPORTED\"]" "$PLIST"
plutil -insert CFBundleVersion -string "1" "$PLIST"

# Build-provenance keys (what Xcode's PROCESS_INFOPLIST injects).
plutil -insert BuildMachineOSBuild -string "$BUILD_MACHINE" "$PLIST"
plutil -insert DTCompiler -string "com.apple.compilers.llvm.clang.1_0" "$PLIST"
plutil -insert DTPlatformBuild -string "$SDK_BUILD" "$PLIST"
plutil -insert DTPlatformName -string "$SDK" "$PLIST"
plutil -insert DTPlatformVersion -string "$SDK_VERSION" "$PLIST"
plutil -insert DTSDKBuild -string "$SDK_BUILD" "$PLIST"
plutil -insert DTSDKName -string "${SDK}${SDK_VERSION}" "$PLIST"
plutil -insert DTXcode -string "$DTXCODE" "$PLIST"
plutil -insert DTXcodeBuild -string "$DTXCODE_BUILD" "$PLIST"

if [[ "$CATALYST" == "1" ]]; then
    plutil -insert LSMinimumSystemVersion -string "$PLIST_MIN" "$PLIST"
else
    plutil -insert MinimumOSVersion -string "$PLIST_MIN" "$PLIST"
fi

family_json="[$(printf '%s,' "${DEVICE_FAMILY[@]}" | sed 's/,$//')]"
plutil -insert UIDeviceFamily -json "$family_json" "$PLIST"
