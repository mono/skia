/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkRawRustDeflateStripDecoder_DEFINED
#define SkRawRustDeflateStripDecoder_DEFINED

#include "third_party/rust/cxx/v1/cxx.h"

#include <cstddef>
#include <cstdint>

namespace rust_raw {

// Same status codes as JpegTileDecoder.h. Decompression is synchronous and
// borrows neither input nor output beyond the call.
uint8_t validate_dng_deflate_strip(rust::Slice<const uint8_t> encoded,
                                   size_t expected_bytes);

uint8_t inflate_dng_strip(rust::Slice<const uint8_t> encoded,
                          rust::Slice<uint8_t> decoded);

}  // namespace rust_raw

#endif  // SkRawRustDeflateStripDecoder_DEFINED
