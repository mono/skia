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
   public-`SkCodec` Adobe/Rust comparison or explicitly marked test-only
   diagnostic in the ledger below. Include the
   SDK copyright notice applicable to copied source expression or data.
3. Preserve unmodified Adobe license/technology notices in the final
   source and package after deleting the old SDK implementation. Do not
   copy SDK example images or modify SDK documentation.
4. Keep any licensed derivative behind the experimental flag until
   public output, resource, sanitizer and platform gates pass.

| Rust location | Adobe SDK 1.7.1.2724 file / symbol | Derived behavior or value | Evidence and public gate | Attribution status |
| --- | --- | --- | --- | --- |
| `ffi/color.rs::SDK_ROBERTSON_UV` | `source/dng_temperature.cpp::kTempTable` | 31 CIE 1960 uv temperature rows, attributed by the SDK to Wyszecki & Stiles | Numerically compared to all 31 pinned SDK rows; `color::tests::sdk_robertson_temperature_is_checked_without_enabling_final_pixels` | Adobe 2006–2019 notice beside table; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::xyz_to_kelvin_sdk_robertson` | `source/dng_temperature.cpp::LegacySetXY` | Interpolation of Robertson temperatures across chromaticity table normals; no tint or low-temperature extension ported | Rust color test and source-controlled Skia camera-profile interpolation test; public final rendering remains Unsupported | Adobe 2006–2019 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::sdk_illuminant_temperature` | `source/dng_camera_profile.cpp::IlluminantToTemperature` | SDK Standard Light A = 2850 K and D65 = 6500 K for the checked dual-illuminant profile (not the nominal 2856 K Standard A) | Rust source-controlled camera-profile interpolation test; separately sourced private controlled real-DNG Stage-4 calculation matches all 608,400 channel bytes with the corrected temperature | Adobe 2006–2023 notice beside function; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::SDK_PCS_XY/sdk_xy_to_xyz/sdk_normalize_to_pcs` | `source/dng_xy_coord.h::D50_xy_coord`, `source/dng_xy_coord.cpp::XYtoXYZ/PCStoXYZ`, `source/dng_camera_profile.cpp::NormalizeForwardMatrix` | D50 PCS xy values and per-row forward-matrix white normalization for validated interior chromaticities | `color::tests::interpolates_source_controlled_camera_profile_independently`; private SDK debugger comparison of camera white and PCS matrix | Adobe 2006–2020 (xy header), 2006–2019 (xy implementation) and 2006–2023 (camera profile) notices beside definitions; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::SdkColorTransform::from_dual_illuminant` | `source/dng_color_spec.cpp::NeutralToXY/SetWhiteXY/FindXYZtoCamera_SingleOrDual`, `source/dng_camera_profile.cpp::NormalizeForwardMatrix`, `source/dng_color_space.cpp::SetMatrixToPCS` | Fixed-point white and inverse-temperature interpolation, camera saturation white, calibrated forward camera-to-PCS transform | `color::tests::interpolates_source_controlled_camera_profile_independently` compares generated values with the pinned SDK's live real-DNG color matrices; private controlled Stage-4 diagnostic matches 608,400/608,400 channel bytes after the 2850 K correction | Adobe 2006–2019 (color spec/space) and 2006–2023 (camera profile) notices beside implementation; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::SDK_SRGB_TO_PCS/SDK_PROPHOTO_TO_PCS` | `source/dng_color_space.cpp::dng_space_sRGB/dng_space_ProPhoto` | Pinned intermediate and final color-space matrix values before PCS white normalization | `color::tests::interpolates_source_controlled_camera_profile_independently`; `tools/raw_dng_color_compare.py` controlled Stage-4 result is exact | Adobe 2006–2019 notice beside values; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color.rs::SdkColorTransform::render_sdr_srgb8_u16` | `source/dng_render.cpp::dng_render_task::ProcessArea`, `source/dng_reference.cpp::RefCopyArea16_R32/RefBaselineABCtoRGB/RefBaselineRGBTone/RefBaselineRGBtoRGB/RefCopyAreaR32_8`, `source/dng_1d_table.h::dng_1d_table::Interpolate` | Checked u16 input scaling, camera-white/ProPhoto clipping, optional SDR exposure ramp, intermediate RGB tone, matrix conversion and byte quantization with caller-supplied tables | `color::tests::interpolates_source_controlled_camera_profile_independently` checks selected identity/default-tone and Auto-black pixels; `tools/raw_dng_color_compare.py` matches independent Rust Stage 3, **608,400/608,400** test-only Rust Stage-4 channels and opaque full-size Adobe **public reference** RGBA8888 in four controlled variants, including the unmodified Skia DNG. The Rust public codec still rejects them. | Adobe 2006–2023 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::SdkToneTable` | `source/dng_1d_table.h/cpp::dng_1d_table::Initialize/Interpolate` | Default 4,096-entry float lookup, terminal sentinel, and interpolation of a validated tone function | `tone::tests::sdk_tone_table_interpolates_checked_curve_samples`; the private SDK comparison matches 768/768 neutral Stage-4 bytes. Public colorful-tone creation still rejects. | Adobe 2006–2019 notice beside type; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/acr3_default.rs::SDK_ACR3_DEFAULT` | `source/dng_render.cpp::dng_tone_curve_acr3_default::Evaluate::kTable` | All 1,025 SDK `real32` artistic default-tone samples; generated by `tools/raw_dng_extract_acr3.py` and checked against table SHA-256 `1ae4726011b5bf18a806c1c029a68fe3c56c30b0c9f289d95f23073a86c6fa27` | `tone::tests::pinned_sdk_acr3_default_tone_is_bounded_and_table_driven`; full test-only default-tone, black-None and original real-DNG Stage-4 comparisons each match 608,400/608,400 bytes | Adobe 2006–2023 notice in generated source; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::sdk_acr3_default_tone` | `source/dng_render.cpp::dng_tone_curve_acr3_default::Evaluate` | Float indexing, clamping and interpolation of the SDK ACR3 table | `tone::tests::pinned_sdk_acr3_default_tone_is_bounded_and_table_driven`; separate-process default-tone full-image Stage-4 comparison is exact | Adobe 2006–2023 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::SdkExposureRamp` | `source/dng_render.cpp::dng_render_task::Start/dng_function_exposure_ramp::Evaluate/DoBaseline1DFunction` | Zero-exposure SDR Auto-black/shadows ramp, including default curve transition constants and checked float transfer | `tone::tests::sdk_sdr_auto_black_ramp_has_checked_shadow_transition`; separate-process real-DNG Stage-4 comparison with Auto black matches 608,400/608,400 channels | Adobe 2006–2023 notice beside type; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/tone.rs::apply_sdk_rgb_tone` | `source/dng_reference.cpp::RefBaselineRGBTone` | Clip the intermediate RGB inputs, transform extrema through the tone curve, then interpolate the remaining channel rather than toning output sRGB separately | `tone::tests::sdk_intermediate_rgb_tone_preserves_color_order`; colorful public final output remains Unsupported | Adobe 2006–2023 notice beside method; unmodified license/technology files in `licenses/`. Test-only, not selected by public SkCodec. |
| `ffi/color_probe.rs` sRGB transfer table | `source/dng_color_space.cpp::dng_function_GammaEncode_sRGB::Evaluate` | Sample the SDK's standard sRGB transfer in its default 4,096-entry float table for the controlled SDR color diagnostic | `tools/raw_dng_color_compare.py` matches 608,400/608,400 test-only Stage-4 channels; public real-DNG creation is still Unimplemented | Adobe 2006–2019 notice beside expression; unmodified license/technology files in `licenses/`. Test-only binary, not selected by public SkCodec. |
| `ffi/color_probe.rs` scene shadow default | `source/dng_render.cpp::dng_render::dng_render` | Scene-referred default `fShadows = 5` for the checked zero-exposure/identity-ShadowScale Skia resource | `tools/raw_dng_color_compare.py --tone=sdk-default --black=auto` matches all 608,400 test-only channels for the unmodified real DNG | Adobe 2006–2023 notice beside value; unmodified license/technology files in `licenses/`. Test-only binary, not selected by public SkCodec. |

`color.rs` and `tone.rs` are compiled by Rust test targets and the
explicitly test-only `color_probe` binary, not by the production CXX FFI
bridge (`FFI.rs` declares both under `#[cfg(test)]`). The controlled
real-DNG zero-difference results are intermediate Stage-4 comparisons,
**not** passing public `SkCodec` requests: profile/layout coverage,
scale/metadata/error parity, and public selection remain unimplemented.
The unmodified real DNG's default tone and Auto-black happen to match
in this test-only diagnostic; other inputs must be validated independently.
The earlier 1,104 one-byte differences resulted
from using a nominal 2856 K instead of the SDK's 2850 K Standard Light A.
