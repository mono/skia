/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedIDChangeListenerList.h"

SkManagedIDChangeListenerList::Procs SkManagedIDChangeListenerList::fProcs;

void SkManagedIDChangeListenerList::setProcs(SkManagedIDChangeListenerList::Procs procs) {
    fProcs = procs;
}

SkManagedIDChangeListenerList::SkManagedIDChangeListenerList(void* context) {
    fContext = context;
}

SkManagedIDChangeListenerList::~SkManagedIDChangeListenerList() {
    if (fProcs.fDestroy) {
        fProcs.fDestroy(this, fContext);
    }
}
