/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkMatrix.h"
#include "include/core/SkMatrix44.h"
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

// additional
sk_matrix_t sk_matrix_scale(float sx, float sy) {
    return ToMatrix(SkMatrix::Scale(sx, sy));
}
sk_matrix_t sk_matrix_translate(float dx, float dy) {
    return ToMatrix(SkMatrix::Translate(dx, dy));
}
sk_matrix_t sk_matrix_translate_point(sk_point_t t) {
    return ToMatrix(SkMatrix::Translate(AsPoint(t)));
}
sk_matrix_t sk_matrix_translate_ipoint(sk_ipoint_t t) {
    return ToMatrix(SkMatrix::Translate(AsIPoint(t)));
}
sk_matrix_t sk_matrix_rotate_deg(float deg) {
    return ToMatrix(SkMatrix::RotateDeg(deg));
}
sk_matrix_t sk_matrix_rotate_deg_point(float deg, sk_point_t t) {
    return ToMatrix(SkMatrix::RotateDeg(deg, AsPoint(t)));
}
sk_matrix_t sk_matrix_rotate_rad(float rad) {
    return ToMatrix(SkMatrix::RotateRad(rad));
}
sk_matrix_type_mask_t sk_matrix_get_type(sk_matrix_t* matrix) {
    return (sk_matrix_type_mask_t)AsMatrix(matrix).getType(); // calls ComputeTypeMask
}
bool sk_matrix_is_identity(sk_matrix_t* matrix) {
    return AsMatrix(matrix).isIdentity(); // calls getType()
}
bool sk_matrix_is_scale_translate(sk_matrix_t* matrix) {
    return AsMatrix(matrix).isScaleTranslate(); // calls getType()
}
bool sk_matrix_is_translate(sk_matrix_t* matrix) {
    return AsMatrix(matrix).isTranslate(); // calls getType()
}
bool sk_matrix_rect_stays_rect(sk_matrix_t* matrix) {
    return AsMatrix(matrix).rectStaysRect(); // calls getType()
}
bool sk_matrix_preserves_axis_alignment(sk_matrix_t* matrix) {
    return AsMatrix(matrix).preservesAxisAlignment();
}
bool sk_matrix_has_perspective(sk_matrix_t* matrix) {
    return AsMatrix(matrix).hasPerspective(); // calls getType()
}
bool sk_matrix_is_similarity(sk_matrix_t* matrix, float tol) {
    return AsMatrix(matrix).isSimilarity(tol);
}
bool sk_matrix_preserves_right_angles(sk_matrix_t* matrix, float tol) {
    return AsMatrix(matrix).preservesRightAngles(tol);
}
float sk_matrix_get(sk_matrix_t* matrix, sk_matrix_row_major_mask_t mask) {
    return AsMatrix(matrix).get(mask);
}
float sk_matrix_rc(sk_matrix_t* matrix, int r, int c) {
    return AsMatrix(matrix).rc(r, c);
}
void sk_matrix_get9(sk_matrix_t* matrix, float* buffer) {
    return AsMatrix(matrix).get9(buffer);
}
void sk_matrix_set9(sk_matrix_t* matrix, float* buffer, sk_matrix_t* result) {
    SkMatrix m = AsMatrix(matrix);
    m.set9(buffer);
    *result = ToMatrix(&m);
}
void sk_matrix_reset(sk_matrix_t* matrix, sk_matrix_t* result) {
    SkMatrix m = AsMatrix(matrix);
    m.reset();
    *result = ToMatrix(&m);
}
void sk_matrix_set_identity(sk_matrix_t* matrix, sk_matrix_t* result) {
    SkMatrix m = AsMatrix(matrix);
    m.setIdentity();
    *result = ToMatrix(&m);
}
void sk_matrix_normalize_perspective(sk_matrix_t* matrix, sk_matrix_t* result) {
    SkMatrix m = AsMatrix(matrix);
    m.normalizePerspective();
    *result = ToMatrix(&m);
}
void sk_matrix_map_homogeneous_points3(sk_matrix_t* matrix, sk_point3_t* dst, sk_point3_t* src, int count) {
    AsMatrix(matrix).mapHomogeneousPoints(AsPoint3(dst), AsPoint3(src), count);
}
void sk_matrix_map_homogeneous_points(sk_matrix_t* matrix, sk_point3_t* dst, sk_point_t* src, int count) {
    AsMatrix(matrix).mapHomogeneousPoints(AsPoint3(dst), AsPoint(src), count);
}
bool sk_matrix_is_finite(sk_matrix_t* matrix) {
    return AsMatrix(matrix).isFinite();
}
void sk_matrix_pre_scale(sk_matrix_t* result, sk_matrix_t* matrix, float sx, float sy) {
    SkMatrix m = AsMatrix(matrix);
    m.preScale(sx, sy);
    *result = ToMatrix(&m);
}
void sk_matrix_pre_scale_with_pivot(sk_matrix_t* result, sk_matrix_t* matrix, float sx, float sy, float px, float py) {
    SkMatrix m = AsMatrix(matrix);
    m.preScale(sx, sy, px, py);
    *result = ToMatrix(&m);
}
void sk_matrix_post_scale(sk_matrix_t* result, sk_matrix_t* matrix, float sx, float sy) {
    SkMatrix m = AsMatrix(matrix);
    m.postScale(sx, sy);
    *result = ToMatrix(&m);
}
void sk_matrix_post_scale_with_pivot(sk_matrix_t* result, sk_matrix_t* matrix, float sx, float sy, float px, float py) {
    SkMatrix m = AsMatrix(matrix);
    m.postScale(sx, sy, px, py);
    *result = ToMatrix(&m);
}
void sk_matrix_pre_translate(sk_matrix_t* result, sk_matrix_t* matrix, float dx, float dy) {
    SkMatrix m = AsMatrix(matrix);
    m.preTranslate(dx, dy);
    *result = ToMatrix(&m);
}
void sk_matrix_post_translate(sk_matrix_t* result, sk_matrix_t* matrix, float dx, float dy) {
    SkMatrix m = AsMatrix(matrix);
    m.postTranslate(dx, dy);
    *result = ToMatrix(&m);
}
bool sk_matrix_set_rect_to_rect(sk_matrix_t* matrix, sk_matrix_t* result, sk_rect_t* dest, sk_rect_t* source, sk_matrix_scale_to_fit_t stf) {
    SkMatrix m = AsMatrix(matrix);
    bool r = m.setRectToRect(*AsRect(source), *AsRect(dest), (SkMatrix::ScaleToFit)stf);
    *result = ToMatrix(&m);
    return r;
}



// 3d view

sk_3dview_t* sk_3dview_new(void) {
    return To3DView(new Sk3DView());
}

void sk_3dview_destroy(sk_3dview_t* cview) {
    delete As3DView(cview);
}

void sk_3dview_save(sk_3dview_t* cview) {
    As3DView(cview)->save();
}

void sk_3dview_restore(sk_3dview_t* cview) {
    As3DView(cview)->restore();
}

void sk_3dview_translate(sk_3dview_t* cview, float x, float y, float z) {
    As3DView(cview)->translate(x, y, z);
}

void sk_3dview_rotate_x_degrees(sk_3dview_t* cview, float degrees) {
    As3DView(cview)->rotateX(degrees);
}

void sk_3dview_rotate_y_degrees(sk_3dview_t* cview, float degrees) {
    As3DView(cview)->rotateY(degrees);
}

void sk_3dview_rotate_z_degrees(sk_3dview_t* cview, float degrees) {
    As3DView(cview)->rotateZ(degrees);
}

void sk_3dview_rotate_x_radians(sk_3dview_t* cview, float radians) {
    As3DView(cview)->rotateX(SkRadiansToDegrees(radians));
}

void sk_3dview_rotate_y_radians(sk_3dview_t* cview, float radians) {
    As3DView(cview)->rotateY(SkRadiansToDegrees(radians));
}

void sk_3dview_rotate_z_radians(sk_3dview_t* cview, float radians) {
    As3DView(cview)->rotateZ(SkRadiansToDegrees(radians));
}

void sk_3dview_get_matrix(sk_3dview_t* cview, sk_matrix_t* cmatrix) {
    SkMatrix matrix;
    As3DView(cview)->getMatrix(&matrix);
    *cmatrix = ToMatrix(&matrix);
}

void sk_3dview_apply_to_canvas(sk_3dview_t* cview, sk_canvas_t* ccanvas) {
    As3DView(cview)->applyToCanvas(AsCanvas(ccanvas));
}

float sk_3dview_dot_with_normal(sk_3dview_t* cview, float dx, float dy, float dz) {
    return As3DView(cview)->dotWithNormal(dx, dy, dz);
}

// matrix 4x4

void sk_matrix44_destroy(sk_matrix44_t* matrix) {
    delete AsMatrix44(matrix);
}

sk_matrix44_t* sk_matrix44_new(void) {
    return ToMatrix44(new SkMatrix44(SkMatrix44::Uninitialized_Constructor::kUninitialized_Constructor));
}

sk_matrix44_t* sk_matrix44_new_identity(void) {
    return ToMatrix44(new SkMatrix44(SkMatrix44::Identity_Constructor::kIdentity_Constructor));
}

sk_matrix44_t* sk_matrix44_new_copy(const sk_matrix44_t* src) {
    return ToMatrix44(new SkMatrix44(AsMatrix44(*src)));
}

sk_matrix44_t* sk_matrix44_new_concat(const sk_matrix44_t* a, const sk_matrix44_t* b) {
    return ToMatrix44(new SkMatrix44(AsMatrix44(*a), AsMatrix44(*b)));
}

sk_matrix44_t* sk_matrix44_new_matrix(const sk_matrix_t* src) {
    return ToMatrix44(new SkMatrix44(AsMatrix(src)));
}

bool sk_matrix44_equals(sk_matrix44_t* matrix, const sk_matrix44_t* other) {
    return AsMatrix44(matrix) == AsMatrix44(other);
}

void sk_matrix44_to_matrix(sk_matrix44_t* matrix, sk_matrix_t* dst) {
    SkMatrix m = (SkMatrix)AsMatrix44(*matrix);
    *dst = ToMatrix(&m);
}

sk_matrix44_type_mask_t sk_matrix44_get_type(sk_matrix44_t* matrix) {
    return(sk_matrix44_type_mask_t)AsMatrix44(matrix)->getType();
}

void sk_matrix44_set_identity(sk_matrix44_t* matrix) {
    AsMatrix44(matrix)->setIdentity();
}

float sk_matrix44_get(sk_matrix44_t* matrix, int row, int col) {
    return AsMatrix44(matrix)->get(row, col);
}

void sk_matrix44_set(sk_matrix44_t* matrix, int row, int col, float value) {
    AsMatrix44(matrix)->set(row, col, value);
}

void sk_matrix44_as_col_major(sk_matrix44_t* matrix, float* dst) {
    AsMatrix44(matrix)->asColMajorf(dst);
}

void sk_matrix44_as_row_major(sk_matrix44_t* matrix, float* dst) {
    AsMatrix44(matrix)->asRowMajorf(dst);
}

void sk_matrix44_set_col_major(sk_matrix44_t* matrix, float* dst) {
    AsMatrix44(matrix)->setColMajorf(dst);
}

void sk_matrix44_set_row_major(sk_matrix44_t* matrix, float* dst) {
    AsMatrix44(matrix)->setRowMajorf(dst);
}

void sk_matrix44_set_3x3_row_major(sk_matrix44_t* matrix, float* dst) {
    AsMatrix44(matrix)->set3x3RowMajorf(dst);
}

void sk_matrix44_set_translate(sk_matrix44_t* matrix, float dx, float dy, float dz) {
    AsMatrix44(matrix)->setTranslate(dx, dy, dz);
}

void sk_matrix44_pre_translate(sk_matrix44_t* matrix, float dx, float dy, float dz) {
    AsMatrix44(matrix)->preTranslate(dx, dy, dz);
}

void sk_matrix44_post_translate(sk_matrix44_t* matrix, float dx, float dy, float dz) {
    AsMatrix44(matrix)->postTranslate(dx, dy, dz);
}

void sk_matrix44_set_scale(sk_matrix44_t* matrix, float sx, float sy, float sz) {
    AsMatrix44(matrix)->setScale(sx, sy, sz);
}

void sk_matrix44_pre_scale(sk_matrix44_t* matrix, float sx, float sy, float sz) {
    AsMatrix44(matrix)->preScale(sx, sy, sz);
}

void sk_matrix44_post_scale(sk_matrix44_t* matrix, float sx, float sy, float sz) {
    AsMatrix44(matrix)->postScale(sx, sy, sz);
}

void sk_matrix44_set_rotate_about_degrees(sk_matrix44_t* matrix, float x, float y, float z, float degrees) {
    AsMatrix44(matrix)->setRotateDegreesAbout(x, y, z, degrees);
}

void sk_matrix44_set_rotate_about_radians(sk_matrix44_t* matrix, float x, float y, float z, float radians) {
    AsMatrix44(matrix)->setRotateAbout(x, y, z, radians);
}

void sk_matrix44_set_rotate_about_radians_unit(sk_matrix44_t* matrix, float x, float y, float z, float radians) {
    AsMatrix44(matrix)->setRotateAboutUnit(x, y, z, radians);
}

void sk_matrix44_set_concat(sk_matrix44_t* matrix, const sk_matrix44_t* a, const sk_matrix44_t* b) {
    AsMatrix44(matrix)->setConcat(AsMatrix44(*a), AsMatrix44(*b));
}

void sk_matrix44_pre_concat(sk_matrix44_t* matrix, const sk_matrix44_t* m) {
    AsMatrix44(matrix)->preConcat(AsMatrix44(*m));
}

void sk_matrix44_post_concat(sk_matrix44_t* matrix, const sk_matrix44_t* m) {
    AsMatrix44(matrix)->postConcat(AsMatrix44(*m));
}

bool sk_matrix44_invert(sk_matrix44_t* matrix, sk_matrix44_t* inverse) {
    return AsMatrix44(matrix)->invert(AsMatrix44(inverse));
}

void sk_matrix44_transpose(sk_matrix44_t* matrix) {
    AsMatrix44(matrix)->transpose();
}

void sk_matrix44_map_scalars(sk_matrix44_t* matrix, const float* src, float* dst) {
    AsMatrix44(matrix)->mapScalars(src, dst);
}

void sk_matrix44_map2(sk_matrix44_t* matrix, const float* src2, int count, float* dst4) {
    AsMatrix44(matrix)->map2(src2, count, dst4);
}

bool sk_matrix44_preserves_2d_axis_alignment(sk_matrix44_t* matrix, float epsilon) {
    return AsMatrix44(matrix)->preserves2dAxisAlignment(epsilon);
}

double sk_matrix44_determinant(sk_matrix44_t* matrix) {
    return AsMatrix44(matrix)->determinant();
}

