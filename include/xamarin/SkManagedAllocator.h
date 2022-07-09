/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManagedAllocator_h
#define SkManagedAllocator_h

#include "include/core/SkBitmap.h"
#include "include/core/SkTypes.h"

class SkBitmap;

class SK_API SkManagedAllocator;

// delegate declarations

// managed Allocator
class SkManagedAllocator : public SkBitmap::Allocator {
public:
    SkManagedAllocator(void* context);

    ~SkManagedAllocator() override;

public:
    typedef bool       (*AllocProc)              (SkManagedAllocator* d, void* context, SkBitmap* bitmap);
    typedef void       (*DestroyProc)            (SkManagedAllocator* d, void* context);

    struct Procs {
        AllocProc fAllocPixelRef = nullptr;
        DestroyProc fDestroy = nullptr;
    };

    static void setProcs(Procs procs);

protected:
    bool allocPixelRef(SkBitmap* bitmap) override;

private:
    void* fContext;
    static Procs fProcs;

    typedef SkBitmap::Allocator INHERITED;
};


#endif
