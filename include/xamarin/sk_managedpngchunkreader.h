/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managedpngchunkreader_DEFINED
#define sk_managedpngchunkreader_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef struct sk_managedpngchunkreader_t sk_managedpngchunkreader_t;

typedef bool          (*sk_managedpngchunkreader_read_chunk_proc) (sk_managedpngchunkreader_t* d, void* context, const char tag[], const void* data, size_t length);
typedef void          (*sk_managedpngchunkreader_destroy_proc)    (sk_managedpngchunkreader_t* d, void* context);

typedef struct {
    sk_managedpngchunkreader_read_chunk_proc fReadChunk;
    sk_managedpngchunkreader_destroy_proc fDestroy;
} sk_managedpngchunkreader_procs_t;

SK_X_API sk_managedpngchunkreader_t* sk_managedpngchunkreader_new(void* context);
SK_X_API void sk_managedpngchunkreader_delete(sk_managedpngchunkreader_t*);
SK_X_API void sk_managedpngchunkreader_set_procs(sk_managedpngchunkreader_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
