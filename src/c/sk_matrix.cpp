/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkMatrix.h"
#include "include/core/SkM44.h"
#include "include/utils/SkCamera.h"

#include "include/c/sk_matrix.h"

#include "src/c/sk_types_priv.h"

bool sk_matrix_try_invert(sk_matrix_t *matrix, sk_matrix_t *result) {
    SkMatrix m = AsMatrix(matrix);
    if (!result)
        return m.invert(nullptr);

    SkMatrix inverse;
    bool invertible = m.invert(&inverse);
    *result = ToMatrix(&inverse);
    return invertible;
}

void sk_matrix_concat(sk_matrix_t *matrix, sk_matrix_t *first, sk_matrix_t *second) {
    SkMatrix m = AsMatrix(matrix);
    m.setConcat(AsMatrix(first), AsMatrix(second));
    *matrix = ToMatrix(&m);
}

void sk_matrix_pre_concat(sk_matrix_t *target, sk_matrix_t *matrix) {
    SkMatrix m = AsMatrix(target);
    m.preConcat(AsMatrix(matrix));
    *target = ToMatrix(&m);
}

void sk_matrix_post_concat(sk_matrix_t *target, sk_matrix_t *matrix) {
    SkMatrix m = AsMatrix(target);
    m.postConcat(AsMatrix(matrix));
    *target = ToMatrix(&m);
}

void sk_matrix_map_rect(sk_matrix_t *matrix, sk_rect_t *dest, sk_rect_t *source) {
    AsMatrix(matrix).mapRect(AsRect(dest), *AsRect(source));
}

void sk_matrix_map_points(sk_matrix_t *matrix, sk_point_t *dst, sk_point_t *src, int count) {
    AsMatrix(matrix).mapPoints(AsPoint(dst), AsPoint(src), count);
}

void sk_matrix_map_vectors(sk_matrix_t *matrix, sk_point_t *dst, sk_point_t *src, int count) {
    AsMatrix(matrix).mapVectors(AsPoint(dst), AsPoint(src), count);
}

void sk_matrix_map_xy(sk_matrix_t *matrix, float x, float y, sk_point_t* cresult) {
    SkPoint result;
    AsMatrix(matrix).mapXY(x, y, &result);
    *cresult = *ToPoint(&result);
}

void sk_matrix_map_vector(sk_matrix_t *matrix, float x, float y, sk_point_t* cresult) {
    SkPoint result;
    AsMatrix(matrix).mapVector(x, y, &result);
    *cresult = *ToPoint(&result);
}

float sk_matrix_map_radius(sk_matrix_t *matrix, float radius) {
    return AsMatrix(matrix).mapRadius(radius);
}
