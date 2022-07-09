/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedAllocator.h"

SkManagedAllocator::Procs SkManagedAllocator::fProcs;

void SkManagedAllocator::setProcs(SkManagedAllocator::Procs procs) {
    fProcs = procs;
}

SkManagedAllocator::SkManagedAllocator(void* context) {
    fContext = context;
}

SkManagedAllocator::~SkManagedAllocator() {
    if (fProcs.fDestroy) {
        fProcs.fDestroy(this, fContext);
    }
}

bool SkManagedAllocator::allocPixelRef(SkBitmap* bitmap) {
    if (!fProcs.fAllocPixelRef) return false;
    return fProcs.fAllocPixelRef(this, fContext, bitmap);
}
