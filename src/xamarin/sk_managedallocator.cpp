/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedAllocator.h"

#include "include/xamarin/sk_managedallocator.h"
#include "src/c/sk_types_priv.h"

static inline SkManagedAllocator* AsManagedAllocator(sk_managedallocator_t* d) {
    return reinterpret_cast<SkManagedAllocator*>(d);
}
static inline sk_managedallocator_t* ToManagedAllocator(SkManagedAllocator* d) {
    return reinterpret_cast<sk_managedallocator_t*>(d);
}

static sk_managedallocator_procs_t gProcs;

bool allocPixelRef(SkManagedAllocator* d, void* context, SkBitmap* bitmap) {
    if (!gProcs.fAllocPixelRef) return false;
    return gProcs.fAllocPixelRef(ToManagedAllocator(d), context, ToBitmap(bitmap));
}

void destroy(SkManagedAllocator* d, void* context) {
    if (gProcs.fDestroy) {
        gProcs.fDestroy(ToManagedAllocator(d), context);
    }
}

sk_managedallocator_t* sk_managedallocator_new(void* context) {
    return ToManagedAllocator(new SkManagedAllocator(context));
}

void sk_managedallocator_delete(sk_managedallocator_t* d) {
    delete AsManagedAllocator(d);
}

void sk_managedallocator_set_procs(sk_managedallocator_procs_t procs) {
    gProcs = procs;

    SkManagedAllocator::Procs p;
    p.fAllocPixelRef = allocPixelRef;
    p.fDestroy = destroy;

    SkManagedAllocator::setProcs(p);
}
