# Rust DNG derivative provenance

The configured Adobe DNG SDK 1.7.1.2724 is a licensed implementation
reference, not a runtime dependency of the eventual Rust codec. Unmodified
copies of its `LICENSE.source_code` and `LICENSE.technology` are preserved
in `licenses/`. This record supplements the SDK's original copyright,
source-code license and technology notices; it does not replace or
modify them. Before
shipping, review the required source/package notices and commercial
distribution terms with the product's legal owner.

For each Rust implementation derived from the SDK:

1. Identify the pinned SDK source file and symbol, and add a nearby Rust
   comment naming that source and the derived algorithm or value.
2. Record the Rust location, SDK source/version, behavior, and independent
   public-`SkCodec` Adobe/Rust comparison in the ledger below. Include the
   SDK copyright notice applicable to copied source expression or data.
3. Preserve unmodified Adobe license/technology notices in the final
   source and package after deleting the old SDK implementation. Do not
   copy SDK example images or modify SDK documentation.
4. Keep any licensed derivative behind the experimental flag until
   public output, resource, sanitizer and platform gates pass.

| Rust location | Adobe SDK 1.7.1.2724 file / symbol | Derived behavior or value | SkCodec A/B evidence | Attribution status |
| --- | --- | --- | --- | --- |
| `ffi/color.rs::SDK_ROBERTSON_UV` | `source/dng_temperature.cpp::kTempTable` | 31 CIE 1960 uv temperature rows, attributed by the SDK to Wyszecki & Stiles | Numerically compared to all 31 pinned SDK rows; `color::tests::sdk_robertson_temperature_is_checked_without_enabling_final_pixels` | Adobe 2006–2019 notice beside table; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::xyz_to_kelvin_sdk_robertson` | `source/dng_temperature.cpp::LegacySetXY` | Interpolation of Robertson temperatures across chromaticity table normals; no tint or low-temperature extension ported | Rust color test and source-controlled Skia camera-profile interpolation test; public final rendering remains Unsupported | Adobe 2006–2019 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::SdkToneTable` | `source/dng_1d_table.h/cpp::dng_1d_table::Initialize/Interpolate` | Default 4,096-entry float lookup, terminal sentinel, and interpolation of a validated tone function | `tone::tests::sdk_tone_table_interpolates_checked_curve_samples`; the private SDK comparison matches 768/768 neutral Stage-4 bytes. Public colorful-tone creation still rejects. | Adobe 2006–2019 notice beside type; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::apply_sdk_rgb_tone` | `source/dng_reference.cpp::RefBaselineRGBTone` | Clip the intermediate RGB inputs, transform extrema through the tone curve, then interpolate the remaining channel rather than toning output sRGB separately | `tone::tests::sdk_intermediate_rgb_tone_preserves_color_order`; colorful public final output remains Unsupported | Adobe 2006–2023 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |

`color.rs` and `tone.rs` are currently compiled by Rust test targets,
not by the production CXX FFI bridge (`FFI.rs` declares both under
`#[cfg(test)]`). The
session-private real-DNG Stage-4 diagnostic is also source-attributed,
but its 1,104 one-byte mismatches are **not** a passing public
comparison and it is not shipping decoder code.
