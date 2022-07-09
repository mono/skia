/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "include/xamarin/sk_managedidchangelistener.h"
#include "include/xamarin/SkManagedIDChangeListener.h"

#include "src/c/sk_types_priv.h"

static inline SkManagedIDChangeListener* AsSkManagedIDChangeListener(sk_idchangelistener_t* d) {
    return reinterpret_cast<SkManagedIDChangeListener*>(d);
}
static inline sk_idchangelistener_t* ToSkManagedIDChangeListener(SkManagedIDChangeListener* d) {
    return reinterpret_cast<sk_idchangelistener_t*>(d);
}

static sk_idchangelistener_procs_t gProcs;

void changed(SkManagedIDChangeListener* d, void* context) {
    if (gProcs.fChanged) {
        gProcs.fChanged(ToSkManagedIDChangeListener(d), context);
    }
}

void destroy(SkManagedIDChangeListener* d, void* context) {
    if (gProcs.fDestroy) {
        gProcs.fDestroy(ToSkManagedIDChangeListener(d), context);
    }
}

sk_idchangelistener_t* sk_managedidchangelistener_new(void* context) {
    return ToSkManagedIDChangeListener(new SkManagedIDChangeListener(context));
}

void sk_managedidchangelistener_delete(sk_idchangelistener_t* d) {
    delete AsSkManagedIDChangeListener(d);
}

void sk_managedidchangelistener_mark_should_deregister(sk_idchangelistener_t* d) {
    AsSkManagedIDChangeListener(d)->markShouldDeregister();
}

bool sk_managedidchangelistener_should_deregister(sk_idchangelistener_t* d) {
    return AsSkManagedIDChangeListener(d)->shouldDeregister();
}

void sk_managedidchangelistener_set_procs(sk_idchangelistener_procs_t procs) {
    gProcs = procs;

    SkManagedIDChangeListener::Procs p;
    p.fChanged = changed;
    p.fDestroy = destroy;

    SkManagedIDChangeListener::setProcs(p);
}
