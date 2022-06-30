/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedPixelRef.h"
#include "include/xamarin/SkManaged_ID_Change_Listener.h"
#include "include/xamarin/sk_managed_pixel_ref.h"
#include "src/c/sk_types_priv.h"

static inline SkManagedPixelRef* AsSkManagedPixelRef(sk_pixel_ref_t* d) {
    return reinterpret_cast<SkManagedPixelRef*>(d);
}
static inline sk_pixel_ref_t* ToSkPixelRef(SkManagedPixelRef* d) {
    return reinterpret_cast<sk_pixel_ref_t*>(d);
}

static inline SkManaged_ID_Change_Listener* AsSkManaged_ID_Change_Listener(sk_id_change_listener_t* d) {
    return reinterpret_cast<SkManaged_ID_Change_Listener*>(d);
}

static sk_pixel_ref_procs_t gProcs;

void destroy(SkManagedPixelRef* d, void* context) {
    if (gProcs.fDestroy) {
        gProcs.fDestroy(ToSkPixelRef(d), context);
    }
}

sk_pixel_ref_t* sk_managed_pixel_ref_new_from_existing(void* context, void* pixelRef) {
    return ToSkPixelRef(new SkManagedPixelRef(context, (SkPixelRef*)pixelRef));
}

sk_pixel_ref_t* sk_managed_pixel_ref_new(
        void* context, int32_t width, int32_t height, void* addr, size_t rowBytes) {
    return ToSkPixelRef(new SkManagedPixelRef(context, width, height, addr, rowBytes));
}

void sk_managed_pixel_ref_delete(sk_pixel_ref_t* d) {
    delete AsSkManagedPixelRef(d);
}

sk_isize_t sk_managed_pixel_ref_dimensions(sk_pixel_ref_t* d) {
    return ToISize(AsSkManagedPixelRef(d)->pixelRef->dimensions());
}
int32_t sk_managed_pixel_ref_width(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->width();
}

int32_t sk_managed_pixel_ref_height(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->height();
}
size_t sk_managed_pixel_ref_rowBytes(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->rowBytes();
}
void* sk_managed_pixel_ref_pixels(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->pixels();
}
void* sk_managed_pixel_ref_pixel_ref(sk_pixel_ref_t* d) {
    // IMPORTANT!!!
    // we must keep our pixel ref in order to keep functioning
    // so we do not call release() nor unref() on it to prevent it pointing to garbage
    return AsSkManagedPixelRef(d)->pixelRef.get();
}
uint32_t sk_managed_pixel_ref_generation_id(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->getGenerationID();
}
void sk_managed_pixel_ref_notify_pixels_changed(sk_pixel_ref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->notifyPixelsChanged();
}
bool sk_managed_pixel_ref_is_immutable(sk_pixel_ref_t* d) {
    return AsSkManagedPixelRef(d)->pixelRef->isImmutable();
}
void sk_managed_pixel_ref_set_immutable(sk_pixel_ref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->setImmutable();
}
//void sk_managed_pixel_ref_add_generation_id_listener(sk_pixel_ref_t* d, sk_id_change_listener_t* listener) {
//    AsSkManagedPixelRef(d)->pixelRef->addGenIDChangeListener(sk_ref_sp(AsSkManaged_ID_Change_Listener(listener)));
//}
void sk_managed_pixel_ref_notify_added_to_cache(sk_pixel_ref_t* d) {
    AsSkManagedPixelRef(d)->pixelRef->notifyAddedToCache();
}

void sk_managed_pixel_ref_set_procs(sk_pixel_ref_procs_t procs) {
    gProcs = procs;

    SkManagedPixelRef::Procs p;
    p.fDestroy = destroy;

    SkManagedPixelRef::setProcs(p);
}
