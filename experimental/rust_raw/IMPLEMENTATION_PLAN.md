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
- The acceptance boundary is the **public Skia codec**, not its internal Rust
  image stages. Compare identical input bytes and `SkCodec` requests in both
  builds: preview selection, creation and decode results, dimensions, origin,
  color/alpha metadata, scaled/subset requests and their results, destination
  color spaces/formats, pixels and row padding, repeated calls, and
  seekable/forward-only streams. Compare encoded-data retention where
  observable through Skia callers. Stage-1/2/3 oracle comparisons isolate
  errors but are not separately exposed through `SkCodec`. A small stage
  difference is not by itself a shipping failure if its public impact is
  explained and **all** affected public results are proved equivalent.
- Follow published DNG/TIFF specifications and existing Rust codecs. The
  maintainer has authorized a **licensed derivative** of the configured SDK
  after establishing the public A/B contract and Skia integration pattern.
  For every source-derived algorithm or value, record the exact SDK version,
  file/symbol and Rust location alongside appropriate Adobe copyright and
  license notices. Keep vendor documentation unmodified and do not redistribute
  SDK example images. Legal review must verify the final attribution and
  packaging; the SDK remains a separate-process reference, never a shipping
  runtime fallback.
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
RGB8, RGB16, float32 RGB, and four guarded 2x2 Bayer CFA phases;
they are not all final-image
decoders. Checked lookup linearization maps each raw plane before black
subtraction. Supported final output is confined to narrow, validated 8-bit
monochrome and output-referred RGB8 sRGB profiles. Unsupported final
processing returns Unimplemented rather than approximate pixels.

Rust PNG is not merely libpng translated to Rust or a linker-only swap.
`SkCodec.cpp` selects either `SkPngDecoder` or
`SkPngRustDecoder` at build time; their C++ codecs share
`SkPngCodecBase` for output handling but decode with different engines
and have documented feature differences. RAW already retains the common
`SkRawDecoder`/PIEX preview selector, but its Adobe and Rust full-DNG
paths have separate `SkCodec` implementations. Converge their
Skia-facing metadata, scaling, conversion and errors before replacing
Adobe by default; equality of internal Rust and SDK stages cannot
substitute for public `SkCodec` parity.

Seekable streams rewind after PIEX inspection. The forward-only stream retains
short reads made during PIEX probing and hands the complete buffered input to
Rust, under the existing 100 MiB limit. PIEX still owns preview selection.

## Existing codec compatibility precedents

| Codec | Skia-facing migration | What its tests actually establish |
| --- | --- | --- |
| PNG | Build-time `SkPngDecoder`/`SkPngRustDecoder` choice and shared `SkPngCodecBase` for color/output conversion. | Selected public requests compare pixels byte-for-byte. The Rust PNG README also documents APNG/CICP capability differences; universal implementation equivalence is not assumed. |
| BMP | Rust `image` decoder behind the same `SkCodec` API, with resumable reads and Skia's swizzler. | Selected full and incremental BMP outputs match the C++ decoder exactly, including checked partial rows; this is not a claim that all invalid inputs behave identically. |
| ICO | Rust directory parser delegates embedded images to Rust PNG/BMP codecs through shared `SkData` subsets. | Tests cover dimensions, frames, rewind, invalid input and partial streams; they do not establish an exhaustive legacy-versus-Rust byte-level A/B corpus. |
| JPEG | Experimental `zune-jpeg` decoder behind `SkCodec`, using Skia swizzling/color and metadata. | Its C++-versus-Rust image tests allow per-channel differences (commonly 8/255, occasionally 16/255) and may skip unsupported images. This JPEG policy is **not** an automatic DNG exception. |

RAW reuses CXX/`SkStreamAdapter` and the original PIEX-first selector,
but its Adobe and Rust full-DNG paths still have separate Skia-facing
codecs. Align the output facade and error/scale semantics, avoid redundant
input buffering, and preserve the source-backed public comparison for
each request rather than demanding identical internal Stage-1/2/3 bytes.
Final-row errors are now preserved as typed Rust/CXX statuses instead of
being folded into a boolean `kErrorInInput`; the checked direct-reader
tests cover unchanged destinations on rejected rows. GN now requests the
generated RAW CXX bridge explicitly when rebuilding the Rust archive.
Output facade, scale, and broader metadata parity remain open.

## Delivery gates

| Gate | Work and acceptance |
| --- | --- |
| P0: baseline | Pin the exact Skia/PIEX/SDK revisions and native build flags; record reference results, licensing constraints, platform targets, and the requirement to retain PIEX. |
| P1: oracle | Build isolated SDK and candidate probes. Compare codec creation and pixel-decode results separately; capture stage geometry/type, main-IFD selection, final metadata, destination formats, errors, pixels, and process failures. Maintain corpus hashes without redistributing restricted files. |
| P2: Rust integration | Build a reusable safe-Rust core and thin CXX adapter with Skia's Rust/Bazel/GN conventions. Require SDK-free linkage and prove ABI/stream ownership, errors, and opt-in behavior. |
| P3: metadata | Validate TIFF field types/counts/ranges, IFD/SubIFD/profile selection, identity and nonidentity geometry, image limits, opcodes, masks, crop, color metadata, and defaults. Reject unknown processing metadata rather than ignoring it. |
| P4: samples | Cover SDK-supported strip/tile encodings, bit depths, predictors, byte order, JPEG/JXL build-dependent behavior, and errors. Preflight compressed inputs before image-sized allocation; never publish partial success-shaped rows. Reuse Skia JPEG/zlib only where byte parity is established. |
| P5: linearization | Implement lookup tables, black/white normalization, and stage-changing opcodes with correct coordinate and per-plane semantics; compare whole-image stages diagnostically, investigate differences, and prove their impact on public output. |
| P6: demosaic/geometry | Implement each admitted CFA class, stage-3 operations, crop/scaling and border behavior. Check advertised dimensions against actual decodable output. |
| P7: final render | Implement camera calibration/signatures and dual-illuminant color, exposure, profile HueSat/look maps, gain maps, tone, black rendering, masks/alpha, output-referred/HDR behavior, and final quantization in the correct order. Demonstrate exact approved final pixels or explicitly reviewed narrow exceptions. |
| P8: public integration | Keep PIEX/JPEG preview first; validate full fallback through registered `SkCodec`, not only direct Rust calls. Check seekable and forward-only input, repeated calls, output padding, metadata, origin, observable encoded-data retention, destination color spaces/formats, scaling, subsets, and malformed inputs in both Adobe and SDK-free builds. Align the Skia-facing Adobe and Rust codec logic as PNG's shared base does where behavior truly agrees. |
| P9: safety/resources | Instrument CXX, native JPEG/zlib, and Rust paths; fuzz deep parsers/decoders with valid and malformed seeds, exercise allocation/overflow/error paths, and compare measured CPU and peak/live allocations. Do not claim a memory advantage based only on selected microbenchmarks. |
| P10: review/platforms | Source-build and validate required macOS, Linux, Windows, Android and other shipping targets. Split upstreamable patches for review and retain opt-in mode until fidelity, functionality, and safety gates pass. |
| P11: promotion/removal | Select Rust for the public full-DNG fallback by default **only after P0–P10**; delete the Adobe source acquisition, adaptation patch, build/DEPS metadata, and package dependency, while retaining the copyright/license/technology notices required by any derivative Rust implementation. Retain PIEX plus shared JPEG/zlib, run SDK-free native and SkiaSharp checks, and verify no Adobe runtime/build closure remains. |

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
- For the **public SkCodec A/B gate**, run `tools/raw_codec_probe_compare.py`
  with `--reference-route public --candidate-route public --public-matrix`
  and same-architecture binaries (required for F16). Ten requests per
  input cover RGBA/BGRA/RGB565/F16, inherited/sRGB/linear output spaces,
  half-scale requests, padded rows, repeated decodes, and file/memory/
  nonseekable/short-read streams. `--expect-request INPUT:REQUEST:rejection`
  documents a *specific* reference rejection; an undeclared rejection
  fails the comparison. Use `--subset center`, `--frame-index 1` and
  `--decode-reject-case` for separate invalid/unsupported public
  requests. A 3-input local matrix (two independently generated full
  DNGs and Skia's PIEX-preview DNG) records **29/29 exact decodes and
  one matching, explicitly declared preview short-read rejection**.
  Real Skia DNGs still yield **20/20 missing candidate support** in
  this matrix; a separate 3000x2000 Bayer half-scale request is also
  unsupported in Rust though Adobe returns a 1500x1000 image. Subset,
  frame-index, Gray8 and RGBA1010102 rejection comparisons match for
  two narrow synthetic inputs each. This tests selected requests on
  macOS ARM64, **not** the complete A/B corpus or platform matrix.
- Current selected baseline: both Rust Bazel suites have **91 passing tests
  each on ARM64 and x64**. Adobe-enabled ARM64 native RAW has **71/71** selected
  passing tests; SDK-free PIEX+Rust has **73/73** on ARM64 and x64. A
  source-built Apple ASan/UBSan SDK-free configuration also passes the
  **73** selected native tests. Apple macOS does
  not support leak detection in that ASan configuration; Rust and dependent
  JPEG/zlib deep-path sanitizer coverage still need separate verification.
- Reference-versus-candidate process comparisons match **114/114** selected
  Stage-1 and Stage-2 cases, **112/112** supported Stage-3 cases, and
  **11/11** narrowly supported final RGBA cases on ARM64 and x64 from the
  previous corpus. **Six additional, independently generated RGB8 unity
  gain-table cases** match Stages 1-3 and every final RGBA8888, BGRA8888
  and RGB565 byte on both tested Mac architectures; RGBAF16 matches
  on ARM64 against the same-architecture SDK. These six were red
  (missing candidate support) before allowing validated identity maps.
  Three additional Deflate RGB8 unity-map layouts also match all three
  stages and full RGBA8888 on ARM64 and x64. Two uncompressed non-unity
  maps (spatial and colorful gain 2) and one compressed gain-2 map
  still report missing candidate support; they are **not** counted as
  matches. Same-arch
  public `SkCodec` comparisons match all 11 final cases in RGBA8888,
  BGRA8888, RGB565, and RGBAF16 and preserve the PIEX JPEG preview. Both
  builds explicitly reject unsupported Gray8 and RGBA1010102 conversions.
  F16 must be compared on the same architecture: ARM64/x64 Skia conversions
  can differ by one half-float ULP even with identical 8-bit source pixels.
- **Six more independently generated multi-strip monochrome DNGs** match
  the SDK at Stages 1-3 and final public RGBA8888/BGRA8888/RGB565 on
  ARM64 and x64. They exercise partial last strips, output-referred
  8-bit midtones, scene-referred binary samples, checked full/short
  linearization tables, and an SDK-ignored color tone curve. ARM64
  RGBAF16 also matches the same-host SDK. All three initial cases
  returned missing candidate support before the strip gate changed;
  a scene-referred midtone in the last strip still rejects final
  rendering rather than publishing partial rows.
- **All four conventional 2x2 Bayer phases** now expose checked Stage
  1-3 under the existing zero-black/identity-processing guard.
  The 36 independently generated **BGGR/GRBG/GBRG** cases (12 per
  phase) match separate SDK processes at all three stages on both
  macOS ARM64 and SDK-free x64. Varied and constant fields, both TIFF
  byte orders, odd borders, uncompressed/Deflate strips, SOF3 tiles,
  and full/short lookup tables are included. Nine cases were red
  (missing Stage 1 and Stage 3) before the CFA generalization.
  Invalid layouts and black-normalized Bayer Stage 3 still fail
  closed; no CFA final RGB output is enabled.
- A JPEG IFD alone does **not** imply PIEX selected a preview. Source-generated
  uncompressed and Deflate RGB16 files with a 3x3 raw child and a larger
  128x128 JPEG parent reach the full-DNG fallback on seekable and forward-only
  streams; the SDK-free Rust route reports Unimplemented for their unverified
  final output instead of substituting the JPEG. These are atypical test
  containers, not representative camera previews.
- A macOS ARM64 source build with both Rust PNG decoding and the SDK-free
  PIEX/Rust RAW decoder passes **111/111 selected RAW/PNG native tests**
  (a wider selection than the previously recorded 78).
  Public output for a generated DNG cube and two source-controlled PNG
  samples matches the single-RAW-decoder build. This tests one multi-codec
  combination, not the full platform or multi-Rust feature matrix.
- A network-isolated **Linux ARM64** container compiled the pure-Rust
  standalone DNG parser with Rust 1.89 and `-Dwarnings`, passing **91/91**
  tests. This does **not** build or exercise the CXX bridge, Skia codec,
  native JPEG/zlib or public RAW selection on Linux. A macOS-host Bazel
  cross-build cannot resolve registered Linux Rust/C++ toolchains; full
  native Linux testing still needs a provisioned Linux build host.
- Under the approved derivative policy, the first SDK-sourced diagnostic
  ported into Rust tests was the 31-row Robertson CIE 1960
  uv temperature table and its temperature interpolation. Its Rust
  comments cite the SDK version/file/symbol and Adobe copyright;
  [PROVENANCE.md](PROVENANCE.md) records the use and unmodified
  Adobe notices are in `licenses/`. At that checkpoint both Rust Bazel
  suites passed **91/91 on ARM64 and x64**, and Linux ARM64 standalone
  parser tests passed **91/91**. The production final renderer remains
  unchanged.
- SDK-derived RGB extrema/interpolation and the 4,096-entry tone lookup
  now have per-definition attribution in test-only `tone.rs` and entries
  in [PROVENANCE.md](PROVENANCE.md). A private comparison against an
  independently generated three-point profile matched all **768
  neutral Stage-4 bytes**. For that same DNG's colored pixels, Adobe's
  public decoder succeeds and Rust still reports Unimplemented; this
  diagnostic does **not** establish final-color parity or enable it.
  Both Rust Bazel suites pass **93/93 on macOS ARM64 and x64** with
  the two new tone tests; the earlier Linux ARM64 standalone result
  predates them.
- The SDK's pinned `IlluminantToTemperature` assigns **2850 K** to
  Standard Light A, rather than the nominal 2856 K used by an earlier
  private trial. With source-attributed, test-only dual-illuminant
  white, ProPhoto/sRGB matrix and transfer behavior, a separately
  generated identity-tone/BlackRender=None version of
  `sample_1mp.dng` matches the SDK's **1,216,800 Stage-3 bytes**
  and **608,400 Stage-4 channel bytes** exactly on macOS ARM64.
  `tools/raw_dng_controlled_fixture.py` and
  `tools/raw_dng_color_compare.py` reproduce this with the explicit
  test-only Rust `//experimental/rust_raw/ffi:color_probe` target.
  The candidate public `SkCodec` still rejects this real DNG.
- An SDK-free Apple ASan/UBSan native fuzzer with opt-in 8-bit coverage of the
  CXX RAW bridge and harness replayed generated DNG/Skia seeds and completed
  **8,000** short mutations without sanitizer failures. It reached **168
  out of 900** native coverage counters; Rust parser and bundled JPEG/zlib
  internals are not coverage-guided. Leak detection is unavailable in this
  macOS ASan runtime. A separate seven-seed, 200-run gain-map corpus
  passed with 67/900 shallow native counters; a six-seed, 500-run
  multi-strip mono corpus passed with 63/900; an 18-seed Bayer-phase
  corpus ran 1,000 mutations with 86/900 shallow counters. None
  closes P9.
  Keep deeper instrumentation and longer runs in P9.
- These are selected tests, **not a claim of complete SDK parity**. The real
  `sample_1mp.dng` family matches SDK Stage 1–3 but the Rust public final
  decode remains Unimplemented. Two nonzero-black Bayer fixtures differ at
  Stage 2 by one LSB in 1/256 and 15/16,384 samples; a full-range mono
  Deflate fixture differs by one LSB in 155/65,536 Stage-2/3 samples.
  Their comparison results remain explicit negatives, never passing matches.

## Promotion blockers

1. Real/official camera-profile **public** final RGB is not implemented.
   An earlier controlled identity-tone/BlackRender=None real DNG had 2,465 different
   Stage-4 channel bytes out of 608,400 under a **test-only,
   non-normative** white-correction trial using an independent CIE 1960
   uv Planckian CCT (down from 3,182 with McCamy CCT). The published
   uncorrected ForwardMatrix calculation differs in 311,040 channels.
   Neither float precision nor an approximate public ProPhoto conversion
   establishes parity. A subsequent **session-private, licensed
   source-attributed diagnostic** using SDK-style temperature, PCS matrix
   normalization, camera-white/ProPhoto clipping and transfer-table
   quantization reduced that difference to **1,104 one-byte channels**
   (R/G/B 39/213/852). Correcting the SDK Standard Light A constant
   from 2856 to 2850 K now makes the **test-only** Rust Stage-4
   output exact for this controlled real DNG. This is **not public
   SkCodec parity**, and no SDK-derived color renderer is enabled in
   the production FFI or public codec.
2. Adobe's default artistic tone and automatic black behavior are not fully
   specified by DNG. On the original real file they cause material,
   multi-byte differences from the controlled variant. Non-unity profile gain/look
   application, masks, further opcode classes, non-2x2 CFA patterns, and output
   quantization also need complete, independently justified implementations.
3. Nonzero-black stage normalization has one-LSB differences in selected
   diagnostic samples. Those are **not independently a public API failure**,
   but final black-normalized Bayer rendering is unavailable, so their effect
   on the public pixels cannot yet be dismissed. Other platform builds,
   deep Rust/JPEG/zlib sanitizers, required resource benchmarks, and
   complete public result/size parity remain open.
4. Do not switch SkiaSharp's submodule or remove Adobe until the native
   replacement passes all relevant gates. A draft native PR may be reviewed
   while these blockers remain; it must not be mistaken for release readiness.

### Rendering-default decision before promotion

The DNG specification does not define Adobe's artistic fallback tone curve
when `ProfileToneCurve` is absent, nor an exact algorithm for
`DefaultBlackRender=Auto`. On a controlled derivative of Skia's real DNG,
changing only the default tone (with black rendering fixed to None) changes
the configured SDK output by a mean **28.06** and a maximum **65** bytes;
changing only Auto black (with identity tone) changes it by a mean **2.54**
and a maximum **16** bytes. These are not narrow quantization exceptions.

The current decision is to keep the **exact-output gate** and leave Adobe
as the default until an independently justified equivalent exists.
If that gate cannot be met without copying licensed implementation data,
maintainers must explicitly approve a *different* independent rendering
policy and its documented visual migration before P10/P11; merely widening
a comparison tolerance or treating previews as full DNGs is not a solution.
Continue stage/format, safety and platform work while this decision is open,
but do not silently promote an inexact full-DNG renderer.

Do not copy SDK example files into this repository. The approved licensed
derivative permits attributed, notice-preserving SDK-derived algorithms and
values in Rust; see [PROVENANCE.md](PROVENANCE.md). The public format reference is the
[DNG 1.7.1 specification](https://helpx.adobe.com/content/dam/help/en/photoshop/pdf/DNG_Spec_1_7_1_0.pdf);
the pinned SDK is used both as a separately built comparison oracle and
as an attributed implementation reference while the replacement is developed.
