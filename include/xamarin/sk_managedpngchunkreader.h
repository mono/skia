/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_managed_png_chunk_reader_DEFINED
#define sk_managed_png_chunk_reader_DEFINED

#include "sk_xamarin.h"

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

typedef struct sk_managed_png_chunk_reader_t sk_managed_png_chunk_reader_t;

typedef bool          (*sk_managed_png_chunk_reader_read_chunk_proc) (sk_managed_png_chunk_reader_t* d, void* context, const char tag[], const void* data, size_t length);
typedef void          (*sk_managed_png_chunk_reader_destroy_proc)    (sk_managed_png_chunk_reader_t* d, void* context);

typedef struct {
    sk_managed_png_chunk_reader_read_chunk_proc fReadChunk;
    sk_managed_png_chunk_reader_destroy_proc fDestroy;
} sk_managed_png_chunk_reader_procs_t;

SK_X_API sk_managed_png_chunk_reader_t* sk_managed_png_chunk_reader_new(void* context);
SK_X_API void sk_managed_png_chunk_reader_delete(sk_managed_png_chunk_reader_t*);
SK_X_API void sk_managed_png_chunk_reader_set_procs(sk_managed_png_chunk_reader_procs_t procs);

SK_C_PLUS_PLUS_END_GUARD

#endif
