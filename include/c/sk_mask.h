/*
 * Copyright 2016 Bluebeam Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// EXPERIMENTAL EXPERIMENTAL EXPERIMENTAL EXPERIMENTAL
// DO NOT USE -- FOR INTERNAL TESTING ONLY

#ifndef sk_mask_DEFINED
#define sk_mask_DEFINED

#include "sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

SK_API sk_mask_t* sk_mask_new(uint8_t* pixels, const sk_mask_format_t format, uint32_t row_bytes, sk_irect_t* crect);
SK_API void sk_mask_destructor(sk_mask_t* cmask);
SK_API uint8_t* sk_mask_get_image(sk_mask_t* cmask);
SK_API sk_irect_t sk_mask_get_bounds(sk_mask_t* cmask);
SK_API size_t sk_mask_get_image_size(sk_mask_t* cmask);
SK_API uint32_t sk_mask_get_row_bytes(sk_mask_t* cmask);
SK_API sk_mask_format_t sk_mask_get_format(sk_mask_t* cmask);
SK_API sk_color_t sk_mask_get_pixel_color(sk_mask_t* cmask, int x, int y);

SK_C_PLUS_PLUS_END_GUARD

#endif

