/*
 * Copyright 2024 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_debugger_DEFINED
#define sk_debugger_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Lifecycle
SK_C_API sk_debug_canvas_t* sk_debug_canvas_new(int width, int height);
SK_C_API void sk_debug_canvas_destroy(sk_debug_canvas_t* canvas);

// Load an SKP file by deserializing data into an SkPicture and playing it back
// into the DebugCanvas to record all commands.
// Returns the number of commands recorded, or -1 on failure.
SK_C_API int sk_debug_canvas_load_skp(sk_debug_canvas_t* canvas, const void* data, size_t length);

// Command count
SK_C_API int sk_debug_canvas_get_command_count(const sk_debug_canvas_t* canvas);

// Rendering: draw all commands to the target canvas
SK_C_API void sk_debug_canvas_draw(sk_debug_canvas_t* canvas, sk_canvas_t* target);

// Rendering: draw commands up to (and including) the given index
SK_C_API void sk_debug_canvas_draw_to(sk_debug_canvas_t* canvas, sk_canvas_t* target, int index);

// Get the JSON command list. The caller must provide an sk_string_t to receive the result.
// The JSON contains an array of command objects with name, parameters, etc.
SK_C_API void sk_debug_canvas_get_command_list_json(sk_debug_canvas_t* canvas, sk_canvas_t* target, sk_string_t* result);

// Get the matrix and clip info after the last drawTo call.
// Returns JSON with ClipRect and ViewMatrix.
SK_C_API void sk_debug_canvas_get_command_info_json(sk_debug_canvas_t* canvas, sk_string_t* result);

// Toggle visibility of a command at the given index
SK_C_API void sk_debug_canvas_set_command_visibility(sk_debug_canvas_t* canvas, int index, bool visible);

// Delete a command at the given index
SK_C_API void sk_debug_canvas_delete_command(sk_debug_canvas_t* canvas, int index);

// Visualization: overdraw
SK_C_API void sk_debug_canvas_set_overdraw_vis(sk_debug_canvas_t* canvas, bool enabled);

// Visualization: clip region color (set alpha to 0 to hide)
SK_C_API void sk_debug_canvas_set_clip_viz_color(sk_debug_canvas_t* canvas, sk_color_t color);

// Visualization: show coordinate origin
SK_C_API void sk_debug_canvas_set_origin_visible(sk_debug_canvas_t* canvas, bool visible);

// Get the bounds (width/height) that the debug canvas was created with
SK_C_API void sk_debug_canvas_get_bounds(const sk_debug_canvas_t* canvas, sk_irect_t* bounds);

SK_C_PLUS_PLUS_END_GUARD

#endif
