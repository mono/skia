/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBitmap.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkPicture.h"
#include "include/core/SkShader.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkPerlinNoiseShader.h"

#include "include/c/sk_shader.h"

#include "src/c/sk_types_priv.h"

// SkShader

void sk_shader_ref(sk_shader_t* shader) {
    SkSafeRef(AsShader(shader));
}

void sk_shader_unref(sk_shader_t* shader) {
    SkSafeUnref(AsShader(shader));
}

sk_shader_t* sk_shader_with_local_matrix(const sk_shader_t* shader, const sk_matrix_t* localMatrix) {
    return ToShader(AsShader(shader)->makeWithLocalMatrix(AsMatrix(localMatrix)).release());
}

sk_shader_t* sk_shader_with_color_filter(const sk_shader_t* shader, const sk_colorfilter_t* filter) {
    return ToShader(AsShader(shader)->makeWithColorFilter(sk_ref_sp(AsColorFilter(filter))).release());
}

// SkShaders

sk_shader_t* sk_shader_new_empty(void) {
    return ToShader(SkShaders::Empty().release());
}

sk_shader_t* sk_shader_new_color(sk_color_t color) {
    return ToShader(SkShaders::Color(color).release());
}

sk_shader_t* sk_shader_new_color4f(const sk_color4f_t* color, const sk_colorspace_t* colorspace) {
    return ToShader(SkShaders::Color(*AsColor4f(color), sk_ref_sp(AsColorSpace(colorspace))).release());
}

sk_shader_t* sk_shader_new_blend(sk_blendmode_t mode, const sk_shader_t* dst, const sk_shader_t* src) {
    return ToShader(SkShaders::Blend((SkBlendMode)mode, sk_ref_sp(AsShader(dst)), sk_ref_sp(AsShader(src))).release());
}

sk_shader_t* sk_shader_new_blender(sk_blender_t* blender, const sk_shader_t* dst, const sk_shader_t* src) {
    return ToShader(SkShaders::Blend(sk_ref_sp(AsBlender(blender)), sk_ref_sp(AsShader(dst)), sk_ref_sp(AsShader(src))).release());
}

// Helper to convert SkColor array to SkColor4f array
static std::vector<SkColor4f> ColorsToColor4f(const sk_color_t colors[], int count) {
    std::vector<SkColor4f> c4f(count);
    for (int i = 0; i < count; i++) {
        c4f[i] = SkColor4f::FromColor(colors[i]);
    }
    return c4f;
}

// SkGradient

sk_shader_t* sk_shader_new_linear_gradient(const sk_point_t points[2], const sk_color_t colors[], const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    auto c4f = ColorsToColor4f(colors, colorCount);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(c4f.data(), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode);
    return ToShader(SkShaders::LinearGradient(AsPoint(points), SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_linear_gradient_color4f(const sk_point_t points[2], const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(AsColor4f(colors), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode,
                          sk_ref_sp(AsColorSpace(colorspace)));
    return ToShader(SkShaders::LinearGradient(AsPoint(points), SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_radial_gradient(const sk_point_t* center, float radius, const sk_color_t colors[], const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    auto c4f = ColorsToColor4f(colors, colorCount);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(c4f.data(), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode);
    return ToShader(SkShaders::RadialGradient(*AsPoint(center), (SkScalar)radius, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_radial_gradient_color4f(const sk_point_t* center, float radius, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(AsColor4f(colors), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode,
                          sk_ref_sp(AsColorSpace(colorspace)));
    return ToShader(SkShaders::RadialGradient(*AsPoint(center), (SkScalar)radius, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_sweep_gradient(const sk_point_t* center, const sk_color_t colors[], const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, float startAngle, float endAngle, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    auto c4f = ColorsToColor4f(colors, colorCount);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(c4f.data(), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode);
    return ToShader(SkShaders::SweepGradient({center->x, center->y}, startAngle, endAngle, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_sweep_gradient_color4f(const sk_point_t* center, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, float startAngle, float endAngle, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(AsColor4f(colors), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode,
                          sk_ref_sp(AsColorSpace(colorspace)));
    return ToShader(SkShaders::SweepGradient({center->x, center->y}, startAngle, endAngle, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_two_point_conical_gradient(const sk_point_t* start, float startRadius, const sk_point_t* end, float endRadius, const sk_color_t colors[], const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    auto c4f = ColorsToColor4f(colors, colorCount);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(c4f.data(), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode);
    return ToShader(SkShaders::TwoPointConicalGradient(*AsPoint(start), startRadius, *AsPoint(end), endRadius, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_two_point_conical_gradient_color4f(const sk_point_t* start, float startRadius, const sk_point_t* end, float endRadius, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    SkGradient::Colors gc(SkSpan<const SkColor4f>(AsColor4f(colors), colorCount),
                          colorPos ? SkSpan<const float>(colorPos, colorCount) : SkSpan<const float>(),
                          (SkTileMode)tileMode,
                          sk_ref_sp(AsColorSpace(colorspace)));
    return ToShader(SkShaders::TwoPointConicalGradient(*AsPoint(start), startRadius, *AsPoint(end), endRadius, SkGradient(gc, {}), localMatrix ? &m : nullptr).release());
}

static SkGradientShader::Interpolation AsInterpolation(const sk_gradient_interpolation_t* interp) {
    SkGradientShader::Interpolation result;
    if (interp) {
        result.fInPremul = interp->fInPremul
            ? SkGradientShader::Interpolation::InPremul::kYes
            : SkGradientShader::Interpolation::InPremul::kNo;
        result.fColorSpace = (SkGradientShader::Interpolation::ColorSpace)interp->fColorSpace;
        result.fHueMethod = (SkGradientShader::Interpolation::HueMethod)interp->fHueMethod;
    }
    return result;
}

sk_shader_t* sk_shader_new_linear_gradient_interpolation(const sk_point_t points[2], const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_gradient_interpolation_t* interpolation, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    return ToShader(SkGradientShader::MakeLinear(AsPoint(points), AsColor4f(colors), sk_ref_sp(AsColorSpace(colorspace)), colorPos, colorCount, (SkTileMode)tileMode, AsInterpolation(interpolation), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_radial_gradient_interpolation(const sk_point_t* center, float radius, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_gradient_interpolation_t* interpolation, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    return ToShader(SkGradientShader::MakeRadial(*AsPoint(center), (SkScalar)radius, AsColor4f(colors), sk_ref_sp(AsColorSpace(colorspace)), colorPos, colorCount, (SkTileMode)tileMode, AsInterpolation(interpolation), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_sweep_gradient_interpolation(const sk_point_t* center, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, float startAngle, float endAngle, const sk_gradient_interpolation_t* interpolation, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    return ToShader(SkGradientShader::MakeSweep(center->x, center->y, AsColor4f(colors), sk_ref_sp(AsColorSpace(colorspace)), colorPos, colorCount, (SkTileMode)tileMode, startAngle, endAngle, AsInterpolation(interpolation), localMatrix ? &m : nullptr).release());
}

sk_shader_t* sk_shader_new_two_point_conical_gradient_interpolation(const sk_point_t* start, float startRadius, const sk_point_t* end, float endRadius, const sk_color4f_t* colors, const sk_colorspace_t* colorspace, const float colorPos[], int colorCount, sk_shader_tilemode_t tileMode, const sk_gradient_interpolation_t* interpolation, const sk_matrix_t* localMatrix) {
    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    return ToShader(SkGradientShader::MakeTwoPointConical(*AsPoint(start), startRadius, *AsPoint(end), endRadius, AsColor4f(colors), sk_ref_sp(AsColorSpace(colorspace)), colorPos, colorCount, (SkTileMode)tileMode, AsInterpolation(interpolation), localMatrix ? &m : nullptr).release());
}

// SkPerlinNoiseShader

sk_shader_t* sk_shader_new_perlin_noise_fractal_noise(float baseFrequencyX, float baseFrequencyY, int numOctaves, float seed, const sk_isize_t* tileSize) {
    return ToShader(SkShaders::MakeFractalNoise(baseFrequencyX, baseFrequencyY, numOctaves, seed, AsISize(tileSize)).release());
}

sk_shader_t* sk_shader_new_perlin_noise_turbulence(float baseFrequencyX, float baseFrequencyY, int numOctaves, float seed, const sk_isize_t* tileSize) {
    return ToShader(SkShaders::MakeTurbulence(baseFrequencyX, baseFrequencyY,  numOctaves,  seed,  AsISize(tileSize)).release());
}
