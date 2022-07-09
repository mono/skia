/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedPixelRef.h"
#include "include/xamarin/SkManagedIDChangeListener.h"
#include "include/xamarin/sk_managedpixelref.h"
#include "src/c/sk_types_priv.h"

static inline SkManagedPixelRef* AsSkManagedPixelRef(sk_pixelref_t* d) {
    return reinterpret_cast<SkManagedPixelRef*>(d);
}
static inline sk_pixelref_t* ToSkPixelRef(SkManagedPixelRef* d) {
    return reinterpret_cast<sk_pixelref_t*>(d);
}

static inline SkManagedIDChangeListener* AsSkManagedIDChangeListener(sk_idchangelistener_t* d) {
    return reinterpret_cast<SkManagedIDChangeListener*>(d);
}

static sk_pixelref_procs_t gProcs;

void destroy(SkManagedPixelRef* d, void* context) {
    if (gProcs.fDestroy) {
        gProcs.fDestroy(ToSkPixelRef(d), context);
    }
}

sk_pixelref_t* sk_managedpixelref_new_from_existing(void* context, void* pixelRef) {
    return ToSkPixelRef(new SkManagedPixelRef(context, (SkPixelRef*)pixelRef));
}

sk_pixelref_t* sk_managedpixelref_new(
        void* context, int32_t width, int32_t height, void* addr, size_t rowBytes) {
    return ToSkPixelRef(new SkManagedPixelRef(context, width, height, addr, rowBytes));
}

void sk_managedpixelref_delete(sk_pixelref_t* d) {
    delete AsSkManagedPixelRef(d);
}

sk_isize_t sk_managedpixelref_dimensions(sk_pixelref_t* d) {
    return ToISize(AsSkManagedPixelRef(d)->pixelRef->dimensions());
}
int32_t sk_managedpixelref_width(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->width();
}

int32_t sk_managedpixelref_height(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->height();
}
size_t sk_managedpixelref_rowBytes(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->rowBytes();
}
void* sk_managedpixelref_pixels(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->pixels();
}
void* sk_managedpixelref_pixelref(sk_pixelref_t* d) {
    // IMPORTANT!!!
    // we must keep our pixel ref in order to keep functioning
    // so we do not call release() nor unref() on it to prevent it pointing to garbage
    return AsSkManagedPixelRef(d)->pixelRef.get();
}
uint32_t sk_managedpixelref_generation_id(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->getGenerationID();
}
void sk_managedpixelref_notify_pixels_changed(sk_pixelref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->notifyPixelsChanged();
}
bool sk_managedpixelref_is_immutable(sk_pixelref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->isImmutable();
}
void sk_managedpixelref_set_immutable(sk_pixelref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->setImmutable();
}
void sk_managedpixelref_add_generation_id_listener(sk_pixelref_t* d, sk_idchangelistener_t* listener) {
    AsSkManagedPixelRef(d)->pixelRef->addGenIDChangeListener(sk_ref_sp(AsSkManagedIDChangeListener(listener)));
}
void sk_managedpixelref_notify_added_to_cache(sk_pixelref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->notifyAddedToCache();
}

void sk_managedpixelref_set_procs(sk_pixelref_procs_t procs) {
    gProcs = procs;

    SkManagedPixelRef::Procs p;
    p.fDestroy = destroy;

    SkManagedPixelRef::setProcs(p);
}
