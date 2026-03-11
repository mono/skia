/*
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/sk_xamarin.h"

// Skia
#include "include/c/gr_context.h"
#include "include/c/sk_bitmap.h"
#include "include/c/sk_blender.h"
#include "include/c/sk_canvas.h"
#include "include/c/sk_codec.h"
#include "include/c/sk_colorfilter.h"
#include "include/c/sk_colorspace.h"
#include "include/c/sk_data.h"
#include "include/c/sk_document.h"
#include "include/c/sk_drawable.h"
#include "include/c/sk_font.h"
#include "include/c/sk_general.h"
#include "include/c/sk_graphics.h"
#include "include/c/sk_image.h"
#include "include/c/sk_imagefilter.h"
#include "include/c/sk_linker.h"
#include "include/c/sk_maskfilter.h"
#include "include/c/sk_matrix.h"
#include "include/c/sk_paint.h"
#include "include/c/sk_path.h"
#include "include/c/sk_patheffect.h"
#include "include/c/sk_picture.h"
#include "include/c/sk_pixmap.h"
#include "include/c/sk_region.h"
#include "include/c/sk_rrect.h"
#include "include/c/sk_runtimeeffect.h"
#include "include/c/sk_shader.h"
#include "include/c/sk_stream.h"
#include "include/c/sk_string.h"
#include "include/c/sk_surface.h"
#include "include/c/sk_svg.h"
#include "include/c/sk_textblob.h"
#include "include/c/sk_typeface.h"
#include "include/c/sk_vertices.h"

// Skottie
#include "include/c/skottie_animation.h"
#include "include/c/sksg_invalidation_controller.h"
#include "include/c/skresources_resource_provider.h"


// Xamarin
#include "include/xamarin/sk_managedstream.h"
#include "include/xamarin/sk_manageddrawable.h"
#include "include/xamarin/sk_managedtracememorydump.h"
#include "include/xamarin/sk_compatpaint.h"

SK_X_API void** KeepSkiaCSymbols (void);

void** KeepSkiaCSymbols (void)
{
    static void* ret[] = {
        // Skia
        (void*)sk_colortype_get_default_8888,
        (void*)gr_recording_context_unref,
        (void*)gr_glinterface_create_native_interface,
        (void*)sk_bitmap_new,
        (void*)sk_blender_unref,
        (void*)sk_canvas_destroy,
        (void*)sk_codec_min_buffered_bytes_needed,
        (void*)sk_colorfilter_unref,
        (void*)sk_colorspace_unref,
        (void*)sk_data_new_empty,
        (void*)sk_document_unref,
        (void*)sk_drawable_unref,
        (void*)sk_font_new,
        (void*)sk_image_ref,
        (void*)sk_imagefilter_unref,
        (void*)sk_maskfilter_ref,
        (void*)sk_matrix_concat,
        (void*)sk_paint_new,
        (void*)sk_path_new,
        (void*)sk_path_effect_unref,
        (void*)sk_picture_recorder_new,
        (void*)sk_pixmap_destructor,
        (void*)sk_region_new,
        (void*)sk_rrect_new,
        (void*)sk_runtimeeffect_unref,
        (void*)sk_shader_ref,
        (void*)sk_stream_asset_destroy,
        (void*)sk_string_new_empty,
        (void*)sk_surface_new_null,
        (void*)sk_svgcanvas_create_with_stream,
        (void*)sk_typeface_unref,
        (void*)sk_textblob_ref,
        (void*)sk_vertices_unref,
        (void*)sk_graphics_init,

        // Animation
        (void*)skottie_animation_make_from_stream,
        (void*)sksg_invalidation_controller_new,
        (void*)skresources_resource_provider_ref,

        // Xamarin
        (void*)sk_compatpaint_new,
        (void*)sk_managedstream_new,
        (void*)sk_manageddrawable_new,
        (void*)sk_managedtracememorydump_new,

        // Linker
        (void*)sk_linker_keep_alive,
    };
    return ret;
}
