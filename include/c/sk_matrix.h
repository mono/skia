/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_matrix_DEFINED
#define sk_matrix_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

SK_C_API bool sk_matrix_try_invert (sk_matrix_t *matrix, sk_matrix_t *result);
SK_C_API void sk_matrix_concat (sk_matrix_t *result, sk_matrix_t *first, sk_matrix_t *second);
SK_C_API void sk_matrix_pre_concat (sk_matrix_t *result, sk_matrix_t *matrix);
SK_C_API void sk_matrix_post_concat (sk_matrix_t *result, sk_matrix_t *matrix);
SK_C_API void sk_matrix_map_rect (sk_matrix_t *matrix, sk_rect_t *dest, sk_rect_t *source);
SK_C_API void sk_matrix_map_points (sk_matrix_t *matrix, sk_point_t *dst, sk_point_t *src, int count);
SK_C_API void sk_matrix_map_vectors (sk_matrix_t *matrix, sk_point_t *dst, sk_point_t *src, int count);
SK_C_API void sk_matrix_map_xy (sk_matrix_t *matrix, float x, float y, sk_point_t* result);
SK_C_API void sk_matrix_map_vector (sk_matrix_t *matrix, float x, float y, sk_point_t* result);
SK_C_API float sk_matrix_map_radius (sk_matrix_t *matrix, float radius);

SK_C_PLUS_PLUS_END_GUARD

#endif
