/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColorFilter.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkPicture.h"
#include "include/core/SkRegion.h"
#include "include/effects/SkImageFilters.h"

#include "include/c/sk_imagefilter.h"

#include "src/c/sk_types_priv.h"


// sk_imagefilter_t

void sk_imagefilter_unref(sk_imagefilter_t* cfilter) {
    SkSafeUnref(AsImageFilter(cfilter));
}

sk_imagefilter_t* sk_imagefilter_new_alpha_threshold(const sk_region_t* region, float innerThreshold, float outerThreshold, const sk_imagefilter_t* input) {
    return ToImageFilter(SkImageFilters::AlphaThreshold(*AsRegion(region), innerThreshold, outerThreshold, sk_ref_sp(AsImageFilter(input))).release());
}

sk_imagefilter_t* sk_imagefilter_new_arithmetic(float k1, float k2, float k3, float k4, bool enforcePMColor, const sk_imagefilter_t* background, const sk_imagefilter_t* foreground, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Arithmetic(k1, k2, k3, k4, enforcePMColor, sk_ref_sp(AsImageFilter(background)), sk_ref_sp(AsImageFilter(foreground)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_blend(sk_blendmode_t mode, const sk_imagefilter_t* background, const sk_imagefilter_t* foreground, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Blend((SkBlendMode)mode, sk_ref_sp(AsImageFilter(background)), sk_ref_sp(AsImageFilter(foreground)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_blur(float sigmaX, float sigmaY, sk_shader_tilemode_t tileMode, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Blur(sigmaX, sigmaY, (SkTileMode)tileMode, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_color_filter(sk_colorfilter_t* cf, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::ColorFilter(sk_ref_sp(AsColorFilter(cf)), sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_compose(const sk_imagefilter_t* outer, const sk_imagefilter_t* inner) {
    return ToImageFilter(SkImageFilters::Compose(sk_ref_sp(AsImageFilter(outer)), sk_ref_sp(AsImageFilter(inner))).release());
}

sk_imagefilter_t* sk_imagefilter_new_displacement_map_effect(sk_color_channel_t xChannelSelector, sk_color_channel_t yChannelSelector, float scale, const sk_imagefilter_t* displacement, const sk_imagefilter_t* color, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::DisplacementMap((SkColorChannel)xChannelSelector, (SkColorChannel)yChannelSelector, scale, sk_ref_sp(AsImageFilter(displacement)), sk_ref_sp(AsImageFilter(color)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_drop_shadow(float dx, float dy, float sigmaX, float sigmaY, sk_color_t color, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::DropShadow(dx, dy, sigmaX, sigmaY, color, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_drop_shadow_only(float dx, float dy, float sigmaX, float sigmaY, sk_color_t color, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::DropShadowOnly(dx, dy, sigmaX, sigmaY, color, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_image(sk_image_t* image, const sk_rect_t* srcRect, const sk_rect_t* dstRect, const sk_sampling_options_t* sampling) {
    return ToImageFilter(SkImageFilters::Image(sk_ref_sp(AsImage(image)), *AsRect(srcRect), *AsRect(dstRect), *AsSamplingOptions(sampling)).release());
}

sk_imagefilter_t* sk_imagefilter_new_image_simple(sk_image_t* image, const sk_sampling_options_t* sampling) {
    return ToImageFilter(SkImageFilters::Image(sk_ref_sp(AsImage(image)), *AsSamplingOptions(sampling)).release());
}

sk_imagefilter_t* sk_imagefilter_new_magnifier(const sk_rect_t* lensBounds, float zoomAmount, float inset, const sk_sampling_options_t* sampling, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Magnifier(*AsRect(lensBounds), zoomAmount, inset, *AsSamplingOptions(sampling), sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_matrix_convolution(const sk_isize_t* kernelSize, const float kernel[], float gain, float bias, const sk_ipoint_t* kernelOffset, sk_shader_tilemode_t ctileMode, bool convolveAlpha, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::MatrixConvolution(*AsISize(kernelSize), kernel, gain, bias, *AsIPoint(kernelOffset), (SkTileMode)ctileMode, convolveAlpha, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_matrix_transform(const sk_matrix_t* cmatrix, const sk_sampling_options_t* sampling, const sk_imagefilter_t* input) {
    return ToImageFilter(SkImageFilters::MatrixTransform(AsMatrix(cmatrix), *AsSamplingOptions(sampling), sk_ref_sp(AsImageFilter(input))).release());
}

sk_imagefilter_t* sk_imagefilter_new_merge(const sk_imagefilter_t* cfilters[], int count, const sk_rect_t* cropRect) {
    std::vector<sk_sp<SkImageFilter>> filters(count);
    for (int i = 0; i < count; i++) {
        filters[i] = sk_ref_sp(AsImageFilter(cfilters[i]));
    }
    return ToImageFilter(SkImageFilters::Merge(filters.data(), count, AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_merge_simple(const sk_imagefilter_t* first, const sk_imagefilter_t* second, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Merge(sk_ref_sp(AsImageFilter(first)), sk_ref_sp(AsImageFilter(second)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_offset(float dx, float dy, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Offset(dx, dy, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_picture(const sk_picture_t* picture) {
    return ToImageFilter(SkImageFilters::Picture(sk_ref_sp(AsPicture(picture))).release());
}

sk_imagefilter_t* sk_imagefilter_new_picture_with_rect(const sk_picture_t* picture, const sk_rect_t* targetRect) {
    return ToImageFilter(SkImageFilters::Picture(sk_ref_sp(AsPicture(picture)), *AsRect(targetRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_shader(const sk_shader_t* shader, bool dither, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Shader(sk_ref_sp(AsShader(shader)), (SkImageFilters::Dither)dither, AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_tile(const sk_rect_t* src, const sk_rect_t* dst, const sk_imagefilter_t* input) {
    return ToImageFilter(SkImageFilters::Tile(*AsRect(src), *AsRect(dst), sk_ref_sp(AsImageFilter(input))).release());
}

sk_imagefilter_t* sk_imagefilter_new_dilate(float radiusX, float radiusY, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Dilate(radiusX, radiusY, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_erode(float radiusX, float radiusY, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::Erode(radiusX, radiusY, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_distant_lit_diffuse(const sk_point3_t* direction, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::DistantLitDiffuse(*AsPoint3(direction), lightColor, surfaceScale, kd, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_point_lit_diffuse(const sk_point3_t* location, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::PointLitDiffuse(*AsPoint3(location), lightColor, surfaceScale, kd, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_spot_lit_diffuse(const sk_point3_t* location, const sk_point3_t* target, float specularExponent, float cutoffAngle, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::SpotLitDiffuse(*AsPoint3(location), *AsPoint3(target), specularExponent, cutoffAngle, lightColor, surfaceScale, kd, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_distant_lit_specular(const sk_point3_t* direction, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::DistantLitSpecular(*AsPoint3(direction), lightColor, surfaceScale, ks, shininess, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_point_lit_specular(const sk_point3_t* location, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::PointLitSpecular(*AsPoint3(location), lightColor, surfaceScale, ks, shininess, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}

sk_imagefilter_t* sk_imagefilter_new_spot_lit_specular(const sk_point3_t* location, const sk_point3_t* target, float specularExponent, float cutoffAngle, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect) {
    return ToImageFilter(SkImageFilters::SpotLitSpecular(*AsPoint3(location), *AsPoint3(target), specularExponent, cutoffAngle, lightColor, surfaceScale, ks, shininess, sk_ref_sp(AsImageFilter(input)), AsRect(cropRect)).release());
}
