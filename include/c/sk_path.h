/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_path_DEFINED
#define sk_path_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

/* Path */
SK_C_API sk_path_t* sk_path_new(void);
SK_C_API void sk_path_delete(sk_path_t*);
SK_C_API sk_path_t* sk_path_clone(const sk_path_t* cpath);
SK_C_API void sk_path_get_bounds(const sk_path_t*, sk_rect_t*);
SK_C_API void sk_path_compute_tight_bounds(const sk_path_t*, sk_rect_t*);
SK_C_API sk_path_filltype_t sk_path_get_filltype(sk_path_t*);
SK_C_API void sk_path_set_filltype(sk_path_t*, sk_path_filltype_t);
SK_C_API void sk_path_transform(sk_path_t* cpath, const sk_matrix_t* cmatrix);
SK_C_API void sk_path_transform_to_dest(const sk_path_t* cpath, const sk_matrix_t* cmatrix, sk_path_t* destination);
SK_C_API void sk_path_reset (sk_path_t* cpath);
SK_C_API void sk_path_rewind (sk_path_t* cpath);
SK_C_API int sk_path_count_points (const sk_path_t* cpath);
SK_C_API int sk_path_count_verbs (const sk_path_t* cpath);
SK_C_API void sk_path_get_point (const sk_path_t* cpath, int index, sk_point_t* point);
SK_C_API int sk_path_get_points (const sk_path_t* cpath, sk_point_t* points, int max);
SK_C_API bool sk_path_contains (const sk_path_t* cpath, float x, float y);
SK_C_API bool sk_path_parse_svg_string (sk_path_t* cpath, const char* str);
SK_C_API void sk_path_to_svg_string (const sk_path_t* cpath, sk_string_t* str);
SK_C_API bool sk_path_get_last_point (const sk_path_t* cpath, sk_point_t* point);
SK_C_API int sk_path_convert_conic_to_quads(const sk_point_t* p0, const sk_point_t* p1, const sk_point_t* p2, float w, sk_point_t* pts, int pow2);
SK_C_API uint32_t sk_path_get_segment_masks(sk_path_t* cpath);
SK_C_API bool sk_path_is_oval(sk_path_t* cpath, sk_rect_t* bounds);
SK_C_API bool sk_path_is_rrect(sk_path_t* cpath, sk_rrect_t* bounds);
SK_C_API bool sk_path_is_line(sk_path_t* cpath, sk_point_t line [2]);
SK_C_API bool sk_path_is_rect(sk_path_t* cpath, sk_rect_t* rect, bool* isClosed, sk_path_direction_t* direction);
SK_C_API bool sk_path_is_convex(const sk_path_t* cpath);

/* Iterators */
SK_C_API sk_path_iterator_t* sk_path_create_iter (sk_path_t *cpath, int forceClose);
SK_C_API sk_path_verb_t sk_path_iter_next (sk_path_iterator_t *iterator, sk_point_t points [4]);
SK_C_API float sk_path_iter_conic_weight (sk_path_iterator_t *iterator);
SK_C_API int sk_path_iter_is_close_line (sk_path_iterator_t *iterator);
SK_C_API int sk_path_iter_is_closed_contour (sk_path_iterator_t *iterator);
SK_C_API void sk_path_iter_destroy (sk_path_iterator_t *iterator);

/* Raw iterators */
SK_C_API sk_path_rawiterator_t* sk_path_create_rawiter (sk_path_t *cpath);
SK_C_API sk_path_verb_t sk_path_rawiter_peek (sk_path_rawiterator_t *iterator);
SK_C_API sk_path_verb_t sk_path_rawiter_next (sk_path_rawiterator_t *iterator, sk_point_t points [4]);
SK_C_API float sk_path_rawiter_conic_weight (sk_path_rawiterator_t *iterator);
SK_C_API void sk_path_rawiter_destroy (sk_path_rawiterator_t *iterator);

/* Path Ops */
SK_C_API bool sk_pathop_op(const sk_path_t* one, const sk_path_t* two, sk_pathop_t op, sk_path_t* result);
SK_C_API bool sk_pathop_simplify(const sk_path_t* path, sk_path_t* result);
SK_C_API bool sk_pathop_tight_bounds(const sk_path_t* path, sk_rect_t* result);
SK_C_API bool sk_pathop_as_winding(const sk_path_t* path, sk_path_t* result);

/* Path Op Builder */
SK_C_API sk_opbuilder_t* sk_opbuilder_new(void);
SK_C_API void sk_opbuilder_destroy(sk_opbuilder_t* builder);
SK_C_API void sk_opbuilder_add(sk_opbuilder_t* builder, const sk_path_t* path, sk_pathop_t op);
SK_C_API bool sk_opbuilder_resolve(sk_opbuilder_t* builder, sk_path_t* result);

/* Path Measure */
SK_C_API sk_pathmeasure_t* sk_pathmeasure_new(void);
SK_C_API sk_pathmeasure_t* sk_pathmeasure_new_with_path(const sk_path_t* path, bool forceClosed, float resScale);
SK_C_API void sk_pathmeasure_destroy(sk_pathmeasure_t* pathMeasure);
SK_C_API void sk_pathmeasure_set_path(sk_pathmeasure_t* pathMeasure, const sk_path_t* path, bool forceClosed);
SK_C_API float sk_pathmeasure_get_length(sk_pathmeasure_t* pathMeasure);
SK_C_API bool sk_pathmeasure_get_pos_tan(sk_pathmeasure_t* pathMeasure, float distance, sk_point_t* position, sk_vector_t* tangent);
SK_C_API bool sk_pathmeasure_get_matrix(sk_pathmeasure_t* pathMeasure, float distance, sk_matrix_t* matrix, sk_pathmeasure_matrixflags_t flags);
SK_C_API bool sk_pathmeasure_get_segment(sk_pathmeasure_t* pathMeasure, float start, float stop, sk_pathbuilder_t* dst, bool startWithMoveTo);
SK_C_API bool sk_pathmeasure_is_closed(sk_pathmeasure_t* pathMeasure);
SK_C_API bool sk_pathmeasure_next_contour(sk_pathmeasure_t* pathMeasure);

SK_C_PLUS_PLUS_END_GUARD

#endif
