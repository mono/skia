/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPixmap.h"
#include "include/core/SkSwizzle.h"
#include "include/core/SkUnPreMultiply.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"

#include "include/c/sk_pixmap.h"

#include "src/c/sk_types_priv.h"


// SkPixmap

void sk_pixmap_destructor(sk_pixmap_t* cpixmap) {
    delete AsPixmap(cpixmap);
}

sk_pixmap_t* sk_pixmap_new(void) {
    return ToPixmap(new SkPixmap());
}

sk_pixmap_t* sk_pixmap_new_with_params(const sk_imageinfo_t* cinfo, const void* addr, size_t rowBytes) {
    return ToPixmap(new SkPixmap(AsImageInfo(cinfo), addr, rowBytes));
}

void sk_pixmap_reset(sk_pixmap_t* cpixmap) {
    AsPixmap(cpixmap)->reset();
}

void sk_pixmap_reset_with_params(sk_pixmap_t* cpixmap, const sk_imageinfo_t* cinfo, const void* addr, size_t rowBytes) {
    AsPixmap(cpixmap)->reset(AsImageInfo(cinfo), addr, rowBytes);
}

void sk_pixmap_set_colorspace(sk_pixmap_t* cpixmap, sk_colorspace_t* colorspace) {
    AsPixmap(cpixmap)->setColorSpace(sk_ref_sp(AsColorSpace(colorspace)));
}

bool sk_pixmap_extract_subset(const sk_pixmap_t* cpixmap, sk_pixmap_t* result, const sk_irect_t* subset) {
    return AsPixmap(cpixmap)->extractSubset(AsPixmap(result), *AsIRect(subset));
}

void sk_pixmap_get_info(const sk_pixmap_t* cpixmap, sk_imageinfo_t* cinfo) {
    *cinfo = ToImageInfo(AsPixmap(cpixmap)->info());
}

size_t sk_pixmap_get_row_bytes(const sk_pixmap_t* cpixmap) {
    return AsPixmap(cpixmap)->rowBytes();
}

sk_colorspace_t* sk_pixmap_get_colorspace(const sk_pixmap_t* cpixmap) {
    return ToColorSpace(AsPixmap(cpixmap)->refColorSpace().release());
}

bool sk_pixmap_compute_is_opaque(const sk_pixmap_t* cpixmap) {
    return AsPixmap(cpixmap)->computeIsOpaque();
}

sk_color_t sk_pixmap_get_pixel_color(const sk_pixmap_t* cpixmap, int x, int y) {
    return AsPixmap(cpixmap)->getColor(x, y);
}

void sk_pixmap_get_pixel_color4f(const sk_pixmap_t* cpixmap, int x, int y, sk_color4f_t* color) {
    *color = ToColor4f(AsPixmap(cpixmap)->getColor4f(x, y));
}

float sk_pixmap_get_pixel_alphaf(const sk_pixmap_t* cpixmap, int x, int y) {
    return AsPixmap(cpixmap)->getAlphaf(x, y);
}

void* sk_pixmap_get_writable_addr(const sk_pixmap_t* cpixmap) {
    return AsPixmap(cpixmap)->writable_addr();
}

void* sk_pixmap_get_writeable_addr_with_xy(const sk_pixmap_t* cpixmap, int x, int y) {
    return AsPixmap(cpixmap)->writable_addr(x, y);
}

bool sk_pixmap_read_pixels(const sk_pixmap_t* cpixmap, const sk_imageinfo_t* dstInfo, void* dstPixels, size_t dstRowBytes, int srcX, int srcY) {
    return AsPixmap(cpixmap)->readPixels(AsImageInfo(dstInfo), dstPixels, dstRowBytes, srcX, srcY);
}

bool sk_pixmap_scale_pixels(const sk_pixmap_t* cpixmap, const sk_pixmap_t* dst, const sk_sampling_options_t* sampling) {
    return AsPixmap(cpixmap)->scalePixels(*AsPixmap(dst), *AsSamplingOptions(sampling));
}

bool sk_pixmap_erase_color(const sk_pixmap_t* cpixmap, sk_color_t color, const sk_irect_t* subset) {
    return AsPixmap(cpixmap)->erase((SkColor)color, *AsIRect(subset));
}

bool sk_pixmap_erase_color4f(const sk_pixmap_t* cpixmap, const sk_color4f_t* color, const sk_irect_t* subset) {
    return AsPixmap(cpixmap)->erase(*AsColor4f(color), AsIRect(subset));
}


// Sk*Encoder

bool sk_webpencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_webpencoder_options_t* options) {
    return SkWebpEncoder::Encode(AsWStream(dst), *AsPixmap(src), *AsWebpEncoderOptions(options));
}

bool sk_jpegencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_jpegencoder_options_t* options) {
    return SkJpegEncoder::Encode(AsWStream(dst), *AsPixmap(src), *AsJpegEncoderOptions(options));
}

bool sk_pngencoder_encode(sk_wstream_t* dst, const sk_pixmap_t* src, const sk_pngencoder_options_t* options) {
    return SkPngEncoder::Encode(AsWStream(dst), *AsPixmap(src), *AsPngEncoderOptions(options));
}


// SkSwizzle

void sk_swizzle_swap_rb(uint32_t* dest, const uint32_t* src, int count) {
    SkSwapRB(dest, src, count);
}


// SkColor

sk_color_t sk_color_unpremultiply(const sk_pmcolor_t pmcolor) {
    return SkUnPreMultiply::PMColorToColor(pmcolor);
}

sk_pmcolor_t sk_color_premultiply(const sk_color_t color) {
    return SkPreMultiplyColor(color);
}

void sk_color_unpremultiply_array(const sk_pmcolor_t* pmcolors, int size, sk_color_t* colors) {
    for (int i = 0; i < size; ++i) {
        colors[i] = SkUnPreMultiply::PMColorToColor(pmcolors[i]);
    }
}

void sk_color_premultiply_array(const sk_color_t* colors, int size, sk_pmcolor_t* pmcolors) {
    for (int i = 0; i < size; ++i) {
        pmcolors[i] = SkPreMultiplyColor(colors[i]);
    }
}

void sk_color_get_bit_shift(int* a, int* r, int* g, int* b) {
    *a = (int)SK_A32_SHIFT;
    *r = (int)SK_R32_SHIFT;
    *g = (int)SK_G32_SHIFT;
    *b = (int)SK_B32_SHIFT;
}
