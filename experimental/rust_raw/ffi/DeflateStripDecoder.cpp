/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/ffi/DeflateStripDecoder.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>

namespace rust_raw {

namespace {

uint8_t inflate_strip(rust::Slice<const uint8_t> encoded,
                      uint8_t* decoded,
                      size_t expected_bytes,
                      bool validate_only) {
    if (encoded.empty() || expected_bytes == 0 || (!validate_only && !decoded)) {
        return 1;
    }
    std::array<uint8_t, 8192> scratch;
    z_stream stream{};
    int status = inflateInit(&stream);
    if (status != Z_OK) {
        return status == Z_MEM_ERROR ? 4 : 1;
    }
    size_t supplied = 0;
    size_t assigned = 0;
    size_t produced = 0;
    uint8_t overflow = 0;
    uint8_t result = 1;
    for (;;) {
        if (stream.avail_in == 0 && supplied < encoded.size()) {
            const auto chunk = static_cast<uInt>(
                    std::min(encoded.size() - supplied, size_t(UINT_MAX)));
            stream.next_in = const_cast<Bytef*>(encoded.data() + supplied);
            stream.avail_in = chunk;
            supplied += chunk;
        }
        if (stream.avail_out == 0) {
            if (produced < expected_bytes) {
                const auto chunk = static_cast<uInt>(
                        std::min({expected_bytes - produced,
                                  validate_only ? scratch.size() : size_t(UINT_MAX),
                                  size_t(UINT_MAX)}));
                stream.next_out = validate_only ? scratch.data() : decoded + assigned;
                stream.avail_out = chunk;
                if (!validate_only) {
                    assigned += chunk;
                }
            } else {
                stream.next_out = &overflow;
                stream.avail_out = 1;
            }
        }
        const uInt input_before = stream.avail_in;
        const uInt output_before = stream.avail_out;
        const bool probing = stream.next_out == &overflow;
        status = inflate(&stream, Z_NO_FLUSH);
        const size_t written = output_before - stream.avail_out;
        if (probing && written) {
            result = 1; // The Deflate stream expands past the declared strip geometry.
            break;
        }
        if (!probing) {
            produced += written;
        }
        if (status == Z_STREAM_END) {
            const size_t consumed = supplied - stream.avail_in;
            result = produced == expected_bytes && consumed == encoded.size() ? 0 : 1;
            break;
        }
        if (status == Z_MEM_ERROR) {
            result = 4;
            break;
        }
        if (status == Z_DATA_ERROR || status == Z_STREAM_ERROR || status == Z_NEED_DICT) {
            result = 1;
            break;
        }
        if (status == Z_BUF_ERROR ||
            (input_before == stream.avail_in && output_before == stream.avail_out)) {
            result = supplied == encoded.size() && stream.avail_in == 0 ? 3 : 1;
            break;
        }
    }
    inflateEnd(&stream);
    return result;
}

}  // namespace

uint8_t validate_dng_deflate_strip(rust::Slice<const uint8_t> encoded,
                                   size_t expected_bytes) {
    return inflate_strip(encoded, nullptr, expected_bytes, true);
}

uint8_t inflate_dng_strip(rust::Slice<const uint8_t> encoded,
                          rust::Slice<uint8_t> decoded) {
    return inflate_strip(encoded, decoded.data(), decoded.size(), false);
}

}  // namespace rust_raw
