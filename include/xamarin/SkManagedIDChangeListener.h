/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManagedIDChangeListener_h
#define SkManagedIDChangeListener_h

#include "include/private/SkIDChangeListener.h"
#include "include/core/SkTypes.h"

class SkIDChangeListener;

class SK_API SkManagedIDChangeListener;

// delegate declarations

// managed Allocator
class SkManagedIDChangeListener : public SkIDChangeListener {
public:
    SkManagedIDChangeListener(void* context);

    ~SkManagedIDChangeListener() override;

public:
    typedef void       (*ChangedProc)              (SkManagedIDChangeListener* d, void* context);
    typedef void       (*DestroyProc)              (SkManagedIDChangeListener* d, void* context);

    struct Procs {
        ChangedProc fChanged = nullptr;
        DestroyProc fDestroy = nullptr;
    };

    static void setProcs(Procs procs);

protected:
    void changed() override;

private:
    void* fContext;
    static Procs fProcs;

    typedef SkIDChangeListener INHERITED;
};


#endif
