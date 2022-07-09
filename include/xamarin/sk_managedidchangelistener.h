/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managedidchangelistener_DEFINED
#define sk_managedidchangelistener_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef void (*sk_idchangelistener_changed_proc)(sk_idchangelistener_t* d, void* context);
typedef void (*sk_idchangelistener_destroy_proc)(sk_idchangelistener_t* d, void* context);

typedef struct {
    sk_idchangelistener_changed_proc fChanged;
    sk_idchangelistener_destroy_proc fDestroy;
} sk_idchangelistener_procs_t;

SK_X_API sk_idchangelistener_t* sk_managedidchangelistener_new(void* context);
SK_X_API void sk_managedidchangelistener_delete(sk_idchangelistener_t*);
SK_X_API void sk_managedidchangelistener_mark_should_deregister(sk_idchangelistener_t*);
SK_X_API bool sk_managedidchangelistener_should_deregister(sk_idchangelistener_t*);
SK_X_API void sk_managedidchangelistener_set_procs(sk_idchangelistener_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
