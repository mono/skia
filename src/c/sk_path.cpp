/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkSpan.h"
#include "include/pathops/SkPathOps.h"
#include "include/utils/SkParsePath.h"

#include "include/c/sk_path.h"

#include "src/c/sk_types_priv.h"

// Path query methods (immutable)

sk_path_t* sk_path_new(void) {
    return ToPath(new SkPath());
}

void sk_path_delete(sk_path_t* cpath) {
    delete AsPath(cpath);
}

sk_path_t* sk_path_clone(const sk_path_t* cpath) {
    return ToPath(new SkPath(*AsPath(cpath)));
}

void sk_path_set_filltype(sk_path_t* cpath, sk_path_filltype_t cfilltype) {
    AsPath(cpath)->setFillType((SkPathFillType)cfilltype);
}

sk_path_filltype_t sk_path_get_filltype(sk_path_t *cpath) {
    return (sk_path_filltype_t)AsPath(cpath)->getFillType();
}

void sk_path_transform(sk_path_t* cpath, const sk_matrix_t* cmatrix) {
    SkPath result = AsPath(cpath)->makeTransform(AsMatrix(cmatrix));
    *AsPath(cpath) = std::move(result);
}

void sk_path_transform_to_dest(const sk_path_t* cpath, const sk_matrix_t* cmatrix, sk_path_t* destination) {
    *AsPath(destination) = AsPath(cpath)->makeTransform(AsMatrix(cmatrix));
}

void sk_path_reset(sk_path_t* cpath) {
    AsPath(cpath)->reset();
}

void sk_path_rewind(sk_path_t* cpath) {
    // rewind() no longer exists on SkPath in m147, use reset() instead
    AsPath(cpath)->reset();
}

// Iterators

sk_path_iterator_t* sk_path_create_iter(sk_path_t *cpath, int forceClose) {
    return ToPathIter(new SkPath::Iter(*AsPath(cpath), forceClose));
}

sk_path_verb_t sk_path_iter_next(sk_path_iterator_t *iterator, sk_point_t points[4]) {
    return (sk_path_verb_t)AsPathIter(iterator)->next(AsPoint(points));
}

float sk_path_iter_conic_weight(sk_path_iterator_t *iterator) {
    return AsPathIter(iterator)->conicWeight();
}

int sk_path_iter_is_close_line(sk_path_iterator_t *iterator) {
    return AsPathIter(iterator)->isCloseLine();
}

int sk_path_iter_is_closed_contour(sk_path_iterator_t *iterator) {
    return AsPathIter(iterator)->isClosedContour();
}

void sk_path_iter_destroy(sk_path_iterator_t *iterator) {
    delete AsPathIter(iterator);
}

sk_path_rawiterator_t* sk_path_create_rawiter(sk_path_t *cpath) {
    return ToPathRawIter(new SkPath::RawIter(*AsPath(cpath)));
}

sk_path_verb_t sk_path_rawiter_next(sk_path_rawiterator_t *iterator, sk_point_t points[4]) {
    return (sk_path_verb_t)AsPathRawIter(iterator)->next(AsPoint(points));
}

sk_path_verb_t sk_path_rawiter_peek(sk_path_rawiterator_t *iterator) {
    return (sk_path_verb_t)AsPathRawIter(iterator)->peek();
}

float sk_path_rawiter_conic_weight(sk_path_rawiterator_t *iterator) {
    return AsPathRawIter(iterator)->conicWeight();
}

void sk_path_rawiter_destroy(sk_path_rawiterator_t *iterator) {
    delete AsPathRawIter(iterator);
}

// Query methods

void sk_path_get_bounds(const sk_path_t* cpath, sk_rect_t* crect) {
    *crect = ToRect(AsPath(cpath)->getBounds());
}

void sk_path_compute_tight_bounds(const sk_path_t* cpath, sk_rect_t* crect) {
    *crect = ToRect(AsPath(cpath)->computeTightBounds());
}

int sk_path_count_points(const sk_path_t* cpath) {
    return AsPath(cpath)->countPoints();
}

int sk_path_count_verbs(const sk_path_t* cpath) {
    return AsPath(cpath)->countVerbs();
}

void sk_path_get_point(const sk_path_t* cpath, int index, sk_point_t* cpoint) {
    *cpoint = ToPoint(AsPath(cpath)->getPoint(index));
}

int sk_path_get_points(const sk_path_t* cpath, sk_point_t* cpoints, int max) {
    return AsPath(cpath)->getPoints(SkSpan<SkPoint>(AsPoint(cpoints), max));
}

bool sk_path_contains(const sk_path_t* cpath, float x, float y) {
    return AsPath(cpath)->contains(x, y);
}

bool sk_path_parse_svg_string(sk_path_t* cpath, const char* str) {
    return SkParsePath::FromSVGString(str, AsPath(cpath));
}

void sk_path_to_svg_string(const sk_path_t* cpath, sk_string_t* str) {
    SkString svg = SkParsePath::ToSVGString(*AsPath(cpath));
    svg.swap(*AsString(str));
}

bool sk_path_get_last_point(const sk_path_t* cpath, sk_point_t* point) {
    return AsPath(cpath)->getLastPt(AsPoint(point));
}

bool sk_path_is_convex(const sk_path_t* cpath) {
    return AsPath(cpath)->isConvex();
}

uint32_t sk_path_get_segment_masks(sk_path_t* cpath) {
    return AsPath(cpath)->getSegmentMasks();
}

bool sk_path_is_oval(sk_path_t* cpath, sk_rect_t* bounds) {
    return AsPath(cpath)->isOval(AsRect(bounds));
}

bool sk_path_is_rrect(sk_path_t* cpath, sk_rrect_t* bounds) {
    return AsPath(cpath)->isRRect(AsRRect(bounds));
}

bool sk_path_is_line(sk_path_t* cpath, sk_point_t line[2]) {
    return AsPath(cpath)->isLine(AsPoint(line));
}

bool sk_path_is_rect(sk_path_t* cpath, sk_rect_t* rect, bool* isClosed, sk_path_direction_t* direction) {
    return AsPath(cpath)->isRect(AsRect(rect), isClosed, (SkPathDirection*)direction);
}

int sk_path_convert_conic_to_quads(const sk_point_t* p0, const sk_point_t* p1, const sk_point_t* p2, float w, sk_point_t* pts, int pow2) {
    return SkPath::ConvertConicToQuads(*AsPoint(p0), *AsPoint(p1), *AsPoint(p2), w, AsPoint(pts), pow2);
}

// Path Ops

bool sk_pathop_op(const sk_path_t* one, const sk_path_t* two, sk_pathop_t op, sk_path_t* result) {
    return Op(*AsPath(one), *AsPath(two), (SkPathOp)op, AsPath(result));
}

bool sk_pathop_simplify(const sk_path_t* path, sk_path_t* result) {
    return Simplify(*AsPath(path), AsPath(result));
}

bool sk_pathop_tight_bounds(const sk_path_t* path, sk_rect_t* result) {
    auto rect = AsPath(path)->computeTightBounds();
    if (rect.isFinite()) {
        *AsRect(result) = rect;
        return true;
    }
    return false;
}

bool sk_pathop_as_winding(const sk_path_t* path, sk_path_t* result) {
    return AsWinding(*AsPath(path), AsPath(result));
}

sk_opbuilder_t* sk_opbuilder_new(void) {
    return ToOpBuilder(new SkOpBuilder());
}

void sk_opbuilder_destroy(sk_opbuilder_t* builder) {
    delete AsOpBuilder(builder);
}

void sk_opbuilder_add(sk_opbuilder_t* builder, const sk_path_t* path, sk_pathop_t op) {
    AsOpBuilder(builder)->add(*AsPath(path), (SkPathOp)op);
}

bool sk_opbuilder_resolve(sk_opbuilder_t* builder, sk_path_t* result) {
    return AsOpBuilder(builder)->resolve(AsPath(result));
}

// Path Measure

sk_pathmeasure_t* sk_pathmeasure_new(void) {
    return ToPathMeasure(new SkPathMeasure());
}

sk_pathmeasure_t* sk_pathmeasure_new_with_path(const sk_path_t* path, bool forceClosed, float resScale) {
    return ToPathMeasure(new SkPathMeasure(*AsPath(path), forceClosed, resScale));
}

void sk_pathmeasure_destroy(sk_pathmeasure_t* pathMeasure) {
    delete AsPathMeasure(pathMeasure);
}

void sk_pathmeasure_set_path(sk_pathmeasure_t* pathMeasure, const sk_path_t* path, bool forceClosed) {
    AsPathMeasure(pathMeasure)->setPath(AsPath(path), forceClosed);
}

float sk_pathmeasure_get_length(sk_pathmeasure_t* pathMeasure) {
    return AsPathMeasure(pathMeasure)->getLength();
}

bool sk_pathmeasure_get_pos_tan(sk_pathmeasure_t* pathMeasure, float distance, sk_point_t* position, sk_vector_t* tangent) {
    return AsPathMeasure(pathMeasure)->getPosTan(distance, AsPoint(position), AsPoint(tangent));
}

bool sk_pathmeasure_get_matrix(sk_pathmeasure_t* pathMeasure, float distance, sk_matrix_t* matrix, sk_pathmeasure_matrixflags_t flags) {
    SkMatrix skmatrix;
    bool result = AsPathMeasure(pathMeasure)->getMatrix(distance, &skmatrix, (SkPathMeasure::MatrixFlags)flags);
    *matrix = ToMatrix(&skmatrix);
    return result;
}

bool sk_pathmeasure_get_segment(sk_pathmeasure_t* pathMeasure, float start, float stop, sk_pathbuilder_t* dst, bool startWithMoveTo) {
    return AsPathMeasure(pathMeasure)->getSegment(start, stop, AsPathBuilder(dst), startWithMoveTo);
}

bool sk_pathmeasure_is_closed(sk_pathmeasure_t* pathMeasure) {
    return AsPathMeasure(pathMeasure)->isClosed();
}

bool sk_pathmeasure_next_contour(sk_pathmeasure_t* pathMeasure) {
    return AsPathMeasure(pathMeasure)->nextContour();
}
