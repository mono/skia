/*
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedDrawable.h"

#include "include/xamarin/sk_manageddrawable.h"
#include "include/c/sk_picture.h"
#include "src/c/sk_types_priv.h"

static inline SkManagedDrawable* AsManagedDrawable(sk_manageddrawable_t* d) {
    return reinterpret_cast<SkManagedDrawable*>(d);
}
static inline sk_manageddrawable_t* ToManagedDrawable(SkManagedDrawable* d) {
    return reinterpret_cast<sk_manageddrawable_t*>(d);
}

static sk_manageddrawable_procs_t gProcs;

void dDraw(SkManagedDrawable* d, void* context, SkCanvas* canvas) {
    if (!gProcs.fDraw) return;
    gProcs.fDraw(ToManagedDrawable(d), context, ToCanvas(canvas));
}
void dGetBounds(SkManagedDrawable* d, void* context, SkRect* rect) {
    if (!gProcs.fGetBounds) return;
    gProcs.fGetBounds(ToManagedDrawable(d), context, ToRect(rect));
}
size_t dApproximateBytesUsed(SkManagedDrawable* d, void* context) {
    if (!gProcs.fApproximateBytesUsed) return 0;
    return gProcs.fApproximateBytesUsed(ToManagedDrawable(d), context);
}
sk_sp<SkPicture> dMakePictureSnapshot(SkManagedDrawable* d, void* context) {
    if (!gProcs.fMakePictureSnapshot) return nullptr;
    sk_picture_t* pic = gProcs.fMakePictureSnapshot(ToManagedDrawable(d), context);
    return sk_ref_sp(AsPicture(pic));
}
void dDestroy(SkManagedDrawable* d, void* context) {
    if (!gProcs.fDestroy) return;
    gProcs.fDestroy(ToManagedDrawable(d), context);
}

sk_manageddrawable_t* sk_manageddrawable_new(void* context) {
    return ToManagedDrawable(new SkManagedDrawable(context));
}
void sk_manageddrawable_unref(sk_manageddrawable_t* drawable) {
    SkSafeUnref(AsManagedDrawable(drawable));
}
void sk_manageddrawable_set_procs(sk_manageddrawable_procs_t procs) {
    gProcs = procs;

    SkManagedDrawable::Procs p;
    p.fDraw = dDraw;
    p.fGetBounds = dGetBounds;
    p.fApproximateBytesUsed = dApproximateBytesUsed;
    p.fMakePictureSnapshot = dMakePictureSnapshot;
    p.fDestroy = dDestroy;

    SkManagedDrawable::setProcs(p);
}
