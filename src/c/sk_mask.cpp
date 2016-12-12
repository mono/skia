/*
 * Copyright 2016 Bluebeam Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkMask.h"
#include "SkColor.h"
#include "SkColorPriv.h"

#include "sk_mask.h"
#include "sk_types_priv.h"
#include "sk_types.h"

sk_mask_t* sk_mask_new(uint8_t* pixels, const sk_mask_format_t format, uint32_t row_bytes, sk_irect_t* crect) {
    SkMask* mask = new SkMask();
    mask->fImage = pixels;
    mask->fFormat = (SkMask::Format)format;
    mask->fRowBytes = row_bytes;
    mask->fBounds = *AsIRect(crect);
    return ToMask(mask);
}

sk_mask_t* sk_mask_new2(size_t byteCount, const sk_mask_format_t format, uint32_t row_bytes, sk_irect_t* crect) {
    SkMask* mask = new SkMask();
    mask->fImage = SKMask::AllocImage(byteCount);
    mask->fFormat = (SkMask::Format)format;
    mask->fRowBytes = row_bytes;
    mask->fBounds = *AsIRect(crect);
    return ToMask(mask);
}

void sk_mask_destructor(sk_mask_t* cmask, bool owns_pixels) {
    SkMask* mask = AsMask(cmask);
    if (owns_pixels) {
        SkMask::FreeImage(mask->fImage);
    }

    delete mask;
}

uint8_t* sk_mask_get_image(sk_mask_t* cmask) {
    return AsMask(cmask)->fImage;
}

sk_irect_t sk_mask_get_bounds(sk_mask_t* cmask) {
    SkMask* mask = AsMask(cmask);
    return ToIRect(mask->fBounds);
}

uint32_t sk_mask_get_row_bytes(sk_mask_t* cmask) {
    return AsMask(cmask)->fRowBytes;
}

sk_mask_format_t sk_mask_get_format(sk_mask_t* cmask) {
    SkMask::Format format = AsMask(cmask)->fFormat;
    return (sk_mask_format_t)format;
}

sk_color_t get_pixel_color(sk_mask_t* cmask, int x, int y) {
    SkMask* mask = AsMask(cmask);
    switch (mask->fFormat) {
        default:
        case SkMask::Format::kBW_Format:
            return SkColorSetA(0, *mask->getAddr1(x, y));
        case SkMask::Format::kA8_Format:
            return SkColorSetA(0, *mask->getAddr8(x, y));
        case SkMask::Format::kARGB32_Format:
            return *mask->getAddr32(x, y);
        case SkMask::Format::kLCD16_Format:
            return SkPixel16ToColor(*mask->getAddrLCD16(x, y));
        case SkMask::Format::k3D_Format:
            return 0;
    }
}
