/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/codec/SkCodec.h"
#include "include/core/SkColorType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "tests/FakeStreams.h"

#if defined(SK_CODEC_DECODES_RAW)
#include "include/codec/SkRawDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_RAW_WITH_RUST)
#include "experimental/rust_raw/decoder/SkRawRustDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_JPEG_WITH_RUST)
#include "experimental/rust_jpeg/decoder/SkJpegRustDecoder.h"
#endif

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace {

bool parse_size(const char* text, size_t* value) {
    const size_t length = std::strlen(text);
    auto [end, error] = std::from_chars(text, text + length, *value);
    return length != 0 && error == std::errc() && end == text + length;
}

class ShortReadStream final : public NonseekableStream {
public:
    using NonseekableStream::NonseekableStream;

    size_t read(void* buffer, size_t size) override {
        return NonseekableStream::read(buffer, std::min(size, size_t(7)));
    }
};

}  // namespace

// Writes one machine-readable result and active destination pixel rows. Build the
// reference and candidate separately to keep crash/resource measurements
// independent. A codec rejection is a measured outcome, not a tool failure.
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: raw_codec_probe {legacy|rust|public|rust-jpeg} INPUT PIXELS "
                             "[rgba|bgra|rgb565|gray8|f16|rgba1010102] "
                             "[--scale=0.5] [--color-space=inherit|srgb|linear] "
                             "[--row-padding=N] [--repeat=N] "
                             "[--stream=file|memory|nonseekable|short-read] "
                             "[--subset=center] [--frame-index=N]\n");
        return 2;
    }

    const char* destination = "rgba";
    const char* requestedColorSpace = "inherit";
    const char* streamMode = "file";
    const char* subsetMode = "none";
    float scale = 1.0f;
    size_t rowPadding = 0;
    size_t repeats = 1;
    size_t frameIndex = 0;
    bool extended = false;
    bool destinationProvided = false;
    for (int i = 4; i < argc; ++i) {
        const char* option = argv[i];
        if (std::strncmp(option, "--scale=", 8) == 0) {
            char* end;
            errno = 0;
            scale = std::strtof(option + 8, &end);
            if (errno != 0 || end == option + 8 || *end != '\0' ||
                !std::isfinite(scale) || scale <= 0.0f || scale > 1.0f) {
                std::fprintf(stderr, "scale must be finite and in (0,1]\n");
                return 2;
            }
            extended = true;
        } else if (std::strncmp(option, "--color-space=", 14) == 0) {
            requestedColorSpace = option + 14;
            if (std::strcmp(requestedColorSpace, "inherit") != 0 &&
                std::strcmp(requestedColorSpace, "srgb") != 0 &&
                std::strcmp(requestedColorSpace, "linear") != 0) {
                std::fprintf(stderr, "unknown destination color space\n");
                return 2;
            }
            extended = true;
        } else if (std::strncmp(option, "--row-padding=", 14) == 0) {
            if (!parse_size(option + 14, &rowPadding)) {
                std::fprintf(stderr, "invalid row padding\n");
                return 2;
            }
            extended = true;
        } else if (std::strncmp(option, "--repeat=", 9) == 0) {
            if (!parse_size(option + 9, &repeats) || repeats == 0) {
                std::fprintf(stderr, "repeat count must be positive\n");
                return 2;
            }
            extended = true;
        } else if (std::strncmp(option, "--stream=", 9) == 0) {
            streamMode = option + 9;
            if (std::strcmp(streamMode, "file") != 0 &&
                std::strcmp(streamMode, "memory") != 0 &&
                std::strcmp(streamMode, "nonseekable") != 0 &&
                std::strcmp(streamMode, "short-read") != 0) {
                std::fprintf(stderr, "unknown stream mode\n");
                return 2;
            }
            extended = true;
        } else if (std::strcmp(option, "--subset=center") == 0) {
            subsetMode = "center";
            extended = true;
        } else if (std::strncmp(option, "--frame-index=", 14) == 0) {
            if (!parse_size(option + 14, &frameIndex) || frameIndex > INT_MAX) {
                std::fprintf(stderr, "invalid frame index\n");
                return 2;
            }
            extended = true;
        } else if (option[0] != '-' && !destinationProvided) {
            destination = option;
            destinationProvided = true;
        } else {
            std::fprintf(stderr, "unknown or duplicate probe option\n");
            return 2;
        }
    }
    if (std::strcmp(subsetMode, "none") != 0 && (scale != 1.0f || frameIndex != 0)) {
        std::fprintf(stderr, "subset cannot be combined with scaling or frame selection\n");
        return 2;
    }

    SkColorType colorType;
    if (std::strcmp(destination, "rgba") == 0) {
        colorType = kRGBA_8888_SkColorType;
    } else if (std::strcmp(destination, "bgra") == 0) {
        colorType = kBGRA_8888_SkColorType;
    } else if (std::strcmp(destination, "rgb565") == 0) {
        colorType = kRGB_565_SkColorType;
    } else if (std::strcmp(destination, "gray8") == 0) {
        colorType = kGray_8_SkColorType;
    } else if (std::strcmp(destination, "f16") == 0) {
        colorType = kRGBA_F16_SkColorType;
    } else if (std::strcmp(destination, "rgba1010102") == 0) {
        colorType = kRGBA_1010102_SkColorType;
    } else {
        std::fprintf(stderr, "unknown destination color type\n");
        return 2;
    }

    const char* route = argv[1];
    auto file = std::make_unique<SkFILEStream>(argv[2]);
    if (!file->isValid()) {
        std::fprintf(stderr, "could not open input\n");
        return 2;
    }
    std::unique_ptr<SkStream> stream;
    if (std::strcmp(streamMode, "file") == 0) {
        stream = std::move(file);
    } else {
        sk_sp<SkData> data = SkData::MakeFromStream(file.get(), file->getLength());
        if (!data) {
            std::fprintf(stderr, "could not load input stream\n");
            return 3;
        }
        if (std::strcmp(streamMode, "memory") == 0) {
            stream = SkMemoryStream::Make(std::move(data));
        } else if (std::strcmp(streamMode, "nonseekable") == 0) {
            stream = std::make_unique<NonseekableStream>(std::move(data));
        } else {
            stream = std::make_unique<ShortReadStream>(std::move(data));
        }
    }

    SkCodec::Result createResult = SkCodec::kInternalError;
    std::unique_ptr<SkCodec> codec;
    if (std::strcmp(route, "public") == 0) {
        codec = SkCodec::MakeFromStream(std::move(stream), &createResult);
    } else if (std::strcmp(route, "legacy") == 0) {
#if defined(SK_CODEC_DECODES_RAW)
        codec = SkRawDecoder::Decode(std::move(stream), &createResult);
#else
        std::fprintf(stderr, "legacy RAW decoder is not in this build\n");
        return 2;
#endif
    } else if (std::strcmp(route, "rust") == 0) {
#if defined(SK_CODEC_DECODES_RAW_WITH_RUST)
        codec = SkRawRustDecoder::Decode(std::move(stream), &createResult);
#else
        std::fprintf(stderr, "Rust RAW decoder is not in this build\n");
        return 2;
#endif
    } else if (std::strcmp(route, "rust-jpeg") == 0) {
#if defined(SK_CODEC_DECODES_JPEG_WITH_RUST)
        codec = SkJpegRustDecoder::Decode(std::move(stream), &createResult);
#else
        std::fprintf(stderr, "Rust JPEG decoder is not in this build\n");
        return 2;
#endif
    } else {
        std::fprintf(stderr, "unknown route\n");
        return 2;
    }

    if (!codec) {
        std::printf("{\"route\":\"%s\",\"created\":false,\"create_result\":\"%s\"}\n",
                    route, SkCodec::ResultToString(createResult));
        return 0;
    }

    const SkImageInfo info = codec->getInfo();
    const SkISize scaled = extended ? codec->getScaledDimensions(scale) : info.dimensions();
    SkImageInfo requested = info.makeDimensions(scaled).makeColorType(colorType);
    if (std::strcmp(requestedColorSpace, "srgb") == 0) {
        requested = requested.makeColorSpace(SkColorSpace::MakeSRGB());
    } else if (std::strcmp(requestedColorSpace, "linear") == 0) {
        requested = requested.makeColorSpace(SkColorSpace::MakeSRGBLinear());
    }
    SkIRect requestedSubset = SkIRect::MakeEmpty();
    SkIRect supportedSubset = SkIRect::MakeEmpty();
    bool subsetSupported = false;
    if (std::strcmp(subsetMode, "center") == 0) {
        requestedSubset = SkIRect::MakeXYWH(info.width() / 4, info.height() / 4,
                                             std::max(info.width() / 2, 1),
                                             std::max(info.height() / 2, 1));
        supportedSubset = requestedSubset;
        subsetSupported = codec->getValidSubset(&supportedSubset);
        if (subsetSupported) {
            requested = requested.makeDimensions(supportedSubset.size());
        }
    }
    SkCodec::Options options;
    options.fFrameIndex = static_cast<int>(frameIndex);
    options.fSubset = std::strcmp(subsetMode, "center") == 0
                              ? (subsetSupported ? &supportedSubset : &requestedSubset)
                              : nullptr;

    if (scaled.width() <= 0 || scaled.height() <= 0 ||
        requested.minRowBytes() > std::numeric_limits<size_t>::max() - rowPadding) {
        std::fprintf(stderr, "invalid requested output dimensions or stride\n");
        return 3;
    }
    const size_t activeRowBytes = requested.minRowBytes();
    const size_t rowBytes = activeRowBytes + rowPadding;
    const size_t height = static_cast<size_t>(scaled.height());
    if (rowBytes > std::numeric_limits<size_t>::max() / height ||
        activeRowBytes > std::numeric_limits<size_t>::max() / height) {
        std::fprintf(stderr, "requested output is too large\n");
        return 3;
    }
    const size_t allocatedBytes = rowBytes * height;
    const size_t activeBytes = activeRowBytes * height;
    std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[allocatedBytes]);
    std::unique_ptr<uint8_t[]> first(new (std::nothrow) uint8_t[activeBytes]);
    if (!pixels || !first) {
        std::fprintf(stderr, "could not allocate output bitmap\n");
        return 3;
    }

    SkCodec::Result decodeResult = SkCodec::kInternalError;
    bool repeatsConsistent = true;
    bool paddingPreserved = true;
    for (size_t repeat = 0; repeat < repeats; ++repeat) {
        std::memset(pixels.get(), 0xa5, allocatedBytes);
        const SkCodec::Result current = codec->getPixels(
                requested, pixels.get(), rowBytes, &options);
        if (repeat == 0) {
            decodeResult = current;
        } else if (current != decodeResult) {
            repeatsConsistent = false;
        }
        if (current != SkCodec::kSuccess) {
            break;
        }
        for (size_t y = 0; y < height; ++y) {
            const uint8_t* row = pixels.get() + y * rowBytes;
            uint8_t* saved = first.get() + y * activeRowBytes;
            if (repeat == 0) {
                std::memcpy(saved, row, activeRowBytes);
            } else if (std::memcmp(saved, row, activeRowBytes) != 0) {
                repeatsConsistent = false;
            }
            for (size_t padding = activeRowBytes; padding < rowBytes; ++padding) {
                if (row[padding] != 0xa5) {
                    paddingPreserved = false;
                }
            }
        }
    }
    if (decodeResult == SkCodec::kSuccess) {
        SkFILEWStream output(argv[3]);
        if (!output.isValid()) {
            std::fprintf(stderr, "could not create pixel output\n");
            return 3;
        }
        for (size_t y = 0; y < height; ++y) {
            if (!output.write(first.get() + y * activeRowBytes, activeRowBytes)) {
                std::fprintf(stderr, "could not write pixel output\n");
                return 3;
            }
        }
        output.flush();
        if (output.bytesWritten() != requested.computeMinByteSize()) {
            std::fprintf(stderr, "pixel output is incomplete\n");
            return 3;
        }
    }

    std::printf("{\"route\":\"%s\",\"created\":true,\"create_result\":\"%s\","
                "\"decode_result\":\"%s\",\"width\":%d,\"height\":%d,"
                "\"encoded_format\":%d,\"origin\":%d,\"alpha_type\":%d,"
                "\"color_type\":%d,\"destination\":\"%s\",\"has_color_space\":%s",
                route, SkCodec::ResultToString(createResult),
                SkCodec::ResultToString(decodeResult), requested.width(), requested.height(),
                static_cast<int>(codec->getEncodedFormat()), static_cast<int>(codec->getOrigin()),
                static_cast<int>(info.alphaType()), static_cast<int>(info.colorType()),
                destination, info.colorSpace() ? "true" : "false");
    if (extended) {
        std::printf(",\"source_width\":%d,\"source_height\":%d,"
                    "\"scaled_width\":%d,\"scaled_height\":%d,"
                    "\"requested_scale\":%.9g,\"requested_color_space\":\"%s\","
                    "\"destination_has_color_space\":%s,\"stream_mode\":\"%s\","
                    "\"subset_mode\":\"%s\",\"frame_index\":%zu,"
                    "\"row_padding\":%zu,"
                    "\"repeats\":%zu,\"repeats_consistent\":%s,\"padding_preserved\":%s",
                    info.width(), info.height(), scaled.width(), scaled.height(),
                    static_cast<double>(scale),
                    requestedColorSpace, requested.colorSpace() ? "true" : "false",
                    streamMode, subsetMode, frameIndex,
                    rowPadding, repeats, repeatsConsistent ? "true" : "false",
                    paddingPreserved ? "true" : "false");
        if (std::strcmp(subsetMode, "center") == 0) {
            std::printf(",\"subset_supported\":%s", subsetSupported ? "true" : "false");
            if (subsetSupported) {
                std::printf(",\"subset_rect\":[%d,%d,%d,%d]",
                            supportedSubset.left(), supportedSubset.top(),
                            supportedSubset.right(), supportedSubset.bottom());
            }
        }
    }
    std::printf("}\n");
    return 0;
}
