/*
 * Copyright 2025 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"

#include "include/c/sk_pathbuilder.h"

#include "src/c/sk_types_priv.h"
#include "src/core/SkPathPriv.h"

sk_pathbuilder_t* sk_pathbuilder_new(void) {
    return ToPathBuilder(new SkPathBuilder());
}

sk_pathbuilder_t* sk_pathbuilder_new_from_path(const sk_path_t* path) {
    return ToPathBuilder(new SkPathBuilder(*AsPath(path)));
}

void sk_pathbuilder_delete(sk_pathbuilder_t* builder) {
    delete AsPathBuilder(builder);
}

void sk_pathbuilder_move_to(sk_pathbuilder_t* builder, float x, float y) {
    AsPathBuilder(builder)->moveTo(x, y);
}

void sk_pathbuilder_line_to(sk_pathbuilder_t* builder, float x, float y) {
    AsPathBuilder(builder)->lineTo(x, y);
}

void sk_pathbuilder_quad_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1) {
    AsPathBuilder(builder)->quadTo(x0, y0, x1, y1);
}

void sk_pathbuilder_conic_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1, float w) {
    AsPathBuilder(builder)->conicTo(x0, y0, x1, y1, w);
}

void sk_pathbuilder_cubic_to(sk_pathbuilder_t* builder, float x0, float y0, float x1, float y1, float x2, float y2) {
    AsPathBuilder(builder)->cubicTo(x0, y0, x1, y1, x2, y2);
}

void sk_pathbuilder_arc_to(sk_pathbuilder_t* builder, float rx, float ry, float xAxisRotate, sk_path_arc_size_t largeArc, sk_path_direction_t sweep, float x, float y) {
    AsPathBuilder(builder)->arcTo(SkPoint::Make(rx, ry), xAxisRotate, (SkPathBuilder::ArcSize)largeArc, (SkPathDirection)sweep, SkPoint::Make(x, y));
}

void sk_pathbuilder_arc_to_with_oval(sk_pathbuilder_t* builder, const sk_rect_t* oval, float startAngle, float sweepAngle, bool forceMoveTo) {
    AsPathBuilder(builder)->arcTo(*AsRect(oval), startAngle, sweepAngle, forceMoveTo);
}

void sk_pathbuilder_arc_to_with_points(sk_pathbuilder_t* builder, float x1, float y1, float x2, float y2, float radius) {
    AsPathBuilder(builder)->arcTo(SkPoint::Make(x1, y1), SkPoint::Make(x2, y2), radius);
}

void sk_pathbuilder_close(sk_pathbuilder_t* builder) {
    AsPathBuilder(builder)->close();
}

void sk_pathbuilder_rmove_to(sk_pathbuilder_t* builder, float dx, float dy) {
    AsPathBuilder(builder)->rMoveTo(dx, dy);
}

void sk_pathbuilder_rline_to(sk_pathbuilder_t* builder, float dx, float dy) {
    AsPathBuilder(builder)->rLineTo(dx, dy);
}

void sk_pathbuilder_rquad_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1) {
    AsPathBuilder(builder)->rQuadTo(dx0, dy0, dx1, dy1);
}

void sk_pathbuilder_rconic_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1, float w) {
    AsPathBuilder(builder)->rConicTo(dx0, dy0, dx1, dy1, w);
}

void sk_pathbuilder_rcubic_to(sk_pathbuilder_t* builder, float dx0, float dy0, float dx1, float dy1, float dx2, float dy2) {
    AsPathBuilder(builder)->rCubicTo(dx0, dy0, dx1, dy1, dx2, dy2);
}

void sk_pathbuilder_rarc_to(sk_pathbuilder_t* builder, float rx, float ry, float xAxisRotate, sk_path_arc_size_t largeArc, sk_path_direction_t sweep, float x, float y) {
    AsPathBuilder(builder)->rArcTo(SkPoint::Make(rx, ry), xAxisRotate, (SkPathBuilder::ArcSize)largeArc, (SkPathDirection)sweep, SkPoint::Make(x, y));
}

void sk_pathbuilder_add_rect(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir) {
    AsPathBuilder(builder)->addRect(*AsRect(rect), (SkPathDirection)dir);
}

void sk_pathbuilder_add_rect_start(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir, uint32_t startIndex) {
    AsPathBuilder(builder)->addRect(*AsRect(rect), (SkPathDirection)dir, startIndex);
}

void sk_pathbuilder_add_rrect(sk_pathbuilder_t* builder, const sk_rrect_t* rect, sk_path_direction_t dir) {
    AsPathBuilder(builder)->addRRect(*AsRRect(rect), (SkPathDirection)dir);
}

void sk_pathbuilder_add_rrect_start(sk_pathbuilder_t* builder, const sk_rrect_t* rect, sk_path_direction_t dir, uint32_t start) {
    AsPathBuilder(builder)->addRRect(*AsRRect(rect), (SkPathDirection)dir, start);
}

void sk_pathbuilder_add_rounded_rect(sk_pathbuilder_t* builder, const sk_rect_t* rect, float rx, float ry, sk_path_direction_t dir) {
    SkRRect rrect;
    rrect.setRectXY(*AsRect(rect), rx, ry);
    AsPathBuilder(builder)->addRRect(rrect, (SkPathDirection)dir);
}

void sk_pathbuilder_add_oval(sk_pathbuilder_t* builder, const sk_rect_t* rect, sk_path_direction_t dir) {
    AsPathBuilder(builder)->addOval(*AsRect(rect), (SkPathDirection)dir);
}

void sk_pathbuilder_add_circle(sk_pathbuilder_t* builder, float x, float y, float radius, sk_path_direction_t dir) {
    AsPathBuilder(builder)->addCircle(x, y, radius, (SkPathDirection)dir);
}

void sk_pathbuilder_add_arc(sk_pathbuilder_t* builder, const sk_rect_t* rect, float startAngle, float sweepAngle) {
    AsPathBuilder(builder)->addArc(*AsRect(rect), startAngle, sweepAngle);
}

void sk_pathbuilder_add_poly(sk_pathbuilder_t* builder, const sk_point_t* points, int count, bool close) {
    AsPathBuilder(builder)->addPolygon(SkSpan<const SkPoint>(AsPoint(points), count), close);
}

void sk_pathbuilder_add_path_offset(sk_pathbuilder_t* builder, const sk_path_t* other, float dx, float dy, sk_path_add_mode_t add_mode) {
    AsPathBuilder(builder)->addPath(*AsPath(other), dx, dy, (SkPath::AddPathMode)add_mode);
}

void sk_pathbuilder_add_path_matrix(sk_pathbuilder_t* builder, const sk_path_t* other, sk_matrix_t* matrix, sk_path_add_mode_t add_mode) {
    AsPathBuilder(builder)->addPath(*AsPath(other), AsMatrix(matrix), (SkPath::AddPathMode)add_mode);
}

void sk_pathbuilder_add_path(sk_pathbuilder_t* builder, const sk_path_t* other, sk_path_add_mode_t add_mode) {
    AsPathBuilder(builder)->addPath(*AsPath(other), (SkPath::AddPathMode)add_mode);
}

void sk_pathbuilder_reverse_add_path(sk_pathbuilder_t* builder, const sk_path_t* other) {
    SkPathPriv::ReverseAddPath(AsPathBuilder(builder), *AsPath(other));
}

void sk_pathbuilder_set_filltype(sk_pathbuilder_t* builder, sk_path_filltype_t filltype) {
    AsPathBuilder(builder)->setFillType((SkPathFillType)filltype);
}

sk_path_filltype_t sk_pathbuilder_get_filltype(const sk_pathbuilder_t* builder) {
    return (sk_path_filltype_t)AsPathBuilder(builder)->fillType();
}

void sk_pathbuilder_reset(sk_pathbuilder_t* builder) {
    AsPathBuilder(builder)->reset();
}

sk_path_t* sk_pathbuilder_detach_path(sk_pathbuilder_t* builder) {
    return ToPath(new SkPath(AsPathBuilder(builder)->detach()));
}

sk_path_t* sk_pathbuilder_snapshot_path(sk_pathbuilder_t* builder) {
    return ToPath(new SkPath(AsPathBuilder(builder)->snapshot()));
}
