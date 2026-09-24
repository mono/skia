/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkData.h"
#include "tests/Test.h"
#include "tools/Resources.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

bool FuzzRAWRustDecoder(const uint8_t* data, size_t size);

DEF_TEST(RustRaw_FuzzSmoke, r) {
    for (const char* name : std::array{
                 "images/sample_1mp.dng",
                 "images/sample_1mp_rotated.dng",
                 "images/dng_with_preview.dng",
         }) {
        auto data = GetResourceAsData(name);
         REPORTER_ASSERT(r, data && data->size() > 0);
         if (!data || data->size() == 0) {
            continue;
        }
        REPORTER_ASSERT(r, FuzzRAWRustDecoder(
                static_cast<const uint8_t*>(data->data()), data->size()));
        std::vector<uint8_t> mutated(static_cast<const uint8_t*>(data->data()),
                                     static_cast<const uint8_t*>(data->data()) + data->size());
        for (size_t offset : std::array<size_t, 4>{
                     0, std::min<size_t>(16, mutated.size() - 1),
                     mutated.size() / 2, mutated.size() - 1,
             }) {
            mutated[offset] ^= 0x80;
            (void)FuzzRAWRustDecoder(mutated.data(), mutated.size());
            mutated[offset] ^= 0x80;
        }
    }
}
