/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorPriv.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkShader.h"
#include "include/core/SkUnPreMultiply.h"

#include "include/c/sk_bitmap.h"

#include "src/c/sk_types_priv.h"

void sk_bitmap_destructor(sk_bitmap_t* cbitmap) {
    delete AsBitmap(cbitmap);
}

sk_bitmap_t* sk_bitmap_new(void) {
    return ToBitmap(new SkBitmap());
}

void sk_bitmap_get_info(sk_bitmap_t* cbitmap, sk_imageinfo_t* info) {
    *info = ToImageInfo(AsBitmap(cbitmap)->info());
}

bool sk_bitmap_set_info(sk_bitmap_t* cbitmap, const sk_imageinfo_t* requestedInfo, size_t rowBytes) {
    return AsBitmap(cbitmap)->setInfo(AsImageInfo(requestedInfo), rowBytes);
}

void* sk_bitmap_get_pixels(sk_bitmap_t* cbitmap, size_t* length) {
    SkBitmap* bmp = AsBitmap(cbitmap);
    *length = bmp->computeByteSize();
    return bmp->getPixels();
}

size_t sk_bitmap_get_row_bytes(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->rowBytes();
}

size_t sk_bitmap_get_byte_count(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->computeByteSize();
}

uint32_t sk_bitmap_get_generation_id(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->getGenerationID();
}

void sk_bitmap_reset(sk_bitmap_t* cbitmap) {
    AsBitmap(cbitmap)->reset();
}

bool sk_bitmap_is_null(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->isNull();
}

bool sk_bitmap_is_immutable(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->isImmutable();
}

void sk_bitmap_set_immutable(sk_bitmap_t* cbitmap) {
    AsBitmap(cbitmap)->setImmutable();
}

void sk_bitmap_erase(sk_bitmap_t* cbitmap, sk_color_t color) {
    AsBitmap(cbitmap)->eraseColor(color);
}

void sk_bitmap_erase_rect(sk_bitmap_t* cbitmap, sk_color_t color, sk_irect_t* rect) {
    AsBitmap(cbitmap)->erase(color, *AsIRect(rect));
}

uint8_t* sk_bitmap_get_addr_8(sk_bitmap_t* cbitmap, int x, int y) {
    return AsBitmap(cbitmap)->getAddr8(x, y);
}

uint16_t* sk_bitmap_get_addr_16(sk_bitmap_t* cbitmap, int x, int y) {
    return AsBitmap(cbitmap)->getAddr16(x, y);
}

uint32_t* sk_bitmap_get_addr_32(sk_bitmap_t* cbitmap, int x, int y) {
    return AsBitmap(cbitmap)->getAddr32(x, y);
}

void* sk_bitmap_get_addr(sk_bitmap_t* cbitmap, int x, int y) {
    return AsBitmap(cbitmap)->getAddr(x, y);
}

sk_color_t sk_bitmap_get_pixel_color(sk_bitmap_t* cbitmap, int x, int y) {
    return AsBitmap(cbitmap)->getColor(x, y);
}

bool sk_bitmap_ready_to_draw(sk_bitmap_t* cbitmap) {
    return AsBitmap(cbitmap)->readyToDraw();
}

bool sk_bitmap_compute_is_opaque(sk_bitmap_t* cbitmap) {
    return SkBitmap::ComputeIsOpaque(*AsBitmap(cbitmap));
}

const sk_pixmap_t* sk_bitmap_get_pixmap(sk_bitmap_t* cbitmap) {
    const SkPixmap& pixmap = AsBitmap(cbitmap)->pixmap();
    return ToPixmap(&pixmap);
}

void sk_bitmap_get_pixel_colors(sk_bitmap_t* cbitmap, sk_color_t* colors) {
    SkBitmap* bmp = AsBitmap(cbitmap);
    int w = bmp->width();
    int h = bmp->height();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *colors = bmp->getColor(x, y);
            colors++;
        }
    }
}

bool sk_bitmap_write_pixels_at_location(sk_bitmap_t* cbitmap, const sk_pixmap_t* cpixmap, int x, int y) {
    return AsBitmap(cbitmap)->writePixels(*AsPixmap(cpixmap), x, y);
}

bool sk_bitmap_install_pixels(sk_bitmap_t* cbitmap, const sk_imageinfo_t* cinfo, void* pixels, size_t rowBytes, const sk_bitmap_release_proc releaseProc, void* context) {
    return AsBitmap(cbitmap)->installPixels(AsImageInfo(cinfo), pixels, rowBytes, releaseProc, context);
}

bool sk_bitmap_install_pixels_with_pixmap(sk_bitmap_t* cbitmap, const sk_pixmap_t* cpixmap) {
    return AsBitmap(cbitmap)->installPixels(*AsPixmap(cpixmap));
}

bool sk_bitmap_try_alloc_pixels(sk_bitmap_t* cbitmap, const sk_imageinfo_t* requestedInfo, size_t rowBytes) {
    return AsBitmap(cbitmap)->tryAllocPixels(AsImageInfo(requestedInfo), rowBytes);
}

bool sk_bitmap_try_alloc_pixels_with_flags(sk_bitmap_t* cbitmap, const sk_imageinfo_t* requestedInfo, uint32_t flags) {
    return AsBitmap(cbitmap)->tryAllocPixelsFlags(AsImageInfo(requestedInfo), flags);
}

void sk_bitmap_set_pixels(sk_bitmap_t* cbitmap, void* pixels) {
    AsBitmap(cbitmap)->setPixels(pixels);
}

bool sk_bitmap_peek_pixels(sk_bitmap_t* cbitmap, sk_pixmap_t* cpixmap) {
    return AsBitmap(cbitmap)->peekPixels(AsPixmap(cpixmap));
}

bool sk_bitmap_extract_subset(sk_bitmap_t* cbitmap, sk_bitmap_t* cdst, sk_irect_t* subset) {
    return AsBitmap(cbitmap)->extractSubset(AsBitmap(cdst), *AsIRect(subset));
}

bool sk_bitmap_extract_alpha(sk_bitmap_t* cbitmap, sk_bitmap_t* cdst, const sk_paint_t* paint, sk_ipoint_t* offset) {
    return AsBitmap(cbitmap)->extractAlpha(AsBitmap(cdst), AsPaint(paint), AsIPoint(offset));
}

void sk_bitmap_notify_pixels_changed(sk_bitmap_t* cbitmap) {
    AsBitmap(cbitmap)->notifyPixelsChanged();
}

void sk_bitmap_swap(sk_bitmap_t* cbitmap, sk_bitmap_t* cother) {
    AsBitmap(cbitmap)->swap(*AsBitmap(cother));
}

sk_shader_t* sk_bitmap_make_shader(sk_bitmap_t* cbitmap, sk_shader_tilemode_t tmx, sk_shader_tilemode_t tmy, sk_sampling_options_t* sampling, const sk_matrix_t* cmatrix) {
    SkMatrix m;
    if (cmatrix) {
        m = AsMatrix(cmatrix);
    }
    return ToShader(AsBitmap(cbitmap)->makeShader((SkTileMode)tmx, (SkTileMode)tmy, *AsSamplingOptions(sampling), cmatrix ? &m : nullptr).release());
}
