# Replace the Adobe DNG backend with an independent Rust decoder

**Status: draft, not ready to remove Adobe.** This plan describes a native
Skia-first implementation and its promotion gates. The current Rust reader is
opt-in; Adobe remains the default full-DNG decoder. Do not use the experimental
reader as evidence that arbitrary DNGs can be rendered.

## Scope and compatibility contract

- Retain PIEX and its existing JPEG-preview-first selection, including previews
  in non-DNG camera formats. PIEX replacement is a separate project.
- Replace only the Adobe full-DNG fallback. Preserve `SkRawDecoder`, `SkCodec`,
  Skia's public C ABI, SkiaSharp's managed API, dimensions, orientation,
  destination conversions, errors, and stream behavior. Do not add a per-file
  Adobe fallback to the final Rust product.
- Target the DNG behavior exposed by the configured Adobe 1.7.1.2724 Skia
  build, not every API in the unrestricted Adobe SDK and not full sensor decoding
  for CR2/NEF/ARW or other proprietary formats.
- Require **identical metadata and pixels by default**, including decoded
  destination formats. A numerical exception needs a named operation, measured
  bounds on specific fixtures/platforms, an independent explanation, and
  explicit review. A broad one-byte tolerance does not meet this gate.
- Follow the published DNG/TIFF specifications and independent implementations.
  Never copy Adobe source, rendering tables, or SDK-supplied example images into
  this implementation or the repository. Run the SDK only in a separate
  reference process; it must not be a shipping dependency or runtime fallback.
- Do native Skia implementation, tests, and review first. Update SkiaSharp's
  submodule, packaging, and managed tests **only after** native decoder proof.

## Current native architecture

```text
SkCodec / SkRawDecoder
  -> PIEX selects an existing JPEG preview? -> existing Skia JPEG codec
  -> otherwise, in an SDK-free opt-in build -> Rust DNG reader via CXX
       -> checked TIFF/IFD graph and stage readers in safe Rust
       -> Skia's existing JPEG tile decoder and zlib for relevant encodings
       -> narrow validated output -> Skia/skcms destination conversion
```

Set `skia_use_rust_raw_decode=true`, `skia_use_piex=true`, and
`skia_use_dng_sdk=false` for the **experimental** public fallback. With Adobe
enabled, public full-DNG selection still uses Adobe. With both full decoders
disabled, RAW remains PIEX-preview-only. The GN build compiles the pinned CXX
native runtime with its own C++ compiler/sanitizer settings; its Rust archive
does not bundle a second copy of Skia's C++ objects. Bazel's self-contained
Rust test target retains its normal CXX dependency.

The reader currently recognizes checked classic TIFF DNG metadata and a
limited set of uncompressed, zlib/Deflate Predictor 1/2, lossy-JPEG, and
lossless SOF3 JPEG layouts. Tested Stage 1/2/3 paths include monochrome,
RGB8, RGB16, float32 RGB, and guarded RGGB CFA; they are not all final-image
decoders. Checked lookup linearization maps each raw plane before black
subtraction. Supported final output is confined to narrow, validated 8-bit
monochrome and output-referred RGB8 sRGB profiles. Unsupported final
processing returns Unimplemented rather than approximate pixels.

Seekable streams rewind after PIEX inspection. The forward-only stream retains
short reads made during PIEX probing and hands the complete buffered input to
Rust, under the existing 100 MiB limit. PIEX still owns preview selection.

## Delivery gates

| Gate | Work and acceptance |
| --- | --- |
| P0: baseline | Pin the exact Skia/PIEX/SDK revisions and native build flags; record reference results, licensing constraints, platform targets, and the requirement to retain PIEX. |
| P1: oracle | Build isolated SDK and candidate probes. Compare codec creation and pixel-decode results separately; capture stage geometry/type, main-IFD selection, final metadata, destination formats, errors, pixels, and process failures. Maintain corpus hashes without redistributing restricted files. |
| P2: Rust integration | Build a reusable safe-Rust core and thin CXX adapter with Skia's Rust/Bazel/GN conventions. Require SDK-free linkage and prove ABI/stream ownership, errors, and opt-in behavior. |
| P3: metadata | Validate TIFF field types/counts/ranges, IFD/SubIFD/profile selection, identity and nonidentity geometry, image limits, opcodes, masks, crop, color metadata, and defaults. Reject unknown processing metadata rather than ignoring it. |
| P4: samples | Cover SDK-supported strip/tile encodings, bit depths, predictors, byte order, JPEG/JXL build-dependent behavior, and errors. Preflight compressed inputs before image-sized allocation; never publish partial success-shaped rows. Reuse Skia JPEG/zlib only where byte parity is established. |
| P5: linearization | Implement lookup tables, black/white normalization, and stage-changing opcodes with correct coordinate and per-plane semantics; prove both boundary values and whole-image stage parity. |
| P6: demosaic/geometry | Implement each admitted CFA class, stage-3 operations, crop/scaling and border behavior. Check advertised dimensions against actual decodable output. |
| P7: final render | Implement camera calibration/signatures and dual-illuminant color, exposure, profile HueSat/look maps, gain maps, tone, black rendering, masks/alpha, output-referred/HDR behavior, and final quantization in the correct order. Demonstrate exact approved final pixels or explicitly reviewed narrow exceptions. |
| P8: public integration | Keep PIEX/JPEG preview first; validate full fallback through registered `SkCodec`, not only direct Rust calls. Check seekable and forward-only input, repeated calls, output padding, metadata, destination color spaces, scaling, and malformed inputs in both Adobe and SDK-free builds. |
| P9: safety/resources | Instrument CXX, native JPEG/zlib, and Rust paths; fuzz deep parsers/decoders with valid and malformed seeds, exercise allocation/overflow/error paths, and compare measured CPU and peak/live allocations. Do not claim a memory advantage based only on selected microbenchmarks. |
| P10: review/platforms | Source-build and validate required macOS, Linux, Windows, Android and other shipping targets. Split upstreamable patches for review and retain opt-in mode until fidelity, functionality, and safety gates pass. |
| P11: promotion/removal | Select Rust for the public full-DNG fallback by default **only after P0–P10**; delete the Adobe source acquisition, adaptation patch, build/DEPS metadata, and package dependency. Retain PIEX plus shared JPEG/zlib, run SDK-free native and SkiaSharp checks, and verify no Adobe runtime/build closure remains. |

## Test strategy and current evidence

- Keep independently generated TIFF/DNG fixtures for little/big endian,
  monochrome and colorful ramps/cubes, 8/16-bit lookup tables, Deflate
  predictor/strip combinations, RGB16 preview-child SubIFDs, RGGB strips and
  cropped SOF3 tiles, and malformed ranges/streams. The source-controlled
  native tests generate their own synthetic cases. Exercise source-controlled
  Skia DNGs and rights-cleared real/camera files separately.
- The configured SDK creates codecs for three JPEG XL examples while their
  pixel decode fails in this build. A candidate rejection at codec creation
  does **not** count as equivalent decode behavior.
- Use `//experimental/rust_raw/ffi:test_tiff_parser` and `:test_raw_ffi`,
  `dm --match RustRaw Codec_raw --config 8888`, and
  `tools/raw_codec_probe_compare.py` with separate reference/candidate
  executables. A positive comparison must agree on full metadata, status, and
  every byte; keep negative cases labeled and out of the positive count.
- Current selected baseline: both Rust Bazel suites have **87 passing tests
  each on ARM64 and x64**. Adobe-enabled ARM64 native RAW has **68/68**
  selected passing tests; SDK-free PIEX+Rust has **70/70**. A source-built
  Apple ASan/UBSan SDK-free
  configuration also passes the **70** selected native tests. Apple macOS does
  not support leak detection in that ASan configuration; Rust and dependent
  JPEG/zlib deep-path sanitizer coverage still need separate verification.
- Reference-versus-candidate process comparisons match **114/114** selected
  Stage-1 and Stage-2 cases, **112/112** supported Stage-3 cases, and
  **11/11** narrowly supported final RGBA cases on ARM64 and x64. Same-arch
  public `SkCodec` comparisons match all 11 final cases in RGBA8888,
  BGRA8888, RGB565, and RGBAF16 and preserve the PIEX JPEG preview. Both
  builds explicitly reject unsupported Gray8 and RGBA1010102 conversions.
  F16 must be compared on the same architecture: ARM64/x64 Skia conversions
  can differ by one half-float ULP even with identical 8-bit source pixels.
- A JPEG IFD alone does **not** imply PIEX selected a preview. Source-generated
  uncompressed and Deflate RGB16 files with a 3x3 raw child and a larger
  128x128 JPEG parent reach the full-DNG fallback on seekable and forward-only
  streams; the SDK-free Rust route reports Unimplemented for their unverified
  final output instead of substituting the JPEG. These are atypical test
  containers, not representative camera previews.
- A macOS ARM64 source build with both Rust PNG decoding and the SDK-free
  PIEX/Rust RAW decoder passes **78/78 selected RAW/PNG native tests**.
  Public output for a generated DNG cube and two source-controlled PNG
  samples matches the single-RAW-decoder build. This tests one multi-codec
  combination, not the full platform or multi-Rust feature matrix.
- These are selected tests, **not a claim of complete SDK parity**. The real
  `sample_1mp.dng` family matches SDK Stage 1–3 but the Rust public final
  decode remains Unimplemented. Two nonzero-black Bayer fixtures differ at
  Stage 2 by one LSB in 1/256 and 15/16,384 samples; a full-range mono
  Deflate fixture differs by one LSB in 155/65,536 Stage-2/3 samples.
  Their comparison results remain explicit negatives, never passing matches.

## Promotion blockers

1. Real/official camera-profile final RGB is not implemented. The controlled
   identity-tone/BlackRender=None real DNG still has 2,465 different
   Stage-4 channel bytes out of 608,400 under a **test-only,
   non-normative** white-correction trial using an independent CIE 1960
   uv Planckian CCT (down from 3,182 with McCamy CCT). The published
   uncorrected ForwardMatrix calculation differs in 311,040 channels.
   Neither float precision nor an approximate public ProPhoto conversion
   establishes parity; no fitted constants or vendor tables are enabled.
2. Adobe's default artistic tone and automatic black behavior are not fully
   specified by DNG. On the original real file they cause material,
   multi-byte differences from the controlled variant. Profile gain/look
   application, masks, further opcode classes, CFA patterns, and output
   quantization also need complete, independently justified implementations.
3. Nonzero-black stage normalization does not yet meet the exactness gate.
   Other platform builds, deep Rust/JPEG/zlib sanitizers, required resource
   benchmarks, and complete public result/size parity remain open.
4. Do not switch SkiaSharp's submodule or remove Adobe until the native
   replacement passes all relevant gates. A draft native PR may be reviewed
   while these blockers remain; it must not be mistaken for release readiness.

No part of this plan requires copying Adobe implementation or example files.
The public reference is the
[DNG 1.7.1 specification](https://helpx.adobe.com/content/dam/help/en/photoshop/pdf/DNG_Spec_1_7_1_0.pdf);
the old SDK is used solely for separately built comparison executables.
