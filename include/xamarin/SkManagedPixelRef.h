/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManagedPixelRef_h
#define SkManagedPixelRef_h

#include "include/core/SkPixelRef.h"
#include "include/core/SkTypes.h"

class SkPixelRef;

class SK_API SkManagedPixelRef;

// delegate declarations

// managed Allocator
class SkManagedPixelRef {
public:

    sk_sp<SkPixelRef> pixelRef;

    SkManagedPixelRef(void* context, SkPixelRef * pixelRef);

    SkManagedPixelRef(void* context, int32_t, int32_t, void*, size_t);

    virtual ~SkManagedPixelRef();

    typedef void (*DestroyProc)(SkManagedPixelRef* d, void* context);

    struct Procs {
        DestroyProc fDestroy = nullptr;
    };

    static void setProcs(Procs procs);

private:
    void* fContext;
    static Procs fProcs;
};


#endif
