/*
 * Copyright 2026 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/rust_raw/decoder/SkRawRustDecoder.h"
#include "experimental/rust_raw/ffi/DeflateStripDecoder.h"
#include "experimental/rust_raw/ffi/FFI.rs.h"

#include "include/codec/SkCodec.h"
#include "include/codec/SkEncodedImageFormat.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkStream.h"
#include "rust/common/SkStreamAdapter.h"
#include "tests/FakeStreams.h"
#include "tests/Test.h"
#include "tools/Resources.h"

#if defined(SK_CODEC_DECODES_RAW)
#include "include/codec/SkRawDecoder.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "third_party/libjpeg-turbo/jconfig.h"
#define JCONFIG_INCLUDED
#include "third_party/externals/libjpeg-turbo/src/jpeglib.h"
#include <zlib.h>

namespace {

void put16(std::vector<uint8_t>* v, uint16_t n) {
    v->push_back(n & 0xff);
    v->push_back(n >> 8);
}

void put32(std::vector<uint8_t>* v, uint32_t n) {
    put16(v, n & 0xffff);
    put16(v, n >> 16);
}

std::vector<uint8_t> pair32(uint32_t a, uint32_t b) {
    std::vector<uint8_t> value;
    put32(&value, a);
    put32(&value, b);
    return value;
}

struct Tag {
    uint16_t id;
    uint16_t type;
    uint32_t count;
    std::vector<uint8_t> bytes;
};

Tag word(uint16_t id, uint16_t value) {
    std::vector<uint8_t> bytes;
    put16(&bytes, value);
    return {id, 3, 1, std::move(bytes)};
}

Tag long_tag(uint16_t id, uint32_t value) {
    std::vector<uint8_t> bytes;
    put32(&bytes, value);
    return {id, 4, 1, std::move(bytes)};
}

Tag rational(uint16_t id, uint32_t numerator) {
    std::vector<uint8_t> bytes;
    put32(&bytes, numerator);
    put32(&bytes, 1);
    return {id, 5, 1, std::move(bytes)};
}

Tag rational_pair(uint16_t id, uint32_t a, uint32_t b) {
    std::vector<uint8_t> bytes;
    put32(&bytes, a);
    put32(&bytes, 1);
    put32(&bytes, b);
    put32(&bytes, 1);
    return {id, 5, 2, std::move(bytes)};
}

// In-test only, no DNG writer in production.
std::vector<uint8_t> make_test_dng(std::vector<Tag> tags, std::vector<uint8_t> pixels) {
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> data = {'I', 'I', 42, 0, 8, 0, 0, 0};
    put16(&data, static_cast<uint16_t>(tags.size()));
    const size_t entries = data.size();
    data.resize(entries + 12 * tags.size() + 4, 0);
    for (size_t i = 0; i < tags.size(); ++i) {
        const Tag& tag = tags[i];
        std::vector<uint8_t> entry;
        put16(&entry, tag.id);
        put16(&entry, tag.type);
        put32(&entry, tag.count);
        if (tag.id == 273) {
            put32(&entry, 0);
        } else if (tag.bytes.size() <= 4) {
            entry.insert(entry.end(), tag.bytes.begin(), tag.bytes.end());
            entry.resize(12, 0);
        } else {
            if (data.size() & 1) {
                data.push_back(0);
            }
            put32(&entry, static_cast<uint32_t>(data.size()));
            data.insert(data.end(), tag.bytes.begin(), tag.bytes.end());
        }
        std::memcpy(data.data() + entries + i * 12, entry.data(), 12);
    }
    if (data.size() & 1) {
        data.push_back(0);
    }
    const uint32_t actualPixels = static_cast<uint32_t>(data.size());
    for (size_t i = 0; i < tags.size(); ++i) {
        if (tags[i].id == 273) {
            const size_t p = entries + i * 12 + 8;
            data[p] = actualPixels & 0xff;
            data[p + 1] = (actualPixels >> 8) & 0xff;
            data[p + 2] = (actualPixels >> 16) & 0xff;
            data[p + 3] = (actualPixels >> 24) & 0xff;
        }
    }
    data.insert(data.end(), pixels.begin(), pixels.end());
    return data;
}

// Single full-resolution IFD, classic TIFF little-endian, uncompressed
// monochrome LinearRaw; no JPEG preview.
std::vector<uint8_t> make_dng(uint16_t compression = 1,
                              uint16_t photo = 34892,
                              bool opcode = false,
                              bool cropped = false,
                              uint8_t lastSample = 255,
                              uint16_t orientation = 1,
                              bool opcode3 = false) {
    std::vector<Tag> tags = {
        long_tag(254, 0),
        long_tag(256, 3),
        long_tag(257, 2),
        word(258, 8),
        word(259, compression),
        word(262, photo),
        long_tag(273, 0),
        word(274, orientation),
        word(277, 1),
        long_tag(278, 2),
        long_tag(279, 6),
        word(284, 1),
        word(339, 1),
        {50706, 1, 4, {1, 4, 0, 0}},
        {50707, 1, 4, {1, 1, 0, 0}},
        {50708, 2, 21, {'S', 'k', 'i', 'a', ' ', 's', 'y', 'n', 't', 'h',
                          'e', 't', 'i', 'c', ' ', 'g', 'r', 'a', 'y', '1', 0}},
        {50713, 3, 2, {1, 0, 1, 0}},
        rational(50714, 0),
        long_tag(50717, 255),
        rational_pair(50718, 1, 1),
        rational_pair(50719, cropped ? 1 : 0, 0),
        rational_pair(50720, cropped ? 2 : 3, 2),
        {50829, 4, 4, [] {
            auto v = pair32(0, 0);
            auto end = pair32(2, 3);
            v.insert(v.end(), end.begin(), end.end());
            return v;
        }()},
    };
    if (opcode) {
        tags.push_back({51009, 7, 4, {0, 0, 0, 0}});
    }
    if (opcode3) {
        tags.push_back({51022, 7, 4, {0, 0, 0, 0}});
    }
    return make_test_dng(std::move(tags), {0, 255, 0, 255, 0, lastSample});
}

std::vector<uint8_t> make_output_mono_ramp(uint16_t bits, bool duplicateReference = false,
                                          bool explicitTone = false) {
    std::vector<Tag> tags = {
        long_tag(254, 0),
        long_tag(256, 256),
        long_tag(257, 1),
        word(258, bits),
        word(259, 1),
        word(262, 34892),
        long_tag(273, 0),
        word(274, 1),
        word(277, 1),
        long_tag(278, 1),
        long_tag(279, 256 * (bits / 8)),
        word(284, 1),
        word(339, 1),
        {50706, 1, 4, {1, 4, 0, 0}},
        {50707, 1, 4, {1, 1, 0, 0}},
        {50708, 2, 17, {'S', 'k', 'i', 'a', ' ', 'o', 'u', 't', 'p',
                          'u', 't', ' ', 'm', 'o', 'n', 'o', 0}},
        {50713, 3, 2, {1, 0, 1, 0}},
        rational(50714, 0),
        long_tag(50717, bits == 8 ? 255 : 65535),
        rational_pair(50718, 1, 1),
        rational_pair(50719, 0, 0),
        rational_pair(50720, 256, 1),
        {50829, 4, 4, [] {
            auto v = pair32(0, 0);
            auto end = pair32(1, 256);
            v.insert(v.end(), end.begin(), end.end());
            return v;
        }()},
        word(50879, 1),
    };
    if (duplicateReference) {
        tags.push_back(word(50879, 1));
    }
    if (explicitTone) {
        std::vector<uint8_t> curve;
        for (float value : {0.0f, 0.0f, 0.5f, 0.7f, 1.0f, 1.0f}) {
            uint32_t bitsValue;
            std::memcpy(&bitsValue, &value, sizeof(bitsValue));
            put32(&curve, bitsValue);
        }
        tags.push_back({50940, 11, 6, std::move(curve)});
        tags.push_back(long_tag(51110, 1));
    }
    std::vector<uint8_t> pixels;
    pixels.reserve(256 * (bits / 8));
    for (uint32_t sample = 0; sample < 256; ++sample) {
        if (bits == 8) {
            pixels.push_back(static_cast<uint8_t>(sample));
        } else {
            put16(&pixels, static_cast<uint16_t>(sample * 257));
        }
    }
    return make_test_dng(std::move(tags), std::move(pixels));
}

size_t entry_for(const std::vector<uint8_t>& bytes, uint16_t tag) {
    const size_t ifd = bytes[4] | (size_t(bytes[5]) << 8) |
                       (size_t(bytes[6]) << 16) | (size_t(bytes[7]) << 24);
    const size_t count = bytes[ifd] | (size_t(bytes[ifd + 1]) << 8);
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = ifd + 2 + 12 * i;
        if (bytes[entry] == (tag & 0xff) && bytes[entry + 1] == (tag >> 8)) {
            return entry;
        }
    }
    return bytes.size();
}

size_t entry_in_ifd(const std::vector<uint8_t>& bytes, size_t ifd, uint16_t tag) {
    if (ifd > bytes.size() || bytes.size() - ifd < 2) {
        return bytes.size();
    }
    const size_t count = bytes[ifd] | (size_t(bytes[ifd + 1]) << 8);
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = ifd + 2 + 12 * i;
        if (entry > bytes.size() || bytes.size() - entry < 12) {
            return bytes.size();
        }
        if (bytes[entry] == (tag & 0xff) && bytes[entry + 1] == (tag >> 8)) {
            return entry;
        }
    }
    return bytes.size();
}

void set32(std::vector<uint8_t>* bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        (*bytes)[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

void append16(std::vector<uint8_t>* bytes, uint16_t value, bool bigEndian) {
    if (bigEndian) {
        bytes->push_back(value >> 8);
        bytes->push_back(value & 0xff);
    } else {
        put16(bytes, value);
    }
}

void append32(std::vector<uint8_t>* bytes, uint32_t value, bool bigEndian) {
    if (bigEndian) {
        append16(bytes, value >> 16, true);
        append16(bytes, value & 0xffff, true);
    } else {
        put32(bytes, value);
    }
}

void store32(std::vector<uint8_t>* bytes, size_t offset, uint32_t value, bool bigEndian) {
    std::vector<uint8_t> encoded;
    append32(&encoded, value, bigEndian);
    std::memcpy(bytes->data() + offset, encoded.data(), 4);
}

uint32_t read32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

uint32_t append_opcode3_ifd(std::vector<uint8_t>* bytes, size_t original) {
    const uint16_t count = (*bytes)[original] | (uint16_t((*bytes)[original + 1]) << 8);
    if (bytes->size() & 1) {
        bytes->push_back(0);
    }
    const uint32_t offset = static_cast<uint32_t>(bytes->size());
    put16(bytes, count + 1);
    const uint8_t newTag[] = {uint8_t(51022 & 0xff), uint8_t(51022 >> 8),
                              7, 0, 4, 0, 0, 0, 0, 0, 0, 0};
    bool inserted = false;
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = original + 2 + 12 * i;
        const uint16_t id = (*bytes)[entry] | (uint16_t((*bytes)[entry + 1]) << 8);
        if (!inserted && id > 51022) {
            bytes->insert(bytes->end(), newTag, newTag + sizeof(newTag));
            inserted = true;
        }
        std::array<uint8_t, 12> originalEntry;
        std::memcpy(originalEntry.data(), bytes->data() + entry, originalEntry.size());
        bytes->insert(bytes->end(), originalEntry.begin(), originalEntry.end());
    }
    if (!inserted) {
        bytes->insert(bytes->end(), newTag, newTag + sizeof(newTag));
    }
    put32(bytes, 0);
    return offset;
}

std::vector<uint8_t> add_linear_tone_and_black_none(const SkData* source) {
    if (!source || source->size() < 14) {
        return {};
    }
    const auto* first = static_cast<const uint8_t*>(source->data());
    std::vector<uint8_t> bytes(first, first + source->size());
    if (std::memcmp(bytes.data(), "II\x2a\0", 4) != 0) {
        return {};
    }
    const size_t original = read32(bytes, 4);
    if (original > bytes.size() || bytes.size() - original < 2) {
        return {};
    }
    const uint16_t count = bytes[original] | (uint16_t(bytes[original + 1]) << 8);
    if (count > UINT16_MAX - 2 || bytes.size() - original < 2 + 12 * size_t(count) + 4 ||
        entry_in_ifd(bytes, original, 50940) != bytes.size() ||
        entry_in_ifd(bytes, original, 51110) != bytes.size()) {
        return {};
    }
    std::vector<std::array<uint8_t, 12>> entries;
    for (size_t index = 0; index < count; ++index) {
        std::array<uint8_t, 12> field;
        std::memcpy(field.data(), bytes.data() + original + 2 + 12 * index, field.size());
        entries.push_back(field);
    }
    auto tag = [](uint16_t id, uint16_t kind, uint32_t amount, uint32_t value) {
        std::vector<uint8_t> encoded;
        put16(&encoded, id);
        put16(&encoded, kind);
        put32(&encoded, amount);
        put32(&encoded, value);
        std::array<uint8_t, 12> field;
        std::memcpy(field.data(), encoded.data(), field.size());
        return field;
    };
    entries.push_back(tag(50940, 11, 4, 0));
    entries.push_back(tag(51110, 4, 1, 1));
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return (a[0] | (uint16_t(a[1]) << 8)) < (b[0] | (uint16_t(b[1]) << 8));
    });
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    if (bytes.size() > UINT32_MAX) {
        return {};
    }
    const uint32_t newIfd = static_cast<uint32_t>(bytes.size());
    put16(&bytes, static_cast<uint16_t>(entries.size()));
    for (const auto& field : entries) {
        bytes.insert(bytes.end(), field.begin(), field.end());
    }
    put32(&bytes, 0);
    const size_t tone = entry_in_ifd(bytes, newIfd, 50940);
    if (tone == bytes.size() || bytes.size() > UINT32_MAX - 16) {
        return {};
    }
    set32(&bytes, tone + 8, static_cast<uint32_t>(bytes.size()));
    for (uint32_t value : {0u, 0u, 0x3f800000u, 0x3f800000u}) {
        put32(&bytes, value);
    }
    set32(&bytes, 4, newIfd);
    return bytes;
}

// Test-only multi-strip stage-1 fixture; its non-binary pixels are deliberately
// NOT enabled for public final-image rendering.
std::vector<uint8_t> make_stage1_dng(bool bigEndian, uint16_t bits,
                                     bool outputReferred = false) {
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> bytes;
        append16(&bytes, value, bigEndian);
        return bytes;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        return bytes;
    };
    auto pair = [bigEndian](uint32_t first, uint32_t second) {
        std::vector<uint8_t> bytes;
        append32(&bytes, first, bigEndian);
        append32(&bytes, 1, bigEndian);
        append32(&bytes, second, bigEndian);
        append32(&bytes, 1, bigEndian);
        return bytes;
    };
    auto quad = [bigEndian](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        std::vector<uint8_t> bytes;
        for (uint32_t value : {a, b, c, d}) {
            append32(&bytes, value, bigEndian);
        }
        return bytes;
    };
    const uint32_t sampleBytes = bits / 8;
    std::vector<uint8_t> stripSizes;
    append32(&stripSizes, 6 * sampleBytes, bigEndian);
    append32(&stripSizes, 3 * sampleBytes, bigEndian);
    const uint32_t black0 = bits == 8 ? 0 : 1024;
    const uint32_t black1 = bits == 8 ? 0 : 2048;
    std::vector<Tag> fields = {
            {254, 4, 1, number(0)},        {256, 4, 1, number(3)},
            {257, 4, 1, number(3)},        {258, 3, 1, word(bits)},
            {259, 3, 1, word(1)},          {262, 3, 1, word(34892)},
            {273, 4, 2, std::vector<uint8_t>(8, 0)},
            {274, 3, 1, word(1)},          {277, 3, 1, word(1)},
            {278, 4, 1, number(2)},        {279, 4, 2, stripSizes},
            {284, 3, 1, word(1)},          {339, 3, 1, word(1)},
            {50706, 1, 4, {1, 4, 0, 0}},  {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 5, {'m', 'o', 'n', 'o', 0}},
            {50713, 3, 2, [&] {
                auto bytes = word(2);
                auto second = word(1);
                bytes.insert(bytes.end(), second.begin(), second.end());
                return bytes;
            }()},
            {50714, 5, 2, pair(black0, black1)},
            {50717, 4, 1, number(bits == 8 ? 255 : 60000)},
            {50718, 5, 2, pair(1, 1)},    {50719, 5, 2, pair(0, 0)},
            {50720, 5, 2, pair(3, 3)},    {50829, 4, 4, quad(0, 0, 3, 3)},
    };
    if (outputReferred) {
        fields.push_back({50879, 3, 1, word(1)});
    }
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> bytes = bigEndian
            ? std::vector<uint8_t>{'M', 'M', 0, 42}
            : std::vector<uint8_t>{'I', 'I', 42, 0};
    append32(&bytes, 8, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t entries = bytes.size();
    bytes.resize(entries + fields.size() * 12 + 4, 0);
    size_t stripOffsets = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
        } else {
            if (bytes.size() & 1) {
                bytes.push_back(0);
            }
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            if (field.id == 273) {
                stripOffsets = bytes.size();
            }
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + entries + i * 12, entry.data(), 12);
    }
    const std::array<uint16_t, 9> samples = bits == 8
            ? std::array<uint16_t, 9>{0, 10, 255, 20, 128, 230, 7, 160, 250}
            : std::array<uint16_t, 9>{0, 1024, 65535, 2048, 32768, 60000,
                                      800, 50000, 12345};
    for (int strip = 0; strip < 2; ++strip) {
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, stripOffsets + strip * 4, static_cast<uint32_t>(bytes.size()),
                bigEndian);
        for (int i = strip ? 6 : 0; i < (strip ? 9 : 6); ++i) {
            if (bits == 8) {
                bytes.push_back(static_cast<uint8_t>(samples[i]));
            } else {
                append16(&bytes, samples[i], bigEndian);
            }
        }
    }
    return bytes;
}

std::vector<uint8_t> make_deflate_mono_dng(bool bigEndian, bool multipleStrips,
                                           bool dng17 = false, bool horizontalPredictor = false) {
    auto bytes = make_stage1_dng(bigEndian, 16);
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto find = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + i * 12;
            const uint16_t value = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (value == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t offsets = find(273);
    const size_t lengths = find(279);
    const size_t rows = find(278);
    const size_t compression = find(259);
    const size_t predictor = find(339);
    const size_t version = find(50706);
    if (offsets == bytes.size() || lengths == bytes.size() ||
        rows == bytes.size() || compression == bytes.size() ||
        predictor == bytes.size() || version == bytes.size()) {
        return {};
    }
    std::array<std::vector<uint8_t>, 2> raw;
    for (int i = 0; i < 2; ++i) {
        const size_t start = get32(get32(offsets + 8) + i * 4);
        const size_t size = get32(get32(lengths + 8) + i * 4);
        raw[i].assign(bytes.begin() + start, bytes.begin() + start + size);
    }
    bytes[compression + 8] = bigEndian ? 0 : 8;
    bytes[compression + 9] = bigEndian ? 8 : 0;
    bytes[predictor] = bigEndian ? 1 : 317 & 0xff;
    bytes[predictor + 1] = bigEndian ? 317 & 0xff : 1;
    if (horizontalPredictor) {
        bytes[predictor + 8] = bigEndian ? 0 : 2;
        bytes[predictor + 9] = bigEndian ? 2 : 0;
    }
    if (dng17) {
        bytes[version + 9] = 7;
    }
    if (!multipleStrips) {
        store32(&bytes, offsets + 4, 1, bigEndian);
        store32(&bytes, lengths + 4, 1, bigEndian);
        store32(&bytes, rows + 8, 3, bigEndian);
        raw[0].insert(raw[0].end(), raw[1].begin(), raw[1].end());
    }
    for (int i = 0; i < (multipleStrips ? 2 : 1); ++i) {
        if (horizontalPredictor) {
            auto sampleAt = [bigEndian, &raw, i](size_t offset) -> uint16_t {
                return bigEndian ? (uint16_t(raw[i][offset]) << 8) | raw[i][offset + 1] :
                                   raw[i][offset] | (uint16_t(raw[i][offset + 1]) << 8);
            };
            for (size_t row = 0; row < raw[i].size(); row += 6) {
                for (int x = 2; x > 0; --x) {
                    const uint16_t difference = static_cast<uint16_t>(
                            sampleAt(row + x * 2) - sampleAt(row + (x - 1) * 2));
                    raw[i][row + x * 2] =
                            bigEndian ? difference >> 8 : difference & 0xff;
                    raw[i][row + x * 2 + 1] =
                            bigEndian ? difference & 0xff : difference >> 8;
                }
            }
        }
        std::vector<uint8_t> zipped(compressBound(raw[i].size()));
        uLongf size = zipped.size();
        if (compress2(zipped.data(), &size, raw[i].data(), raw[i].size(),
                      Z_BEST_SPEED) != Z_OK) {
            return {};
        }
        zipped.resize(size);
        const size_t offsetAt = multipleStrips ? get32(offsets + 8) + i * 4 : offsets + 8;
        const size_t lengthAt = multipleStrips ? get32(lengths + 8) + i * 4 : lengths + 8;
        store32(&bytes, offsetAt, static_cast<uint32_t>(bytes.size()), bigEndian);
        store32(&bytes, lengthAt, static_cast<uint32_t>(zipped.size()), bigEndian);
        bytes.insert(bytes.end(), zipped.begin(), zipped.end());
    }
    return bytes;
}

constexpr std::array<uint16_t, 27> kRgb16Samples = {
        0, 1, 65535, 256, 257, 258, 1000, 2000, 3000,
        4000, 5000, 6000, 0, 65535, 32768, 12345, 23456, 34567,
        7, 8, 9, 50000, 60000, 65534, 42, 43, 44,
};

std::vector<uint8_t> make_rgb16_dng(bool bigEndian, bool multipleStrips,
                                    bool childOfPreview = false) {
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> bytes;
        append16(&bytes, value, bigEndian);
        return bytes;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        return bytes;
    };
    auto rational = [bigEndian](uint32_t first, uint32_t second) {
        std::vector<uint8_t> bytes;
        for (uint32_t value : {first, 1u, second, 1u}) {
            append32(&bytes, value, bigEndian);
        }
        return bytes;
    };
    auto oneRational = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        append32(&bytes, 1, bigEndian);
        return bytes;
    };
    std::vector<uint8_t> allWhite, neutral, matrix;
    for (int i = 0; i < 3; ++i) {
        append16(&allWhite, 65535, bigEndian);
        append32(&neutral, 1, bigEndian);
        append32(&neutral, 1, bigEndian);
    }
    for (int i = 0; i < 9; ++i) {
        append32(&matrix, i % 4 == 0 ? 1 : 0, bigEndian);
        append32(&matrix, 1, bigEndian);
    }
    std::vector<uint8_t> stripSizes;
    append32(&stripSizes, multipleStrips ? 36 : 54, bigEndian);
    if (multipleStrips) {
        append32(&stripSizes, 18, bigEndian);
    }
    std::vector<Tag> fields = {
            {254, 4, 1, number(0)},
            {256, 4, 1, number(3)},
            {257, 4, 1, number(3)},
            {258, 3, 3, [&] {
                auto bits = word(16);
                for (int i = 1; i < 3; ++i) {
                    auto next = word(16);
                    bits.insert(bits.end(), next.begin(), next.end());
                }
                return bits;
            }()},
            {259, 3, 1, word(1)},
            {262, 3, 1, word(34892)},
            {273, 4, multipleStrips ? 2u : 1u,
             multipleStrips ? std::vector<uint8_t>(8, 0) : number(0)},
            {274, 3, 1, word(1)},
            {277, 3, 1, word(3)},
            {278, 4, 1, number(multipleStrips ? 2 : 3)},
            {279, 4, multipleStrips ? 2u : 1u, stripSizes},
            {284, 3, 1, word(1)},
            {339, 3, 1, word(1)},
            {50706, 1, 4, {1, 7, 0, 0}},
            {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 6, {'R', 'G', 'B', '1', '6', 0}},
            {50717, 3, 3, allWhite},
            {50718, 5, 2, rational(1, 1)},
            {50719, 5, 2, rational(0, 0)},
            {50720, 5, 2, rational(3, 3)},
            {50721, 10, 9, matrix},
            {50728, 5, 3, neutral},
            {50778, 3, 1, word(21)},
            {50738, 5, 1, oneRational(1)},
            {50780, 5, 1, oneRational(1)},
    };
    if (childOfPreview) {
        fields.erase(std::remove_if(fields.begin(), fields.end(),
                                    [](const Tag& tag) {
                                        return tag.id == 50706 || tag.id == 50707 ||
                                               tag.id == 50708;
                                    }),
                     fields.end());
    }
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> bytes = bigEndian
            ? std::vector<uint8_t>{'M', 'M', 0, 42}
            : std::vector<uint8_t>{'I', 'I', 42, 0};
    append32(&bytes, 8, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t entries = bytes.size();
    bytes.resize(entries + 12 * fields.size() + 4, 0);
    size_t stripOffsets = 0, singleStripEntry = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
            if (field.id == 273) {
                singleStripEntry = entries + 12 * i + 8;
            }
        } else {
            if (bytes.size() & 1) {
                bytes.push_back(0);
            }
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            if (field.id == 273) {
                stripOffsets = bytes.size();
            }
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + entries + 12 * i, entry.data(), 12);
    }
    const int strips = multipleStrips ? 2 : 1;
    for (int strip = 0; strip < strips; ++strip) {
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, multipleStrips ? stripOffsets + 4 * strip : singleStripEntry,
                static_cast<uint32_t>(bytes.size()), bigEndian);
        const int first = strip == 0 ? 0 : 18;
        const int end = multipleStrips && strip == 0 ? 18 : 27;
        for (int i = first; i < end; ++i) {
            append16(&bytes, kRgb16Samples[i], bigEndian);
        }
    }
    return bytes;
}

std::vector<uint8_t> make_deflate_rgb16_dng(bool bigEndian, bool multipleStrips,
                                            bool horizontalPredictor,
                                            bool childOfPreview = false) {
    auto bytes = make_rgb16_dng(bigEndian, multipleStrips, childOfPreview);
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto find = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t current = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (current == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t compression = find(259);
    const size_t offsets = find(273);
    const size_t lengths = find(279);
    const size_t response = find(50780);
    if (compression == bytes.size() || offsets == bytes.size() ||
        lengths == bytes.size() || response == bytes.size()) {
        return {};
    }
    bytes[compression + 8] = bigEndian ? 0 : 8;
    bytes[compression + 9] = bigEndian ? 8 : 0;
    bytes[response] = bigEndian ? 317 >> 8 : 317 & 0xff;
    bytes[response + 1] = bigEndian ? 317 & 0xff : 317 >> 8;
    bytes[response + 2] = bigEndian ? 0 : 3;
    bytes[response + 3] = bigEndian ? 3 : 0;
    store32(&bytes, response + 4, 1, bigEndian);
    bytes[response + 8] = bigEndian ? 0 : horizontalPredictor ? 2 : 1;
    bytes[response + 9] = bigEndian ? (horizontalPredictor ? 2 : 1) : 0;
    bytes[response + 10] = bytes[response + 11] = 0;
    auto sampleAt = [bigEndian](const std::vector<uint8_t>& raw, size_t at) -> uint16_t {
        return bigEndian ? (uint16_t(raw[at]) << 8) | raw[at + 1] :
                           raw[at] | (uint16_t(raw[at + 1]) << 8);
    };
    const int strips = multipleStrips ? 2 : 1;
    for (int strip = 0; strip < strips; ++strip) {
        const size_t offsetField = multipleStrips ? get32(offsets + 8) + 4 * strip : offsets + 8;
        const size_t lengthField = multipleStrips ? get32(lengths + 8) + 4 * strip : lengths + 8;
        const size_t source = get32(offsetField);
        const size_t size = get32(lengthField);
        if (source > bytes.size() || size > bytes.size() - source || size % 18 != 0) {
            return {};
        }
        std::vector<uint8_t> raw(bytes.begin() + source, bytes.begin() + source + size);
        if (horizontalPredictor) {
            for (size_t row = 0; row < raw.size(); row += 18) {
                for (int lane = 8; lane >= 3; --lane) {
                    const uint16_t difference = static_cast<uint16_t>(
                            sampleAt(raw, row + 2 * lane) -
                            sampleAt(raw, row + 2 * (lane - 3)));
                    raw[row + 2 * lane] = bigEndian ? difference >> 8 : difference & 0xff;
                    raw[row + 2 * lane + 1] = bigEndian ? difference & 0xff : difference >> 8;
                }
            }
        }
        std::vector<uint8_t> zipped(compressBound(raw.size()));
        uLongf zippedSize = zipped.size();
        if (compress2(zipped.data(), &zippedSize, raw.data(), raw.size(), Z_BEST_SPEED) != Z_OK) {
            return {};
        }
        zipped.resize(zippedSize);
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, offsetField, static_cast<uint32_t>(bytes.size()), bigEndian);
        store32(&bytes, lengthField, static_cast<uint32_t>(zipped.size()), bigEndian);
        bytes.insert(bytes.end(), zipped.begin(), zipped.end());
    }
    return bytes;
}

std::vector<uint8_t> make_preview_rgb16_dng(bool bigEndian, bool multipleStrips,
                                             bool compressed = false,
                                             bool horizontalPredictor = false) {
    auto preview = GetResourceAsData("images/color_wheel.jpg");
    if (!preview) {
        return {};
    }
    auto bytes = compressed ? make_deflate_rgb16_dng(
                                      bigEndian, multipleStrips, horizontalPredictor, true)
                            : make_rgb16_dng(bigEndian, multipleStrips, true);
    if (bytes.empty()) {
        return {};
    }
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    const uint32_t previewOffset = static_cast<uint32_t>(bytes.size());
    const auto* jpeg = static_cast<const uint8_t*>(preview->data());
    bytes.insert(bytes.end(), jpeg, jpeg + preview->size());
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    const uint32_t rootOffset = static_cast<uint32_t>(bytes.size());
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> encoded;
        append16(&encoded, value, bigEndian);
        return encoded;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> encoded;
        append32(&encoded, value, bigEndian);
        return encoded;
    };
    auto rational = [bigEndian](uint32_t num, uint32_t den) {
        std::vector<uint8_t> encoded;
        append32(&encoded, num, bigEndian);
        append32(&encoded, den, bigEndian);
        return encoded;
    };
    std::vector<uint8_t> coefficients, referenceBW, matrix, neutral;
    for (const auto& [num, den] : {std::pair{299u, 1000u}, std::pair{587u, 1000u},
                                   std::pair{114u, 1000u}}) {
        auto value = rational(num, den);
        coefficients.insert(coefficients.end(), value.begin(), value.end());
    }
    for (uint32_t value : {0u, 255u, 128u, 255u, 128u, 255u}) {
        auto fraction = rational(value, 1);
        referenceBW.insert(referenceBW.end(), fraction.begin(), fraction.end());
    }
    for (int i = 0; i < 9; ++i) {
        auto fraction = rational(i % 4 == 0 ? 1 : 0, 1);
        matrix.insert(matrix.end(), fraction.begin(), fraction.end());
    }
    for (int i = 0; i < 3; ++i) {
        auto fraction = rational(1, 1);
        neutral.insert(neutral.end(), fraction.begin(), fraction.end());
    }
    std::vector<Tag> fields = {
            {254, 4, 1, number(1)},
            {256, 4, 1, number(128)},
            {257, 4, 1, number(128)},
            {258, 3, 3, [&] {
                auto v = word(8);
                for (int i = 1; i < 3; ++i) {
                    auto next = word(8);
                    v.insert(v.end(), next.begin(), next.end());
                }
                return v;
            }()},
            {259, 3, 1, word(7)},
            {262, 3, 1, word(6)},
            {273, 4, 1, number(previewOffset)},
            {274, 3, 1, word(1)},
            {277, 3, 1, word(3)},
            {278, 4, 1, number(128)},
            {279, 4, 1, number(static_cast<uint32_t>(preview->size()))},
            {284, 3, 1, word(1)},
            {330, 4, 1, number(8)},
            {529, 5, 3, coefficients},
            {530, 3, 2, [&] {
                auto v = word(1);
                auto next = word(1);
                v.insert(v.end(), next.begin(), next.end());
                return v;
            }()},
            {531, 3, 1, word(1)},
            {532, 5, 6, referenceBW},
            {50706, 1, 4, {1, 7, 0, 0}},
            {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 6, {'R', 'G', 'B', '1', '6', 0}},
            {50721, 10, 9, matrix},
            {50728, 5, 3, neutral},
            {50734, 5, 1, rational(1, 1)},
            {50778, 3, 1, word(21)},
    };
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    store32(&bytes, 4, rootOffset, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t entries = bytes.size();
    bytes.resize(entries + 12 * fields.size() + 4, 0);
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
        } else {
            if (bytes.size() & 1) {
                bytes.push_back(0);
            }
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + entries + 12 * i, entry.data(), 12);
    }
    return bytes;
}

std::vector<uint8_t> encode_sof3_tile(const std::array<uint8_t, 12>& samples) {
    jpeg_compress_struct jpeg{};
    jpeg_error_mgr error{};
    jpeg.err = jpeg_std_error(&error);
    jpeg_create_compress(&jpeg);
    unsigned char* encoded = nullptr;
    unsigned long encodedSize = 0;
    jpeg_mem_dest(&jpeg, &encoded, &encodedSize);
    jpeg.image_width = 2;
    jpeg.image_height = 2;
    jpeg.input_components = 3;
    jpeg.in_color_space = JCS_RGB;
    jpeg_set_defaults(&jpeg);
    jpeg_set_colorspace(&jpeg, JCS_RGB);
    jpeg_enable_lossless(&jpeg, 1, 0);
    jpeg_start_compress(&jpeg, TRUE);
    while (jpeg.next_scanline < jpeg.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(samples.data() + jpeg.next_scanline * 6);
        jpeg_write_scanlines(&jpeg, &row, 1);
    }
    jpeg_finish_compress(&jpeg);
    std::vector<uint8_t> result(encoded, encoded + encodedSize);
    jpeg_destroy_compress(&jpeg);
    std::free(encoded);
    return result;
}

void put32_be(std::vector<uint8_t>* bytes, uint32_t value) {
    bytes->push_back(value >> 24);
    bytes->push_back((value >> 16) & 0xff);
    bytes->push_back((value >> 8) & 0xff);
    bytes->push_back(value & 0xff);
}

void put64_be(std::vector<uint8_t>* bytes, uint64_t value) {
    put32_be(bytes, static_cast<uint32_t>(value >> 32));
    put32_be(bytes, static_cast<uint32_t>(value));
}

std::vector<uint8_t> make_root_sof3_dng(bool withIdentityOpcode) {
    const auto first = encode_sof3_tile({1, 3, 5, 7, 9, 11,
                                        13, 15, 17, 19, 21, 23});
    const auto second = encode_sof3_tile({25, 27, 29, 200, 201, 202,
                                         31, 33, 35, 203, 204, 205});
    std::vector<uint8_t> bitDepth, white, black, byteCounts, matrix, neutral;
    for (int i = 0; i < 3; ++i) {
        put16(&bitDepth, 8);
        put16(&white, 255);
        put32(&black, 0);
        put32(&black, 1);
        put32(&neutral, 1);
        put32(&neutral, 1);
    }
    for (int i = 0; i < 9; ++i) {
        put32(&matrix, i % 4 == 0 ? 1 : 0);
        put32(&matrix, 1);
    }
    put32(&byteCounts, static_cast<uint32_t>(first.size()));
    put32(&byteCounts, static_cast<uint32_t>(second.size()));
    std::vector<Tag> tags = {
        long_tag(254, 0),
        long_tag(256, 3),
        long_tag(257, 2),
        {258, 3, 3, bitDepth},
        word(259, 7),
        word(262, 34892),
        word(274, 1),
        word(277, 3),
        word(284, 1),
        long_tag(322, 2),
        long_tag(323, 2),
        {324, 4, 2, std::vector<uint8_t>(8, 0)},
        {325, 4, 2, byteCounts},
        {50706, 1, 4, {1, 7, 0, 0}},
        {50707, 1, 4, {1, 3, 0, 0}},
        {50708, 2, 5, {'S', 'O', 'F', '3', 0}},
        {50713, 3, 2, {1, 0, 1, 0}},
        {50714, 5, 3, black},
        {50717, 3, 3, white},
        rational_pair(50718, 1, 1),
        rational_pair(50719, 0, 0),
        rational_pair(50720, 3, 2),
        {50721, 10, 9, matrix},
        {50728, 5, 3, neutral},
        rational(50738, 1),
        word(50778, 21),
        rational(50780, 1),
    };
    if (withIdentityOpcode) {
        std::vector<uint8_t> list;
        put32_be(&list, 3);
        for (uint32_t plane = 0; plane < 3; ++plane) {
            for (uint32_t value : {8u, 0x01030000u, 0u, 52u,
                                   0u, 0u, 2u, 3u, plane, 1u, 1u, 1u, 1u}) {
                put32_be(&list, value);
            }
            put64_be(&list, 0);
            put64_be(&list, 0x3ff0000000000000ULL);
        }
        tags.push_back({51009, 7, static_cast<uint32_t>(list.size()), std::move(list)});
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> data = {'I', 'I', 42, 0, 8, 0, 0, 0};
    put16(&data, static_cast<uint16_t>(tags.size()));
    const size_t entries = data.size();
    data.resize(entries + tags.size() * 12 + 4, 0);
    size_t offsets = 0;
    for (size_t i = 0; i < tags.size(); ++i) {
        const Tag& tag = tags[i];
        std::vector<uint8_t> entry;
        put16(&entry, tag.id);
        put16(&entry, tag.type);
        put32(&entry, tag.count);
        if (tag.bytes.size() <= 4) {
            entry.insert(entry.end(), tag.bytes.begin(), tag.bytes.end());
            entry.resize(12, 0);
        } else {
            if (data.size() & 1) {
                data.push_back(0);
            }
            put32(&entry, static_cast<uint32_t>(data.size()));
            if (tag.id == 324) {
                offsets = data.size();
            }
            data.insert(data.end(), tag.bytes.begin(), tag.bytes.end());
        }
        std::memcpy(data.data() + entries + i * 12, entry.data(), 12);
    }
    for (size_t i = 0; i < 2; ++i) {
        if (data.size() & 1) {
            data.push_back(0);
        }
        set32(&data, offsets + i * 4, static_cast<uint32_t>(data.size()));
        const auto& tile = i == 0 ? first : second;
        data.insert(data.end(), tile.begin(), tile.end());
    }
    return data;
}

constexpr std::array<uint32_t, 27> kFloatBits = {
        0, 0x3f800000, 0x3f000000, 0x3e800000, 0x3e000000, 0x3d800000,
        0x3f400000, 0x3f200000, 0x3f600000, 0x3f000000, 0x3f000000,
        0x3f000000, 0, 0x3f800000, 0x3f000000, 0x3e800000, 0x3e000000,
        0x3d800000, 0x3f400000, 0x3f200000, 0x3f600000, 0x3f000000,
        0x3f000000, 0, 0x3f800000, 0x3f000000,
};

uint32_t float_bits(const float& value) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float float_from_bits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::vector<uint8_t> make_float32_dng(bool bigEndian, bool multipleStrips,
                                      bool nonfinite = false) {
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> bytes;
        append16(&bytes, value, bigEndian);
        return bytes;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        return bytes;
    };
    auto rational = [bigEndian](uint32_t numerator, uint32_t denominator) {
        std::vector<uint8_t> bytes;
        append32(&bytes, numerator, bigEndian);
        append32(&bytes, denominator, bigEndian);
        return bytes;
    };
    auto pair = [&](uint32_t a, uint32_t b) {
        auto bytes = rational(a, 1);
        auto next = rational(b, 1);
        bytes.insert(bytes.end(), next.begin(), next.end());
        return bytes;
    };
    std::vector<uint8_t> matrix, white, asShotNeutral, sizes;
    for (int i = 0; i < 9; ++i) {
        append32(&matrix, i % 4 == 0 ? 1 : 0, bigEndian);
        append32(&matrix, 1, bigEndian);
    }
    for (int i = 0; i < 3; ++i) {
        append16(&white, 1, bigEndian);
        auto value = rational(1, 1);
        asShotNeutral.insert(asShotNeutral.end(), value.begin(), value.end());
    }
    append32(&sizes, multipleStrips ? 72 : 108, bigEndian);
    if (multipleStrips) {
        append32(&sizes, 36, bigEndian);
    }
    std::vector<uint8_t> precision, format;
    for (int i = 0; i < 3; ++i) {
        append16(&precision, 32, bigEndian);
        append16(&format, 3, bigEndian);
    }
    std::vector<Tag> fields = {
            {254, 4, 1, number(0)}, {256, 4, 1, number(3)}, {257, 4, 1, number(3)},
            {258, 3, 3, precision}, {259, 3, 1, word(1)}, {262, 3, 1, word(34892)},
            {273, 4, multipleStrips ? 2u : 1u,
             multipleStrips ? std::vector<uint8_t>(8, 0) : number(0)},
            {274, 3, 1, word(1)}, {277, 3, 1, word(3)},
            {278, 4, 1, number(multipleStrips ? 2 : 3)},
            {279, 4, multipleStrips ? 2u : 1u, sizes},
            {284, 3, 1, word(1)}, {339, 3, 3, format},
            {50706, 1, 4, {1, 7, 0, 0}}, {50707, 1, 4, {1, 4, 0, 0}},
            {50708, 2, 6, {'F', 'l', 'o', 'a', 't', 0}},
            {50717, 3, 3, white},
            {50718, 5, 2, pair(1, 1)}, {50719, 5, 2, pair(0, 0)},
            {50720, 5, 2, pair(3, 3)}, {50721, 10, 9, matrix},
            {50728, 5, 3, asShotNeutral}, {50738, 5, 1, rational(1, 1)},
            {50778, 3, 1, word(23)}, {50780, 5, 1, rational(1, 1)},
            {51109, 10, 1, rational(uint32_t(-2), 1)},
            {51110, 4, 1, number(1)},
    };
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> bytes = bigEndian
            ? std::vector<uint8_t>{'M', 'M', 0, 42}
            : std::vector<uint8_t>{'I', 'I', 42, 0};
    append32(&bytes, 8, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t entries = bytes.size();
    bytes.resize(entries + 12 * fields.size() + 4, 0);
    size_t offsets = 0, inlineOffset = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
            if (field.id == 273) {
                inlineOffset = entries + i * 12 + 8;
            }
        } else {
            if (bytes.size() & 1) {
                bytes.push_back(0);
            }
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            if (field.id == 273) {
                offsets = bytes.size();
            }
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + entries + i * 12, entry.data(), 12);
    }
    auto samples = kFloatBits;
    if (nonfinite) {
        samples[0] = 0x80000000; // Negative zero.
        samples[1] = 0x7fc12345; // Quiet NaN payload.
        samples[2] = 0x7f800000; // Positive infinity.
        samples[3] = 0x7f812345; // Signaling NaN payload.
    }
    for (int strip = 0; strip < (multipleStrips ? 2 : 1); ++strip) {
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, multipleStrips ? offsets + strip * 4 : inlineOffset,
                static_cast<uint32_t>(bytes.size()), bigEndian);
        const int first = strip == 0 ? 0 : 18;
        const int end = multipleStrips && strip == 0 ? 18 : 27;
        for (int i = first; i < end; ++i) {
            append32(&bytes, samples[i], bigEndian);
        }
    }
    return bytes;
}

constexpr std::array<uint8_t, 27> kRgb8Samples = {
        0, 0, 0, 1, 1, 1, 255, 255, 255,
        255, 0, 0, 128, 0, 127, 0, 0, 255,
        0, 1, 0, 0, 128, 0, 0, 255, 0,
};

std::vector<uint8_t> make_rgb8_root_dng(bool bigEndian, bool multipleStrips, bool ramp,
                                        bool srgbProfile = false, bool cube = false) {
    const uint32_t width = ramp || cube ? 256 : 3;
    const uint32_t height = cube ? 256 : 3;
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> bytes;
        append16(&bytes, value, bigEndian);
        return bytes;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        return bytes;
    };
    auto rational = [bigEndian](uint32_t numerator, uint32_t denominator) {
        std::vector<uint8_t> bytes;
        append32(&bytes, numerator, bigEndian);
        append32(&bytes, denominator, bigEndian);
        return bytes;
    };
    auto pair = [&](uint32_t first, uint32_t second) {
        auto bytes = rational(first, 1);
        auto next = rational(second, 1);
        bytes.insert(bytes.end(), next.begin(), next.end());
        return bytes;
    };
    std::vector<uint8_t> pixels;
    if (cube) {
        pixels.reserve(width * height * 3);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                pixels.insert(pixels.end(), {uint8_t(x), uint8_t(y),
                                              uint8_t((x + 3 * y) % 256)});
            }
        }
    } else if (ramp) {
        pixels.reserve(width * height * 3);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                if (y == 0) {
                    pixels.insert(pixels.end(), {uint8_t(x), uint8_t(x), uint8_t(x)});
                } else if (y == 1) {
                    pixels.insert(pixels.end(), {uint8_t(x), 0, uint8_t(255 - x)});
                } else {
                    pixels.insert(pixels.end(), {0, uint8_t(x), 0});
                }
            }
        }
    } else {
        pixels.insert(pixels.end(), kRgb8Samples.begin(), kRgb8Samples.end());
    }
    std::vector<uint8_t> precision, sampleFormat, white, black, matrix, forward, neutral, active,
            tone;
    for (int i = 0; i < 3; ++i) {
        append16(&precision, 8, bigEndian);
        append16(&sampleFormat, 1, bigEndian);
        append16(&white, 255, bigEndian);
        auto zero = rational(0, 1);
        black.insert(black.end(), zero.begin(), zero.end());
        auto one = rational(1, 1);
        neutral.insert(neutral.end(), one.begin(), one.end());
    }
    constexpr std::array<uint32_t, 9> kSrgbForward =
            {28578, 25241, 9376, 14581, 46981, 3972, 912, 6362, 46799};
    constexpr std::array<uint32_t, 9> kCameraMatrix =
            {1037, 0, 0, 0, 1000, 0, 0, 0, 1212};
    for (int i = 0; i < 9; ++i) {
        auto color = rational(srgbProfile ? kCameraMatrix[i] : uint32_t(i % 4 == 0),
                              srgbProfile ? 1000 : 1);
        auto target = rational(srgbProfile ? kSrgbForward[i] : uint32_t(i % 4 == 0),
                               srgbProfile ? 65536 : 1);
        matrix.insert(matrix.end(), color.begin(), color.end());
        forward.insert(forward.end(), target.begin(), target.end());
    }
    for (uint32_t n : {0u, 0u, height, width}) {
        append32(&active, n, bigEndian);
    }
    for (uint32_t bits : {0u, 0u, 0x3f800000u, 0x3f800000u}) {
        append32(&tone, bits, bigEndian);
    }
    const bool split = multipleStrips && !ramp && !cube;
    std::vector<uint8_t> stripSizes;
    append32(&stripSizes, (split ? 2 : height) * width * 3, bigEndian);
    if (split) {
        append32(&stripSizes, width * 3, bigEndian);
    }
    std::vector<Tag> fields = {
            {254, 4, 1, number(0)}, {256, 4, 1, number(width)}, {257, 4, 1, number(height)},
            {258, 3, 3, precision}, {259, 3, 1, word(1)}, {262, 3, 1, word(34892)},
            {273, 4, split ? 2u : 1u,
             split ? std::vector<uint8_t>(8, 0) : number(0)},
            {274, 3, 1, word(1)}, {277, 3, 1, word(3)},
            {278, 4, 1, number(split ? 2 : height)}, {279, 4, split ? 2u : 1u, stripSizes},
            {284, 3, 1, word(1)}, {339, 3, 3, sampleFormat},
            {50706, 1, 4, {1, 7, 0, 0}}, {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 5, {'R', 'G', 'B', '8', 0}},
            {50713, 3, 2, [&] {
                auto v = word(1);
                auto next = word(1);
                v.insert(v.end(), next.begin(), next.end());
                return v;
            }()},
            {50714, 5, 3, black}, {50717, 3, 3, white},
            {50718, 5, 2, pair(1, 1)}, {50719, 5, 2, pair(0, 0)},
            {50720, 5, 2, pair(width, height)}, {50721, 10, 9, matrix},
            {50728, 5, 3, neutral}, {50778, 3, 1, word(21)},
            {50829, 4, 4, active}, {50879, 3, 1, word(1)},
            {50940, 11, 4, tone}, {50964, 10, 9, forward},
            {51110, 4, 1, number(1)},
    };
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> bytes = bigEndian
            ? std::vector<uint8_t>{'M', 'M', 0, 42}
            : std::vector<uint8_t>{'I', 'I', 42, 0};
    append32(&bytes, 8, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t entries = bytes.size();
    bytes.resize(entries + fields.size() * 12 + 4, 0);
    size_t stripOffsets = 0, inlineOffset = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
            if (field.id == 273) {
                inlineOffset = entries + 12 * i + 8;
            }
        } else {
            if (bytes.size() & 1) {
                bytes.push_back(0);
            }
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            if (field.id == 273) {
                stripOffsets = bytes.size();
            }
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + entries + i * 12, entry.data(), 12);
    }
    for (int strip = 0; strip < (split ? 2 : 1); ++strip) {
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, split ? stripOffsets + 4 * strip : inlineOffset,
                static_cast<uint32_t>(bytes.size()), bigEndian);
        const size_t start = strip == 0 ? 0 : 2 * width * 3;
        const size_t end = split && strip == 0 ? 2 * width * 3 : pixels.size();
        bytes.insert(bytes.end(), pixels.begin() + start, pixels.begin() + end);
    }
    return bytes;
}

std::vector<uint8_t> make_deflate_rgb8_srgb_dng(bool bigEndian, bool multipleStrips,
                                                bool horizontalPredictor, bool cube = false) {
    auto bytes = make_rgb8_root_dng(bigEndian, multipleStrips, false, true, cube);
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto find = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t tag = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (tag == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t compression = find(259);
    const size_t offsets = find(273);
    const size_t lengths = find(279);
    const size_t format = find(339);
    if (compression == bytes.size() || offsets == bytes.size() ||
        lengths == bytes.size() || format == bytes.size()) {
        return {};
    }
    bytes[compression + 8] = bigEndian ? 0 : 8;
    bytes[compression + 9] = bigEndian ? 8 : 0;
    bytes[format] = bigEndian ? 317 >> 8 : 317 & 0xff;
    bytes[format + 1] = bigEndian ? 317 & 0xff : 317 >> 8;
    bytes[format + 2] = bigEndian ? 0 : 3;
    bytes[format + 3] = bigEndian ? 3 : 0;
    store32(&bytes, format + 4, 1, bigEndian);
    bytes[format + 8] = bigEndian ? 0 : horizontalPredictor ? 2 : 1;
    bytes[format + 9] = bigEndian ? (horizontalPredictor ? 2 : 1) : 0;
    bytes[format + 10] = bytes[format + 11] = 0;
    const int stripCount = multipleStrips && !cube ? 2 : 1;
    const size_t rowBytes = cube ? 768 : 9;
    for (int strip = 0; strip < stripCount; ++strip) {
        const size_t offsetField = stripCount == 2 ? get32(offsets + 8) + strip * 4 : offsets + 8;
        const size_t lengthField = stripCount == 2 ? get32(lengths + 8) + strip * 4 : lengths + 8;
        const size_t source = get32(offsetField);
        const size_t size = get32(lengthField);
        if (source > bytes.size() || size > bytes.size() - source || size % rowBytes != 0) {
            return {};
        }
        std::vector<uint8_t> raw(bytes.begin() + source, bytes.begin() + source + size);
        if (horizontalPredictor) {
            for (size_t row = 0; row < raw.size(); row += rowBytes) {
                for (int i = static_cast<int>(rowBytes) - 1; i >= 3; --i) {
                    raw[row + i] = static_cast<uint8_t>(raw[row + i] - raw[row + i - 3]);
                }
            }
        }
        std::vector<uint8_t> zipped(compressBound(raw.size()));
        uLongf zippedSize = zipped.size();
        if (compress2(zipped.data(), &zippedSize, raw.data(), raw.size(), Z_BEST_SPEED) != Z_OK) {
            return {};
        }
        zipped.resize(zippedSize);
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, offsetField, static_cast<uint32_t>(bytes.size()), bigEndian);
        store32(&bytes, lengthField, static_cast<uint32_t>(zipped.size()), bigEndian);
        bytes.insert(bytes.end(), zipped.begin(), zipped.end());
    }
    return bytes;
}

uint16_t bayer_sample(uint32_t x, uint32_t y) {
    if (y == 0 && x == 0) return 256;
    if (y == 0 && x == 1) return 512;
    if (y == 0 && x == 2) return 0;
    if (y == 0 && x == 3) return 4095;
    return static_cast<uint16_t>(1100 + ((y * 16 + x) * 37) % 2700);
}

uint16_t uniform_bayer_sample(uint32_t x, uint32_t y) {
    if (!(x & 1) && !(y & 1)) return 10000;
    if ((x & 1) && (y & 1)) return 30000;
    return 20000;
}

std::vector<uint8_t> make_rggb_dng(bool bigEndian, bool multipleStrips,
                                    bool uniform = false) {
    auto word = [bigEndian](uint16_t value) {
        std::vector<uint8_t> bytes;
        append16(&bytes, value, bigEndian);
        return bytes;
    };
    auto number = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        return bytes;
    };
    auto rational = [bigEndian](uint32_t value) {
        std::vector<uint8_t> bytes;
        append32(&bytes, value, bigEndian);
        append32(&bytes, 1, bigEndian);
        return bytes;
    };
    auto pair = [&](uint32_t first, uint32_t second) {
        auto value = rational(first);
        auto next = rational(second);
        value.insert(value.end(), next.begin(), next.end());
        return value;
    };
    std::vector<uint8_t> black, neutral, matrix, area;
    for (uint32_t value : {256u, 512u, 512u, 1024u}) {
        auto level = rational(uniform ? 0 : value);
        black.insert(black.end(), level.begin(), level.end());
    }
    for (int i = 0; i < 3; ++i) {
        auto level = rational(1);
        neutral.insert(neutral.end(), level.begin(), level.end());
    }
    for (int i = 0; i < 9; ++i) {
        append32(&matrix, i % 4 == 0 ? 1 : 0, bigEndian);
        append32(&matrix, 1, bigEndian);
    }
    for (uint32_t value : {0u, 0u, 16u, 16u}) {
        append32(&area, value, bigEndian);
    }
    std::vector<uint8_t> lengths;
    append32(&lengths, multipleStrips ? 256 : 512, bigEndian);
    if (multipleStrips) {
        append32(&lengths, 256, bigEndian);
    }
    std::vector<Tag> fields = {
            {254, 4, 1, number(0)}, {256, 4, 1, number(16)},
            {257, 4, 1, number(16)}, {258, 3, 1, word(16)},
            {259, 3, 1, word(1)}, {262, 3, 1, word(32803)},
            {273, 4, multipleStrips ? 2u : 1u,
             multipleStrips ? std::vector<uint8_t>(8, 0) : number(0)},
            {274, 3, 1, word(1)}, {277, 3, 1, word(1)},
            {278, 4, 1, number(multipleStrips ? 8 : 16)},
            {279, 4, multipleStrips ? 2u : 1u, lengths},
            {284, 3, 1, word(1)}, {339, 3, 1, word(1)},
            {33421, 3, 2, [&] {
                auto v = word(2);
                auto next = word(2);
                v.insert(v.end(), next.begin(), next.end());
                return v;
            }()},
            {33422, 1, 4, {0, 1, 1, 2}},
            {50706, 1, 4, {1, 4, 0, 0}}, {50707, 1, 4, {1, 1, 0, 0}},
            {50708, 2, 18, {'I', 'n', 'd', 'e', 'p', 'e', 'n', 'd', 'e',
                             'n', 't', ' ', 'B', 'a', 'y', 'e', 'r', 0}},
            {50710, 1, 3, {0, 1, 2}}, {50711, 3, 1, word(1)},
            {50713, 3, 2, [&] {
                auto v = word(2);
                auto next = word(2);
                v.insert(v.end(), next.begin(), next.end());
                return v;
            }()},
            {50714, 5, 4, black}, {50717, 4, 1, number(uniform ? 65535 : 4095)},
            {50718, 5, 2, pair(1, 1)}, {50719, 5, 2, pair(0, 0)},
            {50720, 5, 2, pair(16, 16)}, {50721, 10, 9, matrix},
            {50727, 5, 3, neutral}, {50728, 5, 3, neutral},
            {50778, 3, 1, word(21)}, {50829, 4, 4, area},
    };
    std::sort(fields.begin(), fields.end(), [](const Tag& a, const Tag& b) {
        return a.id < b.id;
    });
    std::vector<uint8_t> bytes = bigEndian
            ? std::vector<uint8_t>{'M', 'M', 0, 42}
            : std::vector<uint8_t>{'I', 'I', 42, 0};
    append32(&bytes, 8, bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    const size_t table = bytes.size();
    bytes.resize(table + fields.size() * 12 + 4, 0);
    size_t offsets = 0, inlineOffset = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const Tag& field = fields[i];
        std::vector<uint8_t> entry;
        append16(&entry, field.id, bigEndian);
        append16(&entry, field.type, bigEndian);
        append32(&entry, field.count, bigEndian);
        if (field.bytes.size() <= 4) {
            entry.insert(entry.end(), field.bytes.begin(), field.bytes.end());
            entry.resize(12, 0);
            if (field.id == 273) inlineOffset = table + 12 * i + 8;
        } else {
            if (bytes.size() & 1) bytes.push_back(0);
            append32(&entry, static_cast<uint32_t>(bytes.size()), bigEndian);
            if (field.id == 273) offsets = bytes.size();
            bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
        }
        std::memcpy(bytes.data() + table + i * 12, entry.data(), 12);
    }
    for (int strip = 0; strip < (multipleStrips ? 2 : 1); ++strip) {
        if (bytes.size() & 1) bytes.push_back(0);
        store32(&bytes, multipleStrips ? offsets + strip * 4 : inlineOffset,
                static_cast<uint32_t>(bytes.size()), bigEndian);
        const uint32_t first = strip == 0 ? 0 : 8;
        const uint32_t end = multipleStrips && strip == 0 ? 8 : 16;
        for (uint32_t y = first; y < end; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                append16(&bytes, uniform ? uniform_bayer_sample(x, y) :
                                            bayer_sample(x, y), bigEndian);
            }
        }
    }
    return bytes;
}

std::vector<uint8_t> encode_sof3_bayer_tile(uint32_t tileX, uint32_t tileY, bool uniform) {
    jpeg_compress_struct jpeg{};
    jpeg_error_mgr error{};
    jpeg.err = jpeg_std_error(&error);
    jpeg_create_compress(&jpeg);
    unsigned char* encoded = nullptr;
    unsigned long encodedSize = 0;
    jpeg_mem_dest(&jpeg, &encoded, &encodedSize);
    jpeg.image_width = 10;
    jpeg.image_height = 10;
    jpeg.input_components = 1;
    jpeg.in_color_space = JCS_GRAYSCALE;
    jpeg_set_defaults(&jpeg);
    jpeg.data_precision = 16;
    jpeg_enable_lossless(&jpeg, 1, 0);
    jpeg_start_compress(&jpeg, TRUE);
    std::array<J16SAMPLE, 10> row{};
    while (jpeg.next_scanline < jpeg.image_height) {
        for (uint32_t x = 0; x < 10; ++x) {
            const uint32_t globalX = tileX * 10 + x;
            const uint32_t globalY = tileY * 10 + jpeg.next_scanline;
            row[x] = uniform ? uniform_bayer_sample(globalX, globalY) :
                               bayer_sample(globalX, globalY);
        }
        J16SAMPROW scanline = row.data();
        if (jpeg16_write_scanlines(&jpeg, &scanline, 1) != 1) {
            jpeg_destroy_compress(&jpeg);
            std::free(encoded);
            return {};
        }
    }
    jpeg_finish_compress(&jpeg);
    std::vector<uint8_t> result(encoded, encoded + encodedSize);
    jpeg_destroy_compress(&jpeg);
    std::free(encoded);
    return result;
}

std::vector<uint8_t> make_tiled_bayer_dng(bool bigEndian, bool uniform = false) {
    auto bytes = make_rggb_dng(bigEndian, false, uniform);
    auto entry = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t candidate = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (candidate == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t offsets = entry(273);
    const size_t tileWidth = entry(278);
    const size_t lengths = entry(279);
    const size_t tileHeight = entry(274);
    const size_t compression = entry(259);
    if (offsets == bytes.size() || lengths == bytes.size() ||
        tileWidth == bytes.size() || tileHeight == bytes.size() ||
        compression == bytes.size()) {
        return {};
    }
    auto replace_tag = [&](size_t at, uint16_t id) {
        bytes[at] = bigEndian ? id >> 8 : id & 0xff;
        bytes[at + 1] = bigEndian ? id & 0xff : id >> 8;
    };
    replace_tag(offsets, 324);
    replace_tag(lengths, 325);
    replace_tag(tileWidth, 322);
    replace_tag(tileHeight, 323);
    bytes[compression + 8] = bigEndian ? 0 : 7;
    bytes[compression + 9] = bigEndian ? 7 : 0;
    bytes[tileHeight + 2] = bigEndian ? 0 : 4;
    bytes[tileHeight + 3] = bigEndian ? 4 : 0;
    store32(&bytes, tileWidth + 8, 10, bigEndian);
    store32(&bytes, tileHeight + 8, 10, bigEndian);
    store32(&bytes, offsets + 4, 4, bigEndian);
    store32(&bytes, lengths + 4, 4, bigEndian);
    const size_t offsetsTable = bytes.size();
    bytes.resize(bytes.size() + 16, 0);
    const size_t lengthsTable = bytes.size();
    bytes.resize(bytes.size() + 16, 0);
    store32(&bytes, offsets + 8, static_cast<uint32_t>(offsetsTable), bigEndian);
    store32(&bytes, lengths + 8, static_cast<uint32_t>(lengthsTable), bigEndian);
    for (uint32_t ty = 0; ty < 2; ++ty) {
        for (uint32_t tx = 0; tx < 2; ++tx) {
            auto tile = encode_sof3_bayer_tile(tx, ty, uniform);
            if (tile.empty()) {
                return {};
            }
            const size_t i = ty * 2 + tx;
            store32(&bytes, offsetsTable + 4 * i, static_cast<uint32_t>(bytes.size()),
                    bigEndian);
            store32(&bytes, lengthsTable + 4 * i, static_cast<uint32_t>(tile.size()),
                    bigEndian);
            bytes.insert(bytes.end(), tile.begin(), tile.end());
        }
    }
    return bytes;
}

std::vector<uint8_t> make_identity_bayer_dng(bool bigEndian, bool tiled,
                                              bool multipleStrips = true,
                                              bool uniform = false) {
    auto bytes = tiled ? make_tiled_bayer_dng(bigEndian, uniform) :
                         make_rggb_dng(bigEndian, multipleStrips, uniform);
    if (bytes.empty()) {
        return {};
    }
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto field = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t candidate = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (candidate == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t black = field(50714);
    const size_t white = field(50717);
    if (black == bytes.size() || white == bytes.size()) {
        return {};
    }
    const size_t levels = get32(black + 8);
    for (int i = 0; i < 4; ++i) {
        store32(&bytes, levels + 8 * i, 0, bigEndian);
    }
    store32(&bytes, white + 8, 65535, bigEndian);
    return bytes;
}

std::vector<uint8_t> make_deflate_bayer_dng(bool bigEndian, bool multipleStrips,
                                            bool horizontalPredictor) {
    auto bytes = make_identity_bayer_dng(bigEndian, false, multipleStrips);
    if (bytes.empty()) {
        return {};
    }
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto find = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t tag = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (tag == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t compression = find(259);
    const size_t offsets = find(273);
    const size_t lengths = find(279);
    const size_t neutral = find(50728);
    if (compression == bytes.size() || offsets == bytes.size() ||
        lengths == bytes.size() || neutral == bytes.size()) {
        return {};
    }
    bytes[compression + 8] = bigEndian ? 0 : 8;
    bytes[compression + 9] = bigEndian ? 8 : 0;
    bytes[neutral] = bigEndian ? 317 >> 8 : 317 & 0xff;
    bytes[neutral + 1] = bigEndian ? 317 & 0xff : 317 >> 8;
    bytes[neutral + 2] = bigEndian ? 0 : 3;
    bytes[neutral + 3] = bigEndian ? 3 : 0;
    store32(&bytes, neutral + 4, 1, bigEndian);
    bytes[neutral + 8] = bigEndian ? 0 : horizontalPredictor ? 2 : 1;
    bytes[neutral + 9] = bigEndian ? (horizontalPredictor ? 2 : 1) : 0;
    bytes[neutral + 10] = bytes[neutral + 11] = 0;
    const int stripCount = multipleStrips ? 2 : 1;
    for (int strip = 0; strip < stripCount; ++strip) {
        const size_t offsetField = multipleStrips ? get32(offsets + 8) + strip * 4 : offsets + 8;
        const size_t lengthField = multipleStrips ? get32(lengths + 8) + strip * 4 : lengths + 8;
        const size_t source = get32(offsetField);
        const size_t size = get32(lengthField);
        if (source > bytes.size() || size > bytes.size() - source || size % 32 != 0) {
            return {};
        }
        std::vector<uint8_t> raw(bytes.begin() + source, bytes.begin() + source + size);
        if (horizontalPredictor) {
            for (size_t row = 0; row < raw.size(); row += 32) {
                for (int x = 15; x > 0; --x) {
                    const size_t current = row + 2 * x;
                    const size_t previous = current - 2;
                    const uint16_t value = bigEndian ?
                            (uint16_t(raw[current]) << 8) | raw[current + 1] :
                            raw[current] | (uint16_t(raw[current + 1]) << 8);
                    const uint16_t prior = bigEndian ?
                            (uint16_t(raw[previous]) << 8) | raw[previous + 1] :
                            raw[previous] | (uint16_t(raw[previous + 1]) << 8);
                    const uint16_t difference = static_cast<uint16_t>(value - prior);
                    raw[current] = bigEndian ? difference >> 8 : difference & 0xff;
                    raw[current + 1] = bigEndian ? difference & 0xff : difference >> 8;
                }
            }
        }
        std::vector<uint8_t> zipped(compressBound(raw.size()));
        uLongf zippedSize = zipped.size();
        if (compress2(zipped.data(), &zippedSize, raw.data(), raw.size(), Z_BEST_SPEED) != Z_OK) {
            return {};
        }
        zipped.resize(zippedSize);
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        store32(&bytes, offsetField, static_cast<uint32_t>(bytes.size()), bigEndian);
        store32(&bytes, lengthField, static_cast<uint32_t>(zipped.size()), bigEndian);
        bytes.insert(bytes.end(), zipped.begin(), zipped.end());
    }
    return bytes;
}

std::vector<uint8_t> with_linearization_table(std::vector<uint8_t> bytes,
                                              bool bigEndian, bool shortTable,
                                              uint16_t maxSample = UINT16_MAX) {
    if (bytes.empty()) {
        return {};
    }
    const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                    bytes[8] | (size_t(bytes[9]) << 8);
    std::vector<std::array<uint8_t, 12>> fields;
    fields.reserve(count + 1);
    for (size_t i = 0; i < count; ++i) {
        std::array<uint8_t, 12> entry;
        std::memcpy(entry.data(), bytes.data() + 10 + 12 * i, entry.size());
        fields.push_back(entry);
    }
    uint32_t tableOffset = 0;
    if (!shortTable) {
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        tableOffset = static_cast<uint32_t>(bytes.size());
        for (uint32_t sample = 0; sample <= maxSample; ++sample) {
            append16(&bytes, static_cast<uint16_t>(std::min(sample * 2, uint32_t(maxSample))),
                     bigEndian);
        }
    }
    std::vector<uint8_t> table;
    append16(&table, 50712, bigEndian);
    append16(&table, 3, bigEndian);
    append32(&table, shortTable ? 2 : uint32_t(maxSample) + 1, bigEndian);
    if (shortTable) {
        append16(&table, 0, bigEndian);
        append16(&table, maxSample, bigEndian);
    } else {
        append32(&table, tableOffset, bigEndian);
    }
    std::array<uint8_t, 12> tableEntry;
    std::copy(table.begin(), table.end(), tableEntry.begin());
    fields.push_back(tableEntry);
    std::sort(fields.begin(), fields.end(), [bigEndian](const auto& a, const auto& b) {
        const uint16_t left = bigEndian ? (uint16_t(a[0]) << 8) | a[1] :
                                          a[0] | (uint16_t(a[1]) << 8);
        const uint16_t right = bigEndian ? (uint16_t(b[0]) << 8) | b[1] :
                                           b[0] | (uint16_t(b[1]) << 8);
        return left < right;
    });
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    store32(&bytes, 4, static_cast<uint32_t>(bytes.size()), bigEndian);
    append16(&bytes, static_cast<uint16_t>(fields.size()), bigEndian);
    for (const auto& entry : fields) {
        bytes.insert(bytes.end(), entry.begin(), entry.end());
    }
    append32(&bytes, 0, bigEndian);
    return bytes;
}

std::vector<uint8_t> with_extra_linearization_entry_le(std::vector<uint8_t> bytes, uint16_t value) {
    if (bytes.empty() || bytes[0] != 'I') {
        return {};
    }
    const size_t field = entry_for(bytes, 50712);
    if (field == bytes.size()) {
        return {};
    }
    const uint32_t count = read32(bytes, field + 4);
    const size_t start = read32(bytes, field + 8);
    if (count == 0 || count > 65536 || start > bytes.size() ||
        size_t(count) * 2 > bytes.size() - start ||
        bytes.size() > UINT32_MAX - 2 * (size_t(count) + 1)) {
        return {};
    }
    std::vector<uint8_t> table(bytes.begin() + start, bytes.begin() + start + 2 * count);
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    const auto offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), table.begin(), table.end());
    append16(&bytes, value, false);
    set32(&bytes, field + 4, count + 1);
    set32(&bytes, field + 8, offset);
    return bytes;
}

std::vector<uint8_t> make_linearized_bayer_dng(bool bigEndian, bool multipleStrips,
                                               bool compressed, bool shortTable,
                                               bool tiled = false, bool uniform = false) {
    auto bytes = tiled ? make_identity_bayer_dng(bigEndian, true, false, uniform) :
                 compressed ? make_deflate_bayer_dng(bigEndian, multipleStrips, false) :
                              make_identity_bayer_dng(bigEndian, false, multipleStrips, uniform);
    return with_linearization_table(std::move(bytes), bigEndian, shortTable);
}

std::vector<uint8_t> make_linearized_rgb16_dng(bool bigEndian, bool multipleStrips,
                                               bool compressed, bool shortTable) {
    auto bytes = compressed ? make_deflate_rgb16_dng(bigEndian, multipleStrips, true) :
                              make_rgb16_dng(bigEndian, multipleStrips);
    return with_linearization_table(std::move(bytes), bigEndian, shortTable);
}

std::vector<uint8_t> make_linearized_rgb8_dng(bool bigEndian, bool multipleStrips,
                                              bool compressed, bool shortTable,
                                              bool cube = false) {
    auto bytes = compressed ? make_deflate_rgb8_srgb_dng(
                                      bigEndian, multipleStrips, true, cube) :
                              make_rgb8_root_dng(bigEndian, multipleStrips, false, true, cube);
    return with_linearization_table(std::move(bytes), bigEndian, shortTable, UINT8_MAX);
}

std::vector<uint8_t> make_linearized_mono16_dng(bool bigEndian, bool multipleStrips,
                                                bool compressed, bool shortTable) {
    auto bytes = compressed ? make_deflate_mono_dng(
                                      bigEndian, multipleStrips, false, true) :
                              make_stage1_dng(bigEndian, 16, true);
    if (bytes.empty()) {
        return {};
    }
    auto get32 = [&](size_t at) {
        if (bigEndian) {
            return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                   (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        }
        return read32(bytes, at);
    };
    auto find = [&](uint16_t id) {
        const size_t count = bigEndian ? (size_t(bytes[8]) << 8) | bytes[9] :
                                        bytes[8] | (size_t(bytes[9]) << 8);
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 10 + 12 * i;
            const uint16_t tag = bigEndian ?
                    (uint16_t(bytes[at]) << 8) | bytes[at + 1] :
                    bytes[at] | (uint16_t(bytes[at + 1]) << 8);
            if (tag == id) {
                return at;
            }
        }
        return bytes.size();
    };
    const size_t black = find(50714);
    const size_t white = find(50717);
    if (black == bytes.size() || white == bytes.size()) {
        return {};
    }
    const size_t levels = get32(black + 8);
    store32(&bytes, levels, 0, bigEndian);
    store32(&bytes, levels + 8, 0, bigEndian);
    store32(&bytes, white + 8, 65535, bigEndian);
    return with_linearization_table(std::move(bytes), bigEndian, shortTable);
}

rust_raw::DecodeStatus stage1_status(const std::vector<uint8_t>& bytes) {
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkMemoryStream stream(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(&stream);
    auto reader = rust_raw::new_reader(std::move(adapter));
    return reader->stage1_status();
}

std::vector<uint8_t> make_late_ifd_dng() {
    auto bytes = make_dng();
    constexpr uint32_t kGap = 16 * 1024;
    bytes.insert(bytes.begin() + 8, kGap, 0);
    set32(&bytes, 4, 8 + kGap);
    for (uint16_t id : {273, 50708, 50714, 50718, 50719, 50720, 50829}) {
        const size_t entry = entry_for(bytes, id);
        if (entry == bytes.size()) {
            return {};
        }
        const uint32_t offset = bytes[entry + 8] |
                                (uint32_t(bytes[entry + 9]) << 8) |
                                (uint32_t(bytes[entry + 10]) << 16) |
                                (uint32_t(bytes[entry + 11]) << 24);
        set32(&bytes, entry + 8, offset + kGap);
    }
    return bytes;
}

class InflatedLengthStream final : public SkStream {
public:
    InflatedLengthStream(sk_sp<const SkData> data, size_t* maxRead)
            : fData(std::move(data)), fMaxRead(maxRead) {}

    size_t read(void* buffer, size_t size) override {
        *fMaxRead = std::max(*fMaxRead, size);
        if (fPosition >= fData->size()) {
            return 0;
        }
        size_t count = std::min(size, fData->size() - fPosition);
        if (buffer) {
            std::memcpy(buffer, static_cast<const uint8_t*>(fData->data()) + fPosition, count);
        }
        fPosition += count;
        return count;
    }

    bool isAtEnd() const override { return fPosition >= fData->size(); }
    bool hasLength() const override { return true; }
    size_t getLength() const override { return std::numeric_limits<size_t>::max() / 2; }
    bool hasPosition() const override { return true; }
    size_t getPosition() const override { return fPosition; }
    bool seek(size_t position) override {
        if (position != 0 && position != this->getLength()) {
            return false;
        }
        fPosition = position;
        return true;
    }
    bool rewind() override { return this->seek(0); }

private:
    sk_sp<const SkData> fData;
    size_t* fMaxRead;
    size_t fPosition = 0;
};

class RawForwardOnlyStream final : public SkStream {
public:
    explicit RawForwardOnlyStream(sk_sp<const SkData> data) : fData(std::move(data)) {}

    size_t read(void* buffer, size_t size) override {
        const size_t count = std::min({size, size_t(7), fData->size() - fPosition});
        if (buffer && count) {
            std::memcpy(buffer, static_cast<const uint8_t*>(fData->data()) + fPosition, count);
        }
        fPosition += count;
        return count;
    }

    bool isAtEnd() const override { return fPosition == fData->size(); }

private:
    sk_sp<const SkData> fData;
    size_t fPosition = 0;
};

class RawExtendedForwardOnlyStream final : public SkStream {
public:
    RawExtendedForwardOnlyStream(sk_sp<const SkData> prefix, size_t size)
            : fPrefix(std::move(prefix)), fSize(size) {}

    size_t read(void* buffer, size_t size) override {
        const size_t count = std::min(size, fSize - fPosition);
        if (buffer && count) {
            auto* dst = static_cast<uint8_t*>(buffer);
            const size_t prefixCount = fPosition < fPrefix->size()
                    ? std::min(count, fPrefix->size() - fPosition)
                    : 0;
            if (prefixCount) {
                std::memcpy(dst, static_cast<const uint8_t*>(fPrefix->data()) + fPosition,
                            prefixCount);
            }
            std::memset(dst + prefixCount, 0, count - prefixCount);
        }
        fPosition += count;
        return count;
    }

    bool isAtEnd() const override { return fPosition == fSize; }

private:
    sk_sp<const SkData> fPrefix;
    size_t fSize;
    size_t fPosition = 0;
};

void assert_rows(skiatest::Reporter* r, SkCodec* codec, uint8_t lastSample) {
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    const size_t stride = 3 * 4 + 8;
    uint8_t pixels[2 * stride];
    for (int repeat = 0; repeat < 2; ++repeat) {
        std::memset(pixels, 0xa5, sizeof(pixels));
        REPORTER_ASSERT(r, codec->getPixels(info, pixels, stride) == SkCodec::kSuccess);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 3; ++x) {
                const uint8_t expected = (x == 2 && y == 1) ? lastSample
                                               : ((x + y) & 1) ? 255 : 0;
                const uint8_t* pixel = pixels + y * stride + 4 * x;
                REPORTER_ASSERT(r, pixel[0] == expected && pixel[1] == expected &&
                                   pixel[2] == expected && pixel[3] == 255);
            }
            for (size_t i = 12; i < stride; ++i) {
                REPORTER_ASSERT(r, pixels[y * stride + i] == 0xa5);
            }
        }
    }
}

uint8_t expected_srgb(uint8_t sample) {
    const double linear = static_cast<double>(sample) / 255.0;
    const double encoded = linear <= 0.0031308
            ? 12.92 * linear
            : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return static_cast<uint8_t>(std::floor(encoded * 255.0 + 0.5));
}

}  // namespace

DEF_TEST(RustRaw_OutputMonoSrgb, r) {
    auto bytes = make_output_mono_ramp(8);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(SkMemoryStream::Make(data), &result);
    REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
    if (!codec) {
        return;
    }
    data.reset();
    REPORTER_ASSERT(r, codec->dimensions() == SkISize::Make(256, 1));
    REPORTER_ASSERT(r, codec->getEncodedFormat() == SkEncodedImageFormat::kDNG);
    REPORTER_ASSERT(r, codec->getInfo().colorType() == kN32_SkColorType);
    REPORTER_ASSERT(r, codec->getInfo().alphaType() == kOpaque_SkAlphaType);
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    constexpr size_t kActiveBytes = 256 * 4;
    constexpr size_t kStride = kActiveBytes + 12;
    for (int repeat = 0; repeat < 2; ++repeat) {
        std::array<uint8_t, kStride> pixels;
        pixels.fill(0xa5);
        REPORTER_ASSERT(r, codec->getPixels(info, pixels.data(), kStride) == SkCodec::kSuccess);
        for (size_t x = 0; x < 256; ++x) {
            const uint8_t expected = expected_srgb(static_cast<uint8_t>(x));
            REPORTER_ASSERT(r, pixels[4 * x] == expected &&
                               pixels[4 * x + 1] == expected &&
                               pixels[4 * x + 2] == expected &&
                               pixels[4 * x + 3] == 255,
                            "output-referred sample %zu", x);
        }
        for (size_t i = kActiveBytes; i < kStride; ++i) {
            REPORTER_ASSERT(r, pixels[i] == 0xa5);
        }
    }
}

DEF_TEST(RustRaw_LinearizedMono8SrgbFinal, r) {
    for (bool shortTable : {false, true}) {
        auto bytes = with_linearization_table(
                make_output_mono_ramp(8), false, shortTable, UINT8_MAX);
        REPORTER_ASSERT(r, !bytes.empty());
        if (bytes.empty()) {
            continue;
        }
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Success);
        std::array<uint16_t, 258> raw, second, third;
        raw.fill(0xa5a5);
        second.fill(0xa5a5);
        third.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage1_row(
                0, rust::Slice<uint16_t>(raw.data(), 256)) == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->read_normalized_row(
                0, rust::Slice<uint16_t>(second.data(), 256)) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->read_stage3_row(
                0, rust::Slice<uint16_t>(third.data(), 256)) ==
                rust_raw::DecodeStatus::Success);
        for (uint32_t x = 0; x < 256; ++x) {
            const uint8_t mapped = shortTable ?
                    (x == 0 ? 0 : 255) :
                    static_cast<uint8_t>(std::min(x * 2, 255u));
            REPORTER_ASSERT(r, raw[x] == x &&
                               second[x] == uint16_t(mapped) * 257 &&
                               third[x] == second[x]);
        }
        REPORTER_ASSERT(r, raw[256] == 0xa5a5 && second[257] == 0xa5a5 &&
                           third[256] == 0xa5a5);
        stream.reset();
        data.reset();
        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess);
        if (!codec) {
            continue;
        }
        const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
        constexpr size_t kActive = 256 * 4;
        constexpr size_t kStride = kActive + 12;
        std::array<uint8_t, kStride> pixels;
        for (int repeat = 0; repeat < 2; ++repeat) {
            pixels.fill(0xa5);
            REPORTER_ASSERT(r, codec->getPixels(info, pixels.data(), kStride) ==
                                SkCodec::kSuccess);
            for (size_t x = 0; x < 256; ++x) {
                const uint8_t mapped = shortTable ?
                        (x == 0 ? 0 : 255) :
                        static_cast<uint8_t>(std::min(x * 2, size_t(255)));
                const uint8_t expected = expected_srgb(mapped);
                REPORTER_ASSERT(r, pixels[4 * x] == expected &&
                                   pixels[4 * x + 1] == expected &&
                                   pixels[4 * x + 2] == expected &&
                                   pixels[4 * x + 3] == 255);
            }
            for (size_t i = kActive; i < kStride; ++i) {
                REPORTER_ASSERT(r, pixels[i] == 0xa5);
            }
        }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
        SkCodec::Result referenceResult = SkCodec::kInternalError;
        auto reference = SkRawDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
        REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
        if (reference) {
            std::array<uint8_t, kStride> expected;
            expected.fill(0xa5);
            REPORTER_ASSERT(r, reference->getPixels(info, expected.data(), kStride) ==
                                SkCodec::kSuccess);
            REPORTER_ASSERT(r, pixels == expected);
        }
#endif
    }
}

DEF_TEST(RustRaw_LinearizedMono8Malformed, r) {
    const auto good = with_linearization_table(
            make_output_mono_ramp(8), false, false, UINT8_MAX);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t table = entry_for(good, 50712);
    REPORTER_ASSERT(r, table != good.size());
    if (table == good.size()) {
        return;
    }
    auto bytes = good;
    bytes[table + 2] = 4;
    set32(&bytes, table + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 4, 0);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = good;
    const size_t last = read32(bytes, table + 8) + 2 * UINT8_MAX;
    bytes[last] = 0;
    bytes[last + 1] = 1;
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 256> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_normalized_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawRustDecoder::Decode(data, &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_OutputMonoIgnoredTone, r) {
    auto bytes = make_output_mono_ramp(8, false, true);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(SkMemoryStream::Make(data), &result);
    REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
    if (!codec) {
        return;
    }
    data.reset();
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    constexpr size_t kActive = 256 * 4;
    constexpr size_t kStride = kActive + 8;
    std::array<uint8_t, kStride> ours;
    for (int repeat = 0; repeat < 2; ++repeat) {
        ours.fill(0xa5);
        REPORTER_ASSERT(r, codec->getPixels(info, ours.data(), kStride) == SkCodec::kSuccess);
        for (size_t x = 0; x < 256; ++x) {
            const uint8_t expected = expected_srgb(static_cast<uint8_t>(x));
            REPORTER_ASSERT(r, ours[4 * x] == expected &&
                               ours[4 * x + 1] == expected &&
                               ours[4 * x + 2] == expected &&
                               ours[4 * x + 3] == 255);
        }
        REPORTER_ASSERT(r, std::all_of(ours.begin() + kActive, ours.end(),
                                       [](uint8_t value) { return value == 0xa5; }));
    }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
    auto referenceData = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkCodec::Result referenceResult = SkCodec::kInternalError;
    auto reference = SkRawDecoder::Decode(SkMemoryStream::Make(referenceData),
                                           &referenceResult);
    REPORTER_ASSERT(r, referenceResult == SkCodec::kSuccess && reference);
    if (reference) {
        std::array<uint8_t, kStride> theirs;
        theirs.fill(0xa5);
        REPORTER_ASSERT(r, reference->getPixels(info, theirs.data(), kStride) ==
                            SkCodec::kSuccess);
        REPORTER_ASSERT(r, theirs == ours);
    }
#endif
    const size_t tone = entry_for(bytes, 50940);
    const size_t black = entry_for(bytes, 51110);
    REPORTER_ASSERT(r, tone != bytes.size() && black != bytes.size());
    if (tone != bytes.size() && black != bytes.size()) {
        auto changed = bytes;
        set32(&changed, read32(changed, tone + 8) + 8, 0x7fc00000);
        auto rejected = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(changed.data(), changed.size()), &result);
        REPORTER_ASSERT(r, !rejected && result == SkCodec::kInvalidInput);
        changed = bytes;
        set32(&changed, black + 8, 0);
        rejected = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(changed.data(), changed.size()), &result);
        REPORTER_ASSERT(r, !rejected && result == SkCodec::kUnimplemented);
    }
    bytes = make_output_mono_ramp(16, false, true);
    auto highPrecision = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, !highPrecision && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_OutputMonoUnsupported, r) {
    auto expect = [&](std::vector<uint8_t> bytes, SkCodec::Result expected) {
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, !codec && result == expected,
                        "unexpected output-referred result %d instead of %d", result, expected);
    };
    expect(make_dng(1, 34892, false, false, 128), SkCodec::kUnimplemented);
    expect(make_stage1_dng(false, 8, true), SkCodec::kUnimplemented);
    expect(make_output_mono_ramp(8, true), SkCodec::kInvalidInput);

    auto bytes = make_output_mono_ramp(8);
    const size_t colorimetric = entry_for(bytes, 50879);
    REPORTER_ASSERT(r, colorimetric != bytes.size());
    if (colorimetric == bytes.size()) {
        return;
    }
    set32(&bytes, colorimetric + 8, 2);  // Output-referred HDR is not this SDR subprofile.
    expect(bytes, SkCodec::kUnimplemented);
    set32(&bytes, colorimetric + 8, 3);
    expect(bytes, SkCodec::kUnimplemented);

    bytes = make_output_mono_ramp(8);
    bytes[colorimetric + 2] = 4;  // LONG instead of SHORT.
    expect(bytes, SkCodec::kInvalidInput);
    bytes = make_output_mono_ramp(8);
    set32(&bytes, colorimetric + 4, 2);  // Wrong element count.
    expect(bytes, SkCodec::kInvalidInput);

    bytes = make_output_mono_ramp(8);
    const size_t white = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, white != bytes.size());
    if (white != bytes.size()) {
        set32(&bytes, white + 8, 254);
        expect(bytes, SkCodec::kUnimplemented);
    }
    bytes = make_output_mono_ramp(8);
    const size_t black = entry_for(bytes, 50714);
    REPORTER_ASSERT(r, black != bytes.size());
    if (black != bytes.size()) {
        set32(&bytes, read32(bytes, black + 8), 1);
        expect(bytes, SkCodec::kUnimplemented);
    }

    bytes = make_output_mono_ramp(16);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    expect(std::move(bytes), SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_LinearMonochrome, r) {
    for (uint8_t lastSample : {uint8_t(0), uint8_t(255)}) {
        auto bytes = make_dng(1, 34892, false, false, lastSample);
        REPORTER_ASSERT(r, SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
        SkCodec::Result result = SkCodec::kInternalError;
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto codec = SkRawRustDecoder::Decode(SkMemoryStream::Make(data), &result);
        REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
        if (!codec) {
            continue;
        }
        data.reset();  // Reader/codec must not borrow the caller's SkData.
        REPORTER_ASSERT(r, codec->dimensions() == SkISize::Make(3, 2));
        REPORTER_ASSERT(r, codec->getEncodedFormat() == SkEncodedImageFormat::kDNG);
        REPORTER_ASSERT(r, codec->getInfo().alphaType() == kOpaque_SkAlphaType);
        REPORTER_ASSERT(r, codec->getInfo().colorType() == kN32_SkColorType);
        assert_rows(r, codec.get(), lastSample);
    }
}

DEF_TEST(RustRaw_ForwardOnlyStream, r) {
    auto bytes = make_dng();
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(std::make_unique<RawForwardOnlyStream>(data), &result);
    REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
    if (codec) {
        data.reset();
        assert_rows(r, codec.get(), 255);
    }

    bytes.pop_back();
    data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    codec = SkRawRustDecoder::Decode(std::make_unique<RawForwardOnlyStream>(data), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kIncompleteInput);

    bytes = make_deflate_mono_dng(false, true, false, true);
    data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = std::make_unique<RawForwardOnlyStream>(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    stream.reset();
    data.reset();
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    std::array<uint16_t, 3> row{};
    REPORTER_ASSERT(r, reader->read_stage1_row(1, rust::Slice<uint16_t>(row.data(), row.size())) ==
                            rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, row[0] == 2048 && row[1] == 32768 && row[2] == 60000);
    REPORTER_ASSERT(r, reader->read_stage3_row(1, rust::Slice<uint16_t>(row.data(), row.size())) ==
                            rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, row[0] == 0 && row[1] == 34740 && row[2] == 65535);
}

#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
DEF_TEST(RustRaw_AdobeParity, r) {
    for (uint8_t lastSample : {uint8_t(0), uint8_t(255)}) {
        auto bytes = make_dng(1, 34892, false, false, lastSample);
        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
        if (!codec) {
            continue;
        }
        // Fixed valid fixtures can safely use both factories in one process;
        // general corpus differential tests run in separate processes.
        SkCodec::Result referenceResult = SkCodec::kInternalError;
        auto reference = SkRawDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
        REPORTER_ASSERT(r, referenceResult == SkCodec::kSuccess && reference);
        if (!reference) {
            continue;
        }
        REPORTER_ASSERT(r, reference->dimensions() == codec->dimensions());
        REPORTER_ASSERT(r, reference->getEncodedFormat() == codec->getEncodedFormat());
        REPORTER_ASSERT(r, reference->getInfo().alphaType() == codec->getInfo().alphaType());
        const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
        uint8_t ours[24] = {};
        uint8_t theirs[24] = {};
        REPORTER_ASSERT(r, codec->getPixels(info, ours, 12) == SkCodec::kSuccess);
        REPORTER_ASSERT(r, reference->getPixels(info, theirs, 12) == SkCodec::kSuccess);
        REPORTER_ASSERT(r, std::memcmp(ours, theirs, sizeof(ours)) == 0);
    }
    auto ramp = make_output_mono_ramp(8);
    SkCodec::Result rustResult = SkCodec::kInternalError;
    SkCodec::Result adobeResult = SkCodec::kInternalError;
    auto candidate = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(ramp.data(), ramp.size()), &rustResult);
    auto reference = SkRawDecoder::Decode(
            SkData::MakeWithCopy(ramp.data(), ramp.size()), &adobeResult);
    REPORTER_ASSERT(r, rustResult == SkCodec::kSuccess && candidate);
    REPORTER_ASSERT(r, adobeResult == SkCodec::kSuccess && reference);
    if (candidate && reference) {
        REPORTER_ASSERT(r, candidate->dimensions() == reference->dimensions());
        REPORTER_ASSERT(r, candidate->getEncodedFormat() == reference->getEncodedFormat());
        REPORTER_ASSERT(r, candidate->getInfo().alphaType() == reference->getInfo().alphaType());
        const SkImageInfo info = candidate->getInfo().makeColorType(kRGBA_8888_SkColorType);
        std::array<uint8_t, 256 * 4> ours{};
        std::array<uint8_t, 256 * 4> theirs{};
        REPORTER_ASSERT(r, candidate->getPixels(info, ours.data(), ours.size()) ==
                           SkCodec::kSuccess);
        REPORTER_ASSERT(r, reference->getPixels(info, theirs.data(), theirs.size()) ==
                           SkCodec::kSuccess);
        REPORTER_ASSERT(r, ours == theirs);
    }
}
#endif

DEF_TEST(RustRaw_LateIFD, r) {
    auto bytes = make_late_ifd_dng();
    REPORTER_ASSERT(r, !bytes.empty());
    if (bytes.empty()) {
        return;
    }
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
    if (codec) {
        assert_rows(r, codec.get(), 255);
    }
}

DEF_TEST(RustRaw_AdvertisedLength, r) {
    auto bytes = make_dng();
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    size_t maxRead = 0;
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(
            std::make_unique<InflatedLengthStream>(data, &maxRead), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kIncompleteInput);
    REPORTER_ASSERT(r, maxRead > 0 && maxRead <= 16 * 1024);
}

DEF_TEST(RustRaw_InvalidTypes, r) {
    struct TypeCase {
        uint16_t tag;
        uint16_t kind;
    };
    constexpr TypeCase kCases[] = {
            {254, 3}, {256, 5}, {257, 5}, {258, 4}, {259, 4}, {262, 4},
            {273, 5}, {274, 4}, {277, 4}, {278, 5}, {279, 5}, {284, 4},
            {339, 4}, {50706, 4}, {50707, 4}, {50708, 1}, {50713, 4},
            {50714, 2}, {50717, 5}, {50718, 4}, {50719, 2}, {50720, 2},
            {50829, 5},
    };
    for (const auto& test : kCases) {
        auto bytes = make_dng();
        const size_t entry = entry_for(bytes, test.tag);
        REPORTER_ASSERT(r, entry != bytes.size(), "missing test tag %u", test.tag);
        if (entry == bytes.size()) {
            continue;
        }
        bytes[entry + 2] = test.kind & 0xff;
        bytes[entry + 3] = test.kind >> 8;
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kInvalidInput,
                        "tag %u type %u produced result %d", test.tag, test.kind, result);
    }
}

DEF_TEST(RustRaw_Stage1Strips, r) {
    for (uint16_t bits : {uint16_t(8), uint16_t(16)}) {
        for (bool bigEndian : {false, true}) {
            auto bytes = make_stage1_dng(bigEndian, bits);
            REPORTER_ASSERT(r, SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3);
            REPORTER_ASSERT(r, reader->bits_per_sample() == bits);
            const std::array<uint16_t, 9> expected = bits == 8
                    ? std::array<uint16_t, 9>{0, 10, 255, 20, 128, 230, 7, 160, 250}
                    : std::array<uint16_t, 9>{0, 1024, 65535, 2048, 32768, 60000,
                                              800, 50000, 12345};
            // Row 2 subtracts its local black (1024), but the DNG scale
            // denominator uses the plane's maximum repeating black (2048).
            const std::array<uint16_t, 9> normalized = bits == 8
                    ? std::array<uint16_t, 9>{0, 2570, 65535, 5140, 32896, 59110,
                                              1799, 41120, 64250}
                    : std::array<uint16_t, 9>{0, 0, 65535, 0, 34740, 65535,
                                              0, 55384, 12802};
            for (uint32_t y = 0; y < 3; ++y) {
                std::array<uint16_t, 5> samples = {0xffff, 0xffff, 0xffff, 0xa5a5, 0xa5a5};
                REPORTER_ASSERT(r, reader->read_stage1_row(
                        y, rust::Slice<uint16_t>(samples.data(), 3)) ==
                        rust_raw::DecodeStatus::Success);
                for (size_t x = 0; x < 3; ++x) {
                    REPORTER_ASSERT(r, samples[x] == expected[y * 3 + x]);
                }
                REPORTER_ASSERT(r, samples[3] == 0xa5a5 && samples[4] == 0xa5a5);
                REPORTER_ASSERT(r, reader->read_normalized_row(
                        y, rust::Slice<uint16_t>(samples.data(), 3)) ==
                        rust_raw::DecodeStatus::Success);
                for (size_t x = 0; x < 3; ++x) {
                    REPORTER_ASSERT(r, samples[x] == normalized[y * 3 + x]);
                }
                REPORTER_ASSERT(r, reader->read_stage3_row(
                        y, rust::Slice<uint16_t>(samples.data(), 3)) ==
                        rust_raw::DecodeStatus::Success);
                for (size_t x = 0; x < 3; ++x) {
                    REPORTER_ASSERT(r, samples[x] == normalized[y * 3 + x]);
                }
                REPORTER_ASSERT(r, samples[3] == 0xa5a5 && samples[4] == 0xa5a5);
            }
            std::array<uint16_t, 2> wrongSize = {};
            REPORTER_ASSERT(r, reader->read_stage1_row(
                    0, rust::Slice<uint16_t>(wrongSize.data(), wrongSize.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_DeflateMonoRows, r) {
    constexpr std::array<uint16_t, 9> kRaw =
            {0, 1024, 65535, 2048, 32768, 60000, 800, 50000, 12345};
    constexpr std::array<uint16_t, 9> kSdkStage2 =
            {0, 0, 65535, 0, 34740, 65535, 0, 55384, 12802};
    for (bool bigEndian : {false, true}) {
        for (bool severalStrips : {false, true}) {
            for (int profile = 0; profile < 4; ++profile) {
                const bool dng17 = (profile & 1) != 0;
                const bool horizontalPredictor = (profile & 2) != 0;
                auto bytes = make_deflate_mono_dng(
                        bigEndian, severalStrips, dng17, horizontalPredictor);
                REPORTER_ASSERT(r, !bytes.empty());
                if (bytes.empty()) {
                    continue;
                }
                auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                auto stream = SkMemoryStream::Make(data);
                auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                auto reader = rust_raw::new_reader(std::move(adapter));
                stream.reset();
                data.reset();
                REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                    continue;
                }
                REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                                   reader->bits_per_sample() == 16 && reader->channels() == 1 &&
                                   reader->stage3_channels() == 1 &&
                                   reader->main_ifd_index() == 0);
                for (int repeat = 0; repeat < 2; ++repeat) {
                    for (uint32_t row = 0; row < 3; ++row) {
                        std::array<uint16_t, 5> raw, stage2, stage3;
                        raw.fill(0xa5a5);
                        stage2.fill(0xa5a5);
                        stage3.fill(0xa5a5);
                        REPORTER_ASSERT(r, reader->read_stage1_row(
                                row, rust::Slice<uint16_t>(raw.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_normalized_row(
                                row, rust::Slice<uint16_t>(stage2.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage3_row(
                                row, rust::Slice<uint16_t>(stage3.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        for (int x = 0; x < 3; ++x) {
                            REPORTER_ASSERT(r, raw[x] == kRaw[row * 3 + x] &&
                                               stage2[x] == kSdkStage2[row * 3 + x] &&
                                               stage3[x] == stage2[x]);
                        }
                        REPORTER_ASSERT(r, raw[3] == 0xa5a5 && raw[4] == 0xa5a5 &&
                                           stage2[3] == 0xa5a5 && stage2[4] == 0xa5a5 &&
                                           stage3[3] == 0xa5a5 && stage3[4] == 0xa5a5);
                    }
                }
                std::array<uint16_t, 3> untouched;
                untouched.fill(0xa5a5);
                REPORTER_ASSERT(r, reader->read_stage1_row(
                        3, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                        rust_raw::DecodeStatus::Invalid);
                REPORTER_ASSERT(r, reader->read_normalized_row(
                        0, rust::Slice<uint16_t>(untouched.data(), 2)) ==
                        rust_raw::DecodeStatus::Invalid);
                REPORTER_ASSERT(r, reader->read_stage3_row(
                        0, rust::Slice<uint16_t>(untouched.data(), 2)) ==
                        rust_raw::DecodeStatus::Invalid);
                REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                        0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                        rust_raw::DecodeStatus::Unsupported);
                REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                               [](uint16_t value) { return value == 0xa5a5; }));
                SkCodec::Result result = SkCodec::kSuccess;
                auto codec = SkRawRustDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
            }
        }
    }
}

DEF_TEST(RustRaw_DeflateMonoMalformed, r) {
    const auto good = make_deflate_mono_dng(false, true);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t offsets = entry_for(good, 273);
    const size_t lengths = entry_for(good, 279);
    const size_t predictor = entry_for(good, 317);
    const size_t crop = entry_for(good, 50719);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size() &&
                       predictor != good.size() && crop != good.size());
    if (offsets == good.size() || lengths == good.size() ||
        predictor == good.size() || crop == good.size()) {
        return;
    }
    auto bytes = good;
    const size_t offsetsArray = read32(good, offsets + 8);
    const size_t lengthsArray = read32(good, lengths + 8);
    set32(&bytes, offsetsArray + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    set32(&bytes, offsets + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    bytes[lengths + 2] = 1;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    bytes[predictor + 8] = 3;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);

    const size_t lastOffset = read32(good, offsetsArray + 4);
    const size_t lastLength = read32(good, lengthsArray + 4);
    bytes = good;
    bytes[lastOffset + lastLength - 1] ^= 0xff;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, lengthsArray + 4, static_cast<uint32_t>(lastLength - 1));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    bytes.push_back(0);
    set32(&bytes, lengthsArray + 4, static_cast<uint32_t>(lastLength + 1));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    std::vector<uint8_t> bomb(compressBound(18));
    const std::array<uint8_t, 18> expanded{};
    uLongf bombSize = bomb.size();
    REPORTER_ASSERT(r, compress2(bomb.data(), &bombSize, expanded.data(),
                                 expanded.size(), Z_BEST_SPEED) == Z_OK);
    bomb.resize(bombSize);
    auto encoded = rust::Slice<const uint8_t>(bomb.data(), bomb.size());
    REPORTER_ASSERT(r, rust_raw::validate_dng_deflate_strip(encoded, expanded.size()) == 0);
    REPORTER_ASSERT(r, rust_raw::validate_dng_deflate_strip(encoded, 6) == 1);
    REPORTER_ASSERT(r, rust_raw::validate_dng_deflate_strip(encoded, 19) == 1);
    auto trailing = bomb;
    trailing.push_back(0);
    REPORTER_ASSERT(r, rust_raw::validate_dng_deflate_strip(
            rust::Slice<const uint8_t>(trailing.data(), trailing.size()),
            expanded.size()) == 1);
    REPORTER_ASSERT(r, rust_raw::validate_dng_deflate_strip(
            rust::Slice<const uint8_t>(bomb.data(), bomb.size() - 1),
            expanded.size()) == 3);
    set32(&bytes, offsetsArray, static_cast<uint32_t>(bytes.size()));
    set32(&bytes, lengthsArray, static_cast<uint32_t>(bomb.size()));
    bytes.insert(bytes.end(), bomb.begin(), bomb.end());
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = good;
    set32(&bytes, read32(good, crop + 8), 1);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 3> sentinel;
    sentinel.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_normalized_row(
            0, rust::Slice<uint16_t>(sentinel.data(), sentinel.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(sentinel.begin(), sentinel.end(),
                                   [](uint16_t value) { return value == 0xa5a5; }));
}

DEF_TEST(RustRaw_Rgb16Stage1, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_rgb16_dng(bigEndian, multipleStrips);
            REPORTER_ASSERT(r, SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                               reader->bits_per_sample() == 16 && reader->channels() == 3 &&
                               reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 3; ++y) {
                    std::array<uint16_t, 11> samples;
                    samples.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                            y, rust::Slice<uint16_t>(samples.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, samples[x] == kRgb16Samples[y * 9 + x]);
                    }
                    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                            y, rust::Slice<uint16_t>(samples.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, samples[x] == kRgb16Samples[y * 9 + x]);
                    }
                    REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                            y, rust::Slice<uint16_t>(samples.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, samples[x] == kRgb16Samples[y * 9 + x]);
                    }
                    REPORTER_ASSERT(r, samples[9] == 0xa5a5 && samples[10] == 0xa5a5);
                }
            }
            std::array<uint16_t, 8> wrong;
            wrong.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](uint16_t sample) { return sample == 0xa5a5; }));
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](uint16_t sample) { return sample == 0xa5a5; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
            SkCodec::Result referenceResult = SkCodec::kInternalError;
            auto reference = SkRawDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
            REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
            if (reference) {
                REPORTER_ASSERT(r, reference->dimensions() == SkISize::Make(3, 3));
                REPORTER_ASSERT(r, reference->getEncodedFormat() == SkEncodedImageFormat::kDNG);
            }
#endif
        }
    }
}

DEF_TEST(RustRaw_DeflateRgb16Rows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool horizontalPredictor : {false, true}) {
                auto bytes = make_deflate_rgb16_dng(
                        bigEndian, multipleStrips, horizontalPredictor);
                REPORTER_ASSERT(r, !bytes.empty());
                if (bytes.empty()) {
                    continue;
                }
                auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                auto stream = SkMemoryStream::Make(data);
                auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                auto reader = rust_raw::new_reader(std::move(adapter));
                stream.reset();
                data.reset();
                REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                    continue;
                }
                REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                                   reader->bits_per_sample() == 16 && reader->channels() == 3 &&
                                   reader->main_ifd_index() == 0);
                for (int repeat = 0; repeat < 2; ++repeat) {
                    for (uint32_t y = 0; y < 3; ++y) {
                        std::array<uint16_t, 11> first, second, third;
                        first.fill(0xa5a5);
                        second.fill(0xa5a5);
                        third.fill(0xa5a5);
                        REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                                y, rust::Slice<uint16_t>(first.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                                y, rust::Slice<uint16_t>(second.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                                y, rust::Slice<uint16_t>(third.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        for (size_t x = 0; x < 9; ++x) {
                            REPORTER_ASSERT(r, first[x] == kRgb16Samples[y * 9 + x] &&
                                               second[x] == first[x] && third[x] == first[x]);
                        }
                        REPORTER_ASSERT(r, first[9] == 0xa5a5 && second[10] == 0xa5a5 &&
                                           third[9] == 0xa5a5);
                    }
                }
                SkCodec::Result result = SkCodec::kSuccess;
                auto codec = SkRawRustDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                SkCodec::Result referenceResult = SkCodec::kInternalError;
                auto reference = SkRawDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
            }
        }
    }
}

DEF_TEST(RustRaw_DeflateRgb16Malformed, r) {
    const auto good = make_deflate_rgb16_dng(false, true, true);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t offsets = entry_for(good, 273);
    const size_t lengths = entry_for(good, 279);
    const size_t predictor = entry_for(good, 317);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size() &&
                       predictor != good.size());
    if (offsets == good.size() || lengths == good.size() || predictor == good.size()) {
        return;
    }
    const size_t offsetTable = read32(good, offsets + 8);
    const size_t lengthTable = read32(good, lengths + 8);
    auto bytes = good;
    set32(&bytes, offsetTable + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    bytes[predictor + 8] = 3;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    bytes = good;
    bytes[predictor + 2] = 4;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    const size_t lastOffset = read32(good, offsetTable + 4);
    const size_t lastLength = read32(good, lengthTable + 4);
    bytes = good;
    bytes[lastOffset + lastLength - 1] ^= 0xff;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(lastLength - 1));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    std::array<uint8_t, 19> expanded{};
    std::vector<uint8_t> overlong(compressBound(expanded.size()));
    uLongf compressedSize = overlong.size();
    REPORTER_ASSERT(r, compress2(overlong.data(), &compressedSize, expanded.data(),
                                 expanded.size(), Z_BEST_SPEED) == Z_OK);
    overlong.resize(compressedSize);
    set32(&bytes, offsetTable + 4, static_cast<uint32_t>(bytes.size()));
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(overlong.size()));
    bytes.insert(bytes.end(), overlong.begin(), overlong.end());
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    std::array<uint16_t, 9> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Invalid);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
}

DEF_TEST(RustRaw_LinearizedRgb16Rows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool compressed : {false, true}) {
                for (bool shortTable : {false, true}) {
                    auto bytes = make_linearized_rgb16_dng(
                            bigEndian, multipleStrips, compressed, shortTable);
                    REPORTER_ASSERT(r, !bytes.empty());
                    if (bytes.empty()) {
                        continue;
                    }
                    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                    auto stream = SkMemoryStream::Make(data);
                    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                    auto reader = rust_raw::new_reader(std::move(adapter));
                    stream.reset();
                    data.reset();
                    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                        continue;
                    }
                    REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                                       reader->channels() == 3 && reader->bits_per_sample() == 16 &&
                                       reader->main_ifd_index() == 0);
                    for (uint32_t y = 0; y < 3; ++y) {
                        std::array<uint16_t, 11> first, second, third;
                        first.fill(0xa5a5);
                        second.fill(0xa5a5);
                        third.fill(0xa5a5);
                        REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                                y, rust::Slice<uint16_t>(first.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                                y, rust::Slice<uint16_t>(second.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                                y, rust::Slice<uint16_t>(third.data(), 9)) ==
                                rust_raw::DecodeStatus::Success);
                        for (size_t i = 0; i < 9; ++i) {
                            const uint16_t expected = shortTable ?
                                    (first[i] == 0 ? 0 : UINT16_MAX) :
                                    static_cast<uint16_t>(std::min(
                                            uint32_t(first[i]) * 2, uint32_t(UINT16_MAX)));
                            REPORTER_ASSERT(r, first[i] == kRgb16Samples[y * 9 + i] &&
                                               second[i] == expected && third[i] == expected);
                        }
                        REPORTER_ASSERT(r, first[9] == 0xa5a5 && second[10] == 0xa5a5 &&
                                           third[9] == 0xa5a5);
                    }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                    SkCodec::Result referenceResult = SkCodec::kInternalError;
                    auto reference = SkRawDecoder::Decode(
                            SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                    REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
                }
            }
        }
    }
}

DEF_TEST(RustRaw_LinearizedRgb16Malformed, r) {
    const auto good = make_linearized_rgb16_dng(false, true, true, false);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t table = entry_for(good, 50712);
    REPORTER_ASSERT(r, table != good.size());
    if (table == good.size()) {
        return;
    }
    auto bytes = good;
    bytes[table + 2] = 4;
    set32(&bytes, table + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 4, 0);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = good;
    const size_t last = read32(bytes, table + 8) + 2 * UINT16_MAX;
    bytes[last] = bytes[last + 1] = 0;
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 9> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
}

DEF_TEST(RustRaw_LinearizedMono16Rows, r) {
    constexpr std::array<uint16_t, 9> raw =
            {0, 1024, 65535, 2048, 32768, 60000, 800, 50000, 12345};
    for (bool bigEndian : {false, true}) {
        for (bool compressed : {false, true}) {
            for (bool multipleStrips : {false, true}) {
                if (!compressed && multipleStrips) {
                    continue;
                }
                for (bool shortTable : {false, true}) {
                    auto bytes = make_linearized_mono16_dng(
                            bigEndian, multipleStrips, compressed, shortTable);
                    REPORTER_ASSERT(r, !bytes.empty());
                    if (bytes.empty()) {
                        continue;
                    }
                    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                    auto stream = SkMemoryStream::Make(data);
                    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                    auto reader = rust_raw::new_reader(std::move(adapter));
                    stream.reset();
                    data.reset();
                    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                        continue;
                    }
                    REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                                       reader->channels() == 1 && reader->bits_per_sample() == 16);
                    for (uint32_t y = 0; y < 3; ++y) {
                        std::array<uint16_t, 5> first, second, third;
                        first.fill(0xa5a5);
                        second.fill(0xa5a5);
                        third.fill(0xa5a5);
                        REPORTER_ASSERT(r, reader->read_stage1_row(
                                y, rust::Slice<uint16_t>(first.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_normalized_row(
                                y, rust::Slice<uint16_t>(second.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage3_row(
                                y, rust::Slice<uint16_t>(third.data(), 3)) ==
                                rust_raw::DecodeStatus::Success);
                        for (size_t x = 0; x < 3; ++x) {
                            const uint16_t expected = shortTable ?
                                    (first[x] == 0 ? 0 : UINT16_MAX) :
                                    static_cast<uint16_t>(std::min(
                                            uint32_t(first[x]) * 2, uint32_t(UINT16_MAX)));
                            REPORTER_ASSERT(r, first[x] == raw[y * 3 + x] &&
                                               second[x] == expected && third[x] == expected);
                        }
                        REPORTER_ASSERT(r, first[3] == 0xa5a5 && second[4] == 0xa5a5 &&
                                           third[3] == 0xa5a5);
                    }
                    SkCodec::Result result = SkCodec::kSuccess;
                    auto codec = SkRawRustDecoder::Decode(
                            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                    SkCodec::Result referenceResult = SkCodec::kInternalError;
                    auto reference = SkRawDecoder::Decode(
                            SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                    REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
                }
            }
        }
    }
}

DEF_TEST(RustRaw_LinearizedMono16Malformed, r) {
    for (bool compressed : {false, true}) {
        const auto good = make_linearized_mono16_dng(false, true, compressed, false);
        REPORTER_ASSERT(r, !good.empty());
        if (good.empty()) {
            continue;
        }
        const size_t table = entry_for(good, 50712);
        REPORTER_ASSERT(r, table != good.size());
        if (table == good.size()) {
            continue;
        }
        auto bytes = good;
        bytes[table + 2] = 4;
        set32(&bytes, table + 4, 1);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
        bytes = good;
        set32(&bytes, table + 4, 0);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
        bytes = good;
        set32(&bytes, table + 8, UINT32_MAX);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

        bytes = good;
        const size_t last = read32(bytes, table + 8) + 2 * UINT16_MAX;
        bytes[last] = bytes[last + 1] = 0;
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        std::array<uint16_t, 3> untouched;
        untouched.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_normalized_row(
                0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                       [](uint16_t sample) { return sample == 0xa5a5; }));
    }
}

DEF_TEST(RustRaw_Rgb16MalformedStrips, r) {
    auto bytes = make_rgb16_dng(false, true);
    bytes.pop_back();
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_rgb16_dng(false, true);
    const size_t offsetsTag = entry_for(bytes, 273);
    const size_t sizesTag = entry_for(bytes, 279);
    REPORTER_ASSERT(r, offsetsTag != bytes.size() && sizesTag != bytes.size());
    if (offsetsTag == bytes.size() || sizesTag == bytes.size()) {
        return;
    }
    const size_t offsets = read32(bytes, offsetsTag + 8);
    set32(&bytes, offsets + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_rgb16_dng(false, true);
    const size_t counts = read32(bytes, sizesTag + 8);
    set32(&bytes, counts + 4, 17);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_rgb16_dng(false, true);
    set32(&bytes, offsetsTag + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_rgb16_dng(false, true);
    const size_t rows = entry_for(bytes, 278);
    REPORTER_ASSERT(r, rows != bytes.size());
    if (rows != bytes.size()) {
        set32(&bytes, rows + 8, 0);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    }

    bytes = make_rgb16_dng(false, true);
    const size_t compression = entry_for(bytes, 259);
    REPORTER_ASSERT(r, compression != bytes.size());
    if (compression != bytes.size()) {
        bytes[compression + 8] = 7;
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
        bytes = make_rgb16_dng(false, true);
        bytes[compression + 2] = 4; // LONG instead of SHORT.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    }

    bytes = make_rgb16_dng(false, true);
    const size_t version = entry_for(bytes, 50706);
    REPORTER_ASSERT(r, version != bytes.size());
    if (version != bytes.size()) {
        bytes[version + 9] = 4;
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }
}

DEF_TEST(RustRaw_Rgb16ProcessingGuards, r) {
    auto check = [&](std::vector<uint8_t> bytes,
                     rust_raw::DecodeStatus expectedStage2,
                     rust_raw::DecodeStatus expectedStage3) {
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == expectedStage2);
        REPORTER_ASSERT(r, reader->stage3_status() == expectedStage3);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint16_t, 9> raw{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                0, rust::Slice<uint16_t>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        for (size_t x = 0; x < raw.size(); ++x) {
            REPORTER_ASSERT(r, raw[x] == kRgb16Samples[x]);
        }
        std::array<uint16_t, 9> output;
        output.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                0, rust::Slice<uint16_t>(output.data(), output.size())) == expectedStage2);
        if (expectedStage2 == rust_raw::DecodeStatus::Success) {
            REPORTER_ASSERT(r, output == raw);
            output.fill(0xa5a5);
        } else {
            REPORTER_ASSERT(r, std::all_of(output.begin(), output.end(),
                                           [](uint16_t sample) { return sample == 0xa5a5; }));
        }
        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                0, rust::Slice<uint16_t>(output.data(), output.size())) == expectedStage3);
        if (expectedStage3 == rust_raw::DecodeStatus::Success) {
            REPORTER_ASSERT(r, output == raw);
        } else {
            REPORTER_ASSERT(r, std::all_of(output.begin(), output.end(),
                                           [](uint16_t sample) { return sample == 0xa5a5; }));
        }
    };

    auto bytes = make_rgb16_dng(false, true);
    const size_t white = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, white != bytes.size());
    if (white != bytes.size()) {
        const size_t values = read32(bytes, white + 8);
        bytes[values] = 0xfe;
        check(bytes, rust_raw::DecodeStatus::Unsupported,
              rust_raw::DecodeStatus::Unsupported);
    }

    bytes = make_rgb16_dng(false, true);
    const size_t crop = entry_for(bytes, 50720);
    REPORTER_ASSERT(r, crop != bytes.size());
    if (crop != bytes.size()) {
        set32(&bytes, read32(bytes, crop + 8), 2);
        check(bytes, rust_raw::DecodeStatus::Unsupported,
              rust_raw::DecodeStatus::Unsupported);
    }

    bytes = make_rgb16_dng(false, true);
    const size_t antiAlias = entry_for(bytes, 50738);
    REPORTER_ASSERT(r, antiAlias != bytes.size());
    if (antiAlias != bytes.size()) {
        set32(&bytes, read32(bytes, antiAlias + 8), 2);
        check(bytes, rust_raw::DecodeStatus::Unsupported,
              rust_raw::DecodeStatus::Unsupported);
    }

    for (uint16_t tag : {uint16_t(50712), uint16_t(50713), uint16_t(33421),
                         uint16_t(51009), uint16_t(51022)}) {
        bytes = make_rgb16_dng(false, true);
        const size_t replaced = entry_for(bytes, 50780);
        REPORTER_ASSERT(r, replaced != bytes.size());
        if (replaced == bytes.size()) {
            continue;
        }
        bytes[replaced] = tag & 0xff;
        bytes[replaced + 1] = tag >> 8;
        bytes[replaced + 2] = tag == 51009 || tag == 51022 ? 7 : 3;
        set32(&bytes, replaced + 4, tag == 51009 || tag == 51022 ? 4 : 2);
        if (tag != 51009 && tag != 51022) {
            bytes[replaced + 8] = 1;
            bytes[replaced + 9] = 0;
            bytes[replaced + 10] = 1;
            bytes[replaced + 11] = 0;
        }
        check(bytes, tag == 51022 || tag == 50713 ? rust_raw::DecodeStatus::Success :
                               rust_raw::DecodeStatus::Unsupported,
              tag == 50713 ? rust_raw::DecodeStatus::Success :
                             rust_raw::DecodeStatus::Unsupported);
    }

    bytes = make_rgb16_dng(false, true);
    const size_t unrecognized = entry_for(bytes, 50780);
    REPORTER_ASSERT(r, unrecognized != bytes.size());
    if (unrecognized != bytes.size()) {
        bytes[unrecognized] = 0xe8;
        bytes[unrecognized + 1] = 0xfd; // Unrecognized processing tag 65000.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }
}

DEF_TEST(RustRaw_Rgb16Subifd, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_preview_rgb16_dng(bigEndian, multipleStrips);
            REPORTER_ASSERT(r, !bytes.empty());
            if (bytes.empty()) {
                continue;
            }
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                               reader->bits_per_sample() == 16 && reader->channels() == 3 &&
                               reader->main_ifd_index() == 1);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 3; ++y) {
                    std::array<uint16_t, 11> raw, second, third;
                    raw.fill(0xa5a5);
                    second.fill(0xa5a5);
                    third.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                            y, rust::Slice<uint16_t>(raw.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                            y, rust::Slice<uint16_t>(second.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                            y, rust::Slice<uint16_t>(third.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, raw[x] == kRgb16Samples[y * 9 + x] &&
                                           second[x] == raw[x] && third[x] == raw[x]);
                    }
                    REPORTER_ASSERT(r, raw[9] == 0xa5a5 && second[10] == 0xa5a5 &&
                                       third[9] == 0xa5a5);
                }
            }
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(data, &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_DeflateRgb16SubifdRows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool horizontalPredictor : {false, true}) {
                auto bytes = make_preview_rgb16_dng(
                        bigEndian, multipleStrips, true, horizontalPredictor);
                REPORTER_ASSERT(r, !bytes.empty());
                if (bytes.empty()) {
                    continue;
                }
                auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                auto stream = SkMemoryStream::Make(data);
                auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                auto reader = rust_raw::new_reader(std::move(adapter));
                stream.reset();
                data.reset();
                REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                    continue;
                }
                REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                                   reader->bits_per_sample() == 16 && reader->channels() == 3 &&
                                   reader->main_ifd_index() == 1);
                for (uint32_t y = 0; y < 3; ++y) {
                    std::array<uint16_t, 11> first, second, third;
                    first.fill(0xa5a5);
                    second.fill(0xa5a5);
                    third.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                            y, rust::Slice<uint16_t>(first.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                            y, rust::Slice<uint16_t>(second.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                            y, rust::Slice<uint16_t>(third.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, first[x] == kRgb16Samples[y * 9 + x] &&
                                           second[x] == first[x] && third[x] == first[x]);
                    }
                    REPORTER_ASSERT(r, first[9] == 0xa5a5 && second[10] == 0xa5a5 &&
                                       third[9] == 0xa5a5);
                }
                SkCodec::Result result = SkCodec::kSuccess;
                auto codec = SkRawRustDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                SkCodec::Result referenceResult = SkCodec::kInternalError;
                auto reference = SkRawDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
            }
        }
    }
}

DEF_TEST(RustRaw_DeflateRgb16SubifdMalformed, r) {
    const auto good = make_preview_rgb16_dng(false, true, true, true);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t root = read32(good, 4);
    const size_t subifd = entry_in_ifd(good, root, 330);
    REPORTER_ASSERT(r, subifd != good.size());
    if (subifd == good.size()) {
        return;
    }
    const size_t child = read32(good, subifd + 8);
    const size_t offsets = entry_in_ifd(good, child, 273);
    const size_t lengths = entry_in_ifd(good, child, 279);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size());
    if (offsets == good.size() || lengths == good.size()) {
        return;
    }
    const size_t offsetTable = read32(good, offsets + 8);
    const size_t lengthTable = read32(good, lengths + 8);
    auto bytes = good;
    set32(&bytes, subifd + 8, static_cast<uint32_t>(root));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, offsetTable + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    const size_t lastOffset = read32(good, offsetTable + 4);
    const size_t lastLength = read32(good, lengthTable + 4);
    bytes = good;
    bytes[lastOffset + lastLength - 1] ^= 0xff;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    std::array<uint8_t, 19> expanded{};
    std::vector<uint8_t> overlong(compressBound(expanded.size()));
    uLongf compressedSize = overlong.size();
    REPORTER_ASSERT(r, compress2(overlong.data(), &compressedSize, expanded.data(),
                                 expanded.size(), Z_BEST_SPEED) == Z_OK);
    overlong.resize(compressedSize);
    set32(&bytes, offsetTable + 4, static_cast<uint32_t>(bytes.size()));
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(overlong.size()));
    bytes.insert(bytes.end(), overlong.begin(), overlong.end());
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    std::array<uint16_t, 9> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Invalid);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
}

DEF_TEST(RustRaw_Rgb16SubifdInvalid, r) {
    auto bytes = make_preview_rgb16_dng(false, true);
    REPORTER_ASSERT(r, !bytes.empty());
    if (bytes.empty()) {
        return;
    }
    const size_t root = read32(bytes, 4);
    const size_t subifds = entry_in_ifd(bytes, root, 330);
    REPORTER_ASSERT(r, subifds != bytes.size());
    if (subifds == bytes.size()) {
        return;
    }
    auto invalid = bytes;
    set32(&invalid, subifds + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Incomplete);
    invalid = bytes;
    set32(&invalid, subifds + 8, static_cast<uint32_t>(root));
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Invalid);
    invalid = bytes;
    invalid[subifds + 2] = 5; // RATIONAL instead of LONG.
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Invalid);

    const size_t child = read32(bytes, subifds + 8);
    const size_t stripOffsets = entry_in_ifd(bytes, child, 273);
    REPORTER_ASSERT(r, stripOffsets != bytes.size());
    if (stripOffsets != bytes.size()) {
        invalid = bytes;
        set32(&invalid, read32(bytes, stripOffsets + 8) + 4, UINT32_MAX);
        REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Incomplete);
    }

    auto checkProcessing = [&](std::vector<uint8_t> modified,
                               rust_raw::DecodeStatus secondStatus,
                               rust_raw::DecodeStatus thirdStatus) {
        auto data = SkData::MakeWithCopy(modified.data(), modified.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == secondStatus);
        REPORTER_ASSERT(r, reader->stage3_status() == thirdStatus);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint16_t, 9> raw{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb16_row(
                0, rust::Slice<uint16_t>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        for (size_t x = 0; x < raw.size(); ++x) {
            REPORTER_ASSERT(r, raw[x] == kRgb16Samples[x]);
        }
        std::array<uint16_t, 9> row;
        row.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                0, rust::Slice<uint16_t>(row.data(), row.size())) == secondStatus);
        if (secondStatus == rust_raw::DecodeStatus::Success) {
            REPORTER_ASSERT(r, row == raw);
            row.fill(0xa5a5);
        }
        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                0, rust::Slice<uint16_t>(row.data(), row.size())) == thirdStatus);
        REPORTER_ASSERT(r, std::all_of(row.begin(), row.end(),
                                       [](uint16_t sample) { return sample == 0xa5a5; }));
    };
    for (uint16_t tag : {uint16_t(51009), uint16_t(51022)}) {
        invalid = bytes;
        const size_t factor = entry_in_ifd(invalid, root, 50734);
        REPORTER_ASSERT(r, factor != invalid.size());
        if (factor == invalid.size()) {
            continue;
        }
        invalid[factor] = tag & 0xff;
        invalid[factor + 1] = tag >> 8;
        invalid[factor + 2] = 7;
        set32(&invalid, factor + 4, 4);
        checkProcessing(invalid,
                        tag == 51009 ? rust_raw::DecodeStatus::Unsupported :
                                       rust_raw::DecodeStatus::Success,
                        rust_raw::DecodeStatus::Unsupported);
    }

    invalid = bytes;
    const size_t crop = entry_in_ifd(invalid, child, 50720);
    REPORTER_ASSERT(r, crop != invalid.size());
    if (crop != invalid.size()) {
        set32(&invalid, read32(invalid, crop + 8), 2);
        checkProcessing(invalid, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
}

DEF_TEST(RustRaw_Sof3RootStage1, r) {
    auto bytes = make_root_sof3_dng(true);
    REPORTER_ASSERT(r, SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    stream.reset();
    data.reset();
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
        return;
    }
    REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 2 &&
                       reader->bits_per_sample() == 8 && reader->channels() == 3 &&
                       reader->main_ifd_index() == 0);
    const std::array<uint8_t, 18> expected = {
            1, 3, 5, 7, 9, 11, 25, 27, 29,
            13, 15, 17, 19, 21, 23, 31, 33, 35,
    };
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint32_t y = 0; y < 2; ++y) {
            std::array<uint8_t, 12> raw;
            raw.fill(0xa5);
            REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                    y, rust::Slice<uint8_t>(raw.data(), 9)) ==
                    rust_raw::DecodeStatus::Success);
            for (size_t i = 0; i < 9; ++i) {
                REPORTER_ASSERT(r, raw[i] == expected[y * 9 + i]);
            }
            REPORTER_ASSERT(r, raw[9] == 0xa5 && raw[11] == 0xa5);
            std::array<uint16_t, 11> stage2, stage3;
            stage2.fill(0xa5a5);
            stage3.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    y, rust::Slice<uint16_t>(stage2.data(), 9)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                    y, rust::Slice<uint16_t>(stage3.data(), 9)) ==
                    rust_raw::DecodeStatus::Success);
            for (size_t i = 0; i < 9; ++i) {
                REPORTER_ASSERT(r, stage2[i] == uint16_t(expected[y * 9 + i]) * 257 &&
                                   stage3[i] == stage2[i]);
            }
            REPORTER_ASSERT(r, stage2[9] == 0xa5a5 && stage3[10] == 0xa5a5);
        }
    }
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_Sof3RootMalformed, r) {
    const auto good = make_root_sof3_dng(true);
    const size_t offsetsTag = entry_for(good, 324);
    const size_t lengthsTag = entry_for(good, 325);
    REPORTER_ASSERT(r, offsetsTag != good.size() && lengthsTag != good.size());
    if (offsetsTag == good.size() || lengthsTag == good.size()) {
        return;
    }
    const size_t offsets = read32(good, offsetsTag + 8);
    auto invalid = good;
    set32(&invalid, offsets + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Incomplete);

    invalid = good;
    set32(&invalid, lengthsTag + 4, 1);
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Invalid);

    invalid = good;
    const size_t crop = entry_for(invalid, 50720);
    REPORTER_ASSERT(r, crop != invalid.size());
    if (crop != invalid.size()) {
        set32(&invalid, read32(invalid, crop + 8), 2);
        REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Unsupported);
    }

    invalid = good;
    const size_t firstTile = read32(invalid, offsets);
    const size_t sizes = read32(invalid, lengthsTag + 8);
    const size_t firstSize = read32(invalid, sizes);
    const uint8_t sof3[] = {0xff, 0xc3};
    auto marker = std::search(invalid.begin() + firstTile,
                              invalid.begin() + firstTile + firstSize,
                              sof3, sof3 + sizeof(sof3));
    REPORTER_ASSERT(r, marker != invalid.begin() + firstTile + firstSize);
    if (marker != invalid.begin() + firstTile + firstSize) {
        marker[1] = 0xc0;
        REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Unsupported);
    }

    invalid = good;
    const size_t opcodes = entry_for(invalid, 51009);
    REPORTER_ASSERT(r, opcodes != invalid.size());
    if (opcodes == invalid.size()) {
        return;
    }
    const size_t list = read32(invalid, opcodes + 8);
    invalid[list + 7] = 9; // Unknown required operation.
    auto data = SkData::MakeWithCopy(invalid.data(), invalid.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 9> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t value) { return value == 0xa5a5; }));
}

DEF_TEST(RustRaw_Float32Rows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_float32_dng(bigEndian, multipleStrips);
            REPORTER_ASSERT(r, SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                               reader->bits_per_sample() == 32 && reader->channels() == 3 &&
                               reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 3; ++y) {
                    std::array<float, 11> raw, second, third;
                    raw.fill(float_from_bits(0x7fcfffff));
                    second.fill(float_from_bits(0x7fcfffff));
                    third.fill(float_from_bits(0x7fcfffff));
                    REPORTER_ASSERT(r, reader->read_stage1_rgb_f32_row(
                            y, rust::Slice<float>(raw.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_rgb_f32_row(
                            y, rust::Slice<float>(second.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage3_rgb_f32_row(
                            y, rust::Slice<float>(third.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 9; ++x) {
                        REPORTER_ASSERT(r, float_bits(raw[x]) == kFloatBits[y * 9 + x] &&
                                           float_bits(second[x]) == float_bits(raw[x]) &&
                                           float_bits(third[x]) == float_bits(raw[x]));
                    }
                    REPORTER_ASSERT(r, float_bits(raw[9]) == 0x7fcfffff &&
                                       float_bits(second[10]) == 0x7fcfffff &&
                                       float_bits(third[9]) == 0x7fcfffff);
                }
            }
            std::array<float, 8> wrong;
            wrong.fill(float_from_bits(0x7fcfffff));
            REPORTER_ASSERT(r, reader->read_stage1_rgb_f32_row(
                    0, rust::Slice<float>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](float sample) { return float_bits(sample) == 0x7fcfffff; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_Float32MalformedAndGuards, r) {
    auto bytes = make_float32_dng(false, true, true);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    if (reader->stage1_status() == rust_raw::DecodeStatus::Success) {
        std::array<float, 9> raw{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_f32_row(
                0, rust::Slice<float>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, float_bits(raw[0]) == 0x80000000 &&
                           float_bits(raw[1]) == 0x7fc12345 &&
                           float_bits(raw[2]) == 0x7f800000 &&
                           float_bits(raw[3]) == 0x7f812345);
        std::array<float, 9> untouched;
        untouched.fill(float_from_bits(0x7fcfffff));
        REPORTER_ASSERT(r, reader->read_stage2_rgb_f32_row(
                0, rust::Slice<float>(untouched.data(), untouched.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->read_stage3_rgb_f32_row(
                0, rust::Slice<float>(untouched.data(), untouched.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                       [](float value) { return float_bits(value) == 0x7fcfffff; }));
    }

    bytes = make_float32_dng(false, true);
    bytes.pop_back();
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_float32_dng(false, true);
    const size_t offsetsTag = entry_for(bytes, 273);
    const size_t sizesTag = entry_for(bytes, 279);
    REPORTER_ASSERT(r, offsetsTag != bytes.size() && sizesTag != bytes.size());
    if (offsetsTag == bytes.size() || sizesTag == bytes.size()) {
        return;
    }
    const size_t offsets = read32(bytes, offsetsTag + 8);
    set32(&bytes, offsets + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_float32_dng(false, true);
    const size_t counts = read32(bytes, sizesTag + 8);
    set32(&bytes, counts + 4, 35);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_float32_dng(false, true);
    set32(&bytes, offsetsTag + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_float32_dng(false, true);
    const size_t format = entry_for(bytes, 339);
    REPORTER_ASSERT(r, format != bytes.size());
    if (format != bytes.size()) {
        bytes[format + 2] = 4; // LONG instead of SHORT.
        const auto status = stage1_status(bytes);
        REPORTER_ASSERT(r, status == rust_raw::DecodeStatus::Invalid ||
                           status == rust_raw::DecodeStatus::Incomplete);
        bytes = make_float32_dng(false, true);
        set32(&bytes, read32(bytes, format + 8), 1); // Integer samples, not IEEE float.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }

    auto checkProcessing = [&](std::vector<uint8_t> modified,
                               rust_raw::DecodeStatus expectedStage2,
                               rust_raw::DecodeStatus expectedStage3) {
        auto owned = SkData::MakeWithCopy(modified.data(), modified.size());
        auto input = SkMemoryStream::Make(owned);
        auto source = std::make_unique<rust::stream::SkStreamAdapter>(input.get());
        auto parsed = rust_raw::new_reader(std::move(source));
        REPORTER_ASSERT(r, parsed->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, parsed->stage2_status() == expectedStage2);
        REPORTER_ASSERT(r, parsed->stage3_status() == expectedStage3);
        REPORTER_ASSERT(r, parsed->status() == rust_raw::DecodeStatus::Unsupported);
        if (parsed->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<float, 9> raw{};
        REPORTER_ASSERT(r, parsed->read_stage1_rgb_f32_row(
                0, rust::Slice<float>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, float_bits(raw[1]) == kFloatBits[1]);
        std::array<float, 9> output;
        output.fill(float_from_bits(0x7fcfffff));
        REPORTER_ASSERT(r, parsed->read_stage2_rgb_f32_row(
                0, rust::Slice<float>(output.data(), output.size())) == expectedStage2);
        if (expectedStage2 == rust_raw::DecodeStatus::Success) {
            for (size_t x = 0; x < output.size(); ++x) {
                REPORTER_ASSERT(r, float_bits(output[x]) == float_bits(raw[x]));
            }
            output.fill(float_from_bits(0x7fcfffff));
        }
        REPORTER_ASSERT(r, parsed->read_stage3_rgb_f32_row(
                0, rust::Slice<float>(output.data(), output.size())) == expectedStage3);
        REPORTER_ASSERT(r, std::all_of(output.begin(), output.end(),
                                       [](float value) { return float_bits(value) == 0x7fcfffff; }));
    };
    for (uint16_t tag : {uint16_t(51009), uint16_t(51022)}) {
        bytes = make_float32_dng(false, true);
        const size_t replaced = entry_for(bytes, 51110);
        REPORTER_ASSERT(r, replaced != bytes.size());
        if (replaced == bytes.size()) {
            continue;
        }
        bytes[replaced] = tag & 0xff;
        bytes[replaced + 1] = tag >> 8;
        bytes[replaced + 2] = 7;
        checkProcessing(bytes,
                        tag == 51022 ? rust_raw::DecodeStatus::Success :
                                       rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    bytes = make_float32_dng(false, true);
    const size_t white = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, white != bytes.size());
    if (white != bytes.size()) {
        set32(&bytes, read32(bytes, white + 8), 2);
        checkProcessing(bytes, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    bytes = make_float32_dng(false, true);
    const size_t crop = entry_for(bytes, 50720);
    REPORTER_ASSERT(r, crop != bytes.size());
    if (crop != bytes.size()) {
        set32(&bytes, read32(bytes, crop + 8), 2);
        checkProcessing(bytes, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
}

DEF_TEST(RustRaw_Rgb8RootRamp, r) {
    auto bytes = make_rgb8_root_dng(false, false, true);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    stream.reset();
    data.reset();
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
        return;
    }
    REPORTER_ASSERT(r, reader->width() == 256 && reader->height() == 3 &&
                       reader->bits_per_sample() == 8 && reader->channels() == 3 &&
                       reader->main_ifd_index() == 0);
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint32_t y = 0; y < 3; ++y) {
            std::array<uint8_t, 775> raw;
            raw.fill(0xa5);
            std::array<uint16_t, 770> second, third;
            second.fill(0xa5a5);
            third.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                    y, rust::Slice<uint8_t>(raw.data(), 768)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    y, rust::Slice<uint16_t>(second.data(), 768)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                    y, rust::Slice<uint16_t>(third.data(), 768)) ==
                    rust_raw::DecodeStatus::Success);
            for (size_t x = 0; x < 256; ++x) {
                const uint8_t expected[] = {
                        static_cast<uint8_t>(y == 2 ? 0 : x),
                        static_cast<uint8_t>(y == 1 ? 0 : x),
                        static_cast<uint8_t>(y == 0 ? x : y == 1 ? 255 - x : 0),
                };
                for (int c = 0; c < 3; ++c) {
                    const size_t i = 3 * x + c;
                    REPORTER_ASSERT(r, raw[i] == expected[c] &&
                                       second[i] == uint16_t(expected[c]) * 257 &&
                                       third[i] == second[i]);
                }
            }
            REPORTER_ASSERT(r, raw[768] == 0xa5 && raw[774] == 0xa5 &&
                               second[768] == 0xa5a5 && third[769] == 0xa5a5);
        }
    }
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_Rgb8RootStrips, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_rgb8_root_dng(bigEndian, multipleStrips, false);
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 3 && reader->height() == 3 &&
                               reader->channels() == 3 && reader->bits_per_sample() == 8 &&
                               reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 3; ++y) {
                    std::array<uint8_t, 11> raw;
                    std::array<uint16_t, 11> stage2, stage3;
                    raw.fill(0xa5);
                    stage2.fill(0xa5a5);
                    stage3.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                            y, rust::Slice<uint8_t>(raw.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                            y, rust::Slice<uint16_t>(stage2.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                            y, rust::Slice<uint16_t>(stage3.data(), 9)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t i = 0; i < 9; ++i) {
                        REPORTER_ASSERT(r, raw[i] == kRgb8Samples[y * 9 + i] &&
                                           stage2[i] == uint16_t(raw[i]) * 257 &&
                                           stage3[i] == stage2[i]);
                    }
                    REPORTER_ASSERT(r, raw[9] == 0xa5 && stage2[10] == 0xa5a5 &&
                                       stage3[9] == 0xa5a5);
                }
            }
            std::array<uint16_t, 8> wrong;
            wrong.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(data, &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_Rgb8SrgbFinal, r) {
    auto check = [&](bool bigEndian, bool multipleStrips, bool cube) {
        auto bytes = make_rgb8_root_dng(bigEndian, multipleStrips, false, true, cube);
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(SkMemoryStream::Make(data), &result);
        REPORTER_ASSERT(r, result == SkCodec::kSuccess && codec);
        if (!codec) {
            return;
        }
        data.reset();
        const uint32_t width = cube ? 256 : 3;
        const uint32_t height = cube ? 256 : 3;
        REPORTER_ASSERT(r, codec->dimensions() == SkISize::Make(width, height));
        REPORTER_ASSERT(r, codec->getEncodedFormat() == SkEncodedImageFormat::kDNG);
        REPORTER_ASSERT(r, codec->getInfo().alphaType() == kOpaque_SkAlphaType);
        const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
        const size_t active = width * 4;
        const size_t stride = active + 8;
        std::vector<uint8_t> pixels(stride * height);
        for (int repeat = 0; repeat < 2; ++repeat) {
            std::fill(pixels.begin(), pixels.end(), 0xa5);
            REPORTER_ASSERT(r, codec->getPixels(info, pixels.data(), stride) ==
                                SkCodec::kSuccess);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t source[3] = {
                            cube ? uint8_t(x) : kRgb8Samples[9 * y + 3 * x],
                            cube ? uint8_t(y) : kRgb8Samples[9 * y + 3 * x + 1],
                            cube ? uint8_t((x + 3 * y) % 256) : kRgb8Samples[9 * y + 3 * x + 2],
                    };
                    const auto* actual = pixels.data() + y * stride + 4 * x;
                    for (int channel = 0; channel < 3; ++channel) {
                        REPORTER_ASSERT(r, actual[channel] == expected_srgb(source[channel]),
                                        "color (%u,%u) channel %d", x, y, channel);
                    }
                    REPORTER_ASSERT(r, actual[3] == 255);
                }
                REPORTER_ASSERT(r, std::all_of(
                        pixels.begin() + y * stride + active,
                        pixels.begin() + (y + 1) * stride,
                        [](uint8_t value) { return value == 0xa5; }));
            }
        }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
        auto source = SkData::MakeWithCopy(bytes.data(), bytes.size());
        SkCodec::Result referenceResult = SkCodec::kInternalError;
        auto reference = SkRawDecoder::Decode(SkMemoryStream::Make(source), &referenceResult);
        REPORTER_ASSERT(r, referenceResult == SkCodec::kSuccess && reference);
        if (reference) {
            REPORTER_ASSERT(r, reference->dimensions() == codec->dimensions());
            std::vector<uint8_t> expected(pixels.size(), 0xa5);
            REPORTER_ASSERT(r, reference->getPixels(info, expected.data(), stride) ==
                                SkCodec::kSuccess);
            REPORTER_ASSERT(r, pixels == expected);
        }
#endif
    };
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            check(bigEndian, multipleStrips, false);
        }
    }
    check(false, false, true);
}

#if defined(SK_CODEC_DECODES_RAW) && !defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
DEF_TEST(RustRaw_PublicSdkFreeFallback, r) {
    auto check = [&](const std::vector<uint8_t>& bytes) {
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        SkCodec::Result directResult = SkCodec::kInternalError;
        auto direct = SkRawRustDecoder::Decode(data, &directResult);
        REPORTER_ASSERT(r, direct && directResult == SkCodec::kSuccess);
        if (!direct) {
            return;
        }
        const auto info = direct->getInfo().makeColorType(kRGBA_8888_SkColorType);
        const size_t stride = info.minRowBytes() + 8;
        std::vector<uint8_t> expected(stride * info.height(), 0xa5);
        const auto directStatus = direct->getPixels(info, expected.data(), stride);
        REPORTER_ASSERT(r, directStatus == SkCodec::kSuccess);
        if (directStatus != SkCodec::kSuccess) {
            return;
        }
        for (int streamKind : {0, 1, 2}) {
            SkCodec::Result result = SkCodec::kInternalError;
            std::unique_ptr<SkStream> stream;
            if (streamKind == 1) {
                stream = std::make_unique<NonseekableStream>(data);
            } else if (streamKind == 2) {
                stream = std::make_unique<RawForwardOnlyStream>(data);
            } else {
                stream = SkMemoryStream::Make(data);
            }
            auto codec = SkRawDecoder::Decode(std::move(stream), &result);
            REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess,
                            "stream %d: %s", streamKind,
                            SkCodec::ResultToString(result));
            if (!codec) {
                continue;
            }
            REPORTER_ASSERT(r, codec->getEncodedFormat() == SkEncodedImageFormat::kDNG &&
                               codec->dimensions() == direct->dimensions());
            std::vector<uint8_t> actual(expected.size(), 0xa5);
            for (int repeat = 0; repeat < 2; ++repeat) {
                REPORTER_ASSERT(r, codec->getPixels(info, actual.data(), stride) ==
                                    SkCodec::kSuccess);
                REPORTER_ASSERT(r, actual == expected);
            }
        }
        SkCodec::Result result = SkCodec::kInternalError;
        auto registered = SkCodec::MakeFromStream(SkMemoryStream::Make(data), &result);
        REPORTER_ASSERT(r, registered && result == SkCodec::kSuccess);
        if (registered) {
            std::vector<uint8_t> actual(expected.size(), 0xa5);
            REPORTER_ASSERT(r, registered->getPixels(info, actual.data(), stride) ==
                                SkCodec::kSuccess);
            REPORTER_ASSERT(r, actual == expected);
        }
    };
    check(make_output_mono_ramp(8));
    check(make_rgb8_root_dng(false, false, false, true, false));

    auto previewData = GetResourceAsData("images/dng_with_preview.dng");
    REPORTER_ASSERT(r, previewData);
    if (!previewData) {
        return;
    }
    SkCodec::Result assetResult = SkCodec::kInternalError;
    SkCodec::Result forwardResult = SkCodec::kInternalError;
    auto asset = SkRawDecoder::Decode(SkMemoryStream::Make(previewData), &assetResult);
    auto forward = SkRawDecoder::Decode(
            std::make_unique<NonseekableStream>(previewData), &forwardResult);
    REPORTER_ASSERT(r, asset && assetResult == SkCodec::kSuccess);
    REPORTER_ASSERT(r, forward && forwardResult == SkCodec::kSuccess);
    if (!asset || !forward) {
        return;
    }
    REPORTER_ASSERT(r, asset->getEncodedFormat() == SkEncodedImageFormat::kJPEG &&
                       forward->getEncodedFormat() == SkEncodedImageFormat::kJPEG &&
                       asset->dimensions() == forward->dimensions());
    const auto info = asset->getInfo().makeColorType(kRGBA_8888_SkColorType);
    std::vector<uint8_t> expected(info.computeMinByteSize());
    std::vector<uint8_t> actual(expected.size());
    REPORTER_ASSERT(r, asset->getPixels(info, expected.data(), info.minRowBytes()) ==
                        SkCodec::kSuccess);
    REPORTER_ASSERT(r, forward->getPixels(info, actual.data(), info.minRowBytes()) ==
                        SkCodec::kSuccess);
    REPORTER_ASSERT(r, actual == expected);

    auto truncated = make_output_mono_ramp(8);
    truncated.pop_back();
    auto incomplete = SkData::MakeWithCopy(truncated.data(), truncated.size());
    for (bool forwardOnly : {false, true}) {
        SkCodec::Result result = SkCodec::kSuccess;
        std::unique_ptr<SkStream> stream;
        if (forwardOnly) {
            stream = std::make_unique<NonseekableStream>(incomplete);
        } else {
            stream = SkMemoryStream::Make(incomplete);
        }
        auto codec = SkRawDecoder::Decode(std::move(stream), &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kIncompleteInput,
                        "%s: %s", forwardOnly ? "nonseekable" : "asset",
                        SkCodec::ResultToString(result));
    }
}

DEF_TEST(RustRaw_PublicPreviewNotSelected, r) {
    for (bool compressed : {false, true}) {
        auto bytes = make_preview_rgb16_dng(false, false, compressed, compressed);
        REPORTER_ASSERT(r, !bytes.empty());
        if (bytes.empty()) {
            continue;
        }
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto adapterStream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(adapterStream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success &&
                           reader->main_ifd_index() == 1 &&
                           reader->status() == rust_raw::DecodeStatus::Unsupported);

        for (int streamKind : {0, 1, 2}) {
            std::unique_ptr<SkStream> stream;
            if (streamKind == 1) {
                stream = std::make_unique<NonseekableStream>(data);
            } else if (streamKind == 2) {
                stream = std::make_unique<RawForwardOnlyStream>(data);
            } else {
                stream = SkMemoryStream::Make(data);
            }
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawDecoder::Decode(std::move(stream), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented,
                            "compressed=%d stream=%d result=%s",
                            compressed, streamKind, SkCodec::ResultToString(result));
        }
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkCodec::MakeFromStream(SkMemoryStream::Make(data), &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
    }
}

DEF_TEST(RustRaw_PublicForwardStreamLimit, r) {
    auto bytes = make_output_mono_ramp(8);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawDecoder::Decode(std::make_unique<RawExtendedForwardOnlyStream>(
                                             data, 100 * 1024 * 1024 + 1), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented,
                    "%s", SkCodec::ResultToString(result));
}
#endif

DEF_TEST(RustRaw_DeflateRgb8SrgbFinal, r) {
    auto check = [&](bool bigEndian, bool multipleStrips, bool predictor2, bool cube) {
        auto bytes = make_deflate_rgb8_srgb_dng(bigEndian, multipleStrips, predictor2, cube);
        REPORTER_ASSERT(r, !bytes.empty());
        if (bytes.empty()) {
            return;
        }
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        stream.reset();
        data.reset();
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Success);
        if (reader->status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        const uint32_t width = cube ? 256 : 3;
        const uint32_t height = cube ? 256 : 3;
        REPORTER_ASSERT(r, reader->width() == width && reader->height() == height &&
                           reader->channels() == 3 && reader->bits_per_sample() == 8);
        std::vector<uint8_t> first(width * 3 + 2, 0xa5);
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(first.data(), width * 3)) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, first[width * 3] == 0xa5);

        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess);
        if (!codec) {
            return;
        }
        REPORTER_ASSERT(r, codec->dimensions() == SkISize::Make(width, height) &&
                           codec->getEncodedFormat() == SkEncodedImageFormat::kDNG);
        const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
        const size_t active = width * 4;
        const size_t stride = active + 8;
        std::vector<uint8_t> pixels(stride * height);
        for (int repeat = 0; repeat < 2; ++repeat) {
            std::fill(pixels.begin(), pixels.end(), 0xa5);
            REPORTER_ASSERT(r, codec->getPixels(info, pixels.data(), stride) ==
                                SkCodec::kSuccess);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t source[3] = {
                            cube ? uint8_t(x) : kRgb8Samples[9 * y + 3 * x],
                            cube ? uint8_t(y) : kRgb8Samples[9 * y + 3 * x + 1],
                            cube ? uint8_t((x + 3 * y) % 256) : kRgb8Samples[9 * y + 3 * x + 2],
                    };
                    const auto* actual = pixels.data() + y * stride + 4 * x;
                    for (int channel = 0; channel < 3; ++channel) {
                        REPORTER_ASSERT(r, actual[channel] == expected_srgb(source[channel]));
                    }
                    REPORTER_ASSERT(r, actual[3] == 255);
                }
                REPORTER_ASSERT(r, std::all_of(
                        pixels.begin() + y * stride + active,
                        pixels.begin() + (y + 1) * stride,
                        [](uint8_t sample) { return sample == 0xa5; }));
            }
        }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
        SkCodec::Result referenceResult = SkCodec::kInternalError;
        auto reference = SkRawDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
        REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
        if (reference) {
            std::vector<uint8_t> expected(pixels.size(), 0xa5);
            REPORTER_ASSERT(r, reference->getPixels(info, expected.data(), stride) ==
                                SkCodec::kSuccess);
            REPORTER_ASSERT(r, pixels == expected);
        }
#endif
    };
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool predictor2 : {false, true}) {
                check(bigEndian, multipleStrips, predictor2, false);
            }
        }
    }
    check(false, false, true, true);
}

DEF_TEST(RustRaw_DeflateRgb8Malformed, r) {
    const auto good = make_deflate_rgb8_srgb_dng(false, true, true);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t offsets = entry_for(good, 273);
    const size_t lengths = entry_for(good, 279);
    const size_t predictor = entry_for(good, 317);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size() &&
                       predictor != good.size());
    if (offsets == good.size() || lengths == good.size() || predictor == good.size()) {
        return;
    }
    const size_t offsetTable = read32(good, offsets + 8);
    const size_t lengthTable = read32(good, lengths + 8);
    auto bytes = good;
    set32(&bytes, offsetTable + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    bytes[predictor + 8] = 3;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    bytes = good;
    bytes[predictor + 2] = 4;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    const size_t lastOffset = read32(good, offsetTable + 4);
    const size_t lastLength = read32(good, lengthTable + 4);
    bytes = good;
    bytes[lastOffset + lastLength - 1] ^= 0xff;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(lastLength - 1));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    std::array<uint8_t, 10> expanded{};
    std::vector<uint8_t> overlong(compressBound(expanded.size()));
    uLongf compressedSize = overlong.size();
    REPORTER_ASSERT(r, compress2(overlong.data(), &compressedSize, expanded.data(),
                                 expanded.size(), Z_BEST_SPEED) == Z_OK);
    overlong.resize(compressedSize);
    set32(&bytes, offsetTable + 4, static_cast<uint32_t>(bytes.size()));
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(overlong.size()));
    bytes.insert(bytes.end(), overlong.begin(), overlong.end());
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    std::array<uint8_t, 9> untouched;
    untouched.fill(0xa5);
    REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
            0, rust::Slice<uint8_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Invalid);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint8_t sample) { return sample == 0xa5; }));

    bytes = good;
    const size_t matrix = entry_for(bytes, 50721);
    REPORTER_ASSERT(r, matrix != bytes.size());
    if (matrix != bytes.size()) {
        set32(&bytes, read32(bytes, matrix + 8), 1038);
        data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        stream = SkMemoryStream::Make(data);
        adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(data, &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
    }
}

DEF_TEST(RustRaw_LinearizedRgb8SrgbFinal, r) {
    auto check = [&](bool bigEndian, bool multipleStrips, bool compressed,
                     bool shortTable, bool cube) {
        auto bytes = make_linearized_rgb8_dng(
                bigEndian, multipleStrips, compressed, shortTable, cube);
        REPORTER_ASSERT(r, !bytes.empty());
        if (bytes.empty()) {
            return;
        }
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        stream.reset();
        data.reset();
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Success);
        if (reader->status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        const uint32_t width = cube ? 256 : 3;
        const uint32_t height = cube ? 256 : 3;
        const size_t rowSamples = width * 3;
        for (uint32_t y : {0u, height / 2, height - 1}) {
            std::vector<uint8_t> first(rowSamples + 1, 0xa5);
            std::vector<uint16_t> second(rowSamples + 1, 0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                    y, rust::Slice<uint8_t>(first.data(), rowSamples)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    y, rust::Slice<uint16_t>(second.data(), rowSamples)) ==
                    rust_raw::DecodeStatus::Success);
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t raw[3] = {
                        cube ? uint8_t(x) : kRgb8Samples[9 * y + 3 * x],
                        cube ? uint8_t(y) : kRgb8Samples[9 * y + 3 * x + 1],
                        cube ? uint8_t((x + 3 * y) % 256) : kRgb8Samples[9 * y + 3 * x + 2],
                };
                for (int channel = 0; channel < 3; ++channel) {
                    const uint8_t mapped = shortTable ?
                            (raw[channel] == 0 ? 0 : 255) :
                            static_cast<uint8_t>(std::min(uint32_t(raw[channel]) * 2, 255u));
                    REPORTER_ASSERT(r, first[3 * x + channel] == raw[channel] &&
                                       second[3 * x + channel] == uint16_t(mapped) * 257);
                }
            }
            REPORTER_ASSERT(r, first[rowSamples] == 0xa5 &&
                               second[rowSamples] == 0xa5a5);
        }
        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess);
        if (!codec) {
            return;
        }
        const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
        const size_t active = width * 4;
        const size_t stride = active + 8;
        std::vector<uint8_t> pixels(stride * height);
        for (int repeat = 0; repeat < 2; ++repeat) {
            std::fill(pixels.begin(), pixels.end(), 0xa5);
            REPORTER_ASSERT(r, codec->getPixels(info, pixels.data(), stride) ==
                                SkCodec::kSuccess);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t raw[3] = {
                            cube ? uint8_t(x) : kRgb8Samples[9 * y + 3 * x],
                            cube ? uint8_t(y) : kRgb8Samples[9 * y + 3 * x + 1],
                            cube ? uint8_t((x + 3 * y) % 256) : kRgb8Samples[9 * y + 3 * x + 2],
                    };
                    const auto* actual = pixels.data() + y * stride + 4 * x;
                    for (int channel = 0; channel < 3; ++channel) {
                        const uint8_t mapped = shortTable ?
                                (raw[channel] == 0 ? 0 : 255) :
                                static_cast<uint8_t>(std::min(
                                        uint32_t(raw[channel]) * 2, 255u));
                        REPORTER_ASSERT(r, actual[channel] == expected_srgb(mapped));
                    }
                    REPORTER_ASSERT(r, actual[3] == 255);
                }
                REPORTER_ASSERT(r, std::all_of(
                        pixels.begin() + y * stride + active,
                        pixels.begin() + (y + 1) * stride,
                        [](uint8_t sample) { return sample == 0xa5; }));
            }
        }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
        SkCodec::Result referenceResult = SkCodec::kInternalError;
        auto reference = SkRawDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
        REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
        if (reference) {
            std::vector<uint8_t> expected(pixels.size(), 0xa5);
            REPORTER_ASSERT(r, reference->getPixels(info, expected.data(), stride) ==
                                SkCodec::kSuccess);
            REPORTER_ASSERT(r, pixels == expected);
        }
#endif
    };
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool compressed : {false, true}) {
                for (bool shortTable : {false, true}) {
                    check(bigEndian, multipleStrips, compressed, shortTable, false);
                }
            }
        }
    }
    check(false, false, false, false, true);
    check(false, false, true, true, true);
}

DEF_TEST(RustRaw_LinearizedRgb8Malformed, r) {
    const auto good = make_linearized_rgb8_dng(false, true, true, false);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t table = entry_for(good, 50712);
    REPORTER_ASSERT(r, table != good.size());
    if (table == good.size()) {
        return;
    }
    auto bytes = good;
    bytes[table + 2] = 4;
    set32(&bytes, table + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 4, 0);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = good;
    const size_t last = read32(bytes, table + 8) + 2 * UINT8_MAX;
    bytes[last] = 0;
    bytes[last + 1] = 1;  // 256 is not an 8-bit output.
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 9> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawRustDecoder::Decode(data, &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_LinearizedOversizedTable, r) {
    for (const auto& good : {
                 with_linearization_table(make_output_mono_ramp(8), false, false, UINT8_MAX),
                 make_linearized_rgb8_dng(false, true, true, false),
         }) {
        REPORTER_ASSERT(r, !good.empty());
        if (good.empty()) {
            continue;
        }
        const size_t table = entry_for(good, 50712);
        REPORTER_ASSERT(r, table != good.size());
        if (table == good.size()) {
            continue;
        }
        auto bytes = good;
        if (bytes.size() & 1) {
            bytes.push_back(0);
        }
        const auto offset = static_cast<uint32_t>(bytes.size());
        for (uint32_t sample = 0; sample <= 65536; ++sample) {
            append16(&bytes, sample == 65535 ? 255 : sample == 65536 ? 256 : 0, false);
        }
        set32(&bytes, table + 4, 65537);
        set32(&bytes, table + 8, offset);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(data, &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kInvalidInput);
    }
    const auto mono16 = with_extra_linearization_entry_le(
            make_linearized_mono16_dng(false, false, false, false), UINT16_MAX);
    REPORTER_ASSERT(r, !mono16.empty());
    if (!mono16.empty()) {
        REPORTER_ASSERT(r, stage1_status(mono16) == rust_raw::DecodeStatus::Invalid);
    }
}

DEF_TEST(RustRaw_LinearizedRedundantTableEntry, r) {
    const auto bytes = with_extra_linearization_entry_le(
            make_linearized_rgb8_dng(false, false, false, false), UINT8_MAX);
    REPORTER_ASSERT(r, !bytes.empty());
    if (bytes.empty()) {
        return;
    }
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success &&
                       reader->stage2_status() == rust_raw::DecodeStatus::Success &&
                       reader->stage3_status() == rust_raw::DecodeStatus::Success);
    if (reader->stage3_status() != rust_raw::DecodeStatus::Success) {
        return;
    }
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(data, &result);
    REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess);
    if (!codec) {
        return;
    }
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType);
    std::array<uint8_t, 3 * 3 * 4> pixels;
    const auto status = codec->getPixels(info, pixels.data(), 3 * 4);
    REPORTER_ASSERT(r, status == SkCodec::kSuccess);
    if (status != SkCodec::kSuccess) {
        return;
    }
    for (size_t i = 0; i < 9; ++i) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const uint8_t mapped = static_cast<uint8_t>(
                    std::min(uint32_t(kRgb8Samples[i * 3 + channel]) * 2, 255u));
            REPORTER_ASSERT(r, pixels[i * 4 + channel] == expected_srgb(mapped));
        }
        REPORTER_ASSERT(r, pixels[i * 4 + 3] == 255);
    }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
    SkCodec::Result referenceResult = SkCodec::kInternalError;
    auto reference = SkRawDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
    REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
    if (reference) {
        std::array<uint8_t, 3 * 3 * 4> expected;
        const auto referenceStatus = reference->getPixels(info, expected.data(), 3 * 4);
        REPORTER_ASSERT(r, referenceStatus == SkCodec::kSuccess);
        if (referenceStatus == SkCodec::kSuccess) {
            REPORTER_ASSERT(r, pixels == expected);
        }
    }
#endif
}

DEF_TEST(RustRaw_Rgb8SrgbProfileGuard, r) {
    const auto good = make_rgb8_root_dng(false, false, false, true);
    auto check = [&](std::vector<uint8_t> bytes, bool stage2Identity) {
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() ==
                                   (stage2Identity ? rust_raw::DecodeStatus::Success :
                                                     rust_raw::DecodeStatus::Unsupported));
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        std::array<uint8_t, 9> untouched;
        untouched.fill(0xa5);
        REPORTER_ASSERT(r, !reader->copy_rgb_row(
                0, rust::Slice<uint8_t>(untouched.data(), untouched.size())));
        REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                       [](uint8_t sample) { return sample == 0xa5; }));
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(data, &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
    };
    for (uint16_t id : {uint16_t(50721), uint16_t(50964), uint16_t(50728),
                        uint16_t(50940)}) {
        auto bytes = good;
        const size_t entry = entry_for(bytes, id);
        REPORTER_ASSERT(r, entry != bytes.size());
        if (entry == bytes.size()) {
            continue;
        }
        const size_t value = read32(bytes, entry + 8);
        if (id == 50728) {
            set32(&bytes, value, 2);
        } else if (id == 50940) {
            set32(&bytes, value + 8, 0x3f000000);
        } else {
            set32(&bytes, value, read32(bytes, value) + 1);
        }
        check(std::move(bytes), true);
    }
    auto bytes = good;
    const size_t blackRender = entry_for(bytes, 51110);
    REPORTER_ASSERT(r, blackRender != bytes.size());
    if (blackRender != bytes.size()) {
        set32(&bytes, blackRender + 8, 0);
        check(std::move(bytes), false);
    }
    bytes = good;
    const uint16_t count = bytes[8] | (uint16_t(bytes[9]) << 8);
    const size_t firstEntries = 10;
    if (bytes.size() & 1) {
        bytes.push_back(0);
    }
    const uint32_t nextIfd = static_cast<uint32_t>(bytes.size());
    put16(&bytes, count + 1);
    bytes.insert(bytes.end(), good.begin() + firstEntries,
                 good.begin() + firstEntries + count * 12);
    put16(&bytes, 52544);
    put16(&bytes, 7);
    put32(&bytes, 4);
    put32(&bytes, 0);
    put32(&bytes, 0);
    set32(&bytes, 4, nextIfd);
    check(std::move(bytes), true);
}

DEF_TEST(RustRaw_Rgb8ExplicitToneStageGuard, r) {
    auto bytes = make_rgb8_root_dng(false, false, true, true);
    const size_t tone = entry_for(bytes, 50940);
    REPORTER_ASSERT(r, tone != bytes.size());
    if (tone == bytes.size()) {
        return;
    }
    while (bytes.size() & 3) {
        bytes.push_back(0);
    }
    set32(&bytes, tone + 4, 6);
    set32(&bytes, tone + 8, static_cast<uint32_t>(bytes.size()));
    for (float value : {0.0f, 0.0f, 0.5f, 0.7f, 1.0f, 1.0f}) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        put32(&bytes, bits);
    }
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 770> stage3;
    stage3.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
            1, rust::Slice<uint16_t>(stage3.data(), 768)) ==
            rust_raw::DecodeStatus::Success);
    for (size_t x = 0; x < 256; ++x) {
        REPORTER_ASSERT(r, stage3[3 * x] == x * 257 &&
                           stage3[3 * x + 1] == 0 &&
                           stage3[3 * x + 2] == (255 - x) * 257);
    }
    REPORTER_ASSERT(r, stage3[768] == 0xa5a5 && stage3[769] == 0xa5a5);
    SkCodec::Result result = SkCodec::kSuccess;
    auto codec = SkRawRustDecoder::Decode(data, &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
}

DEF_TEST(RustRaw_Rgb8RootMalformed, r) {
    const auto good = make_rgb8_root_dng(false, true, false);
    auto bytes = good;
    bytes.pop_back();
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    const size_t offsetsTag = entry_for(good, 273);
    const size_t lengthsTag = entry_for(good, 279);
    REPORTER_ASSERT(r, offsetsTag != good.size() && lengthsTag != good.size());
    if (offsetsTag == good.size() || lengthsTag == good.size()) {
        return;
    }
    bytes = good;
    set32(&bytes, read32(good, offsetsTag + 8) + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = good;
    set32(&bytes, read32(good, lengthsTag + 8) + 4, 8);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = good;
    const size_t version = entry_for(good, 50706);
    REPORTER_ASSERT(r, version != good.size());
    if (version != good.size()) {
        bytes[version + 9] = 4;
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }
    bytes = good;
    const size_t format = entry_for(good, 339);
    REPORTER_ASSERT(r, format != good.size());
    if (format != good.size()) {
        bytes[read32(bytes, format + 8)] = 3; // FLOAT is not this integer profile.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }

    auto checkProcessing = [&](std::vector<uint8_t> changed,
                               rust_raw::DecodeStatus expectedStage2,
                               rust_raw::DecodeStatus expectedStage3) {
        auto data = SkData::MakeWithCopy(changed.data(), changed.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == expectedStage2);
        REPORTER_ASSERT(r, reader->stage3_status() == expectedStage3);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint8_t, 9> raw{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, std::equal(raw.begin(), raw.end(), kRgb8Samples.begin()));
        std::array<uint16_t, 9> row;
        row.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                0, rust::Slice<uint16_t>(row.data(), row.size())) == expectedStage2);
        if (expectedStage2 == rust_raw::DecodeStatus::Success) {
            for (size_t i = 0; i < row.size(); ++i) {
                REPORTER_ASSERT(r, row[i] == uint16_t(raw[i]) * 257);
            }
            row.fill(0xa5a5);
        }
        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                0, rust::Slice<uint16_t>(row.data(), row.size())) == expectedStage3);
        REPORTER_ASSERT(r, std::all_of(row.begin(), row.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    };

    const size_t colorimetric = entry_for(good, 50879);
    REPORTER_ASSERT(r, colorimetric != good.size());
    if (colorimetric != good.size()) {
        bytes = good;
        bytes[colorimetric + 8] = 2; // HDR not this SDR stage profile.
        checkProcessing(bytes, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    for (uint16_t opcode : {uint16_t(51009), uint16_t(51022)}) {
        bytes = good;
        const size_t profile = entry_for(good, 50940);
        REPORTER_ASSERT(r, profile != good.size());
        if (profile == good.size()) {
            continue;
        }
        bytes[profile] = opcode & 0xff;
        bytes[profile + 1] = opcode >> 8;
        bytes[profile + 2] = 7;
        set32(&bytes, profile + 4, 4);
        checkProcessing(bytes,
                        opcode == 51022 ? rust_raw::DecodeStatus::Success :
                                          rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    bytes = good;
    const size_t crop = entry_for(good, 50719);
    REPORTER_ASSERT(r, crop != good.size());
    if (crop != good.size()) {
        set32(&bytes, read32(bytes, crop + 8), 1);
        checkProcessing(bytes, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    bytes = good;
    const size_t black = entry_for(good, 50714);
    REPORTER_ASSERT(r, black != good.size());
    if (black != good.size()) {
        set32(&bytes, read32(bytes, black + 8), 1);
        checkProcessing(bytes, rust_raw::DecodeStatus::Unsupported,
                        rust_raw::DecodeStatus::Unsupported);
    }
    bytes = good;
    const size_t profile = entry_for(good, 50940);
    if (profile != good.size()) {
        bytes[profile] = 0xe8;
        bytes[profile + 1] = 0xfd; // Unknown processing tag 65000.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
        bytes = good;
        bytes[profile] = 330 & 0xff;
        bytes[profile + 1] = 330 >> 8;
        bytes[profile + 2] = 4;
        set32(&bytes, profile + 4, 1);
        set32(&bytes, profile + 8, 8); // Cyclic SubIFD must never succeed.
        REPORTER_ASSERT(r, stage1_status(bytes) != rust_raw::DecodeStatus::Success);
    }
}

DEF_TEST(RustRaw_BayerStages, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_rggb_dng(bigEndian, multipleStrips);
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 16 && reader->height() == 16 &&
                               reader->bits_per_sample() == 16 && reader->channels() == 1 &&
                               reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 16; ++y) {
                    std::array<uint16_t, 18> raw, linear;
                    raw.fill(0xa5a5);
                    linear.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                            y, rust::Slice<uint16_t>(raw.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                            y, rust::Slice<uint16_t>(linear.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    for (uint32_t x = 0; x < 16; ++x) {
                        const uint16_t sample = bayer_sample(x, y);
                        const uint32_t black = (y & 1) ?
                                ((x & 1) ? 1024 : 512) :
                                ((x & 1) ? 512 : 256);
                        const uint32_t expected = sample <= black ? 0 :
                                std::min<uint32_t>(65535,
                                  (uint32_t(sample - black) * 65535 + 3071 / 2) / 3071);
                        REPORTER_ASSERT(r, raw[x] == sample && linear[x] == expected);
                    }
                    REPORTER_ASSERT(r, raw[16] == 0xa5a5 && raw[17] == 0xa5a5 &&
                                       linear[16] == 0xa5a5 && linear[17] == 0xa5a5);
                }
            }
            std::array<uint16_t, 15> wrong;
            wrong.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            std::array<uint16_t, 16> untouched;
            untouched.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_row(
                    0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                    rust_raw::DecodeStatus::Unsupported);
            std::array<uint16_t, 48> noDemosaic;
            noDemosaic.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                    0, rust::Slice<uint16_t>(noDemosaic.data(), noDemosaic.size())) ==
                    rust_raw::DecodeStatus::Unsupported);
            REPORTER_ASSERT(r, reader->read_normalized_row(
                    0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                    rust_raw::DecodeStatus::Unsupported);
            REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            REPORTER_ASSERT(r, std::all_of(noDemosaic.begin(), noDemosaic.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
            SkCodec::Result referenceResult = SkCodec::kInternalError;
            auto reference = SkRawDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
            REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
        }
    }
}

DEF_TEST(RustRaw_BayerUniformStage3, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            auto bytes = make_rggb_dng(bigEndian, multipleStrips, true);
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage3_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 16 && reader->height() == 16 &&
                               reader->bits_per_sample() == 16 && reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 16; ++y) {
                    std::array<uint16_t, 17> raw, normalized;
                    raw.fill(0xa5a5);
                    normalized.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                            y, rust::Slice<uint16_t>(raw.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                            y, rust::Slice<uint16_t>(normalized.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    std::array<uint16_t, 50> output;
                    output.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                            y, rust::Slice<uint16_t>(output.data(), 48)) ==
                            rust_raw::DecodeStatus::Success);
                    for (size_t x = 0; x < 16; ++x) {
                        REPORTER_ASSERT(r, raw[x] == uniform_bayer_sample(x, y) &&
                                           normalized[x] == raw[x]);
                        REPORTER_ASSERT(r, output[x * 3] == 10000 &&
                                           output[x * 3 + 1] == 20000 &&
                                           output[x * 3 + 2] == 30000);
                    }
                    REPORTER_ASSERT(r, raw[16] == 0xa5a5 && normalized[16] == 0xa5a5);
                    REPORTER_ASSERT(r, output[48] == 0xa5a5 && output[49] == 0xa5a5);
                }
            }
            std::array<uint16_t, 47> wrong;
            wrong.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                    0, rust::Slice<uint16_t>(wrong.data(), wrong.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(wrong.begin(), wrong.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            std::array<uint16_t, 16> mono;
            mono.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_row(
                    0, rust::Slice<uint16_t>(mono.data(), mono.size())) ==
                    rust_raw::DecodeStatus::Unsupported);
            REPORTER_ASSERT(r, std::all_of(mono.begin(), mono.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_BayerSof3Tiles, r) {
    for (bool bigEndian : {false, true}) {
        for (bool uniform : {false, true}) {
            auto bytes = make_tiled_bayer_dng(bigEndian, uniform);
            REPORTER_ASSERT(r, !bytes.empty());
            if (bytes.empty()) {
                continue;
            }
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() ==
                                       (uniform ? rust_raw::DecodeStatus::Success :
                                                  rust_raw::DecodeStatus::Unsupported));
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                continue;
            }
            REPORTER_ASSERT(r, reader->width() == 16 && reader->height() == 16 &&
                               reader->bits_per_sample() == 16 && reader->channels() == 1 &&
                               reader->main_ifd_index() == 0);
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (uint32_t y = 0; y < 16; ++y) {
                    std::array<uint16_t, 18> raw, normalized;
                    raw.fill(0xa5a5);
                    normalized.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                            y, rust::Slice<uint16_t>(raw.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                            y, rust::Slice<uint16_t>(normalized.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    for (uint32_t x = 0; x < 16; ++x) {
                        const uint16_t value = uniform ? uniform_bayer_sample(x, y) :
                                                         bayer_sample(x, y);
                        const uint32_t black = uniform ? 0 :
                                (y & 1) ? ((x & 1) ? 1024 : 512) :
                                          ((x & 1) ? 512 : 256);
                        const uint32_t expected = uniform ? value :
                                value <= black ? 0 :
                                std::min<uint32_t>(
                                        65535, (uint32_t(value - black) * 65535 + 3071 / 2) /
                                                       3071);
                        REPORTER_ASSERT(r, raw[x] == value && normalized[x] == expected);
                    }
                    REPORTER_ASSERT(r, raw[16] == 0xa5a5 && raw[17] == 0xa5a5 &&
                                       normalized[16] == 0xa5a5 &&
                                       normalized[17] == 0xa5a5);
                }
            }
            std::array<uint16_t, 16> untouched;
            untouched.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                    16, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                    0, rust::Slice<uint16_t>(untouched.data(), 15)) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                           [](uint16_t sample) { return sample == 0xa5a5; }));
            std::array<uint16_t, 48> noDemosaic;
            noDemosaic.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                    0, rust::Slice<uint16_t>(noDemosaic.data(), noDemosaic.size())) ==
                    (uniform ? rust_raw::DecodeStatus::Success :
                               rust_raw::DecodeStatus::Unsupported));
            if (uniform) {
                for (size_t x = 0; x < 16; ++x) {
                    REPORTER_ASSERT(r, noDemosaic[3 * x] == 10000 &&
                                       noDemosaic[3 * x + 1] == 20000 &&
                                       noDemosaic[3 * x + 2] == 30000);
                }
            } else {
                REPORTER_ASSERT(r, std::all_of(
                        noDemosaic.begin(), noDemosaic.end(),
                        [](uint16_t sample) { return sample == 0xa5a5; }));
            }
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_BayerVariedStage3, r) {
    const struct {
        uint32_t x;
        uint32_t y;
        uint16_t rgb[3];
    } expected[] = {
            {0, 0, {256, 1102, 1729}},
            {2, 0, {0, 2035, 1766}},
            {4, 0, {1248, 2265, 1840}},
            {0, 2, {2284, 2303, 2321}},
            {15, 3, {2044, 2738, 3431}},
            {7, 7, {2803, 2803, 2803}},
            {0, 14, {1288, 1982, 2675}},
            {1, 15, {1325, 1621, 1917}},
            {15, 15, {1806, 2121, 2435}},
    };
    for (bool tiled : {false, true}) {
        for (bool bigEndian : {false, true}) {
            auto bytes = make_identity_bayer_dng(bigEndian, tiled);
            REPORTER_ASSERT(r, !bytes.empty());
            if (bytes.empty()) {
                continue;
            }
            auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
            auto stream = SkMemoryStream::Make(data);
            auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
            auto reader = rust_raw::new_reader(std::move(adapter));
            stream.reset();
            data.reset();
            REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
            for (const auto& sample : expected) {
                std::array<uint16_t, 50> row;
                row.fill(0xa5a5);
                REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                        sample.y, rust::Slice<uint16_t>(row.data(), 48)) ==
                        rust_raw::DecodeStatus::Success);
                for (int channel = 0; channel < 3; ++channel) {
                    REPORTER_ASSERT(r, row[3 * sample.x + channel] == sample.rgb[channel]);
                }
                REPORTER_ASSERT(r, row[48] == 0xa5a5 && row[49] == 0xa5a5);
            }
            std::array<uint16_t, 48> untouched;
            untouched.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                    16, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                    0, rust::Slice<uint16_t>(untouched.data(), 47)) ==
                    rust_raw::DecodeStatus::Invalid);
            REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                           [](uint16_t value) { return value == 0xa5a5; }));
            SkCodec::Result result = SkCodec::kSuccess;
            auto codec = SkRawRustDecoder::Decode(
                    SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
            REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
        }
    }
}

DEF_TEST(RustRaw_DeflateBayerRows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool horizontalPredictor : {false, true}) {
                auto bytes = make_deflate_bayer_dng(
                        bigEndian, multipleStrips, horizontalPredictor);
                REPORTER_ASSERT(r, !bytes.empty());
                if (bytes.empty()) {
                    continue;
                }
                auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                auto stream = SkMemoryStream::Make(data);
                auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                auto reader = rust_raw::new_reader(std::move(adapter));
                stream.reset();
                data.reset();
                REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                    continue;
                }
                REPORTER_ASSERT(r, reader->width() == 16 && reader->height() == 16 &&
                                   reader->channels() == 1 && reader->stage3_channels() == 3 &&
                                   reader->bits_per_sample() == 16 && reader->main_ifd_index() == 0);
                for (uint32_t y = 0; y < 16; ++y) {
                    std::array<uint16_t, 17> first, second;
                    first.fill(0xa5a5);
                    second.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                            y, rust::Slice<uint16_t>(first.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                            y, rust::Slice<uint16_t>(second.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    for (uint32_t x = 0; x < 16; ++x) {
                        REPORTER_ASSERT(r, first[x] == bayer_sample(x, y) &&
                                           second[x] == first[x]);
                    }
                    REPORTER_ASSERT(r, first[16] == 0xa5a5 && second[16] == 0xa5a5);
                }
                std::array<uint16_t, 50> rgb;
                rgb.fill(0xa5a5);
                REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                        0, rust::Slice<uint16_t>(rgb.data(), 48)) ==
                        rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, rgb[0] == 256 && rgb[1] == 1102 && rgb[2] == 1729);
                REPORTER_ASSERT(r, rgb[48] == 0xa5a5 && rgb[49] == 0xa5a5);
                SkCodec::Result result = SkCodec::kSuccess;
                auto codec = SkRawRustDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                SkCodec::Result referenceResult = SkCodec::kInternalError;
                auto reference = SkRawDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
            }
        }
    }
}

DEF_TEST(RustRaw_LinearizedBayerRows, r) {
    for (bool bigEndian : {false, true}) {
        for (bool multipleStrips : {false, true}) {
            for (bool compressed : {false, true}) {
                for (bool shortTable : {false, true}) {
                    auto bytes = make_linearized_bayer_dng(
                            bigEndian, multipleStrips, compressed, shortTable);
                    REPORTER_ASSERT(r, !bytes.empty());
                    if (bytes.empty()) {
                        continue;
                    }
                    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                    auto stream = SkMemoryStream::Make(data);
                    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                    auto reader = rust_raw::new_reader(std::move(adapter));
                    stream.reset();
                    data.reset();
                    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                    if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                        continue;
                    }
                    for (uint32_t y = 0; y < 16; ++y) {
                        std::array<uint16_t, 17> first, second;
                        first.fill(0xa5a5);
                        second.fill(0xa5a5);
                        REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                                y, rust::Slice<uint16_t>(first.data(), 16)) ==
                                rust_raw::DecodeStatus::Success);
                        REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                                y, rust::Slice<uint16_t>(second.data(), 16)) ==
                                rust_raw::DecodeStatus::Success);
                        for (uint32_t x = 0; x < 16; ++x) {
                            REPORTER_ASSERT(r, first[x] == bayer_sample(x, y));
                            const uint16_t expected = shortTable ?
                                    (first[x] == 0 ? 0 : UINT16_MAX) :
                                    static_cast<uint16_t>(std::min(
                                            uint32_t(first[x]) * 2, uint32_t(UINT16_MAX)));
                            REPORTER_ASSERT(r, second[x] == expected);
                        }
                        REPORTER_ASSERT(r, first[16] == 0xa5a5 && second[16] == 0xa5a5);
                    }
                    std::array<uint16_t, 50> rgb;
                    rgb.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                            0, rust::Slice<uint16_t>(rgb.data(), 48)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, rgb[0] == (shortTable ? UINT16_MAX : 512) &&
                                       rgb[48] == 0xa5a5 && rgb[49] == 0xa5a5);
                    SkCodec::Result result = SkCodec::kSuccess;
                    auto codec = SkRawRustDecoder::Decode(
                            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
                    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                    SkCodec::Result referenceResult = SkCodec::kInternalError;
                    auto reference = SkRawDecoder::Decode(
                            SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                    REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
                }
            }
        }
    }
}

DEF_TEST(RustRaw_LinearizedBayerTiles, r) {
    for (bool bigEndian : {false, true}) {
        for (bool uniform : {false, true}) {
            for (bool shortTable : {false, true}) {
                auto bytes = make_linearized_bayer_dng(
                        bigEndian, false, false, shortTable, true, uniform);
                REPORTER_ASSERT(r, !bytes.empty());
                if (bytes.empty()) {
                    continue;
                }
                auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
                auto stream = SkMemoryStream::Make(data);
                auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
                auto reader = rust_raw::new_reader(std::move(adapter));
                stream.reset();
                data.reset();
                REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
                REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
                if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
                    continue;
                }
                std::array<uint16_t, 17> first, second;
                for (uint32_t y = 0; y < 16; ++y) {
                    first.fill(0xa5a5);
                    second.fill(0xa5a5);
                    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                            y, rust::Slice<uint16_t>(first.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                            y, rust::Slice<uint16_t>(second.data(), 16)) ==
                            rust_raw::DecodeStatus::Success);
                    for (uint32_t x = 0; x < 16; ++x) {
                        const uint16_t raw = uniform ? uniform_bayer_sample(x, y) :
                                                       bayer_sample(x, y);
                        const uint16_t linear = shortTable ?
                                (raw == 0 ? 0 : UINT16_MAX) :
                                static_cast<uint16_t>(std::min(
                                        uint32_t(raw) * 2, uint32_t(UINT16_MAX)));
                        REPORTER_ASSERT(r, first[x] == raw && second[x] == linear);
                    }
                    REPORTER_ASSERT(r, first[16] == 0xa5a5 && second[16] == 0xa5a5);
                }
                std::array<uint16_t, 50> rgb;
                rgb.fill(0xa5a5);
                REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                        0, rust::Slice<uint16_t>(rgb.data(), 48)) ==
                        rust_raw::DecodeStatus::Success);
                const uint16_t red = uniform ? (shortTable ? UINT16_MAX : 20000) :
                                               (shortTable ? UINT16_MAX : 512);
                REPORTER_ASSERT(r, rgb[0] == red && rgb[48] == 0xa5a5 &&
                                   rgb[49] == 0xa5a5);
                if (uniform) {
                    const std::array<uint16_t, 3> expected = shortTable ?
                            std::array<uint16_t, 3>{UINT16_MAX, UINT16_MAX, UINT16_MAX} :
                            std::array<uint16_t, 3>{20000, 40000, 60000};
                    for (uint32_t x = 0; x < 16; ++x) {
                        REPORTER_ASSERT(r, rgb[3 * x] == expected[0] &&
                                           rgb[3 * x + 1] == expected[1] &&
                                           rgb[3 * x + 2] == expected[2]);
                    }
                }
#if defined(SK_CODEC_DECODES_RAW_WITH_DNG_SDK)
                SkCodec::Result referenceResult = SkCodec::kInternalError;
                auto reference = SkRawDecoder::Decode(
                        SkData::MakeWithCopy(bytes.data(), bytes.size()), &referenceResult);
                REPORTER_ASSERT(r, reference && referenceResult == SkCodec::kSuccess);
#endif
            }
        }
    }
}

DEF_TEST(RustRaw_LinearizedBayerMalformed, r) {
    const auto good = make_linearized_bayer_dng(false, true, true, false);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t table = entry_for(good, 50712);
    REPORTER_ASSERT(r, table != good.size());
    if (table == good.size()) {
        return;
    }
    auto bytes = good;
    set32(&bytes, table + 4, 1);
    bytes[table + 2] = 4;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 4, 0);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, table + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = good;
    const size_t white = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, white != bytes.size());
    if (white == bytes.size()) {
        return;
    }
    set32(&bytes, white + 8, 4095);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 16> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
}

DEF_TEST(RustRaw_DeflateBayerMalformed, r) {
    const auto good = make_deflate_bayer_dng(false, true, true);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t offsets = entry_for(good, 273);
    const size_t lengths = entry_for(good, 279);
    const size_t predictor = entry_for(good, 317);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size() &&
                       predictor != good.size());
    if (offsets == good.size() || lengths == good.size() || predictor == good.size()) {
        return;
    }
    const size_t offsetTable = read32(good, offsets + 8);
    const size_t lengthTable = read32(good, lengths + 8);
    auto bytes = good;
    set32(&bytes, offsetTable + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    bytes[predictor + 8] = 3;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    bytes = good;
    bytes[predictor + 2] = 4;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    const size_t lastOffset = read32(good, offsetTable + 4);
    const size_t lastLength = read32(good, lengthTable + 4);
    bytes = good;
    bytes[lastOffset + lastLength - 1] ^= 0xff;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(lastLength - 1));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    std::array<uint8_t, 257> expanded{};
    std::vector<uint8_t> overlong(compressBound(expanded.size()));
    uLongf compressedSize = overlong.size();
    REPORTER_ASSERT(r, compress2(overlong.data(), &compressedSize, expanded.data(),
                                 expanded.size(), Z_BEST_SPEED) == Z_OK);
    overlong.resize(compressedSize);
    set32(&bytes, offsetTable + 4, static_cast<uint32_t>(bytes.size()));
    set32(&bytes, lengthTable + 4, static_cast<uint32_t>(overlong.size()));
    bytes.insert(bytes.end(), overlong.begin(), overlong.end());
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    std::array<uint16_t, 16> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Invalid);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));

    bytes = good;
    const size_t black = entry_for(bytes, 50714);
    REPORTER_ASSERT(r, black != bytes.size());
    if (black == bytes.size()) {
        return;
    }
    set32(&bytes, read32(bytes, black + 8), 256);
    data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    stream = SkMemoryStream::Make(data);
    adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));

    bytes = good;
    const size_t white = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, white != bytes.size());
    if (white != bytes.size()) {
        set32(&bytes, white + 8, 4095);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Success);
        data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        stream = SkMemoryStream::Make(data);
        adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
    }
}

DEF_TEST(RustRaw_BayerSof3Malformed, r) {
    const auto good = make_tiled_bayer_dng(false);
    REPORTER_ASSERT(r, !good.empty());
    if (good.empty()) {
        return;
    }
    const size_t offsets = entry_for(good, 324);
    const size_t lengths = entry_for(good, 325);
    REPORTER_ASSERT(r, offsets != good.size() && lengths != good.size());
    if (offsets == good.size() || lengths == good.size()) {
        return;
    }
    auto bytes = good;
    set32(&bytes, read32(bytes, offsets + 8) + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    auto owned = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto source = SkMemoryStream::Make(owned);
    auto streamAdapter = std::make_unique<rust::stream::SkStreamAdapter>(source.get());
    auto rejected = rust_raw::new_reader(std::move(streamAdapter));
    std::array<uint16_t, 16> untouched;
    untouched.fill(0xa5a5);
    REPORTER_ASSERT(r, rejected->read_stage1_bayer_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Incomplete);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
    bytes = good;
    set32(&bytes, lengths + 4, 3);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    bytes[offsets + 2] = 1;
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    bytes = good;
    const size_t secondTile = read32(good, read32(good, offsets + 8) + 4);
    const size_t secondLength = read32(good, read32(good, lengths + 8) + 4);
    set32(&bytes, read32(bytes, lengths + 8) + 4,
          static_cast<uint32_t>(secondLength - 8));
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);
    bytes = good;
    constexpr std::array<uint8_t, 2> kSof3 = {0xff, 0xc3};
    const auto frame = std::search(bytes.begin() + secondTile,
                                   bytes.begin() + secondTile + secondLength,
                                   kSof3.begin(), kSof3.end());
    REPORTER_ASSERT(r, frame != bytes.begin() + secondTile + secondLength);
    if (frame != bytes.begin() + secondTile + secondLength) {
        bytes[static_cast<size_t>(frame - bytes.begin()) + 4] = 8;
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }
    auto stage2_guard = [&](std::vector<uint8_t> input) {
        auto data = SkData::MakeWithCopy(input.data(), input.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        std::array<uint16_t, 16> sentinel;
        sentinel.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                0, rust::Slice<uint16_t>(sentinel.data(), sentinel.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(sentinel.begin(), sentinel.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    };
    bytes = good;
    const size_t crop = entry_for(bytes, 50719);
    REPORTER_ASSERT(r, crop != bytes.size());
    if (crop != bytes.size()) {
        set32(&bytes, read32(bytes, crop + 8), 1);
        stage2_guard(bytes);
    }
    bytes = good;
    const size_t matrix = entry_for(bytes, 50721);
    REPORTER_ASSERT(r, matrix != bytes.size());
    if (matrix == bytes.size()) {
        return;
    }
    bytes[matrix] = 51009 & 0xff;
    bytes[matrix + 1] = 51009 >> 8;
    bytes[matrix + 2] = 7;
    set32(&bytes, matrix + 4, 4);
    stage2_guard(bytes);

    bytes = make_tiled_bayer_dng(false, true);
    const size_t uniformMatrix = entry_for(bytes, 50721);
    REPORTER_ASSERT(r, uniformMatrix != bytes.size());
    if (uniformMatrix != bytes.size()) {
        bytes[uniformMatrix] = 51022 & 0xff;
        bytes[uniformMatrix + 1] = 51022 >> 8;
        bytes[uniformMatrix + 2] = 7;
        set32(&bytes, uniformMatrix + 4, 4);
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        std::array<uint16_t, 48> sentinel;
        sentinel.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                0, rust::Slice<uint16_t>(sentinel.data(), sentinel.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(sentinel.begin(), sentinel.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    }
}

DEF_TEST(RustRaw_BayerStage3Guard, r) {
    auto check = [&](std::vector<uint8_t> bytes) {
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint16_t, 48> sentinel;
        sentinel.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage3_bayer_rgb_row(
                0, rust::Slice<uint16_t>(sentinel.data(), sentinel.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(sentinel.begin(), sentinel.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    };

    auto bytes = make_rggb_dng(false, true, true);
    const size_t matrix = entry_for(bytes, 50721);
    REPORTER_ASSERT(r, matrix != bytes.size());
    if (matrix != bytes.size()) {
        bytes[matrix] = 50739 & 0xff;
        bytes[matrix + 1] = 50739 >> 8;
        bytes[matrix + 2] = 5;
        set32(&bytes, matrix + 4, 1);
        check(bytes);
    }

    bytes = make_rggb_dng(false, true, true);
    const size_t illuminant = entry_for(bytes, 50778);
    REPORTER_ASSERT(r, illuminant != bytes.size());
    if (illuminant != bytes.size()) {
        bytes[illuminant] = 51022 & 0xff;
        bytes[illuminant + 1] = 51022 >> 8;
        bytes[illuminant + 2] = 7;
        bytes[illuminant + 3] = 0;
        check(bytes); // Stage-3 opcodes cannot be ignored even for a uniform field.
    }
}

DEF_TEST(RustRaw_BayerMalformed, r) {
    auto bytes = make_rggb_dng(false, true);
    bytes.pop_back();
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_rggb_dng(false, true);
    const size_t offsetsTag = entry_for(bytes, 273);
    const size_t countsTag = entry_for(bytes, 279);
    REPORTER_ASSERT(r, offsetsTag != bytes.size() && countsTag != bytes.size());
    if (offsetsTag == bytes.size() || countsTag == bytes.size()) {
        return;
    }
    set32(&bytes, read32(bytes, offsetsTag + 8) + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_rggb_dng(false, true);
    set32(&bytes, read32(bytes, countsTag + 8) + 4, 255);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_rggb_dng(false, true);
    const size_t pattern = entry_for(bytes, 33422);
    REPORTER_ASSERT(r, pattern != bytes.size());
    if (pattern != bytes.size()) {
        bytes[pattern + 11] = 0;
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }

    auto checkStage2Unsupported = [&](std::vector<uint8_t> altered) {
        auto owned = SkData::MakeWithCopy(altered.data(), altered.size());
        auto stream = SkMemoryStream::Make(owned);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint16_t, 16> raw{};
        REPORTER_ASSERT(r, reader->read_stage1_bayer_row(
                0, rust::Slice<uint16_t>(raw.data(), raw.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, raw[0] == 256 && raw[1] == 512 && raw[3] == 4095);
        std::array<uint16_t, 16> untouched;
        untouched.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_bayer_row(
                0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    };

    bytes = make_rggb_dng(false, true);
    const size_t crop = entry_for(bytes, 50719);
    REPORTER_ASSERT(r, crop != bytes.size());
    if (crop != bytes.size()) {
        set32(&bytes, read32(bytes, crop + 8), 1);
        checkStage2Unsupported(bytes);
    }

    bytes = make_rggb_dng(false, true);
    const size_t matrix = entry_for(bytes, 50721);
    REPORTER_ASSERT(r, matrix != bytes.size());
    if (matrix != bytes.size()) {
        bytes[matrix] = 51009 & 0xff;
        bytes[matrix + 1] = 51009 >> 8;
        bytes[matrix + 2] = 7; // OpcodeList2 is not implemented for this CFA.
        set32(&bytes, matrix + 4, 4);
        checkStage2Unsupported(bytes);
    }
}

DEF_TEST(RustRaw_RealDngStage1, r) {
    for (const char* name : {"images/sample_1mp.dng", "images/sample_1mp_rotated.dng",
                             "images/dng_with_preview.dng"}) {
        skiatest::ReporterContext context(r, name);
        auto data = GetResourceAsData(name);
        REPORTER_ASSERT(r, data);
        if (!data) {
            continue;
        }
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            continue;
        }
        REPORTER_ASSERT(r, reader->width() == 600 && reader->height() == 338);
        REPORTER_ASSERT(r, reader->channels() == 3 && reader->bits_per_sample() == 8);
        REPORTER_ASSERT(r, reader->main_ifd_index() == 1);
        std::array<uint16_t, 600> uncheckedStage2;
        uncheckedStage2.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_normalized_row(
                0, rust::Slice<uint16_t>(uncheckedStage2.data(), uncheckedStage2.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->read_stage1_row(
                0, rust::Slice<uint16_t>(uncheckedStage2.data(), uncheckedStage2.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(uncheckedStage2.begin(), uncheckedStage2.end(),
                                       [](uint16_t sample) { return sample == 0xa5a5; }));
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t y = 0; y < 338; ++y) {
            std::array<uint8_t, 1804> row;
            row.fill(0xa5);
            REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                    y, rust::Slice<uint8_t>(row.data(), 1800)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, row[1800] == 0xa5 && row[1803] == 0xa5);
            for (size_t x = 0; x < 1800; ++x) {
                hash = (hash ^ row[x]) * 1099511628211ULL;
            }
            if (y == 0) {
                REPORTER_ASSERT(r, row[0] == 112 && row[1] == 121 && row[2] == 126);
                REPORTER_ASSERT(r, row[912] == 129 && row[913] == 137 && row[914] == 140);
            }
            if (y == 337) {
                REPORTER_ASSERT(r, row[1797] == 25 && row[1798] == 32 && row[1799] == 24);
            }
        }
        REPORTER_ASSERT(r, hash == 0x4f84fbd56e0d13e0ULL,
                        "Stage1 RGB fingerprint mismatch: 0x%016llx",
                        static_cast<unsigned long long>(hash));
        SkCodec::Result result = SkCodec::kSuccess;
        auto codec = SkRawRustDecoder::Decode(data, &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
    }
}

DEF_TEST(RustRaw_RealDngStage2, r) {
    for (const char* name : {"images/sample_1mp.dng", "images/sample_1mp_rotated.dng",
                             "images/dng_with_preview.dng"}) {
        skiatest::ReporterContext context(r, name);
        auto data = GetResourceAsData(name);
        REPORTER_ASSERT(r, data);
        if (!data) {
            continue;
        }
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage2_status() != rust_raw::DecodeStatus::Success) {
            continue;
        }
        REPORTER_ASSERT(r, reader->width() == 600 && reader->height() == 338 &&
                           reader->channels() == 3 && reader->bits_per_sample() == 8 &&
                           reader->main_ifd_index() == 1);
        std::array<uint8_t, 1800> original{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                50, rust::Slice<uint8_t>(original.data(), original.size())) ==
                rust_raw::DecodeStatus::Success);
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t y = 0; y < 338; ++y) {
            std::array<uint16_t, 1802> samples;
            samples.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    y, rust::Slice<uint16_t>(samples.data(), 1800)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, samples[1800] == 0xa5a5 && samples[1801] == 0xa5a5);
            for (size_t i = 0; i < 1800; ++i) {
                hash = (hash ^ (samples[i] & 0xff)) * 1099511628211ULL;
                hash = (hash ^ (samples[i] >> 8)) * 1099511628211ULL;
            }
            if (y == 0) {
                REPORTER_ASSERT(r, samples[0] == 3604 && samples[1] == 8659 &&
                                   samples[2] == 6823);
                REPORTER_ASSERT(r, samples[912] == 5143 && samples[913] == 11864 &&
                                   samples[914] == 8945);
            }
            if (y == 337) {
                REPORTER_ASSERT(r, samples[1797] == 272 && samples[1798] == 825 &&
                                   samples[1799] == 414);
            }
        }
        REPORTER_ASSERT(r, hash == 0xfa8bbaf99820f4a5ULL,
                        "Stage2 u16 fingerprint mismatch: 0x%016llx",
                        static_cast<unsigned long long>(hash));
        std::array<uint8_t, 1800> after{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                50, rust::Slice<uint8_t>(after.data(), after.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, original == after);
    }
}

DEF_TEST(RustRaw_RealDngStage3Identity, r) {
    for (const char* name : {"images/sample_1mp.dng", "images/sample_1mp_rotated.dng",
                             "images/dng_with_preview.dng"}) {
        skiatest::ReporterContext context(r, name);
        auto data = GetResourceAsData(name);
        REPORTER_ASSERT(r, data);
        if (!data) {
            continue;
        }
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage3_status() != rust_raw::DecodeStatus::Success) {
            continue;
        }
        REPORTER_ASSERT(r, reader->width() == 600 && reader->height() == 338 &&
                           reader->channels() == 3 && reader->main_ifd_index() == 1);
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t y = 0; y < 338; ++y) {
            std::array<uint16_t, 1800> stage2{};
            std::array<uint16_t, 1802> stage3;
            stage3.fill(0xa5a5);
            REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                    y, rust::Slice<uint16_t>(stage2.data(), stage2.size())) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                    y, rust::Slice<uint16_t>(stage3.data(), 1800)) ==
                    rust_raw::DecodeStatus::Success);
            REPORTER_ASSERT(r, stage3[1800] == 0xa5a5 && stage3[1801] == 0xa5a5);
            for (size_t x = 0; x < 1800; ++x) {
                REPORTER_ASSERT(r, stage3[x] == stage2[x]);
                hash = (hash ^ (stage3[x] & 0xff)) * 1099511628211ULL;
                hash = (hash ^ (stage3[x] >> 8)) * 1099511628211ULL;
            }
        }
        REPORTER_ASSERT(r, hash == 0xfa8bbaf99820f4a5ULL,
                        "Stage3 u16 fingerprint mismatch: 0x%016llx",
                        static_cast<unsigned long long>(hash));
    }
}

DEF_TEST(RustRaw_RealDngFinalOnlyProfileMetadata, r) {
    auto source = GetResourceAsData("images/sample_1mp.dng");
    REPORTER_ASSERT(r, source);
    if (!source) {
        return;
    }
    auto bytes = add_linear_tone_and_black_none(source.get());
    REPORTER_ASSERT(r, !bytes.empty());
    if (bytes.empty()) {
        return;
    }
    auto originalStream = SkMemoryStream::Make(source);
    auto originalAdapter = std::make_unique<rust::stream::SkStreamAdapter>(originalStream.get());
    auto original = rust_raw::new_reader(std::move(originalAdapter));
    auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    stream.reset();
    data.reset();
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    for (uint32_t y = 0; y < 338; ++y) {
        std::array<uint16_t, 1800> before{};
        std::array<uint16_t, 1802> after;
        after.fill(0xa5a5);
        REPORTER_ASSERT(r, original->read_stage3_rgb_row(
                y, rust::Slice<uint16_t>(before.data(), before.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                y, rust::Slice<uint16_t>(after.data(), before.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, std::equal(before.begin(), before.end(), after.begin()));
        REPORTER_ASSERT(r, after[1800] == 0xa5a5 && after[1801] == 0xa5a5);
    }
    auto checkRejected = [&](std::vector<uint8_t> input) {
        auto storage = SkData::MakeWithCopy(input.data(), input.size());
        auto inputStream = SkMemoryStream::Make(storage);
        auto inputAdapter = std::make_unique<rust::stream::SkStreamAdapter>(inputStream.get());
        auto parsed = rust_raw::new_reader(std::move(inputAdapter));
        REPORTER_ASSERT(r, parsed->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, parsed->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, parsed->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        std::array<uint16_t, 1800> sentinel;
        sentinel.fill(0xa5a5);
        REPORTER_ASSERT(r, parsed->read_stage3_rgb_row(
                0, rust::Slice<uint16_t>(sentinel.data(), sentinel.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(sentinel.begin(), sentinel.end(),
                                       [](uint16_t value) { return value == 0xa5a5; }));
    };
    const size_t tone = entry_for(bytes, 50940);
    const size_t black = entry_for(bytes, 51110);
    REPORTER_ASSERT(r, tone != bytes.size() && black != bytes.size());
    if (tone != bytes.size() && black != bytes.size()) {
        auto changed = bytes;
        set32(&changed, read32(changed, tone + 8) + 8, 0x7fc00000);
        checkRejected(std::move(changed));
        changed = bytes;
        set32(&changed, black + 8, 2);
        checkRejected(std::move(changed));
    }
}

DEF_TEST(RustRaw_RealDngInvalidOpcodes, r) {
    auto resource = GetResourceAsData("images/sample_1mp.dng");
    REPORTER_ASSERT(r, resource);
    if (!resource) {
        return;
    }
    const std::vector<uint8_t> good(static_cast<const uint8_t*>(resource->data()),
                                    static_cast<const uint8_t*>(resource->data()) +
                                            resource->size());
    const size_t subifds = entry_for(good, 330);
    REPORTER_ASSERT(r, subifds != good.size());
    if (subifds == good.size()) {
        return;
    }
    const size_t rawIfd = read32(good, subifds + 8);
    const size_t opcodeEntry = entry_in_ifd(good, rawIfd, 51009);
    const size_t antiAliasEntry = entry_in_ifd(good, rawIfd, 50738);
    REPORTER_ASSERT(r, opcodeEntry != good.size() && antiAliasEntry != good.size());
    if (opcodeEntry == good.size() || antiAliasEntry == good.size()) {
        return;
    }
    const size_t opcodeData = read32(good, opcodeEntry + 8);
    auto check = [&](std::vector<uint8_t> bytes, rust_raw::DecodeStatus expected) {
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == expected);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            return;
        }
        std::array<uint8_t, 1800> before{}, after{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(before.data(), before.size())) ==
                rust_raw::DecodeStatus::Success);
        std::array<uint16_t, 1800> output;
        output.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                0, rust::Slice<uint16_t>(output.data(), output.size())) == expected);
        REPORTER_ASSERT(r, std::all_of(output.begin(), output.end(),
                                       [](uint16_t sample) { return sample == 0xa5a5; }));
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(after.data(), after.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, before == after);
    };

    auto malformed = good;
    std::memset(malformed.data() + opcodeData, 0xff, 4);
    check(std::move(malformed), rust_raw::DecodeStatus::Invalid);

    auto unknown = good;
    unknown[opcodeData + 4] = 0;
    unknown[opcodeData + 5] = 0;
    unknown[opcodeData + 6] = 0;
    unknown[opcodeData + 7] = 9;
    check(std::move(unknown), rust_raw::DecodeStatus::Unsupported);

    auto wrongPitch = good;
    std::memset(wrongPitch.data() + opcodeData + 44, 0, 4);
    check(std::move(wrongPitch), rust_raw::DecodeStatus::Invalid);

    auto wrongPlane = good;
    wrongPlane[opcodeData + 39] = 3;
    check(std::move(wrongPlane), rust_raw::DecodeStatus::Invalid);

    auto nan = good;
    const uint8_t qnan[] = {0x7f, 0xf8, 0, 0, 0, 0, 0, 0};
    std::memcpy(nan.data() + opcodeData + 56, qnan, sizeof(qnan));
    check(std::move(nan), rust_raw::DecodeStatus::Invalid);

    auto otherProcessing = good;
    set32(&otherProcessing, read32(good, antiAliasEntry + 8), 2);
    check(std::move(otherProcessing), rust_raw::DecodeStatus::Unsupported);
}

DEF_TEST(RustRaw_Stage3OpcodeList3, r) {
    auto resource = GetResourceAsData("images/sample_1mp.dng");
    REPORTER_ASSERT(r, resource);
    if (!resource) {
        return;
    }
    const std::vector<uint8_t> original(static_cast<const uint8_t*>(resource->data()),
                                        static_cast<const uint8_t*>(resource->data()) +
                                                resource->size());
    const size_t subifd = entry_for(original, 330);
    REPORTER_ASSERT(r, subifd != original.size());
    if (subifd == original.size()) {
        return;
    }
    for (bool inRoot : {false, true}) {
        auto bytes = original;
        const size_t ifd = inRoot ? read32(bytes, 4) : read32(bytes, subifd + 8);
        const uint32_t newIfd = append_opcode3_ifd(&bytes, ifd);
        set32(&bytes, inRoot ? 4 : subifd + 8, newIfd);
        auto data = SkData::MakeWithCopy(bytes.data(), bytes.size());
        auto stream = SkMemoryStream::Make(data);
        auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
        auto reader = rust_raw::new_reader(std::move(adapter));
        REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
        if (reader->stage1_status() != rust_raw::DecodeStatus::Success) {
            continue;
        }
        std::array<uint8_t, 1800> before{}, after{};
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(before.data(), before.size())) ==
                rust_raw::DecodeStatus::Success);
        std::array<uint16_t, 1800> stage2{};
        REPORTER_ASSERT(r, reader->read_stage2_rgb_row(
                0, rust::Slice<uint16_t>(stage2.data(), stage2.size())) ==
                rust_raw::DecodeStatus::Success);
        std::array<uint16_t, 1800> untouched;
        untouched.fill(0xa5a5);
        REPORTER_ASSERT(r, reader->read_stage3_rgb_row(
                0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
                rust_raw::DecodeStatus::Unsupported);
        REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                       [](uint16_t sample) { return sample == 0xa5a5; }));
        REPORTER_ASSERT(r, reader->read_stage1_rgb_row(
                0, rust::Slice<uint8_t>(after.data(), after.size())) ==
                rust_raw::DecodeStatus::Success);
        REPORTER_ASSERT(r, before == after);
    }

    auto mono = make_dng(1, 34892, false, false, 255, 1, true);
    auto data = SkData::MakeWithCopy(mono.data(), mono.size());
    auto stream = SkMemoryStream::Make(data);
    auto adapter = std::make_unique<rust::stream::SkStreamAdapter>(stream.get());
    auto reader = rust_raw::new_reader(std::move(adapter));
    REPORTER_ASSERT(r, reader->stage1_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage2_status() == rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, reader->stage3_status() == rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, reader->status() == rust_raw::DecodeStatus::Unsupported);
    std::array<uint16_t, 3> stage2{};
    REPORTER_ASSERT(r, reader->read_normalized_row(
            0, rust::Slice<uint16_t>(stage2.data(), stage2.size())) ==
            rust_raw::DecodeStatus::Success);
    REPORTER_ASSERT(r, stage2[0] == 0 && stage2[1] == 65535 && stage2[2] == 0);
    std::array<uint16_t, 3> untouched = {0xa5a5, 0xa5a5, 0xa5a5};
    REPORTER_ASSERT(r, reader->read_stage3_row(
            0, rust::Slice<uint16_t>(untouched.data(), untouched.size())) ==
            rust_raw::DecodeStatus::Unsupported);
    REPORTER_ASSERT(r, std::all_of(untouched.begin(), untouched.end(),
                                   [](uint16_t sample) { return sample == 0xa5a5; }));
}

DEF_TEST(RustRaw_RealDngCorruptTiles, r) {
    auto resource = GetResourceAsData("images/sample_1mp.dng");
    REPORTER_ASSERT(r, resource);
    if (!resource) {
        return;
    }
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(resource->data()),
                               static_cast<const uint8_t*>(resource->data()) + resource->size());
    const size_t subifds = entry_for(bytes, 330);
    REPORTER_ASSERT(r, subifds != bytes.size());
    if (subifds == bytes.size()) {
        return;
    }
    const size_t rawIfd = read32(bytes, subifds + 8);
    const size_t offsetsEntry = entry_in_ifd(bytes, rawIfd, 324);
    const size_t sizesEntry = entry_in_ifd(bytes, rawIfd, 325);
    const size_t opcodeEntry = entry_in_ifd(bytes, rawIfd, 51009);
    REPORTER_ASSERT(r, offsetsEntry != bytes.size() &&
                       sizesEntry != bytes.size() && opcodeEntry != bytes.size());
    if (offsetsEntry == bytes.size() || sizesEntry == bytes.size() ||
        opcodeEntry == bytes.size()) {
        return;
    }
    const size_t offsets = read32(bytes, offsetsEntry + 8);
    auto invalid = bytes;
    set32(&invalid, offsets + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Incomplete);

    invalid = bytes;
    set32(&invalid, sizesEntry + 4, 1);
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Invalid);

    invalid = bytes;
    invalid[read32(bytes, offsets)] = 0;
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Invalid);

    invalid = bytes;
    invalid[opcodeEntry] = 51008 & 0xff;
    invalid[opcodeEntry + 1] = 51008 >> 8;
    REPORTER_ASSERT(r, stage1_status(invalid) == rust_raw::DecodeStatus::Unsupported);
}

DEF_TEST(RustRaw_TinyImageHugeTile, r) {
    auto resource = GetResourceAsData("images/sample_1mp.dng");
    REPORTER_ASSERT(r, resource);
    if (!resource) {
        return;
    }
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(resource->data()),
                               static_cast<const uint8_t*>(resource->data()) + resource->size());
    const size_t subifd = entry_for(bytes, 330);
    REPORTER_ASSERT(r, subifd != bytes.size());
    if (subifd == bytes.size()) {
        return;
    }
    const size_t raw = read32(bytes, subifd + 8);
    const size_t width = entry_in_ifd(bytes, raw, 256);
    const size_t height = entry_in_ifd(bytes, raw, 257);
    const size_t tileWidth = entry_in_ifd(bytes, raw, 322);
    const size_t tileHeight = entry_in_ifd(bytes, raw, 323);
    const size_t offsets = entry_in_ifd(bytes, raw, 324);
    const size_t lengths = entry_in_ifd(bytes, raw, 325);
    const size_t crop = entry_in_ifd(bytes, raw, 50720);
    const size_t active = entry_in_ifd(bytes, raw, 50829);
    REPORTER_ASSERT(r, width != bytes.size() && height != bytes.size() &&
                       tileWidth != bytes.size() && tileHeight != bytes.size() &&
                       offsets != bytes.size() && lengths != bytes.size() &&
                       crop != bytes.size() && active != bytes.size());
    if (width == bytes.size() || height == bytes.size() ||
        tileWidth == bytes.size() || tileHeight == bytes.size() ||
        offsets == bytes.size() || lengths == bytes.size() ||
        crop == bytes.size() || active == bytes.size()) {
        return;
    }
    const uint32_t firstTile = read32(bytes, read32(bytes, offsets + 8));
    set32(&bytes, width + 8, 1);
    set32(&bytes, height + 8, 1);
    const size_t cropValues = read32(bytes, crop + 8);
    set32(&bytes, cropValues, 1);
    set32(&bytes, cropValues + 8, 1);
    const size_t activeValues = read32(bytes, active + 8);
    set32(&bytes, activeValues + 8, 1);
    set32(&bytes, activeValues + 12, 1);
    set32(&bytes, offsets + 4, 1);
    set32(&bytes, lengths + 4, 1);
    set32(&bytes, offsets + 8, UINT32_MAX);
    set32(&bytes, lengths + 8, 1);
    set32(&bytes, tileWidth + 8, 300000);
    set32(&bytes, tileHeight + 8, 300000);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);

    set32(&bytes, tileWidth + 8, 65535);
    set32(&bytes, tileHeight + 8, 65535);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    set32(&bytes, offsets + 8, firstTile);
    set32(&bytes, lengths + 8, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    // A JPEG with only one byte cannot authorize allocating 65535*65535 RGB
    // pixels. The first pass must reject it using row-sized decode storage.
    set32(&bytes, width + 8, 65535);
    set32(&bytes, height + 8, 65535);
    set32(&bytes, cropValues, 65535);
    set32(&bytes, cropValues + 8, 65535);
    set32(&bytes, activeValues + 8, 65535);
    set32(&bytes, activeValues + 12, 65535);
    set32(&bytes, lengths + 8, 1);
    const auto shortJpeg = stage1_status(bytes);
    REPORTER_ASSERT(r, shortJpeg == rust_raw::DecodeStatus::Invalid ||
                       shortJpeg == rust_raw::DecodeStatus::Incomplete ||
                       shortJpeg == rust_raw::DecodeStatus::Unsupported);
}

DEF_TEST(RustRaw_Stage1MalformedStrips, r) {
    auto bytes = make_stage1_dng(false, 16);
    bytes.pop_back();
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_stage1_dng(false, 16);
    const size_t offsetsEntry = entry_for(bytes, 273);
    const size_t sizesEntry = entry_for(bytes, 279);
    REPORTER_ASSERT(r, offsetsEntry != bytes.size() && sizesEntry != bytes.size());
    if (offsetsEntry == bytes.size() || sizesEntry == bytes.size()) {
        return;
    }
    const size_t offsets = read32(bytes, offsetsEntry + 8);
    set32(&bytes, offsets + 4, UINT32_MAX);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Incomplete);

    bytes = make_stage1_dng(false, 16);
    const size_t sizes = read32(bytes, sizesEntry + 8);
    set32(&bytes, sizes + 4, 5);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_stage1_dng(false, 16);
    set32(&bytes, offsetsEntry + 4, 1);
    REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);

    bytes = make_stage1_dng(false, 16);
    set32(&bytes, offsetsEntry + 4, UINT32_MAX);
    const auto oversizedCount = stage1_status(bytes);
    REPORTER_ASSERT(r, oversizedCount == rust_raw::DecodeStatus::Invalid ||
                       oversizedCount == rust_raw::DecodeStatus::Incomplete);

    bytes = make_stage1_dng(false, 16);
    const size_t rowsEntry = entry_for(bytes, 278);
    REPORTER_ASSERT(r, rowsEntry != bytes.size());
    if (rowsEntry != bytes.size()) {
        set32(&bytes, rowsEntry + 8, 0);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    }

    bytes = make_stage1_dng(false, 16);
    const size_t repeatEntry = entry_for(bytes, 50713);
    REPORTER_ASSERT(r, repeatEntry != bytes.size());
    if (repeatEntry != bytes.size()) {
        bytes[repeatEntry + 8] = 9;  // SDK maximum repeat dimension is 8.
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Unsupported);
    }

    bytes = make_stage1_dng(false, 16);
    const size_t blackEntry = entry_for(bytes, 50714);
    REPORTER_ASSERT(r, blackEntry != bytes.size());
    if (blackEntry != bytes.size()) {
        set32(&bytes, blackEntry + 4, 1);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    }

    bytes = make_stage1_dng(false, 16);
    const size_t whiteEntry = entry_for(bytes, 50717);
    REPORTER_ASSERT(r, whiteEntry != bytes.size());
    if (whiteEntry != bytes.size()) {
        set32(&bytes, whiteEntry + 8, 65536);
        REPORTER_ASSERT(r, stage1_status(bytes) == rust_raw::DecodeStatus::Invalid);
    }
}

DEF_TEST(RustRaw_UnsupportedAndMalformed, r) {
    for (auto bytes : {make_dng(7), make_dng(1, 32803), make_dng(1, 1),
                       make_dng(1, 34892, true), make_dng(1, 34892, false, true),
                       make_dng(1, 34892, false, false, 128),
                       make_dng(1, 34892, false, false, 255, 3)}) {
        SkCodec::Result result = SkCodec::kInternalError;
        auto codec = SkRawRustDecoder::Decode(
                SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
        REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);
    }
    auto truncated = make_dng();
    truncated.pop_back();
    SkCodec::Result result = SkCodec::kInternalError;
    auto codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(truncated.data(), truncated.size()), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kIncompleteInput);

    auto bytes = make_dng();
    // A strip offset past EOF must not be dereferenced.
    const size_t stripEntry = entry_for(bytes, 273);
    REPORTER_ASSERT(r, stripEntry != bytes.size());
    if (stripEntry != bytes.size()) {
        std::memset(bytes.data() + stripEntry + 8, 0xff, 4);
    }
    codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kIncompleteInput);

    bytes = make_dng();
    bytes[2] = 43;  // BigTIFF is not this classic TIFF profile.
    REPORTER_ASSERT(r, !SkRawRustDecoder::IsDng(bytes.data(), bytes.size()));
    codec = SkRawRustDecoder::Decode(
            SkData::MakeWithCopy(bytes.data(), bytes.size()), &result);
    REPORTER_ASSERT(r, !codec && result == SkCodec::kUnimplemented);

    auto valid = make_dng();
    auto data = SkData::MakeWithCopy(valid.data(), valid.size());
    REPORTER_ASSERT(r, !SkRawRustDecoder::IsDng(nullptr, valid.size()));
    codec = SkRawRustDecoder::Decode(std::make_unique<NonseekableStream>(data), &result);
    REPORTER_ASSERT(r, codec && result == SkCodec::kSuccess);
    if (codec) {
        assert_rows(r, codec.get(), 255);
    }
}
