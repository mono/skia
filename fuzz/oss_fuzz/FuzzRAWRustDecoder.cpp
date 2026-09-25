/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/decoder/SkRawRustDecoder.h"
#include "experimental/rust_raw/ffi/FFI.rs.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkStream.h"
#include "rust/common/SkStreamAdapter.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

bool FuzzRAWRustDecoder(const uint8_t* data, size_t size) {
    if (!data || !size) {
        return false;
    }

    auto stream = std::make_unique<SkMemoryStream>(data, size, false);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
        return false;
    }

    const uint32_t width = reader->width();
    const uint32_t height = reader->height();
    const uint32_t channels = reader->channels();
    const uint16_t bits = reader->bits_per_sample();
    if (!width || !height || !channels || width > SIZE_MAX / channels ||
        (channels != 1 && channels != 3) ||
        (bits != 8 && bits != 16 && bits != 32) ||
        (bits == 32 && channels != 3)) {
        return false;
    }
    const size_t samples = static_cast<size_t>(width) * channels;
    auto rgb8 = channels == 3 && bits == 8
                        ? std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[samples])
                        : nullptr;
    auto words = bits != 32
                         ? std::unique_ptr<uint16_t[]>(new (std::nothrow) uint16_t[samples])
                         : nullptr;
    auto floats = bits == 32
                          ? std::unique_ptr<float[]>(new (std::nothrow) float[samples])
                          : nullptr;
    const bool bayerStage3 = channels == 1 &&
                             reader->stage3_status() == rust_raw::DecodeStatus::Success &&
                             reader->stage3_channels() == 3;
    const size_t bayerStage3Samples = bayerStage3 ? static_cast<size_t>(width) * 3 : 0;
    auto bayerStage3Row = bayerStage3
                                  ? std::unique_ptr<uint16_t[]>(
                                            new (std::nothrow) uint16_t[bayerStage3Samples])
                                  : nullptr;
    if ((channels == 3 && bits == 8 && !rgb8) ||
        (bits != 32 && !words) || (bits == 32 && !floats) ||
        (bayerStage3 && !bayerStage3Row)) {
        return false;
    }

    for (uint32_t row : std::array<uint32_t, 3>{0, height / 2, height - 1}) {
        rust_raw::DecodeStatus result;
        if (channels == 1) {
            result = reader->read_stage1_bayer_row(
                    row, rust::Slice<uint16_t>(words.get(), samples));
            if (result == rust_raw::DecodeStatus::Unsupported) {
                result = reader->read_stage1_row(
                        row, rust::Slice<uint16_t>(words.get(), samples));
            }
        } else if (bits == 8) {
            result = reader->read_stage1_rgb_row(row, rust::Slice<uint8_t>(rgb8.get(), samples));
        } else if (bits == 16) {
            result = reader->read_stage1_rgb16_row(
                    row, rust::Slice<uint16_t>(words.get(), samples));
        } else {
            result = reader->read_stage1_rgb_f32_row(
                    row, rust::Slice<float>(floats.get(), samples));
        }
        if (result != rust_raw::DecodeStatus::Success) {
            return false;
        }

        if (reader->stage2_status() == rust_raw::DecodeStatus::Success) {
            if (channels == 1) {
                result = reader->read_stage2_bayer_row(
                        row, rust::Slice<uint16_t>(words.get(), samples));
                if (result == rust_raw::DecodeStatus::Unsupported) {
                    result = reader->read_normalized_row(
                            row, rust::Slice<uint16_t>(words.get(), samples));
                }
            } else if (bits == 32) {
                result = reader->read_stage2_rgb_f32_row(
                        row, rust::Slice<float>(floats.get(), samples));
            } else {
                result = reader->read_stage2_rgb_row(
                        row, rust::Slice<uint16_t>(words.get(), samples));
            }
            if (result != rust_raw::DecodeStatus::Success) {
                return false;
            }
        }
        if (reader->stage3_status() == rust_raw::DecodeStatus::Success) {
            if (bayerStage3) {
                result = reader->read_stage3_bayer_rgb_row(
                        row, rust::Slice<uint16_t>(bayerStage3Row.get(), bayerStage3Samples));
            } else if (channels == 1) {
                result = reader->read_stage3_row(
                        row, rust::Slice<uint16_t>(words.get(), samples));
            } else if (bits == 32) {
                result = reader->read_stage3_rgb_f32_row(
                        row, rust::Slice<float>(floats.get(), samples));
            } else {
                result = reader->read_stage3_rgb_row(
                        row, rust::Slice<uint16_t>(words.get(), samples));
            }
            if (result != rust_raw::DecodeStatus::Success) {
                return false;
            }
        }
    }

    if (reader->status() == rust_raw::DecodeStatus::Success) {
        SkCodec::Result createResult;
        auto codec = SkRawRustDecoder::Decode(
                std::make_unique<SkMemoryStream>(data, size, false), &createResult);
        if (!codec || createResult != SkCodec::kSuccess) {
            return false;
        }
        SkBitmap pixels;
        if (!pixels.tryAllocPixels(codec->getInfo())) {
            return false;
        }
        (void)codec->getPixels(pixels.info(), pixels.getPixels(), pixels.rowBytes());
    }
    return true;
}

#if defined(SK_BUILD_FOR_LIBFUZZER)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size <= 16 * 1024 * 1024) {
        FuzzRAWRustDecoder(data, size);
    }
    return 0;
}
#endif
