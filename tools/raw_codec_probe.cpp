/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkStream.h"

#if defined(SK_CODEC_DECODES_RAW)
#include "include/codec/SkRawDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_RAW_WITH_RUST)
#include "experimental/rust_raw/decoder/SkRawRustDecoder.h"
#endif
#if defined(SK_CODEC_DECODES_JPEG_WITH_RUST)
#include "experimental/rust_jpeg/decoder/SkJpegRustDecoder.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

// Writes one machine-readable result and active RGBA pixel rows. Build the
// reference and candidate separately to keep crash/resource measurements
// independent. A codec rejection is a measured outcome, not a tool failure.
int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: raw_codec_probe {legacy|rust|public|rust-jpeg} INPUT PIXELS "
                             "[rgba|bgra|rgb565|gray8|f16|rgba1010102]\n");
        return 2;
    }

    const char* destination = argc == 5 ? argv[4] : "rgba";
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
    auto stream = std::make_unique<SkFILEStream>(argv[2]);
    if (!stream->isValid()) {
        std::fprintf(stderr, "could not open input\n");
        return 2;
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
    const SkImageInfo requested = info.makeColorType(colorType);
    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(requested)) {
        std::fprintf(stderr, "could not allocate output bitmap\n");
        return 3;
    }

    const SkCodec::Result decodeResult = codec->getPixels(requested, bitmap.getPixels(),
                                                           bitmap.rowBytes());
    if (decodeResult == SkCodec::kSuccess) {
        SkFILEWStream output(argv[3]);
        if (!output.isValid()) {
            std::fprintf(stderr, "could not create pixel output\n");
            return 3;
        }
        const size_t activeRowBytes = requested.minRowBytes();
        for (int y = 0; y < requested.height(); ++y) {
            const auto* row = static_cast<const uint8_t*>(bitmap.getPixels()) +
                              static_cast<size_t>(y) * bitmap.rowBytes();
            if (!output.write(row, activeRowBytes)) {
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
                "\"color_type\":%d,\"destination\":\"%s\",\"has_color_space\":%s}\n",
                route, SkCodec::ResultToString(createResult),
                SkCodec::ResultToString(decodeResult), requested.width(), requested.height(),
                static_cast<int>(codec->getEncodedFormat()), static_cast<int>(codec->getOrigin()),
                static_cast<int>(info.alphaType()), static_cast<int>(info.colorType()),
                destination, info.colorSpace() ? "true" : "false");
    return 0;
}
