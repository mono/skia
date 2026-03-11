/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_pixmap_DEFINED
#define sk_pixmap_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// SkPixmap

SK_C_API void sk_pixmap_destructor(sk_pixmap_t* cpixmap);
SK_C_API sk_pixmap_t* sk_pixmap_new(void);
SK_C_API sk_pixmap_t* sk_pixmap_new_with_params(const sk_imageinfo_t* cinfo, const void* addr, size_t rowBytes);
SK_C_API void sk_pixmap_reset(sk_pixmap_t* cpixmap);
SK_C_API void sk_pixmap_reset_with_params(sk_pixmap_t* cpixmap, const sk_imageinfo_t* cinfo, const void* addr, size_t rowBytes);
SK_C_API void sk_pixmap_set_colorspace(sk_pixmap_t* cpixmap, sk_colorspace_t* colorspace);
SK_C_API bool sk_pixmap_extract_subset(const sk_pixmap_t* cpixmap, sk_pixmap_t* result, const sk_irect_t* subset);
SK_C_API void sk_pixmap_get_info(const sk_pixmap_t* cpixmap, sk_imageinfo_t* cinfo);
SK_C_API size_t sk_pixmap_get_row_bytes(const sk_pixmap_t* cpixmap);
SK_C_API sk_colorspace_t* sk_pixmap_get_colorspace(const sk_pixmap_t* cpixmap);
SK_C_API bool sk_pixmap_compute_is_opaque(const sk_pixmap_t* cpixmap);
SK_C_API sk_color_t sk_pixmap_get_pixel_color(const sk_pixmap_t* cpixmap, int x, int y);
SK_C_API void sk_pixmap_get_pixel_color4f(const sk_pixmap_t* cpixmap, int x, int y, sk_color4f_t* color);
SK_C_API float sk_pixmap_get_pixel_alphaf(const sk_pixmap_t* cpixmap, int x, int y);
SK_C_API void* sk_pixmap_get_writable_addr(const sk_pixmap_t* cpixmap);
SK_C_API void* sk_pixmap_get_writeable_addr_with_xy(const sk_pixmap_t* cpixmap, int x, int y);
SK_C_API bool sk_pixmap_read_pixels(const sk_pixmap_t* cpixmap, const sk_imageinfo_t* dstInfo, void* dstPixels, size_t dstRowBytes, int srcX, int srcY);
SK_C_API bool sk_pixmap_scale_pixels(const sk_pixmap_t* cpixmap, const sk_pixmap_t* dst, const sk_sampling_options_t* sampling);
SK_C_API bool sk_pixmap_erase_color(const sk_pixmap_t* cpixmap, sk_color_t color, const sk_irect_t* subset);
SK_C_API bool sk_pixmap_erase_color4f(const sk_pixmap_t* cpixmap, const sk_color4f_t* color, const sk_irect_t* subset);

// Sk*Encoder

SK_C_API bool sk_webpencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_webpencoder_options_t* options);
SK_C_API bool sk_jpegencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_jpegencoder_options_t* options);
SK_C_API bool sk_pngencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_pngencoder_options_t* options);

// SkSwizzle

SK_C_API void sk_swizzle_swap_rb(uint32_t* dest, const uint32_t* src, int count);

// SkColor

SK_C_API sk_color_t sk_color_unpremultiply(const sk_pmcolor_t pmcolor);
SK_C_API sk_pmcolor_t sk_color_premultiply(const sk_color_t color);
SK_C_API void sk_color_unpremultiply_array(const sk_pmcolor_t* pmcolors, int size, sk_color_t* colors);
SK_C_API void sk_color_premultiply_array(const sk_color_t* colors, int size, sk_pmcolor_t* pmcolors);
SK_C_API void sk_color_get_bit_shift(int* a, int* r, int* g, int* b);

SK_C_PLUS_PLUS_END_GUARD

#endif
