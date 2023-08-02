/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_imagefilter_DEFINED
#define sk_imagefilter_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD


// sk_imagefilter_t

SK_C_API void sk_imagefilter_unref(sk_imagefilter_t* cfilter);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_alpha_threshold(const sk_region_t* region, float innerThreshold, float outerThreshold, const sk_imagefilter_t* input);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_arithmetic(float k1, float k2, float k3, float k4, bool enforcePMColor, const sk_imagefilter_t* background, const sk_imagefilter_t* foreground, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_blend(sk_blendmode_t mode, const sk_imagefilter_t* background, const sk_imagefilter_t* foreground, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_blur(float sigmaX, float sigmaY, sk_shader_tilemode_t tileMode, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_color_filter(sk_colorfilter_t* cf, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_compose(const sk_imagefilter_t* outer, const sk_imagefilter_t* inner);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_displacement_map_effect(sk_color_channel_t xChannelSelector, sk_color_channel_t yChannelSelector, float scale, const sk_imagefilter_t* displacement, const sk_imagefilter_t* color, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_drop_shadow(float dx, float dy, float sigmaX, float sigmaY, sk_color_t color, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_drop_shadow_only(float dx, float dy, float sigmaX, float sigmaY, sk_color_t color, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_image(sk_image_t* image, const sk_rect_t* srcRect, const sk_rect_t* dstRect, const sk_sampling_options_t* sampling);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_image_simple(sk_image_t* image, const sk_sampling_options_t* sampling);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_magnifier(const sk_rect_t* lensBounds, float zoomAmount, float inset, const sk_sampling_options_t* sampling, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_matrix_convolution(const sk_isize_t* kernelSize, const float kernel[], float gain, float bias, const sk_ipoint_t* kernelOffset, sk_shader_tilemode_t ctileMode, bool convolveAlpha, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_matrix_transform(const sk_matrix_t* cmatrix, const sk_sampling_options_t* sampling, const sk_imagefilter_t* input);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_merge(const sk_imagefilter_t* cfilters[], int count, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_merge_simple(const sk_imagefilter_t* first, const sk_imagefilter_t* second, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_offset(float dx, float dy, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_picture(const sk_picture_t* picture);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_picture_with_rect(const sk_picture_t* picture, const sk_rect_t* targetRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_shader(const sk_shader_t* shader, bool dither, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_tile(const sk_rect_t* src, const sk_rect_t* dst, const sk_imagefilter_t* input);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_dilate(float radiusX, float radiusY, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_erode(float radiusX, float radiusY, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_distant_lit_diffuse(const sk_point3_t* direction, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_point_lit_diffuse(const sk_point3_t* location, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_spot_lit_diffuse(const sk_point3_t* location, const sk_point3_t* target, float specularExponent, float cutoffAngle, sk_color_t lightColor, float surfaceScale, float kd, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_distant_lit_specular(const sk_point3_t* direction, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_point_lit_specular(const sk_point3_t* location, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect);
SK_C_API sk_imagefilter_t* sk_imagefilter_new_spot_lit_specular(const sk_point3_t* location, const sk_point3_t* target, float specularExponent, float cutoffAngle, sk_color_t lightColor, float surfaceScale, float ks, float shininess, const sk_imagefilter_t* input, const sk_rect_t* cropRect);


SK_C_PLUS_PLUS_END_GUARD

#endif
