# Experimental Rust DNG decoder

See [the implementation and removal gates](IMPLEMENTATION_PLAN.md) before
changing RAW selection or deleting the Adobe SDK.

This vertical slice is **not a replacement** for Skia's production RAW
selection. The existing Adobe full-image fallback and PIEX JPEG-preview-first
behavior remain unchanged in Adobe-enabled builds. With the off-by-default
`skia_use_rust_raw_decode` flag enabled and `skia_use_dng_sdk` disabled,
PIEX-enabled builds use the Rust decoder for the narrowly verified full-DNG
profiles only, after PIEX has declined a usable JPEG preview. Unsupported
full DNGs return Unimplemented; there is no Adobe runtime fallback in the
SDK-free build. With both flags off, RAW remains preview-only. The Rust
build define is `SK_CODEC_DECODES_RAW_WITH_RUST`.

Tests and experimental callers use
`experimental/rust_raw/decoder/SkRawRustDecoder.h` and
`SkRawRustDecoder::Decode(stream, result)`. Local Bazel targets are
`//experimental/rust_raw/ffi:{ffi_rs,cxx_bridge,deflate_strip_decoder,jpeg_tile_decoder}` and
`//experimental/rust_raw/decoder:rust_raw_decoder`.
The SDK-free public RAW fallback reuses the stream after PIEX inspection:
seekable input is rewound, while forward-only input retains every short-read
byte before buffering the remainder under Skia's existing 100 MiB limit.
Source-generated public `SkCodec` tests compare exact final pixels for the
supported mono8/RGB8 profiles and JPEG preview bytes for both stream kinds.
This opt-in route does not establish real or official DNG final-color parity.
Separate-process Adobe/Rust public comparisons match all 11 selected full
DNGs in RGBA8888, BGRA8888, RGB565 and RGBAF16 on each tested Mac
architecture, and both reject Gray8 and RGBA1010102 pixel conversion in
the same way. F16 output must be compared against an SDK build for the
same architecture: ARM64 and x64 Skia differ by one half-float ULP for some
midtones even when their decoded 8-bit RGB agrees.

The separate-process `raw_codec_probe_compare.py --public-matrix` now tests
ten public `SkCodec` requests per file: several destination formats and
color spaces, half-scale, repeated/padded output and
seekable/forward-only/short-read input. A selected ARM64 matrix matches
29 supported output requests on two synthetic full DNGs and the existing
PIEX preview; one short-read preview request is explicitly declared a
matching *creation rejection*, never counted as decoded pixels.
The same matrix reports 20 missing Rust full-decode cases on the real
Skia DNGs, and a scalable Bayer DNG is also a missing candidate case.
`--subset center` and `--frame-index 1` check public rejection results
separately. This A/B gate is incomplete until real full-DNG images and
all shipping request/platform combinations pass.

The checked stage-1 profile is classic little- or big-endian TIFF DNG 1.4:
one full-size monochrome LinearRaw IFD, uncompressed chunky 8- or 16-bit
unsigned samples in one or more strips, identity orientation/crop/scale,
and optional scalar WhiteLevel and up-to-8-by-8 repeating BlackLevel.
The internal Rust bridge exposes exact raw `u16` rows and separately
normalized `u16` rows (black maps to 0, white to 65535, rounded to nearest).
This stage-1 access does not establish Adobe final-render parity.

The shared safe-Rust `linearization.rs` parses bounded DNG `SHORT`
`LinearizationTable` entries and clamps out-of-range raw codes to the last
entry. Tables declaring more than 65,536 entries are rejected rather than
truncated, so final-render profile checks cover the entire table; valid
redundant entries within that limit remain accepted. It now
maps 16-bit monochrome, RGB16 and checked Bayer samples at Stage 2,
**after** raw Stage 1 and **before** black subtraction/normalization; Stage 3
consumes the mapped rows. For the newly verified table profile, black must
be zero, white 65535 and table endpoints 0/65535. Independent full/short
table tests cover all 65,536 mono raw values on uncompressed strips,
compressed monochrome strips, RGB16 root/preview-child Deflate strips,
and RGGB strips/SOF3 tiles on ARM64 and SDK-free x64;
untested black/white combinations return Unsupported at Stage 2/3. No
16-bit final `SkCodec` output is enabled by this stage support.
The existing one-strip **8-bit output-referred monochrome** final profile
also accepts a validated full 256-entry or inline two-entry table when
every mapped value is 0..255 and the table endpoints are 0/255. Raw Stage 1
is unchanged; Stage 2/3 use the mapped values and final `SkCodec` output
applies the already verified sRGB transfer. Independent 256-level
mono fixtures match every SDK Stage-1/2/3 sample and final RGBA byte
on ARM64 and SDK-free x64; other bit depths/profile combinations stay
Stage-1-only or final Unsupported.
For the separately verified **8-bit output-referred RGB8 sRGB profile**,
the same table is accepted only when every mapped entry fits in a byte,
its endpoints are 0/255, and the existing color/tone/black guards hold.
The mapped channels then feed both checked Stage 2/3 and the exact
piecewise sRGB final conversion; other table/profile combinations keep
Stage 1 but cannot render approximate pixels.

A separate checked root/main-IFD path accepts **classic-TIFF DNG 1.4 or
1.7, Compression 8 (zlib/Deflate), 16-bit unsigned monochrome LinearRaw**
with Predictor 1 (or absent) or horizontal Predictor 2 and one or more
strips. For Predictor 2, each 16-bit sample is reconstructed with wrapping
addition in TIFF byte order; the predictor resets at every row, including
across strip boundaries. TIFF types, strip
counts/offsets, and decoded strip geometry are checked before allocation;
the synchronous CXX/zlib callback requires exactly the expected number
of inflated bytes and consumes every compressed byte. Overlong output,
trailing data, corrupt input and truncation are errors, not partial rows.
Stage 1 preserves the container sample byte order; Stage 2 follows the
published local repeating-black subtraction and plane-maximum-black
denominator only with validated black/white values, identity geometry,
and no Stage-2 deltas/opcodes. For that no-CFA profile, Stage 3 reuses
the checked Stage-2 row **only if OpcodeList3 is absent**. SDK/Rust
Stages 1-3 match on nine independently generated small Deflate files
(four of them using Predictor 2) and a 12-MB decompressed zero image on
macOS ARM64 and SDK-free x64; final `SkCodec` output remains Unsupported.
An additional independent 256x256 Deflate DNG covering all 65,536 raw
values with alternating black 1024/2048 exposes **155 one-LSB Stage-2
differences** from the SDK; Stage 3 inherits those differences because
this no-CFA profile aliases Stage 2. That fixture is explicitly
`pixel_mismatch`, not a passing parity case or an approved exception.
Predictor 3, Deflate tiles/SubIFDs, other sample widths, CFA outside the
checked 2x2 Bayer path, general RGB,
unknown processing tags and
non-identity Stage-2 geometry are not enabled.

The independently generated, session-private 3x3 mono fixtures cover
little/big endian, one/three strips, Predictor 1/2, and DNG 1.4/1.7.
The pinned SDK
accepted them as main IFD 0, one-plane 16-bit samples; Stage 1 was
18 bytes with SHA-256
`b2adc03773609664f750370fa6528891767275123b71a162f9b32b0c8fe29aa3`,
and Stage 2 was 18 bytes with SHA-256
`249bb987e9ae2aab378debc3da4e24a7f5fdd9a44d117de35770a65181b27ee8`.
All tested endian/strip combinations agree byte-for-byte at both stages.
The private source and oracle buffers are in `files/dng-research/`;
no generated DNG fixture is shipped in this repository.

A second Stage-1 path accepts checked, uncompressed
three-channel 16-bit LinearRaw DNG 1.6/1.7 with one or more strips and
unsigned samples. DNG 1.7 with a JPEG preview in IFD0 can also select
a checked full-resolution raw SubIFD (main IFD index 1 for the validated
layout); 1.4 tiled JPEG dispatch and strict monochrome errors are unchanged.
The bridge exposes
interleaved `u16` rows via `read_stage1_rgb16_row`; malformed strip ranges
and SubIFD pointers/cycles are rejected before strip-index allocation.
Camera profiles and gain-table
metadata (including ProfileGainTableMap2) are recognized structurally, and
ImageSequenceInfo/ImageStats are informational, but none is treated as a
renderer. SDK example 08 declares DNG 1.6; inspected examples 04-07 and
09-11 declare 1.7. The pinned SDK reports Stage-1/2/3 identity for all
eight private official examples; they are not redistributed in this
repository. The native reader selects example 04's 1000x1000 raw SubIFD
as index 1, and separate-process comparisons with the SDK match its
6,000,000-byte Stage-1/2/3 samples exactly on macOS ARM64 and SDK-free x64.
Stage 2/3 reuse the checked Stage-1 `u16` row **only** with explicit
white levels 65535 for all three planes, identity crop/scale (and ActiveArea
when present), identity optional anti-alias/quality/linear-response factors,
unsigned samples, and no black-level/repeat/delta, linearization, CFA, or
OpcodeList2 processing. OpcodeList3 leaves Stage 2 available but blocks
Stage 3. The preview parent IFD is validated structurally and for
stage-changing tags as well as the selected raw child. Extra camera profiles
and ProfileGainTableMap2 are final-render metadata, not evidence of
Stage-2/3 processing. Unknown processing tags are rejected; known profile/gain and
informational metadata cannot enable final rendering. RGB16 final
`SkCodec` output remains unsupported.

The checked RGB16 Stage-2/3 path also accepts a validated raw
`LinearizationTable` when the black/white and geometry guards above
hold. One table maps each of the three channels, and Stage 3 aliases
the mapped Stage-2 samples only without OpcodeList3. Independent
full/short table Deflate RGB16 files in both root and JPEG-preview
raw-child IFDs match the configured SDK at all three stages;
a malformed table leaves raw Stage 1 available or
rejects bad TIFF ranges/types without publishing rows.

The same checked RGB16 profile also accepts **Compression 8
(zlib/Deflate) strips**, with Predictor 1 (or absent) or horizontal
Predictor 2. It validates every compressed strip's complete inflated size
into an 8-KiB scratch buffer before allocating the decoded image; Predictor
2 reconstructs each of the three 16-bit channels independently, resetting
on every row. The checked Stage-2/3 identity guards above still apply.
Eight independent little/big-endian, one/three-strip, Predictor-1/2
fixtures match the pinned SDK byte-for-byte through Stage 3 on macOS ARM64
and SDK-free x64. Native tests exercise real zlib streams and reject
out-of-range strips, truncation, excess inflated bytes and invalid
predictors without publishing rows. After successful inflation the RGB16
reader retains only decoded pixels and checked strip metadata, not a
second copy of the encoded input. A DNG 1.7 JPEG-preview parent can
also select a **checked RGB16 Deflate raw SubIFD**, returning main IFD
index 1. Eight independently generated preview/raw-child variants
(both byte orders, one/three strips, Predictor 1/2) match the SDK
at Stages 1-3 on ARM64 and SDK-free x64; the preview parent and all
compressed child strips are validated before decoding. RGB16 Deflate
tiles, unverified SubIFD graphs, Predictor 3 and final RGB rendering
remain unsupported.
This direct-reader result does not imply PIEX chooses that preview:
on a synthetic 3x3 raw SubIFD with a larger 128x128 embedded JPEG,
the public SDK-backed selector falls through to full DNG and the
SDK-free public selector reports Unimplemented. The separate existing
Skia DNG-with-preview resource still takes the byte-identical PIEX
JPEG route.

The separate SubIFD stage-1 path selects the full main IFD from a checked
classic-TIFF graph and accepts the narrow 8-bit, three-channel LinearRaw
layout with lossy JPEG (34892) tiles used by `sample_1mp.dng`. Rust validates
tile offsets, byte counts, dimensions, and placement; a synchronous CXX
callback decodes each tile through Skia's existing libjpeg-backed
`SkJpegDecoder`. All tile byte ranges and visible geometry are checked first;
tile dimensions must fit the JPEG SOF format limit (65535). A separate
row-buffered validation pass decodes every JPEG tile before allocating the
image-sized output. The output pass decodes scanlines into a single RGBA row
while buffering only each tile's visible RGB intersection. A genuinely huge
valid image still requires image-sized output; streaming/host budgeting is a
later P9 task, not an arbitrary new acceptance cap here. The reader exposes
RGB Stage-1 rows and, when all processing metadata is supported, checked
Stage-2 `u16` rows after the published OpcodeList2 MapPolynomial operations.
For the validated no-CFA, identity-geometry subset without OpcodeList3,
Stage 3 is the same `u16` row as Stage 2. Unknown IFD0 metadata is gated
conservatively; OpcodeList3 in either IFD0 or the raw IFD reports unsupported
at Stage 3 without modifying the caller's row. This does not implement
demosaicing or non-identity Stage-3 processing.
`read_normalized_row` remains the monochrome-only Stage-2 accessor; tiled
RGB uses `read_stage2_rgb_row`; Stage 3 uses `read_stage3_row` for mono and
`read_stage3_rgb_row` for tiled RGB. Unsupported or malformed operations never
produce partial success-shaped rows. Camera profiles and tiled RGB final
rendering are not applied. The experimental Rust/zune JPEG decoder is not
used: its decoded tile samples differ from the pinned SDK.

A separate root/main-IFD Stage-1 path accepts DNG 1.7, backward version 1.3,
8-bit three-channel LinearRaw with TIFF Compression 7 **only when every
tile declares matching RGB SOF3 lossless-JPEG geometry**. It uses the same
checked tile-range preflight, row-buffered libjpeg validation, visible-edge
placement and MapPolynomial/identity-stage guards; ImageStats (52550) is
informational and vetted color/profile tags are not interpreted as final
rendering. Private examples 12 and 13 each contain 150 tiles (208x208)
for a 3000x2000 Stage-1 image. The native scanline callback decodes all
tiles with checked visible edges; separate-process comparisons match the
SDK's 18,000,000 Stage-1 and 36,000,000 Stage-2/3 bytes for both images
on macOS ARM64 and SDK-free x64. Other JPEG frame types, bit depths, CFA
and final RGB rendering are unsupported.

The checked root-IFD float path accepts uncompressed three-channel 32-bit
IEEE-float LinearRaw DNG 1.7/backward 1.4 with one or more strips.
`read_stage1_rgb_f32_row` returns exact IEEE **bits**, including signed zero
and NaN payloads, without normalizing them. Identity
`read_stage2_rgb_f32_row`/`read_stage3_rgb_f32_row` require explicit
white level 1 on all planes, identity geometry and factors, no black/delta/
linearization/CFA or Stage-2 opcodes, and finite samples in [0, 1] with
no negative zero. Other samples retain Stage 1 but report later stages
unsupported; OpcodeList3 blocks Stage 3. Private official example 14 has
identical SDK Stage-1/2/3 bytes; BaselineExposureOffset, DefaultBlackRender,
extra profiles and ProfileGainTableMap2 are final-render metadata, not
permission to render its HDR/SDR output. Final RGB `SkCodec` output remains
unsupported. Separate-process Stage-1/2/3 comparison for example 14
matches every one of its 1,200,000 bytes on macOS ARM64 and SDK-free x64;
the private example is not included in this repository.

A separate root-IFD reader accepts checked uncompressed or Deflate RGB8 LinearRaw
DNG 1.7/backward 1.1 with ColorimetricReference 1 (output-referred SDR).
It exposes byte-exact interleaved Stage-1 rows for single or multiple
strips; with explicit black level 0/white 255 across three planes,
identity crop/scale and processing factors, no CFA/linearization/deltas or
OpcodeList2, Stage 2 expands each source byte to `u16` by multiplying by
257. Stage 3 aliases Stage 2 only without OpcodeList3. Structural checks
admit vetted camera/profile metadata for later final rendering but never
apply it here. The independent session-private 256x3 color ramp matches
the pinned SDK at all three stages on macOS ARM64 and SDK-free x64.
The standalone, unregistered `color.rs` implements an independent
forward-matrix/XYZD50/sRGB calculation, but still differs from SDK final
RGB in ten channels by one byte on that colorful ramp. General RGB8 final
rendering therefore remains Unsupported.

A separate **strict output-referred sRGB RGB8 profile** does render through
the direct experimental `SkCodec`: zero black, white 255, identity
Stage-2/3 geometry and processing, unity `AsShotNeutral`, identity
`ProfileToneCurve`, explicit `DefaultBlackRender=None`, ColorMatrix1
diagonal 1.037/1/1.212, and ForwardMatrix1 equal to Skia's public
16.16 sRGB-to-XYZ-D50 ICC matrix. Only the vetted tag set is accepted
for final output; changing either matrix, tone curve, neutral or
rendering metadata leaves Stage-1 samples available but returns
`kUnimplemented` for final pixels. The shared piecewise sRGB transfer
applied per channel matches the SDK on an independently generated
256x256 color cube at all **196,608 Stage-4 channels** and all
**262,144 final RGBA bytes**, on macOS ARM64 and SDK-free x64. A
separate 256x3 saturated RGB ramp also matches full RGBA. Native tests
exercise both TIFF byte orders, multiple strips, repeated decoding,
opaque alpha and row padding. This is not support for ordinary camera
profiles or for the real Skia RGB DNG fixtures.

The same strict profile now supports a bounded `LinearizationTable` for
RGB8 input in uncompressed or Deflate strips, including Predictor 2.
Full 256-entry and inline two-entry tables map all three byte channels
before color conversion. Eight independent 256x256 cube/256x3 ramp
fixtures match the SDK at Stages 1-3 **and every final RGBA byte** on
ARM64 and SDK-free x64. The native tests include both byte orders,
multiple strips, repeated rendering, padding, malformed tables and
unverified values that stay Stage-1-only. General camera/profile RGB
is not promoted by this exact sRGB case.

The strict RGB8 profile also permits `ProfileGainTableMap` or
`ProfileGainTableMap2` **only when a checked parse proves every table
entry evaluates to gain 1**. Checked sampling at both image corners
must also yield 1; conflicting, malformed and non-identity maps remain
Unsupported at final rendering without changing checked Stage 1-3.
Six independent 256x256 color-cube DNGs cover integer U8/U16, float16/
float32 and legacy float32 storage, including a 1x1 spatial grid.
All six match the separate-process SDK at Stages 1-3 and at every
public final RGBA8888, BGRA8888 and RGB565 byte on macOS ARM64 and
SDK-free x64; RGBAF16 also matches on the same-architecture ARM64
reference and candidate. Three additional identity-map color cubes
using Deflate Predictor 1/2 likewise match SDK Stages 1-3 and full
RGBA8888 on both Mac architectures; a compressed non-unity case
stays unsupported. Native tests cover both TIFF byte orders,
multiple strips, seekable/forward-only public fallthrough, padding,
Deflate strips with Predictor 1/2, and explicit rejection of colorful
constant-gain and spatial-gain
inputs. This is no-op metadata recognition, **not** gain-map
application or general camera-profile rendering.

The same strict sRGB profile also accepts **Compression 8 (zlib/Deflate)
root-IFD RGB8 strips** with Predictor 1 (or absent) or horizontal Predictor 2
per channel. TIFF ranges and every strip's complete inflated size are checked
with the existing 8-KiB scratch preflight before allocating decoded pixels.
Eight independently generated 256x256 color-cube and 256x3 saturated-ramp
layouts (one/multiple strips and both predictors) match the SDK through
Stages 1-3 **and every final RGBA byte** on ARM64 and SDK-free x64. The
source-controlled native tests cover both TIFF byte orders, two-strip
boundary resets, real zlib streams, row padding and repeat decode; invalid
streams, unsupported predictors and altered rendering profiles fail without
publishing pixels. The verified color profile is still narrowly gated: this
does not make general compressed camera RGB, tone or Auto black renderable.

The separate **2x2 Bayer DNG sensor** reader accepts a root/main
classic-TIFF DNG 1.4 with uncompressed, unsigned, one-plane 16-bit
samples, explicit RGGB/BGGR/GRBG/GBRG CFA/plane-color/layout metadata,
repeating 2x2 BlackLevel, and WhiteLevel with identity crop/scale.
`read_stage1_bayer_row` exposes raw
sensor `u16` rows; `read_stage2_bayer_row` applies the published local
black subtraction and plane-maximum-black denominator, without allocating
an image-sized output. The independent private 16x16 test fixture at
`files/dng-research/rggb-synthetic-le.dng` has 512-byte SDK Stage-1/2
one-plane images, and every candidate Stage-1/2 sample matched the SDK.
The SDK Stage 3 is a separate three-plane demosaiced image. A first
bilinear attempt disagreed on 28 border pixels of the varied 16x16
fixture, all in green. Equal horizontal/vertical weighting at image
edges removed those differences without changing interior samples.
The now-checked three-row Rust Bayer interpolator matches every Stage-3
sample on eight independently generated **zero-black/white-65535**
varied strip/tile cases: little/big endian, multiple strips, cropped
SOF3 tiles and 5x5 through 3000x2000 sizes. It never allocates a
full normalized Stage-2 image. Stage 3 still requires identity geometry
and absence of Stage-2/3 corrections, opcodes, noise/factor metadata and
other CFA layouts. Guarded constant CFA fields retain their
fast exact-row path; independent SDK tests cover sizes 2x2 to 16x16,
channel extremes and both byte orders.
An additional **36 independently generated non-RGGB Bayer cases**
(12 each for BGGR, GRBG and GBRG) match the SDK through Stages 1-3 on
macOS ARM64 and x64. They cover varied and constant fields, both TIFF
byte orders, multiple strips, odd-size borders, Deflate Predictor 1/2,
SOF3 uniform/varied tiles, and full/short linearization tables. The
constant-field fast path and interpolated rows use the checked CFA
phase; malformed and non-Bayer 2x2 patterns remain Unsupported. None
of these Stage-3 matches enables final sensor-color rendering.

**Black-normalized varied CFA remains Stage-3 Unsupported.** Although the
original 16x16 black-level fixture matched SDK Stage 2, an independent
128x128 sweep over all 4,096 raw values at each CFA site exposed 15
one-LSB Stage-2 SDK differences among 16,384 samples; another varied
16x16 case has one. The mathematically rounded normalization is not
silently declared equivalent, and no numerical exception is approved.
Both cases remain explicitly reported as Stage-2 pixel mismatches and
Stage-3 missing support. Final `SkCodec` Bayer output is Unsupported
even for exact Stage-3 cases. This is DNG sensor data, not proprietary
camera-RAW decoding.

The checked Bayer reader accepts a `SHORT` DNG `LinearizationTable`
without changing raw Stage-1 samples. Stage 2 maps each raw code through
the table **before** black subtraction and normalization; codes beyond
the last entry use that entry. Stage 3 consumes those checked Stage-2
rows, including the properly mapped constant-field fast path. Six
independent 16x16 files (full 65,536-entry and two-entry inline TIFF
tables for strips, varied SOF3 tiles and uniform SOF3 tiles) match the
configured SDK at Stages 1-3 on ARM64 and SDK-free x64.
The new Stage-2/3 profile is gated to zero black, white 65535, a
zero first table entry and a 65535 last entry; other valid metadata
retains Stage 1 but returns Unsupported for unverified processing.
All table ranges and types are checked before use; no default final
Bayer rendering is enabled.

Another Bayer Stage-1 path accepts **root-IFD DNG 1.4 Compression 8
(zlib/Deflate) 16-bit sensor strips** with Predictor 1 (or absent) or
horizontal Predictor 2. The same guarded black normalization and
zero-black/white-65535 Stage-3 reconstruction as uncompressed Bayer
apply. Both byte orders and one or multiple strips are checked; Predictor 2
resets each row. Every strip's complete inflated size and input consumption
are validated in an 8-KiB scratch buffer before allocating decoded pixels.
Eight independently generated varied-CFA files match the SDK's Stage
1/2/3 output on macOS ARM64 and SDK-free x64, and native tests exercise
real zlib streams, bad ranges, truncated/corrupt/overlong streams and
unsupported predictors. For this new compressed-CFA route, Stage 2/3
are explicitly Unsupported if black is nonzero or white differs from
65535: a separate nonzero-black DNG matches SDK Stage 1 but is not
misreported as exact at later stages. The uncompressed-CFA numerical gap
remains open; Deflate CFA tiles/SubIFDs, other CFA layouts and final
Bayer rendering remain unsupported.

The SOF3 Bayer Stage-1 path accepts **classic-TIFF DNG 1.4 Compression 7**
with 16-bit, single-component lossless-JPEG SOF3 tiles. Checked TIFF
offsets/counts, 2x2 CFA metadata, and edge geometry precede a full
bitstream validation pass through the bundled libjpeg-turbo
`jpeg16_read_scanlines` API. Only then does it allocate and assemble
image-sized `u16` samples. The Stage-2 row reuses checked local black
subtraction and plane-maximum-black scaling with identity crop/scale
and no linearization, black deltas, OpcodeList2 or unvalidated
processing factors. **Uniform and varied tiled fields** expose
three-plane Stage 3 under the same zero-black/white-65535/identity-processing
guard as uncompressed CFA, using three checked Stage-2 rows at a time.
Independent 16x16 four-tile uniform and varied fixtures match every
1,536-byte SDK Stage-3 image on macOS ARM64 and SDK-free x64; the
varied fixture has SHA-256
`7ab84204f317952212c3be2b7135f80dd66744bcedfcfcc5575f17fef8720e27`.
The original tiled CFA fixture with nonzero black still reports
Stage-3 Unsupported. Final output is Unsupported for all Bayer
profiles. JPEG tile dimensions above the SOF limit of 65535,
other JPEG precisions/components/processes, SubIFD CFA, and unknown
processing metadata are unsupported. The independent private 16x16
four-tile fixture has 10x10 JPEG tiles with cropped right/bottom edges;
the pinned SDK accepts it at main IFD index 0 and its Stage-1 and
Stage-2 512-byte buffers match the independent uncompressed Bayer
fixture byte-for-byte on macOS ARM64 and SDK-free x64. Their respective
SHA-256 values are
`e4d782c0d12e95f5eaf6846d55b72aff368a928b701a7f9a088cd8d1293f1c73`
and `71cb8682701b0aeebc8d72a632cf049e4784db1d8d219790ca1156dff69fd59d`.
The source-controlled native tests create their own JPEG16 bitstreams;
the oracle fixture and tile binaries stay in session-private artifacts.
This experimental 16-bit tile path needs the bundled libjpeg-turbo
headers and 16-bit symbols; a system JPEG library is not substituted.

The checked `gain_map.rs` parses the published DNG 1.6
`ProfileGainTableMap` and DNG 1.7 `ProfileGainTableMap2` layouts without
copying Adobe code or data. It checks dimensions, byte counts, all four
integer/float storage types, both byte orders, pixel-centered interpolation,
RIMM input weights, and gamma. SDK-oracle comparisons established that the
configured decoder accepts gamma `0.125..8` rather than the specification's
`0.25..4`, and floating table gains in `1/4096..4096`. Private examples
05-08 yield identical sampled gains across their storage types. This
component is now compiled in the native decoder **only** to guard the
strict RGB8 final profile for mathematical identity; interpolation
is tested but never used to apply a non-unity gain. General profile
selection, exposure/RIMM processing and final RGB gain application
remain unsupported.

The **test-only** `tone.rs` validates SDR `ProfileToneCurve` points and
evaluates a natural cubic spline with checked allocations and finite
arithmetic. A separately attributed SDK-style 4,096-entry floating-point
lookup and intermediate-RGB extrema/interpolation operation are compiled
in Rust tests only. On an independent three-point color-profile ramp,
the table and RGB-tone operation plus sRGB match all **768 SDK neutral
Stage-4 bytes**. Applying tone separately to saturated sRGB channels
instead misses 1,391 of 1,536 bytes (max 74): a colored SDK comparison
still reports public Rust creation as Unimplemented. The exact
intermediate camera color/ProPhoto/exposure and output transfer must be
verified together before publishing this profile. Two separately
generated monochrome output-referred ramps
with substantially different valid color tone curves produce
identical 768-byte SDK Stage-4 outputs; mono skips this color-profile
tone under the checked one-plane/black-None layout.

**Final `SkCodec` output has two narrow 8-bit monochrome profiles plus
the strict RGB8 profile described above.** The monochrome path accepts one
or more checked uncompressed strips with identity geometry, black level
zero, and white level 255. Scene-referred/default ColorimetricReference 0
still permits only black (0) or white (255). Output-referred SDR
ColorimetricReference 1 also permits
midtones and applies the published piecewise sRGB transfer to normalized
samples, producing opaque RGB. All 256 8-bit sample values matched the
pinned SDK in an independent no-preview ramp. Six independently generated
three- to five-strip mono DNGs with partial final strips, binary scene
values, full/short linearization tables, and color tone metadata match
the SDK at Stages 1-3 and final RGBA8888, BGRA8888 and RGB565 on ARM64
and x64; ARM64 RGBAF16 also matches a same-host reference. A scene-referred
midtone in the last strip disables **all** final rows. HDR value 2 is
unsupported.
The output-referred monochrome profile also accepts a validated
`ProfileToneCurve` with explicit `DefaultBlackRender=None` and leaves
its pixels unchanged, matching both distinct SDK tone-tag fixtures on
ARM64 and SDK-free x64. An invalid tone tag fails parsing; Auto black
or an unverified tone/profile combination remains unsupported for
final output.
Scene-referred midtones, **all 16-bit final output**, and tiled RGB final
output remain unsupported even when checked Stage 1/2/3 rows are
available; the 16-bit
output-referred ramp was not byte-exact.
Unverified linearization tables and other compression,
general CFA demosaicing, Stage1-changing opcodes, other Stage-2 opcodes,
non-identity Stage-3 processing,
non-identity crop/scale, extra IFDs, non-unity gain maps,
general camera color transforms,
and general color-profile tone rendering remain unsupported rather than
being silently ignored.
The reader
buffers seekable files in checked chunks without imposing a forward-only
size limit. For forward-only streams, its Skia-specific input adapter reads
short chunks until EOF and applies the existing 100 MiB stream limit,
checking for one extra byte at the boundary. A truncated stream or
over-limit input returns an explicit error; the TIFF/DNG parser never
borrows the input stream.

**Existing Skia DNG resources remain unsupported for final rendering.**
`sample_1mp.dng` and `sample_1mp_rotated.dng` keep their full-resolution
600x338, three-channel LinearRaw image in a SubIFD, using lossy ordinary JPEG
compression (34892), two 304x352 tiles, and OpcodeList2 plus camera color
metadata. The new direct stage-1 path targets their raw tiled RGB samples;
byte-exact comparison with the isolated Adobe stage-1 oracle passes for
both files and the underlying image in `dng_with_preview.dng`. The latter
also contains that full-image path,
but Skia's default PIEX route selects its embedded JPEG preview. A future
final-image compatibility gate still requires remaining opcodes, color
processing, and comparison with the isolated Adobe oracle. These particular
resources do not exercise CFA/Bayer
or lossless JPEG/SOF3; that observation does not narrow the wider RAW
compatibility plan.

`experimental/rust_raw/ffi/opcode.rs` parses checked big-endian OpcodeList2
MapPolynomial operations (ID 8) and evaluates them in f32 Horner order.
The integrated safe-Rust row path, fed independently captured Stage-1 RGB,
matched all 608,400 SDK Stage-2 and Stage-3 channel values for each of the
three real files. Native/process-isolated Stage-2 and Stage-3 comparisons
pass on both macOS ARM64 and x64 for all three files and four additional
mono fixtures. This identity Stage-3 result is not a finished DNG renderer;
final `SkCodec` output remains unsupported for tiled RGB.

The real DNG parent may now carry a validated SDR `ProfileToneCurve`
and `DefaultBlackRender` value without changing its exact Stage-1/2/3
rows; malformed tone/black-render tags block Stage 3 without publishing
rows. A private metadata-only version of `sample_1mp.dng` with identity
tone and black rendering set to None matches the SDK at all three
stages. The **test-only** dual-illuminant color component interpolates
camera calibration in inverse CCT using public DNG formulas. It now
compares McCamy's approximate CCT with a separate
[published Planckian xy locus](https://en.wikipedia.org/wiki/Planckian_locus#Approximation)
minimized in CIE 1960 uv; the resulting weights for the Skia
sample are 0.231868 and 0.232836, respectively. With the latter weight,
the normative ForwardMatrix transform still differs from the controlled
SDK Stage-4 image in 311,040/608,400 RGB channels. An explicitly
separate, non-normative forward-white correction reduces that
diagnostic gap from 3,182 McCamy-weighted to 2,465 CIE-uv-weighted
channels (max 3 bytes, including 47 blue-channel differences), but is
not enabled for final rendering. Neither CCT method nor white
correction establishes SDK color parity. Against the original default
tone/Auto-black image even that trial differs by a mean 26.46 bytes.
Two further private metadata-only variants isolate the SDK defaults:
default tone alone changes output by a mean 28.06 bytes (max 65)
versus identity tone with black rendering None; Auto black alone under
identity tone changes it by a mean 2.54 bytes (max 16). All three
variants retain exact Stages 1-3 on ARM64 and SDK-free x64. Neither
default is specified sufficiently by the public DNG text to silently
publish an Adobe-compatible replacement.
Those independent diagnostics used no Adobe source. After the
maintainer authorized a licensed derivative, a separate **test-only**
Robertson CIE 1960 uv temperature table and interpolation were ported
from SDK 1.7.1.2724 with Adobe attribution beside both Rust definitions,
unmodified license/technology notices in `licenses/`, and a per-symbol
entry in [PROVENANCE.md](PROVENANCE.md). The production CXX FFI does
**not** compile that color module. A private, source-attributed trial
also follows SDK matrix-white normalization and intermediate ProPhoto
clipping: for the controlled identity-tone real DNG it differs in
1,104/608,400 channel bytes (R/G/B 39/213/852, maximum one byte).
Neither that diagnostic nor Adobe's default tone curve is enabled for
public final pixels. Real-image `SkCodec` output remains Unsupported.

The native tests create synthetic DNGs in test code. `RustRaw_OutputMonoSrgb`
checks the 256-value ramp against the published sRGB transfer, including
active rows, padding and repeated decode. `RustRaw_OutputMonoIgnoredTone`
compares an explicit color-tone-tagged mono ramp with the SDK and rejects
malformed tone or unverified black handling. `RustRaw_OutputMonoUnsupported`
checks malformed/HDR and still-unsupported profiles. `RustRaw_AdobeParity`
compares the monochrome final-output profiles with the pinned Adobe
factory, and is registered only in Adobe-enabled reference builds rather
than failing an SDK-free run when the oracle is unavailable.
`RustRaw_Stage1Strips` checks both byte orders and both sample
widths directly through the bridge. `RustRaw_RealDngStage1` checks decoded
RGB rows across both tiles against the pinned stage-1 reference fingerprint;
`RustRaw_RealDngStage2` compares the checked Stage-2 row fingerprint against
the pinned SDK samples, while `RustRaw_RealDngInvalidOpcodes` tests rejection
without altering cached Stage-1 data. `RustRaw_RealDngStage3Identity` compares
all Stage-3 and Stage-2 rows, and `RustRaw_Stage3OpcodeList3` checks
root/raw/mono rejection with untouched destinations. None claims final-render
parity.
`RustRaw_RealDngFinalOnlyProfileMetadata` compares all 338 real-file
Stage-3 rows after adding valid parent tone/black-render tags and
rejects invalid values without touching row buffers.
`RustRaw_Rgb16Stage1` and `RustRaw_Rgb16MalformedStrips` cover independent
little/big-endian single/multiple-strip synthetic RGB16 inputs and rejection
without publishing malformed rows. `RustRaw_Rgb16ProcessingGuards` checks
that Stage 1 survives later-stage metadata while Stage 2/3 either use proven
identity or report unsupported without modifying the output.
`RustRaw_LinearizedRgb16Rows` and `RustRaw_LinearizedRgb16Malformed`
exercise one shared lookup on all three 16-bit channels, including
Deflate Predictor 2, bounded table metadata and untouched output on
unsupported endpoints. `RustRaw_LinearizedMono16Rows` and
`RustRaw_LinearizedMono16Malformed` check the same stage ordering for
one-plane uncompressed/Deflate 16-bit input while rejecting final
16-bit output. `RustRaw_LinearizedMono8SrgbFinal` and
`RustRaw_LinearizedMono8Malformed` verify all 256 corrected grayscale
values, SDK final RGBA parity and fail-closed LUT types/bounds.
`RustRaw_Rgb16Subifd` and `RustRaw_Rgb16SubifdInvalid` cover an independent
JPEG-preview/raw-SubIFD fixture in both byte orders and strip layouts,
including parent pointers, cycles, and Stage-2/3-only processing guards.
`RustRaw_Sof3RootStage1` and `RustRaw_Sof3RootMalformed` use the bundled
libjpeg-turbo `jpeg_enable_lossless` encoder **only in test code** to create
independent SOF3 tiles. They check cropped RGB rows, guarded Stage-2/3,
malformed offsets, JPEG frame type, and unknown required opcodes. The native
test target needs the bundled libjpeg-turbo encoder headers and symbols;
the production decoder gains no new dependency.
`RustRaw_Float32Rows` and `RustRaw_Float32MalformedAndGuards` check independent
endian/strip fixtures, IEEE bit identity, untouched output buffers, and
unsupported final rendering.
`RustRaw_Rgb8RootRamp`, `RustRaw_Rgb8RootStrips` and
`RustRaw_Rgb8RootMalformed` generate an independent color ramp and small
endian/multi-strip cases; they check exact Stage-1 bytes, guarded Stage-2/3
values, padding and repeated reads. `RustRaw_Rgb8SrgbFinal` compares the
strict RGB8 profile's 256x256 color cube and endianness/strip variants
against analytic sRGB and the configured SDK's full RGBA output.
`RustRaw_Rgb8SrgbProfileGuard` verifies different matrices, neutral,
tone, automatic black and gain metadata never enable approximate final
pixels. `RustRaw_Rgb8ExplicitToneStageGuard` retains exact Stage 3
for a nonlinear tone curve while refusing unverified final RGB pixels.
`RustRaw_DeflateRgb8SrgbFinal` and `RustRaw_DeflateRgb8Malformed` exercise
both predictor modes, full-color output parity and strict compressed-stream
and profile guards.
`RustRaw_LinearizedRgb8SrgbFinal` and `RustRaw_LinearizedRgb8Malformed`
verify public table processing in the strict color profile through final
RGBA, including 8-bit bounds and fail-closed metadata.
`RustRaw_BayerStages`, `RustRaw_BayerUniformStage3`, `RustRaw_BayerVariedStage3`,
`RustRaw_BayerStage3Guard`, `RustRaw_BayerMalformed`,
`RustRaw_LinearizedBayerRows`, `RustRaw_LinearizedBayerTiles`
and `RustRaw_LinearizedBayerMalformed`,
`RustRaw_BayerSof3Tiles` and `RustRaw_BayerSof3Malformed` generate independent
RGGB sensor patterns in both byte orders/strip layouts and check Stage-1/2
values, constant and varied zero-black Stage-3 RGB rows for strips and SOF3
tiles, black-normalizing Stage-3 rejection, JPEG16 cropped tiles, sentinels,
later-stage processing guards and malformed metadata.
`RustRaw_DeflateBayerRows` and `RustRaw_DeflateBayerMalformed` exercise
real zlib-compressed Bayer strips, Predictor-2 reconstruction, Stage-3
interpolation and preflight/error handling without publishing partial rows.
`RustRaw_DeflateRgb16SubifdRows` and
`RustRaw_DeflateRgb16SubifdMalformed` verify preview-parent selection,
independent compressed child strips, ownership and error handling.
`RustRaw_DeflateMonoRows` and `RustRaw_DeflateMonoMalformed` generate
monochrome Deflate strips with the bundled zlib encoder and check
byte-exact Stage-1/2/3 samples, Predictor-2 row reconstruction and
wrapping, both byte orders, repeated rows, sentinels,
truncated/trailing/corrupt/overlong streams, and explicit later-stage
rejection.
`RustRaw_ForwardOnlyStream` checks a nonseekable, length-unknown stream
that supplies at most seven bytes per read: exact final monochrome pixels,
truncation, and later Stage-1/3 Deflate/Predictor-2 reads after the
stream is destroyed. The Rust input tests check short-read accumulation,
the seekable-path limit exemption, an exact-limit EOF, and rejection on
one extra byte using a small simulated limit. Native public RAW
registration remains unchanged.
The production registration is separate work.

## Building and testing

The bridge uses Skia's existing Bazel/CXX integration and the pinned Rust
toolchain. It has no new third-party Rust decoding dependency.

From the Skia root, the Rust bridge tests can be run with:

```sh
bazelisk test //experimental/rust_raw/ffi:test_raw_ffi \
  //experimental/rust_raw/ffi:test_tiff_parser
```

Native tests use the same `dm` runner as the Rust PNG/JPEG tests. Enable
`skia_use_rust_raw_decode`, and keep `skia_use_dng_sdk` and `skia_use_piex`
enabled for the reference comparison. The current `dm` build also requires
Ganesh or Graphite to be compiled; `--nogpu` selects CPU tests at runtime.

For a macOS ARM64 build of this fork:

```sh
bin/gn gen out/RustRaw --args='
  is_debug=false
  is_official_build=false
  target_cpu="arm64"
  skia_enable_tools=true
  skia_enable_ganesh=true
  skia_enable_graphite=false
  skia_enable_pdf=false
  skia_use_gl=false
  skia_use_metal=false
  skia_use_vulkan=false
  skia_use_rust_raw_decode=true
  skia_use_dng_sdk=true
  skia_use_piex=true
  skia_use_icu=false
  skia_use_harfbuzz=false
  skia_use_fontations=false
  skia_use_freetype=false
  skia_use_partition_alloc=false
  skia_use_perfetto=false
  skia_use_system_libjpeg_turbo=false
  skia_use_system_libpng=false
  skia_use_system_libwebp=false
  skia_use_system_zlib=false
  extra_cflags_c=["-DHAVE_ARC4RANDOM_BUF"]
'
ninja -C out/RustRaw dm
out/RustRaw/dm --src tests --nogpu --resourcePath resources \
  --match '^RustRaw_' '^Codec_raw'
```

Check that the selected tests actually ran. A successful build or a run with
zero matching tests does not establish decoder correctness.

`RustRaw_AdobeParity` is the separately named reference test. It reports a
failure if the Adobe RAW decoder is not compiled in, rather than silently
omitting the comparison. For an SDK-free standalone test run, select
`RustRaw_OutputMonoSrgb`, `RustRaw_OutputMonoUnsupported`,
`RustRaw_LinearMonochrome`, `RustRaw_UnsupportedAndMalformed`,
`RustRaw_LateIFD`, `RustRaw_AdvertisedLength`, `RustRaw_InvalidTypes`,
`RustRaw_Stage1Strips`, `RustRaw_Stage1MalformedStrips`,
`RustRaw_RealDngStage1`, `RustRaw_RealDngStage2`,
`RustRaw_RealDngStage3Identity`, `RustRaw_Stage3OpcodeList3`,
`RustRaw_RealDngInvalidOpcodes`, `RustRaw_RealDngCorruptTiles`,
`RustRaw_TinyImageHugeTile`, `RustRaw_Rgb16Stage1`,
`RustRaw_Rgb16MalformedStrips`, `RustRaw_Rgb16ProcessingGuards`,
`RustRaw_Rgb16Subifd`, and `RustRaw_Rgb16SubifdInvalid`.
The SDK-free SOF3 tests are `RustRaw_Sof3RootStage1` and
`RustRaw_Sof3RootMalformed`; they generate their own tiny lossless-JPEG
tiles using the already-linked libjpeg-turbo encoder. The independent
SDK comparison additionally passed for private examples 12/13 at
Stages 1-3 on the two tested Mac architectures.
The float tests are `RustRaw_Float32Rows` and
`RustRaw_Float32MalformedAndGuards`; stage probes serialize f32 samples
by raw bits, not text or numeric conversion.
The SDK-free RGB8 tests are `RustRaw_Rgb8RootRamp`,
`RustRaw_Rgb8RootStrips` and `RustRaw_Rgb8RootMalformed`; the
session-private 256x3 ramp remains an independent Stage-1/2/3 oracle.
The Bayer tests are `RustRaw_BayerStages`, `RustRaw_BayerUniformStage3`,
`RustRaw_BayerVariedStage3`, `RustRaw_BayerStage3Guard`, `RustRaw_BayerMalformed`,
`RustRaw_DeflateBayerRows`, `RustRaw_DeflateBayerMalformed`,
`RustRaw_BayerSof3Tiles` and `RustRaw_BayerSof3Malformed`; their private
fixtures and pinned SDK Stage-1/2/3 buffers stay out of the repo.
The Deflate mono tests are `RustRaw_DeflateMonoRows` and
`RustRaw_DeflateMonoMalformed`; SDK-free test builds need the bundled
zlib encoder/decoder dependency for these source-generated strips.
They also check full compressed-stream preflight before image allocation
and the guarded Stage-3 identity row.
Such a run checks the candidate alone and does not establish Adobe parity.

The test-only `raw_codec_probe` (public/factory pixels),
`dng_stage_oracle` (SDK Stage 1/2/3 samples) and `raw_rust_stage_probe`
(candidate Stage 1/2/3) executables support independent-process comparisons.
With both SDK and Rust RAW enabled, build all three via GN/Ninja and compare
the existing full-image fixtures:

```sh
ninja -C out/RustRaw raw_codec_probe dng_stage_oracle raw_rust_stage_probe
python3 tools/raw_codec_probe_compare.py \
  --reference out/RustRaw/dng_stage_oracle \
  --candidate out/RustRaw/raw_rust_stage_probe \
  --stage 1 --case resources/images/sample_1mp.dng \
  --case resources/images/sample_1mp_rotated.dng \
  --report out/RustRaw/stage1-comparison.json
```

The comparison command exits nonzero if either expected decoder is
unavailable or the samples/metadata differ. Stage 2 now matches on the three
real DNG fixtures, but neither this result nor a successful preview test
implies full-DNG rendering parity.
For the private example 04, compare Stage 1, 2, and 3 separately using its
local path as `--case`; require raw `main_ifd_index=1`, 1000x1000x3 `u16`
samples, and byte-exact agreement. Keep the private file out of the repository.

The direct Rust stage probe can also be built **without the SDK** by
setting `skia_use_dng_sdk=false` and retaining
`skia_use_libjpeg_turbo_decode=true`. The macOS x64 prototype uses
`target_cpu="x64"` and `min_macos_version="10.13"`. Its GN-linked
executable has been run under Rosetta against the same independent SDK
reference: all three real DNGs, four synthetic mono DNGs, two independent
RGB8 color fixtures, and all 11 private official examples that decode
under the configured SDK match at Stages 1/2/3 (eight RGB16, two SOF3
RGB8, one float32). The original varied 16x16 RGGB Bayer DNG also matches
at Stages 1/2; its black-normalized Stage 3 remains unsupported in Rust.
Separately validated zero-black varied and uniform strip/tiled profiles
match Stage 3 exactly. The strict output-referred sRGB RGB8 cube and
color ramp match full SDK RGBA; other RGB profiles remain Unsupported. Its
output-referred 8-bit mono final RGBA also matches the SDK across
all 256 source values. These SDK-free *test builds* do not make Rust
the default production full-DNG backend. In a separate SDK-free,
PIEX-enabled opt-in build (`skia_use_dng_sdk=false`, `skia_use_piex=true`,
`skia_use_rust_raw_decode=true`), public `SkRawDecoder` invokes Rust
only after PIEX declines a usable JPEG preview. Eleven narrowly
verified full-DNG final outputs match Adobe through registered
`SkCodec`, while unsupported real DNG final rendering still reports
`kUnimplemented`. On `dng_with_preview.dng`, PIEX chooses JPEG with
byte-identical metadata and RGBA pixels to the Adobe-backed build,
including nonseekable input. `RustRaw_PublicPreviewNotSelected`
exercises an atypical larger JPEG parent IFD with a 3x3 RGB16 raw child
in uncompressed and Deflate forms: PIEX does not choose that JPEG, so
the SDK-free public route reports `kUnimplemented` for seekable,
nonseekable, and short-read streams instead of pretending the preview
was selected. Other camera-preview formats and native platforms
remain P8 gates.

A separate macOS ARM64 GN build enables Rust PNG decoding alongside the
SDK-free PIEX/Rust RAW path. It links both Rust libraries, passes 111 selected
RAW/PNG native tests (including four Bayer phases), and matches
previously checked public `SkCodec` output against the
single-RAW-codec build for an independent RGB8 DNG cube and two existing
Skia PNGs. This is a coexistence smoke, not proof for every combination of
Rust codecs or target platforms.

A network-isolated Linux ARM64 container with Rust 1.89 passed all 90
standalone safe-Rust parser tests with warnings denied. This is **not**
a Linux build of Skia, CXX, JPEG/zlib, or the public decoder: that
requires a native Linux toolchain and codec tests before promotion.

The SDK-free test-only `raw_rust_decode` libFuzzer target exercises typed
Stage-1/2/3 rows for accepted DNGs and the final mono codec path. It is built
only when `skia_build_fuzzers` and `skia_use_rust_raw_decode` are enabled;
fuzz-input size is bounded in the harness, not in the decoder. Seed it with
repository DNG resources and independently generated fixtures. A short local
run and individual replays cover the currently supported profile families,
but the Bazel-built Rust library is not yet instrumented for libFuzzer
coverage, so this is not the required deep-path P9 fuzz gate.

For a macOS ASan/UBSan build using a separately provided libFuzzer runtime,
`skia_rust_raw_fuzz_coverage=true` (default false) instruments only the
native RAW bridge and its fuzzer harness with 8-bit counters. It requires
`skia_build_fuzzers=true`, `skia_provide_default_fuzz_engine=false`, the
Rust RAW decoder and an external engine supplied through the build
environment; unsupported combinations fail during GN generation. On one
source-built Apple ASan configuration, generated monochrome, RGB8/RGB16,
RGGB, Deflate and SOF3 seeds plus Skia DNG resources replayed successfully.
Two short mutations sets completed **8,000** inputs without a sanitizer
failure and reached **168/900** instrumented native bridge/harness
counters. These numbers do not establish deep Rust parser or libjpeg/zlib
coverage: those paths execute under native ASan/UBSan where applicable,
but their internal decisions are not yet exposed to the fuzzer. A
separate 200-run gain-map smoke with seven generated unity/non-unity
and Deflate seeds also passed; it reached 67/900 shallow native
counters, not a new deep-path coverage claim. A later six-seed
multi-strip monochrome smoke completed 500 mutations without sanitizer
failure, reaching 63/900 shallow native counters; neither run replaces
deep Rust/JPEG/zlib instrumentation. Another 18-seed Bayer-pattern
corpus (uncompressed, Deflate, SOF3 tiles and lookup tables) completed
1,000 mutations with no sanitizer failure; it reached 86/900 native
counters, not internal Rust or codec coverage.
