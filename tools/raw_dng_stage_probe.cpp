/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dng_color_space.h"
#include "dng_exceptions.h"
#include "dng_file_stream.h"
#include "dng_host.h"
#include "dng_image.h"
#include "dng_info.h"
#include "dng_negative.h"
#include "dng_pixel_buffer.h"
#include "dng_render.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 4 || argv[1][0] < '1' || argv[1][0] > '4' || argv[1][1] != '\0') {
        std::fprintf(stderr, "usage: dng_stage_oracle {1|2|3|4} INPUT SAMPLES\n");
        return 2;
    }
    const int stage = argv[1][0] - '0';
    try {
        dng_host host;
        dng_file_stream stream(argv[2]);
        host.ValidateSizes();

        dng_info info;
        info.Parse(host, stream);
        info.PostParse(host);
        if (!info.IsValidDNG()) {
            std::fprintf(stderr, "not a valid DNG\n");
            return 4;
        }

        std::unique_ptr<dng_negative> negative(host.Make_dng_negative());
        negative->Parse(host, stream, info);
        negative->PostParse(host, stream, info);
        negative->SynchronizeMetadata();
        negative->ReadStage1Image(host, stream, info);
        if (info.fMaskIndex != -1) {
            negative->ReadTransparencyMask(host, stream, info);
        }
        negative->ValidateRawImageDigest(host);
        if (negative->IsDamaged()) {
            std::fprintf(stderr, "raw image digest mismatch\n");
            return 4;
        }

        const dng_image* image = negative->Stage1Image();
        if (stage >= 2) {
            negative->BuildStage2Image(host);
            image = negative->Stage2Image();
        }
        if (stage >= 3) {
            negative->BuildStage3Image(host, -1);
            image = negative->Stage3Image();
        }
        std::unique_ptr<dng_image> rendered;
        if (stage == 4 && image) {
            dng_render render(host, *negative);
            render.SetFinalSpace(dng_space_sRGB::Get());
            render.SetFinalPixelType(ttByte);
            const dng_point size = image->Size();
            render.SetMaximumSize(std::max(size.h, size.v));
            rendered.reset(render.Render());
            image = rendered.get();
        }
        if (!image || image->Width() == 0 || image->Height() == 0 || image->Planes() == 0) {
            std::fprintf(stderr, "requested image stage is unavailable\n");
            return 4;
        }

        const uint32_t width = image->Width();
        const uint32_t height = image->Height();
        const uint32_t planes = image->Planes();
        const uint32_t pixelType = image->PixelType();
        const uint32_t pixelSize = TagTypeSize(pixelType);
        const uint64_t samples = static_cast<uint64_t>(width) * planes;
        if (pixelSize == 0 || samples > std::numeric_limits<int32_t>::max() ||
            samples > std::numeric_limits<size_t>::max() / pixelSize ||
            height > std::numeric_limits<size_t>::max() / (samples * pixelSize)) {
            std::fprintf(stderr, "image row is too large\n");
            return 4;
        }
        const size_t rowBytes = static_cast<size_t>(samples * pixelSize);
        std::vector<uint8_t> row(rowBytes);
        auto output = std::unique_ptr<std::FILE, decltype(&std::fclose)>(
                std::fopen(argv[3], "wb"), &std::fclose);
        if (!output) {
            std::fprintf(stderr, "could not create stage output\n");
            return 3;
        }

        const dng_rect bounds = image->Bounds();
        for (int32_t y = bounds.t; y < bounds.b; ++y) {
            dng_pixel_buffer buffer(dng_rect(y, bounds.l, y + 1, bounds.r), 0, planes,
                                    pixelType, pcInterleaved, row.data());
            image->Get(buffer);
            if (std::fwrite(row.data(), 1, rowBytes, output.get()) != rowBytes) {
                std::fprintf(stderr, "could not write stage output\n");
                return 3;
            }
        }
        if (std::fflush(output.get()) != 0 || std::fclose(output.release()) != 0) {
            std::fprintf(stderr, "could not finish stage output\n");
            return 3;
        }
        std::printf("{\"stage\":%d,\"width\":%u,\"height\":%u,\"planes\":%u,"
                    "\"pixel_type\":%u,\"bytes_per_sample\":%u,\"main_ifd_index\":%d,"
                    "\"row_bytes\":%zu,\"total_bytes\":%zu}\n",
                    stage, width, height, planes, pixelType, pixelSize, info.fMainIndex,
                    rowBytes, rowBytes * height);
        return 0;
    } catch (const dng_exception& error) {
        std::fprintf(stderr, "DNG SDK rejected input (error %d)\n", error.ErrorCode());
        return 4;
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "could not allocate stage row\n");
        return 3;
    }
}
