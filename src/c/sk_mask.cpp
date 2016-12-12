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
    mask->fBounds = *AsIRect(crect);
    mask->fFormat = (SkMask::Format)format;
    mask->fRowBytes = row_bytes;
    size_t size = row_bytes * mask->fBounds.height();
    if (mask->fFormat == SkMask::k3D_Format) {
        size *= 3;
    }
    mask->fImage = SkMask::AllocImage(size);
    memcpy(mask->fImage, pixels, size);
    return ToMask(mask);
}

void sk_mask_destructor(sk_mask_t* cmask) {
    SkMask* mask = AsMask(cmask);
    SkMask::FreeImage(mask->fImage);
    delete mask;
}

uint8_t* sk_mask_get_image(sk_mask_t* cmask) {
    return AsMask(cmask)->fImage;
}

sk_irect_t sk_mask_get_bounds(sk_mask_t* cmask) {
    SkMask* mask = AsMask(cmask);
    return ToIRect(mask->fBounds);
}

SK_API size_t sk_mask_get_image_size(sk_mask_t* cmask) {
    SkMask* mask = AsMask(cmask);
    if (mask->fFormat == SkMask::k3D_Format) {
        return mask->computeTotalImageSize();
    }
    return mask->computeImageSize();
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
        case SkMask::kBW_Format:
            return SkColorSetA(0, *mask->getAddr1(x, y));
        default:
        case SkMask::kA8_Format:
            return SkColorSetA(0, *mask->getAddr8(x, y));
        case SkMask::kARGB32_Format:
            return *mask->getAddr32(x, y);
        case SkMask::Format::kLCD16_Format:
            return SkPixel16ToColor(*mask->getAddrLCD16(x, y));
        case SkMask::k3D_Format:
            return 0;
    }
}
