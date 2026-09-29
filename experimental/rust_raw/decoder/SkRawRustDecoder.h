/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkRawRustDecoder_DEFINED
#define SkRawRustDecoder_DEFINED

#include "include/codec/SkCodec.h"
#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <memory>

class SkData;
class SkStream;

// Experimental: used by the public RAW fallback only in SDK-free opt-in builds.
namespace SkRawRustDecoder {

bool IsDng(const void*, size_t);
std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream>,
                                SkCodec::Result*,
                                SkCodecs::DecodeContext = nullptr);
std::unique_ptr<SkCodec> Decode(sk_sp<const SkData>,
                                SkCodec::Result*,
                                SkCodecs::DecodeContext = nullptr);

}  // namespace SkRawRustDecoder

#endif  // SkRawRustDecoder_DEFINED
