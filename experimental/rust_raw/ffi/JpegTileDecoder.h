/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkRawRustJpegTileDecoder_DEFINED
#define SkRawRustJpegTileDecoder_DEFINED

#include "third_party/rust/cxx/v1/cxx.h"

#include <cstdint>

namespace rust_raw {

// Return 0 for success, 1 for invalid input, 2 for unsupported input,
// 3 for incomplete input, or 4 for allocation failure.
uint8_t validate_jpeg_tile(rust::Slice<const uint8_t> jpeg,
                           uint32_t width,
                           uint32_t height);

uint8_t decode_jpeg_tile(rust::Slice<const uint8_t> jpeg,
                         uint32_t width,
                         uint32_t height,
                         uint32_t visible_width,
                         uint32_t visible_height,
                         rust::Slice<uint8_t> rgb);

uint8_t validate_jpeg16_bayer_tile(rust::Slice<const uint8_t> jpeg,
                                   uint32_t width,
                                   uint32_t height);

uint8_t decode_jpeg16_bayer_tile(rust::Slice<const uint8_t> jpeg,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t visible_width,
                                 uint32_t visible_height,
                                 rust::Slice<uint16_t> samples);

}  // namespace rust_raw

#endif  // SkRawRustJpegTileDecoder_DEFINED
