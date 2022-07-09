/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_idchangelistener_DEFINED
#define sk_idchangelistener_DEFINED

#include "include/c/sk_idchangelistener.h"
#include "src/c/sk_types_priv.h"

static inline SkIDChangeListener* AsSkIDChangeListener(sk_idchangelistener_t* d) {
    return reinterpret_cast<SkIDChangeListener*>(d);
}

static sk_idchangelistener_procs_t gProcs;

void changed(SkIDChangeListener* d, void* context) {
    if (gProcs.fChanged) {
        gProcs.fChanged(ToSkIDChangeListener(d), context);
    }
}

sk_idchangelistener_t* sk_idchangelistener_new(void* context) {
    return ToSKIDChangeListener(new SkIDChangeListener(context));
}

void sk_idchangelistener_delete(sk_idchangelistener_t* d) { delete AsManagedAllocator(d); }

void sk_idchangelistener_set_procs(sk_idchangelistener_procs_t procs) {
    gProcs = procs;

    SkManagedAllocator::Procs p;
    p.fAllocPixelRef = allocPixelRef;

    SkManagedAllocator::setProcs(p);
}

#endif
