/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedIDChangeListener.h"

SkManagedIDChangeListener::Procs SkManagedIDChangeListener::fProcs;

void SkManagedIDChangeListener::setProcs(SkManagedIDChangeListener::Procs procs) {
    fProcs = procs;
}

SkManagedIDChangeListener::SkManagedIDChangeListener(void* context) {
    fContext = context;
}

SkManagedIDChangeListener::~SkManagedIDChangeListener() {
    if (fProcs.fDestroy) {
        fProcs.fDestroy(this, fContext);
    }
}

void SkManagedIDChangeListener::changed() {
    if (fProcs.fChanged) {
        fProcs.fChanged(this, fContext);
    }
}
