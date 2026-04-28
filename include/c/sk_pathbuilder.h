/*
 * Copyright 2025 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_pathbuilder_DEFINED
#define sk_pathbuilder_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

SK_C_API sk_pathbuilder_t* sk_pathbuilder_new(void);
SK_C_API sk_pathbuilder_t* sk_pathbuilder_new_from_path(const sk_path_t* path);
SK_C_API void sk_pathbuilder_delete(sk_pathbuilder_t* builder);

SK_C_API void sk_pathbuilder_move_to(sk_pathbuilder_t* builder, float x, float y);
SK_C_API void sk_pathbuilder_line_to(sk_pathbuilder_t* builder, float x, float y);
SK_C_API void sk_pathbuilder_quad_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1);
SK_C_API void sk_pathbuilder_conic_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1, float w);
SK_C_API void sk_pathbuilder_cubic_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1, float x2, float y2);
SK_C_API void sk_pathbuilder_arc_to(sk_pathbuilder_t* builder, float rx, float ry, float xAxisRotate, sk_path_arc_size_t largeArc, sk_path_direction_t sweep, float x, float y);
SK_C_API void sk_pathbuilder_arc_to_with_oval(sk_pathbuilder_t* builder, const sk_rect_t* oval, float startAngle, float sweepAngle, bool forceMoveTo);
SK_C_API void sk_pathbuilder_arc_to_with_points(sk_pathbuilder_t* builder, float x1, float y1, float x2, float y2, float radius);
SK_C_API void sk_pathbuilder_close(sk_pathbuilder_t* builder);

SK_C_API void sk_pathbuilder_rmove_to(sk_pathbuilder_t* builder, float dx, float dy);
SK_C_API void sk_pathbuilder_rline_to(sk_pathbuilder_t* builder, float dx, float dy);
SK_C_API void sk_pathbuilder_rquad_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1);
SK_C_API void sk_pathbuilder_rconic_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1, float w);
SK_C_API void sk_pathbuilder_rcubic_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1, float dx2, float dy2);
SK_C_API void sk_pathbuilder_rarc_to(sk_pathbuilder_t* builder, float rx, float ry, float xAxisRotate, sk_path_arc_size_t largeArc, sk_path_direction_t sweep, float x, float y);

SK_C_API void sk_pathbuilder_add_rect(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir);
SK_C_API void sk_pathbuilder_add_rect_start(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir, uint32_t startIndex);
SK_C_API void sk_pathbuilder_add_rrect(sk_pathbuilder_t* builder, const sk_rrect_t* rect, sk_path_direction_t dir);
SK_C_API void sk_pathbuilder_add_rrect_start(sk_pathbuilder_t* builder, const sk_rrect_t* rect, sk_path_direction_t dir, uint32_t start);
SK_C_API void sk_pathbuilder_add_rounded_rect(sk_pathbuilder_t* builder, const sk_rect_t* rect, float rx, float ry, sk_path_direction_t dir);
SK_C_API void sk_pathbuilder_add_oval(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir);
SK_C_API void sk_pathbuilder_add_circle(sk_pathbuilder_t* builder, float x, float y, float radius, sk_path_direction_t dir);
SK_C_API void sk_pathbuilder_add_arc(sk_pathbuilder_t* builder, const sk_rect_t* rect, float startAngle, float sweepAngle);
SK_C_API void sk_pathbuilder_add_poly(sk_pathbuilder_t* builder, const sk_point_t* points, int count, bool close);

SK_C_API void sk_pathbuilder_add_path_offset(sk_pathbuilder_t* builder, const sk_path_t* other, float dx, float dy, sk_path_add_mode_t add_mode);
SK_C_API void sk_pathbuilder_add_path_matrix(sk_pathbuilder_t* builder, const sk_path_t* other, sk_matrix_t* matrix, sk_path_add_mode_t add_mode);
SK_C_API void sk_pathbuilder_add_path(sk_pathbuilder_t* builder, const sk_path_t* other, sk_path_add_mode_t add_mode);
SK_C_API void sk_pathbuilder_reverse_add_path(sk_pathbuilder_t* builder, const sk_path_t* other);

SK_C_API void sk_pathbuilder_set_filltype(sk_pathbuilder_t* builder, sk_path_filltype_t filltype);
SK_C_API sk_path_filltype_t sk_pathbuilder_get_filltype(const sk_pathbuilder_t* builder);

SK_C_API void sk_pathbuilder_reset(sk_pathbuilder_t* builder);

SK_C_API sk_path_t* sk_pathbuilder_detach_path(sk_pathbuilder_t* builder);
SK_C_API sk_path_t* sk_pathbuilder_snapshot_path(sk_pathbuilder_t* builder);

SK_C_PLUS_PLUS_END_GUARD

#endif
