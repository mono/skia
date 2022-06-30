/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managed_pixel_ref_DEFINED
#define sk_managed_pixel_ref_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef void (*sk_pixel_ref_destroy_proc)(sk_pixel_ref_t* d, void* context);

typedef struct {
    sk_pixel_ref_destroy_proc fDestroy;
} sk_pixel_ref_procs_t;

SK_X_API sk_pixel_ref_t* sk_managed_pixel_ref_new_from_existing(void* context, void* pixelRef);
SK_X_API sk_pixel_ref_t* sk_managed_pixel_ref_new(void* context, int32_t, int32_t, void*, size_t);
SK_X_API void sk_managed_pixel_ref_delete(sk_pixel_ref_t*);
SK_X_API sk_isize_t sk_managed_pixel_ref_dimensions(sk_pixel_ref_t*);
SK_X_API int32_t sk_managed_pixel_ref_width(sk_pixel_ref_t*);
SK_X_API int32_t sk_managed_pixel_ref_height(sk_pixel_ref_t*);
SK_X_API size_t sk_managed_pixel_ref_rowBytes(sk_pixel_ref_t*);
SK_X_API void* sk_managed_pixel_ref_pixels(sk_pixel_ref_t*);
SK_X_API void* sk_managed_pixel_ref_pixel_ref(sk_pixel_ref_t* d);
SK_X_API uint32_t sk_managed_pixel_ref_generation_id(sk_pixel_ref_t*);
SK_X_API void sk_managed_pixel_ref_notify_pixels_changed(sk_pixel_ref_t*);
SK_X_API bool sk_managed_pixel_ref_is_immutable(sk_pixel_ref_t*);
SK_X_API void sk_managed_pixel_ref_set_immutable(sk_pixel_ref_t*);
//SK_X_API void sk_managed_pixel_ref_add_generation_id_listener(sk_pixel_ref_t*, sk_id_change_listener_t*);
SK_X_API void sk_managed_pixel_ref_notify_added_to_cache(sk_pixel_ref_t*);
SK_X_API void sk_managed_pixel_ref_set_procs(sk_pixel_ref_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
