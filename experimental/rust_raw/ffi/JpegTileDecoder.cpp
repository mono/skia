/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/ffi/JpegTileDecoder.h"

#include "include/codec/SkJpegDecoder.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"

#include <jconfig.h>
#define JCONFIG_INCLUDED
#include <jpeglib.h>
#include <jerror.h>

#include <cstddef>
#include <cstdint>
#include <climits>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace rust_raw {

namespace {

constexpr uint8_t map_failure(SkCodec::Result result) {
    switch (result) {
        case SkCodec::kOutOfMemory:       return 4;
        case SkCodec::kIncompleteInput:   return 3;
        case SkCodec::kUnimplemented:
        case SkCodec::kInvalidConversion: return 2;
        default:                          return 1;
    }
}

static_assert(map_failure(SkCodec::kOutOfMemory) == 4);
static_assert(sizeof(J16SAMPLE) == sizeof(uint16_t));

uint8_t decode_tile(rust::Slice<const uint8_t> jpeg,
                    uint32_t width,
                    uint32_t height,
                    uint32_t visible_width,
                    uint32_t visible_height,
                    uint8_t* rgb,
                    size_t rgb_size,
                    bool validate_only) {
    if (!width || !height || !jpeg.size() ||
        width > 65535 || height > 65535 ||
        width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        static_cast<size_t>(width) > SIZE_MAX / 4) {
        return 1;
    }
    if (validate_only) {
        if (rgb || rgb_size || visible_width || visible_height) {
            return 1;
        }
    } else {
        if (!rgb || !visible_width || !visible_height ||
            visible_width > width || visible_height > height ||
            static_cast<size_t>(visible_width) > SIZE_MAX / 3 ||
            visible_height > SIZE_MAX / (static_cast<size_t>(visible_width) * 3) ||
            rgb_size != static_cast<size_t>(visible_width) * visible_height * 3) {
            return 1;
        }
    }
    // The borrowed JPEG bytes remain alive for the entire synchronous codec
    // decode. Neither the SkData wrapper nor the codec escapes this call.
    auto data = SkData::MakeWithoutCopy(jpeg.data(), jpeg.size());
    SkCodec::Result result;
    auto codec = SkJpegDecoder::Decode(std::move(data), &result);
    if (!codec) {
        return map_failure(result);
    }
    if (codec->dimensions() != SkISize::Make(width, height) ||
        codec->getInfo().alphaType() != kOpaque_SkAlphaType) {
        return 2;
    }
    const size_t rgbaRowBytes = static_cast<size_t>(width) * 4;
    std::unique_ptr<uint8_t[]> rgba(new (std::nothrow) uint8_t[rgbaRowBytes]);
    if (!rgba) {
        return 4;
    }
    const auto info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    result = codec->startScanlineDecode(info);
    if (result != SkCodec::kSuccess) {
        return map_failure(result);
    }
    for (uint32_t y = 0; y < height; ++y) {
        if (codec->getScanlines(rgba.get(), 1, rgbaRowBytes) != 1) {
            return 3;
        }
        if (validate_only || y >= visible_height) {
            continue;
        }
        const size_t dest = static_cast<size_t>(y) * visible_width * 3;
        for (uint32_t x = 0; x < visible_width; ++x) {
            rgb[dest + 3 * x] = rgba[4 * x];
            rgb[dest + 3 * x + 1] = rgba[4 * x + 1];
            rgb[dest + 3 * x + 2] = rgba[4 * x + 2];
        }
    }
    return 0;
}

uint8_t check_bayer_sof3(rust::Slice<const uint8_t> jpeg, uint32_t width, uint32_t height) {
    if (jpeg.size() < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        return 1;
    }
    size_t pos = 2;
    bool found = false;
    while (pos < jpeg.size()) {
        if (jpeg[pos++] != 0xff) {
            return 1;
        }
        while (pos < jpeg.size() && jpeg[pos] == 0xff) {
            ++pos;
        }
        if (pos == jpeg.size()) {
            return 3;
        }
        const uint8_t marker = jpeg[pos++];
        if (marker == 0xda) {
            return found ? 0 : 2;
        }
        if (marker == 0xd9) {
            return 3;
        }
        if (marker == 0x00 || marker == 0xd8 || marker == 0x01 ||
            (marker >= 0xd0 && marker <= 0xd7)) {
            return 1;
        }
        if (jpeg.size() - pos < 2) {
            return 3;
        }
        const size_t size = (size_t(jpeg[pos]) << 8) | jpeg[pos + 1];
        if (size < 2) {
            return 1;
        }
        if (size > jpeg.size() - pos) {
            return 3;
        }
        if (marker >= 0xc0 && marker <= 0xcf &&
            marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (marker != 0xc3 || found || size != 11) {
                return 2;
            }
            if (jpeg[pos + 2] != 16 ||
                ((uint32_t(jpeg[pos + 3]) << 8) | jpeg[pos + 4]) != height ||
                ((uint32_t(jpeg[pos + 5]) << 8) | jpeg[pos + 6]) != width ||
                jpeg[pos + 7] != 1 || jpeg[pos + 9] != 0x11 ||
                jpeg[pos + 10] != 0) {
                return 2;
            }
            found = true;
        }
        pos += size;
    }
    return 3;
}

struct Jpeg16Error {
    jpeg_error_mgr manager;
    std::jmp_buf jump;
    int warning;
};

struct Jpeg16State {
    jpeg_decompress_struct decoder;
    Jpeg16Error error;
    J16SAMPLE* row;
};

void jpeg16_error_exit(j_common_ptr info) {
    auto* error = reinterpret_cast<Jpeg16Error*>(info->err);
    std::longjmp(error->jump, 1);
}

void jpeg16_emit_message(j_common_ptr info, int level) {
    if (level < 0) {
        auto* error = reinterpret_cast<Jpeg16Error*>(info->err);
        if (!error->manager.num_warnings) {
            error->warning = error->manager.msg_code;
        }
        ++error->manager.num_warnings;
    }
}

uint8_t jpeg16_failure(int code) {
    if (code == JERR_OUT_OF_MEMORY) {
        return 4;
    }
    if (code == JERR_INPUT_EOF || code == JWRN_JPEG_EOF) {
        return 3;
    }
    if (code == JERR_SOF_UNSUPPORTED) {
        return 2;
    }
    return 1;
}

uint8_t decode_bayer_tile(rust::Slice<const uint8_t> jpeg,
                          uint32_t width,
                          uint32_t height,
                          uint32_t visible_width,
                          uint32_t visible_height,
                          uint16_t* samples,
                          size_t count,
                          bool validate_only) {
    if (!width || !height || width > 65535 || height > 65535 ||
        jpeg.size() > ULONG_MAX) {
        return 1;
    }
    if (validate_only) {
        if (visible_width || visible_height || samples || count) {
            return 1;
        }
    } else if (!samples || !visible_width || !visible_height ||
               visible_width > width || visible_height > height ||
               visible_width > SIZE_MAX / visible_height ||
               count != size_t(visible_width) * visible_height) {
        return 1;
    }
    const uint8_t frame = check_bayer_sof3(jpeg, width, height);
    if (frame) {
        return frame;
    }
    auto* state = static_cast<Jpeg16State*>(std::calloc(1, sizeof(Jpeg16State)));
    if (!state) {
        return 4;
    }
    state->row = static_cast<J16SAMPLE*>(std::malloc(size_t(width) * sizeof(J16SAMPLE)));
    if (!state->row) {
        std::free(state);
        return 4;
    }
    state->decoder.err = jpeg_std_error(&state->error.manager);
    state->error.manager.error_exit = jpeg16_error_exit;
    state->error.manager.emit_message = jpeg16_emit_message;
    uint8_t result = 0;
    if (setjmp(state->error.jump)) {
        result = jpeg16_failure(state->error.manager.msg_code);
    } else {
        jpeg_create_decompress(&state->decoder);
        jpeg_mem_src(&state->decoder, jpeg.data(), static_cast<unsigned long>(jpeg.size()));
        const int header = jpeg_read_header(&state->decoder, TRUE);
        if (header == JPEG_SUSPENDED) {
            result = 3;
        } else if (header != JPEG_HEADER_OK ||
            state->decoder.image_width != width || state->decoder.image_height != height ||
            state->decoder.data_precision != 16 || state->decoder.num_components != 1) {
            result = 2;
        } else {
            state->decoder.out_color_space = JCS_GRAYSCALE;
            if (!jpeg_start_decompress(&state->decoder)) {
                result = 3;
            } else if (state->decoder.output_width != width ||
                state->decoder.output_height != height ||
                state->decoder.output_components != 1) {
                result = 2;
            } else {
                while (state->decoder.output_scanline < height) {
                    const size_t y = state->decoder.output_scanline;
                    J16SAMPROW row = state->row;
                    if (jpeg16_read_scanlines(&state->decoder, &row, 1) != 1) {
                        result = 3;
                        break;
                    }
                    if (!validate_only && y < visible_height) {
                        std::memcpy(samples + y * visible_width, row,
                                    size_t(visible_width) * sizeof(J16SAMPLE));
                    }
                }
                if (!result && !jpeg_finish_decompress(&state->decoder)) {
                    result = 3;
                }
            }
        }
        if (!result && state->error.manager.num_warnings) {
            result = jpeg16_failure(state->error.warning);
        }
    }
    if (state->decoder.mem) {
        jpeg_destroy_decompress(&state->decoder);
    }
    std::free(state->row);
    std::free(state);
    return result;
}

}  // namespace

uint8_t validate_jpeg_tile(rust::Slice<const uint8_t> jpeg,
                           uint32_t width,
                           uint32_t height) {
    return decode_tile(jpeg, width, height, 0, 0, nullptr, 0, true);
}

uint8_t decode_jpeg_tile(rust::Slice<const uint8_t> jpeg,
                         uint32_t width,
                         uint32_t height,
                         uint32_t visible_width,
                         uint32_t visible_height,
                         rust::Slice<uint8_t> rgb) {
    return decode_tile(jpeg, width, height, visible_width, visible_height,
                       rgb.data(), rgb.size(), false);
}

uint8_t validate_jpeg16_bayer_tile(rust::Slice<const uint8_t> jpeg,
                                   uint32_t width,
                                   uint32_t height) {
    return decode_bayer_tile(jpeg, width, height, 0, 0, nullptr, 0, true);
}

uint8_t decode_jpeg16_bayer_tile(rust::Slice<const uint8_t> jpeg,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t visible_width,
                                 uint32_t visible_height,
                                 rust::Slice<uint16_t> samples) {
    return decode_bayer_tile(jpeg, width, height, visible_width, visible_height,
                             samples.data(), samples.size(), false);
}

}  // namespace rust_raw
