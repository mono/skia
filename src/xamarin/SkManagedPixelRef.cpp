/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// TODO: make this more like SKDrawable
//   - inherit directly instead of using obj ptr

#include "include/xamarin/SkManagedPixelRef.h"

SkManagedPixelRef::Procs SkManagedPixelRef::fProcs;

void SkManagedPixelRef::setProcs(SkManagedPixelRef::Procs procs) {
    fProcs = procs;
}

SkManagedPixelRef::SkManagedPixelRef(void* context, SkPixelRef * pixelRef) {
    fContext = context;
    this->pixelRef = sk_ref_sp(pixelRef);
}

SkManagedPixelRef::SkManagedPixelRef(void* context, int width, int height, void* addr, size_t rowBytes) {
    fContext = context;
    this->pixelRef = sk_ref_sp(new SkPixelRef(width, height, addr, rowBytes));
}

SkManagedPixelRef::~SkManagedPixelRef() {
    if (fProcs.fDestroy) {
        fProcs.fDestroy(this, fContext);
    }
}
