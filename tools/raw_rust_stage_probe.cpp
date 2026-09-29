/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/ffi/FFI.rs.h"
#include "include/core/SkStream.h"
#include "rust/common/SkStreamAdapter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace {

static_assert(sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559);

const char* status_name(rust_raw::DecodeStatus status) {
    switch (status) {
        case rust_raw::DecodeStatus::Success: return "success";
        case rust_raw::DecodeStatus::Invalid: return "invalid";
        case rust_raw::DecodeStatus::Unsupported: return "unsupported";
        case rust_raw::DecodeStatus::Incomplete: return "incomplete";
        case rust_raw::DecodeStatus::OutOfMemory: return "out_of_memory";
    }
    return "unknown";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4 || argv[1][0] < '1' || argv[1][0] > '3' || argv[1][1] != '\0') {
        std::fprintf(stderr, "usage: raw_rust_stage_probe {1|2|3} INPUT SAMPLES\n");
        return 2;
    }
    const int stage = argv[1][0] - '0';
    SkFILEStream stream(argv[2]);
    if (!stream.isValid()) {
        std::fprintf(stderr, "could not open input\n");
        return 2;
    }
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(&stream);
    auto reader = rust_raw::new_reader(std::move(adapter));
    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
        std::printf("{\"stage\":%d,\"created\":false,\"status\":\"%s\"}\n",
                    stage, status_name(reader->stage1_status()));
        return 0;
    }
    if (stage == 2 && reader->stage2_status() != rust_raw::DecodeStatus::Success) {
        std::printf("{\"stage\":%d,\"created\":false,\"status\":\"%s\"}\n",
                    stage, status_name(reader->stage2_status()));
        return 0;
    }
    if (stage == 3 && reader->stage3_status() != rust_raw::DecodeStatus::Success) {
        std::printf("{\"stage\":%d,\"created\":false,\"status\":\"%s\"}\n",
                    stage, status_name(reader->stage3_status()));
        return 0;
    }

    const uint32_t width = reader->width();
    const uint32_t height = reader->height();
    const uint16_t bits = reader->bits_per_sample();
    const uint32_t sourceChannels = reader->channels();
    const uint32_t channels = stage == 3 ? reader->stage3_channels() : sourceChannels;
    if (width == 0 || height == 0 || (bits != 8 && bits != 16 && bits != 32) ||
        (channels != 1 && channels != 3) ||
        (bits == 32 && channels != 3) ||
        width > std::numeric_limits<size_t>::max() / channels) {
        std::fprintf(stderr, "unsupported stage geometry or sample width\n");
        return 4;
    }
    const size_t sampleBytes = bits == 32 ? 4 : stage != 1 || bits == 16 ? 2 : 1;
    const size_t samples = static_cast<size_t>(width) * channels;
    if (samples > std::numeric_limits<size_t>::max() / sampleBytes ||
        height > std::numeric_limits<size_t>::max() / (samples * sampleBytes)) {
        std::fprintf(stderr, "stage output is too large\n");
        return 4;
    }
    const size_t rowBytes = samples * sampleBytes;
    auto row = bits == 32 ? std::unique_ptr<uint16_t[]>() :
                            std::unique_ptr<uint16_t[]>(new (std::nothrow) uint16_t[samples]);
    auto floatRow = bits == 32 ? std::unique_ptr<float[]>(new (std::nothrow) float[samples]) :
                                 std::unique_ptr<float[]>();
    auto bytes = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[rowBytes]);
    if (!bytes || (bits == 32 ? !floatRow : !row)) {
        std::fprintf(stderr, "could not allocate stage row\n");
        return 3;
    }
    auto output = std::unique_ptr<std::FILE, decltype(&std::fclose)>(
            std::fopen(argv[3], "wb"), &std::fclose);
    if (!output) {
        std::fprintf(stderr, "could not create stage output\n");
        return 3;
    }

    for (uint32_t y = 0; y < height; ++y) {
        if (bits == 32) {
            auto samplesRow = rust::Slice<float>(floatRow.get(), samples);
            const auto status = stage == 1 ? reader->read_stage1_rgb_f32_row(y, samplesRow) :
                                stage == 2 ? reader->read_stage2_rgb_f32_row(y, samplesRow) :
                                             reader->read_stage3_rgb_f32_row(y, samplesRow);
            if (status != rust_raw::DecodeStatus::Success) {
                std::fprintf(stderr, "float stage row %u failed: %s\n", y, status_name(status));
                return 4;
            }
            for (size_t x = 0; x < samples; ++x) {
                uint32_t bitsValue;
                std::memcpy(&bitsValue, &floatRow[x], sizeof(bitsValue));
                for (size_t byte = 0; byte < 4; ++byte) {
                    bytes[4 * x + byte] = static_cast<uint8_t>(bitsValue >> (8 * byte));
                }
            }
        } else if (channels == 3 && stage == 1 && bits == 8) {
            if (reader->read_stage1_rgb_row(y, rust::Slice<uint8_t>(bytes.get(), rowBytes)) !=
                rust_raw::DecodeStatus::Success) {
                std::fprintf(stderr, "RGB stage row %u is unavailable\n", y);
                return 4;
            }
        } else {
            rust_raw::DecodeStatus status;
            if (stage == 3 && sourceChannels == 1 && channels == 3) {
                status = reader->read_stage3_bayer_rgb_row(
                        y, rust::Slice<uint16_t>(row.get(), samples));
            } else if (channels == 3) {
                status = stage == 1 ? reader->read_stage1_rgb16_row(
                                              y, rust::Slice<uint16_t>(row.get(), samples))
                                    : stage == 3 ? reader->read_stage3_rgb_row(
                                              y, rust::Slice<uint16_t>(row.get(), samples))
                                    : reader->read_stage2_rgb_row(
                                              y, rust::Slice<uint16_t>(row.get(), samples));
            } else if (stage == 1) {
                status = reader->read_stage1_bayer_row(
                        y, rust::Slice<uint16_t>(row.get(), width));
                if (status == rust_raw::DecodeStatus::Unsupported) {
                    status = reader->read_stage1_row(
                            y, rust::Slice<uint16_t>(row.get(), width));
                }
            } else if (stage == 3) {
                status = reader->read_stage3_row(y, rust::Slice<uint16_t>(row.get(), width));
            } else {
                status = reader->read_stage2_bayer_row(
                        y, rust::Slice<uint16_t>(row.get(), width));
                if (status == rust_raw::DecodeStatus::Unsupported) {
                    status = reader->read_normalized_row(
                            y, rust::Slice<uint16_t>(row.get(), width));
                }
            }
            if (status != rust_raw::DecodeStatus::Success) {
                std::fprintf(stderr, "stage row %u failed: %s\n", y, status_name(status));
                return 4;
            }
            for (size_t x = 0; x < samples; ++x) {
                if (sampleBytes == 1) {
                    bytes[x] = static_cast<uint8_t>(row[x]);
                } else {
                    bytes[2 * x] = static_cast<uint8_t>(row[x]);
                    bytes[2 * x + 1] = static_cast<uint8_t>(row[x] >> 8);
                }
            }
        }
        if (std::fwrite(bytes.get(), 1, rowBytes, output.get()) != rowBytes) {
            std::fprintf(stderr, "could not write stage output\n");
            return 3;
        }
    }
    if (std::fflush(output.get()) != 0 || std::fclose(output.release()) != 0) {
        std::fprintf(stderr, "could not finish stage output\n");
        return 3;
    }
    std::printf("{\"stage\":%d,\"width\":%u,\"height\":%u,\"planes\":%u,"
                "\"pixel_type\":%d,\"bytes_per_sample\":%zu,\"main_ifd_index\":%u,"
                "\"row_bytes\":%zu,\"total_bytes\":%zu}\n",
                stage, width, height, channels,
                sampleBytes == 4 ? 11 : sampleBytes == 1 ? 1 : 3, sampleBytes,
                reader->main_ifd_index(), rowBytes, rowBytes * height);
    return 0;
}
