/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skresources_resource_provider_DEFINED
#define skresources_resource_provider_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

SK_C_API void skresources_resource_provider_ref(skresources_resource_provider_t* instance);
SK_C_API void skresources_resource_provider_unref(skresources_resource_provider_t* instance);
SK_C_API void skresources_resource_provider_delete(skresources_resource_provider_t *instance);

SK_C_API skresources_resource_provider_t* skresources_file_resource_provider_make(const char* base_dir, size_t length, bool predecode);
SK_C_API skresources_resource_provider_t* skresources_caching_resource_provider_proxy_make(skresources_resource_provider_t* rp);
SK_C_API skresources_resource_provider_t* skresources_data_uri_resource_provider_proxy_make(skresources_resource_provider_t* rp, bool predecode);

SK_C_PLUS_PLUS_END_GUARD

#endif
