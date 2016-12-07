/*
 * Copyright 2016 Bluebeam Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkMask.h"
#include "SkColor.h"
#include "SkColorPriv.h"
#include "sk_types.h"
#include "sk_mask.h"

sk_mask_t* sk_mask_new(uint8_t* pixels, const sk_mask_format_t format, uint32_t row_bytes, sk_irect_t* crect) {
    SkMask* mask = new SkMask();
    mask->fImage = pixels;
    mask->fFormat = (SkMask::Format)format;
    mask->fRowBytes = row_bytes;
    mask-fBounds = *AsIRect(crect);
    return ToMask(mask);
}

void sk_mask_destructor(sk_mask_t* cmask) {
    delete AsMask(cmask);
}

sk_color_t get_pixel_color(sk_mask_t* cmask, int x, int y) {
    SkMask* mask = AsMask(cmask);
    switch (mask->fFormat) {
        default:
        case SkMask::Format::kBW_Format:
            return SkColorSetA(0, mask->getAddr1(x, y));
        case SkMask::Format::kA8_Format:
            return SkColorSetA(0, mask->getAddr8(x, y));
        case SkMask::Format::kARGB32_Format:
            return mask->getAddr32(x, y);
        case SkMask::Format::kLCD16_Format:
            return SkPixel16ToColor(mask->getAddr16(x, y));
        case SkMask::Format::k3D_Format:
            return 0;
    }
}