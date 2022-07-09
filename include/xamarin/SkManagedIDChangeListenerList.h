/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManagedIDChangeListenerList_h
#define SkManagedIDChangeListenerList_h

#include "include/private/SkIDChangeListener.h"
#include "include/core/SkTypes.h"

class SK_API SkManagedIDChangeListenerList;

// delegate declarations

// managed Allocator
class SkManagedIDChangeListenerList : public SkIDChangeListener::List {
public:
    SkManagedIDChangeListenerList(void* context);

    ~SkManagedIDChangeListenerList();

    typedef void (*DestroyProc)(SkManagedIDChangeListenerList* d, void* context);

    struct Procs {
        DestroyProc fDestroy = nullptr;
    };

    static void setProcs(Procs procs);

private:
    void* fContext;
    static Procs fProcs;

    typedef SkIDChangeListener::List INHERITED;
};


#endif
