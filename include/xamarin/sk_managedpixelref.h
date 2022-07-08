/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managedpixelref_DEFINED
#define sk_managedpixelref_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef void (*sk_pixelref_destroy_proc)(sk_pixelref_t* d, void* context);

typedef struct {
    sk_pixelref_destroy_proc fDestroy;
} sk_pixelref_procs_t;

SK_X_API sk_pixelref_t* sk_managedpixelref_new_from_existing(void* context, void* pixelRef);
SK_X_API sk_pixelref_t* sk_managedpixelref_new(void* context, int32_t, int32_t, void*, size_t);
SK_X_API void sk_managedpixelref_delete(sk_pixelref_t*);
SK_X_API sk_isize_t sk_managedpixelref_dimensions(sk_pixelref_t*);
SK_X_API int32_t sk_managedpixelref_width(sk_pixelref_t*);
SK_X_API int32_t sk_managedpixelref_height(sk_pixelref_t*);
SK_X_API size_t sk_managedpixelref_rowBytes(sk_pixelref_t*);
SK_X_API void* sk_managedpixelref_pixels(sk_pixelref_t*);
SK_X_API void* sk_managedpixelref_pixelref(sk_pixelref_t* d);
SK_X_API uint32_t sk_managedpixelref_generation_id(sk_pixelref_t*);
SK_X_API void sk_managedpixelref_notify_pixels_changed(sk_pixelref_t*);
SK_X_API bool sk_managedpixelref_is_immutable(sk_pixelref_t*);
SK_X_API void sk_managedpixelref_set_immutable(sk_pixelref_t*);
//SK_X_API void sk_managedpixelref_add_generation_id_listener(sk_pixelref_t*, sk_id_change_listener_t*);
SK_X_API void sk_managedpixelref_notify_added_to_cache(sk_pixelref_t*);
SK_X_API void sk_managedpixelref_set_procs(sk_pixelref_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
