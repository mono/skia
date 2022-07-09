/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managedidchangelistenerlist_DEFINED
#define sk_managedidchangelistenerlist_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef void (*sk_idchangelistenerlist_destroy_proc)(sk_idchangelistenerlist_t* d, void* context);

typedef struct {
    sk_idchangelistenerlist_destroy_proc fDestroy;
} sk_idchangelistenerlist_procs_t;

SK_X_API sk_idchangelistenerlist_t* sk_managedidchangelistenerlist_new(void* context);
SK_X_API void sk_managedidchangelistenerlist_delete(sk_idchangelistenerlist_t*);
SK_X_API void sk_managedidchangelistenerlist_add(sk_idchangelistenerlist_t*, sk_idchangelistener_t*, bool single_threaded);
SK_X_API int32_t sk_managedidchangelistenerlist_count(sk_idchangelistenerlist_t*);
SK_X_API void sk_managedidchangelistenerlist_changed(sk_idchangelistenerlist_t*, bool single_threaded);
SK_X_API void sk_managedidchangelistenerlist_reset(sk_idchangelistenerlist_t*, bool single_threaded);
SK_X_API void sk_managedidchangelistenerlist_set_procs(sk_idchangelistenerlist_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
