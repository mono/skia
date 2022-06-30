/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managedallocator_DEFINED
#define sk_managedallocator_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef struct sk_managedallocator_t sk_managedallocator_t;

typedef bool          (*sk_managedallocator_allocpixelref_proc)    (sk_managedallocator_t* d, void* context, sk_bitmap_t* bitmap);
typedef void          (*sk_managedallocator_destroy_proc)    (sk_managedallocator_t* d, void* context);

typedef struct {
    sk_managedallocator_allocpixelref_proc fAllocPixelRef;
    sk_managedallocator_destroy_proc fDestroy;
} sk_managedallocator_procs_t;

SK_X_API sk_managedallocator_t* sk_managedallocator_new(void* context);
SK_X_API void sk_managedallocator_delete(sk_managedallocator_t*);
SK_X_API void sk_managedallocator_set_procs(sk_managedallocator_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
