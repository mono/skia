/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBBHFactory.h"
#include "include/core/SkData.h"
#include "include/core/SkDrawable.h"
#include "include/core/SkImage.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkShader.h"
#include "include/encode/SkPngEncoder.h"

#include "include/c/sk_picture.h"

#include "src/c/sk_types_priv.h"

#include <optional>

namespace {

// [mono/skia fork patch] Skia m147's default {Ser,Deser}ialProcs leave both
// fImageProc fields null, and serialize_image()/deserialize_image() return
// nullptr if no encoder/decoder is supplied — silently dropping every raster
// image drawn into the picture (e.g. via DrawBitmap). Earlier Skia versions
// had a built-in PNG fallback on both sides. Restore that behaviour at the C
// API boundary so SKPicture.Serialize/Deserialize stays lossless for managed
// callers.
SkSerialProcs default_serial_procs() {
    SkSerialProcs procs;
    procs.fImageProc = [](SkImage* img, void*) -> sk_sp<const SkData> {
        return SkPngEncoder::Encode(nullptr, img, SkPngEncoder::Options{});
    };
    return procs;
}

SkDeserialProcs default_deserial_procs() {
    SkDeserialProcs procs;
    procs.fImageProc = [](const void* data, size_t length, std::optional<SkAlphaType>, void*) -> sk_sp<SkImage> {
        return SkImages::DeferredFromEncodedData(SkData::MakeWithCopy(data, length));
    };
    return procs;
}

}  // namespace

// SkPictureRecorder

sk_picture_recorder_t* sk_picture_recorder_new(void) {
    return ToPictureRecorder(new SkPictureRecorder);
}

void sk_picture_recorder_delete(sk_picture_recorder_t* crec) {
    delete AsPictureRecorder(crec);
}

sk_canvas_t* sk_picture_recorder_begin_recording(sk_picture_recorder_t* crec, const sk_rect_t* cbounds) {
    return ToCanvas(AsPictureRecorder(crec)->beginRecording(*AsRect(cbounds)));
}

sk_canvas_t* sk_picture_recorder_begin_recording_with_bbh_factory(sk_picture_recorder_t* crec, const sk_rect_t* cbounds, sk_bbh_factory_t* factory) {
    return ToCanvas(AsPictureRecorder(crec)->beginRecording(*AsRect(cbounds), AsBBHFactory(factory)));
}

sk_picture_t* sk_picture_recorder_end_recording(sk_picture_recorder_t* crec) {
    return ToPicture(AsPictureRecorder(crec)->finishRecordingAsPicture().release());
}

sk_drawable_t* sk_picture_recorder_end_recording_as_drawable(sk_picture_recorder_t* crec) {
    return ToDrawable(AsPictureRecorder(crec)->finishRecordingAsDrawable().release());
}

sk_canvas_t* sk_picture_get_recording_canvas(sk_picture_recorder_t* crec) {
    return ToCanvas(AsPictureRecorder(crec)->getRecordingCanvas());
}

// SkPicture

void sk_picture_ref(sk_picture_t* cpic) {
    SkSafeRef(AsPicture(cpic));
}

void sk_picture_unref(sk_picture_t* cpic) {
    SkSafeUnref(AsPicture(cpic));
}

uint32_t sk_picture_get_unique_id(sk_picture_t* cpic) {
    return AsPicture(cpic)->uniqueID();
}

void sk_picture_get_cull_rect(sk_picture_t* cpic, sk_rect_t* crect) {
    *crect = ToRect(AsPicture(cpic)->cullRect());
}

sk_shader_t* sk_picture_make_shader(sk_picture_t* src, sk_shader_tilemode_t tmx, sk_shader_tilemode_t tmy, sk_filter_mode_t mode, const sk_matrix_t* localMatrix, const sk_rect_t* tile) {
    SkMatrix m;
    if (localMatrix) {
        m = AsMatrix(localMatrix);
    }
    return ToShader(AsPicture(src)->makeShader((SkTileMode)tmx, (SkTileMode)tmy, (SkFilterMode)mode, localMatrix ? &m : nullptr, AsRect(tile)).release());
}

sk_data_t* sk_picture_serialize_to_data(const sk_picture_t* picture) {
    SkSerialProcs procs = default_serial_procs();
    return ToData(AsPicture(picture)->serialize(&procs).release());
}

void sk_picture_serialize_to_stream(const sk_picture_t* picture, sk_wstream_t* stream) {
    SkSerialProcs procs = default_serial_procs();
    AsPicture(picture)->serialize(AsWStream(stream), &procs);
}

sk_picture_t* sk_picture_deserialize_from_stream(sk_stream_t* stream) {
    SkDeserialProcs procs = default_deserial_procs();
    return ToPicture(SkPicture::MakeFromStream(AsStream(stream), &procs).release());
}

sk_picture_t* sk_picture_deserialize_from_data(sk_data_t* data) {
    SkDeserialProcs procs = default_deserial_procs();
    return ToPicture(SkPicture::MakeFromData(AsData(data), &procs).release());
}

sk_picture_t* sk_picture_deserialize_from_memory(void* buffer, size_t length) {
    SkDeserialProcs procs = default_deserial_procs();
    return ToPicture(SkPicture::MakeFromData(buffer, length, &procs).release());
}

void sk_picture_playback(const sk_picture_t* picture, sk_canvas_t* canvas) {
    AsPicture(picture)->playback(AsCanvas(canvas));
}

int sk_picture_approximate_op_count(const sk_picture_t* picture, bool nested) {
    return AsPicture(picture)->approximateOpCount(nested);
}

size_t sk_picture_approximate_bytes_used(const sk_picture_t* picture) {
    return AsPicture(picture)->approximateBytesUsed();
}

// SkRTreeFactory

sk_rtree_factory_t* sk_rtree_factory_new(void) {
    return ToRTreeFactory(new SkRTreeFactory);
}

void sk_rtree_factory_delete(sk_rtree_factory_t* factory) {
    delete AsRTreeFactory(factory);
}
