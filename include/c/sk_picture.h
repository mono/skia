/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_picture_DEFINED
#define sk_picture_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// SkPictureRecorder

SK_C_API sk_picture_recorder_t* sk_picture_recorder_new(void);
SK_C_API void sk_picture_recorder_delete(sk_picture_recorder_t*);
SK_C_API sk_canvas_t* sk_picture_recorder_begin_recording(sk_picture_recorder_t*, const sk_rect_t*);
SK_C_API sk_canvas_t* sk_picture_recorder_begin_recording_with_bbox_hierarchy(sk_picture_recorder_t*, const sk_rect_t*, sk_bbh_factory_t*);
SK_C_API sk_picture_t* sk_picture_recorder_end_recording(sk_picture_recorder_t*);
SK_C_API sk_drawable_t* sk_picture_recorder_end_recording_as_drawable(sk_picture_recorder_t*);
SK_C_API sk_canvas_t* sk_picture_get_recording_canvas(sk_picture_recorder_t* crec);

// SkPicture

SK_C_API void sk_picture_ref(sk_picture_t*);
SK_C_API void sk_picture_unref(sk_picture_t*);
SK_C_API uint32_t sk_picture_get_unique_id(sk_picture_t*);
SK_C_API void sk_picture_get_cull_rect(sk_picture_t*, sk_rect_t*);
SK_C_API sk_shader_t* sk_picture_make_shader(sk_picture_t* src, sk_shader_tilemode_t tmx, sk_shader_tilemode_t tmy, sk_filter_mode_t mode, const sk_matrix_t* localMatrix, const sk_rect_t* tile);
SK_C_API sk_data_t* sk_picture_serialize_to_data(const sk_picture_t* picture);
SK_C_API void sk_picture_serialize_to_stream(const sk_picture_t* picture, sk_wstream_t* stream);
SK_C_API sk_picture_t* sk_picture_deserialize_from_stream(sk_stream_t* stream);
SK_C_API sk_picture_t* sk_picture_deserialize_from_data(sk_data_t* data);
SK_C_API sk_picture_t* sk_picture_deserialize_from_memory(void* buffer, size_t length);
SK_C_API void sk_picture_playback(const sk_picture_t* picture, sk_canvas_t* canvas);
SK_C_API int sk_picture_approximate_op_count(const sk_picture_t* picture, bool nested);
SK_C_API size_t sk_picture_approximate_bytes_used(const sk_picture_t* picture);

// SkRTreeFactory

SK_C_API sk_rtree_factory_t* sk_rtree_factory_new(void);
SK_C_API void sk_rtree_factory_delete(sk_rtree_factory_t*);

SK_C_PLUS_PLUS_END_GUARD

#endif
