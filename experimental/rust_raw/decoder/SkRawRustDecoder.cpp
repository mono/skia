/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/decoder/SkRawRustDecoder.h"

#include "experimental/rust_raw/ffi/FFI.rs.h"
#include "include/codec/SkEncodedImageFormat.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkStream.h"
#include "include/private/SkEncodedInfo.h"
#include "modules/skcms/skcms.h"
#include "rust/common/SkStreamAdapter.h"
#include "src/codec/SkCodecPriv.h"
#include "src/core/SkStreamPriv.h"

#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace {

SkCodec::Result to_sk_result(rust_raw::DecodeStatus status) {
    switch (status) {
        case rust_raw::DecodeStatus::Success:     return SkCodec::kSuccess;
        case rust_raw::DecodeStatus::Invalid:     return SkCodec::kInvalidInput;
        case rust_raw::DecodeStatus::Unsupported: return SkCodec::kUnimplemented;
        case rust_raw::DecodeStatus::Incomplete:  return SkCodec::kIncompleteInput;
        case rust_raw::DecodeStatus::OutOfMemory: return SkCodec::kInternalError;
    }
    return SkCodec::kInternalError;
}

class SkRawRustCodec final : public SkCodec {
public:
    SkRawRustCodec(std::unique_ptr<SkStream> stream, rust::Box<rust_raw::Reader> reader)
            : SkCodec(SkEncodedInfo::Make(reader->width(), reader->height(),
                                          SkEncodedInfo::kRGB_Color,
                                          SkEncodedInfo::kOpaque_Alpha, 8),
                      skcms_PixelFormat_RGB_888, nullptr)
            , fStream(std::move(stream))
            , fReader(std::move(reader)) {}

    ~SkRawRustCodec() override = default;

protected:
    SkEncodedImageFormat onGetEncodedFormat() const override {
        return SkEncodedImageFormat::kDNG;
    }

    bool onRewind() override { return true; }  // Rust owns immutable decoded input.

    bool usesColorXform() const override { return false; }

    sk_sp<const SkData> getEncodedData() const override {
        if (auto data = fStream->getData()) {
            return data;
        }
        auto stream = fStream->duplicate();
        if (!stream || !stream->hasLength()) {
            return nullptr;
        }
        return SkData::MakeFromStream(stream.get(), stream->getLength());
    }

    Result onGetPixels(const SkImageInfo& dstInfo, void* dst, size_t dstRowBytes,
                       const Options& options, int* rowsDecoded) override {
        if (options.fSubset || options.fFrameIndex != 0) {
            return kUnimplemented;
        }
        if (dstInfo.dimensions() != this->dimensions()) {
            return kInvalidScale;
        }
        skcms_PixelFormat dstFormat;
        if (!SkCodecPriv::SelectXformFormat(dstInfo.colorType(), false, &dstFormat)) {
            return kInvalidConversion;
        }
        const size_t width = static_cast<size_t>(dstInfo.width());
        if (width > SIZE_MAX / 3) {
            return kInvalidInput;
        }
        const size_t srcRowBytes = width * 3;
        std::unique_ptr<uint8_t[]> row(new (std::nothrow) uint8_t[srcRowBytes]);
        if (!row) {
            return kInternalError;
        }
        skcms_ICCProfile dstProfileStorage;
        const skcms_ICCProfile* dstProfile = nullptr;
        if (auto cs = dstInfo.colorSpace()) {
            cs->toProfile(&dstProfileStorage);
            dstProfile = &dstProfileStorage;
        }

        auto* dstRow = static_cast<uint8_t*>(dst);
        if (rowsDecoded) {
            *rowsDecoded = 0;
        }
        for (int y = 0; y < dstInfo.height(); ++y) {
            if (!fReader->copy_rgb_row(static_cast<uint32_t>(y),
                                       rust::Slice<uint8_t>(row.get(), srcRowBytes))) {
                return kErrorInInput;
            }
            if (!skcms_Transform(row.get(), skcms_PixelFormat_RGB_888,
                                 skcms_AlphaFormat_Unpremul, this->getEncodedInfo().profile(),
                                 dstRow, dstFormat, skcms_AlphaFormat_Unpremul,
                                 dstProfile, width)) {
                return kInternalError;
            }
            if (rowsDecoded) {
                *rowsDecoded = y + 1;
            }
            dstRow += dstRowBytes;
        }
        return kSuccess;
    }

private:
    // The adapter used during construction borrows this stream; the reader
    // retains its own checked input bytes, never a destination buffer.
    std::unique_ptr<SkStream> fStream;
    rust::Box<rust_raw::Reader> fReader;
};

}  // namespace

namespace SkRawRustDecoder {

bool IsDng(const void* data, size_t size) {
    if (!data) {
        return false;
    }
    return rust_raw::is_dng(rust::Slice<const uint8_t>(
            static_cast<const uint8_t*>(data), size));
}

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext) {
    SkCodec::Result resultStorage;
    if (!result) {
        result = &resultStorage;
    }
    if (!stream) {
        *result = SkCodec::kInvalidInput;
        return nullptr;
    }
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    rust::Box<rust_raw::Reader> reader = rust_raw::new_reader(std::move(adapter));
    *result = to_sk_result(reader->status());
    if (*result != SkCodec::kSuccess) {
        return nullptr;
    }
    return std::make_unique<SkRawRustCodec>(std::move(stream), std::move(reader));
}

std::unique_ptr<SkCodec> Decode(sk_sp<const SkData> data,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
    if (!data) {
        if (result) {
            *result = SkCodec::kInvalidInput;
        }
        return nullptr;
    }
    return Decode(SkMemoryStream::Make(std::move(data)), result, context);
}

}  // namespace SkRawRustDecoder
