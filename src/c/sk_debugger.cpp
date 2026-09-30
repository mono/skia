/*
 * Copyright 2024 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/sk_debugger.h"

#include "include/core/SkData.h"
#include "include/core/SkPicture.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "src/utils/SkJSONWriter.h"
#include "tools/UrlDataManager.h"
#include "tools/debugger/DebugCanvas.h"
#include "tools/debugger/DrawCommand.h"

#include "src/c/sk_types_priv.h"

sk_debug_canvas_t* sk_debug_canvas_new(int width, int height) {
    return ToDebugCanvas(new DebugCanvas(width, height));
}

void sk_debug_canvas_destroy(sk_debug_canvas_t* canvas) {
    if (canvas) {
        delete AsDebugCanvas(canvas);
    }
}

int sk_debug_canvas_load_skp(sk_debug_canvas_t* canvas, const void* data, size_t length) {
    if (!canvas || !data || length == 0) {
        return -1;
    }

    // Clear existing commands first so a failed load leaves a clean state
    DebugCanvas* dc = AsDebugCanvas(canvas);
    SkTDArray<DrawCommand*> oldCmds;
    dc->detachCommands(&oldCmds);
    for (int i = 0; i < oldCmds.size(); i++) {
        delete oldCmds[i];
    }

    auto skData = SkData::MakeWithoutCopy(data, length);
    auto stream = SkMemoryStream::Make(skData);

    auto picture = SkPicture::MakeFromStream(stream.get());
    if (!picture) {
        return -1;
    }

    // Play the picture into the DebugCanvas to record all commands
    picture->playback(dc);

    return dc->getSize();
}

int sk_debug_canvas_get_command_count(const sk_debug_canvas_t* canvas) {
    if (!canvas) return 0;
    return AsDebugCanvas(canvas)->getSize();
}

void sk_debug_canvas_draw(sk_debug_canvas_t* canvas, sk_canvas_t* target) {
    if (!canvas || !target) return;
    AsDebugCanvas(canvas)->draw(AsCanvas(target));
}

void sk_debug_canvas_draw_to(sk_debug_canvas_t* canvas, sk_canvas_t* target, int index) {
    if (!canvas || !target) return;
    DebugCanvas* dc = AsDebugCanvas(canvas);
    if (index < 0) index = 0;
    if (index >= dc->getSize()) index = dc->getSize() - 1;
    if (dc->getSize() > 0) {
        dc->drawTo(AsCanvas(target), index);
    }
}

void sk_debug_canvas_get_command_list_json(sk_debug_canvas_t* canvas, sk_canvas_t* target, sk_string_t* result) {
    if (!canvas || !target || !result) return;
    SkDynamicMemoryWStream stream;
    SkJSONWriter writer(&stream, SkJSONWriter::Mode::kFast);
    UrlDataManager urlDataManager(SkString("data"));

    writer.beginObject();
    AsDebugCanvas(canvas)->toJSON(writer, urlDataManager, AsCanvas(target));
    writer.endObject();
    writer.flush();

    auto data = stream.detachAsData();
    AsString(result)->set(static_cast<const char*>(data->data()), data->size());
}

void sk_debug_canvas_get_command_info_json(sk_debug_canvas_t* canvas, sk_string_t* result) {
    if (!canvas || !result) return;
    DebugCanvas* dc = AsDebugCanvas(canvas);

    SkDynamicMemoryWStream stream;
    SkJSONWriter writer(&stream, SkJSONWriter::Mode::kFast);

    writer.beginObject();

    // Write clip rect
    const SkIRect& clip = dc->getCurrentClip();
    writer.beginArray("ClipRect");
    writer.appendS32(clip.fLeft);
    writer.appendS32(clip.fTop);
    writer.appendS32(clip.fRight);
    writer.appendS32(clip.fBottom);
    writer.endArray();

    // Write view matrix (4x4, expose as 3x3 for compatibility)
    const SkM44& m44 = dc->getCurrentMatrix();
    SkMatrix m = m44.asM33();
    writer.beginArray("ViewMatrix");
    // Row 0
    writer.beginArray();
    writer.appendFloat(m.getScaleX());
    writer.appendFloat(m.getSkewX());
    writer.appendFloat(m.getTranslateX());
    writer.endArray();
    // Row 1
    writer.beginArray();
    writer.appendFloat(m.getSkewY());
    writer.appendFloat(m.getScaleY());
    writer.appendFloat(m.getTranslateY());
    writer.endArray();
    // Row 2
    writer.beginArray();
    writer.appendFloat(m.getPerspX());
    writer.appendFloat(m.getPerspY());
    writer.appendFloat(m[SkMatrix::kMPersp2]);
    writer.endArray();
    writer.endArray();

    writer.endObject();
    writer.flush();

    auto data = stream.detachAsData();
    AsString(result)->set(static_cast<const char*>(data->data()), data->size());
}

void sk_debug_canvas_set_command_visibility(sk_debug_canvas_t* canvas, int index, bool visible) {
    if (!canvas) return;
    DebugCanvas* dc = AsDebugCanvas(canvas);
    if (index >= 0 && index < dc->getSize()) {
        dc->toggleCommand(index, visible);
    }
}

void sk_debug_canvas_delete_command(sk_debug_canvas_t* canvas, int index) {
    if (!canvas) return;
    DebugCanvas* dc = AsDebugCanvas(canvas);
    if (index >= 0 && index < dc->getSize()) {
        dc->deleteDrawCommandAt(index);
    }
}

void sk_debug_canvas_set_overdraw_vis(sk_debug_canvas_t* canvas, bool enabled) {
    if (!canvas) return;
    AsDebugCanvas(canvas)->setOverdrawViz(enabled);
}

void sk_debug_canvas_set_clip_viz_color(sk_debug_canvas_t* canvas, sk_color_t color) {
    if (!canvas) return;
    AsDebugCanvas(canvas)->setClipVizColor(color);
}

void sk_debug_canvas_set_origin_visible(sk_debug_canvas_t* canvas, bool visible) {
    if (!canvas) return;
    AsDebugCanvas(canvas)->setOriginVisible(visible);
}

void sk_debug_canvas_get_bounds(const sk_debug_canvas_t* canvas, sk_irect_t* bounds) {
    if (!canvas || !bounds) return;
    const DebugCanvas* dc = AsDebugCanvas(canvas);
    // Use getBaseLayerSize for the original canvas dimensions (not clipped)
    SkISize size = dc->getBaseLayerSize();
    bounds->left = 0;
    bounds->top = 0;
    bounds->right = size.width();
    bounds->bottom = size.height();
}
