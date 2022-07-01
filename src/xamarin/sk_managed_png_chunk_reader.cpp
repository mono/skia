/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManaged_Png_Chunk_Reader.h"

#include "include/xamarin/sk_managed_png_chunk_reader.h"
#include "src/c/sk_types_priv.h"

static inline SkManaged_Png_Chunk_Reader* AsManaged_Png_Chunk_Reader(sk_managed_png_chunk_reader_t* d) {
    return reinterpret_cast<SkManaged_Png_Chunk_Reader*>(d);
}
static inline sk_managed_png_chunk_reader_t* ToManaged_Png_Chunk_Reader(SkManaged_Png_Chunk_Reader* d) {
    return reinterpret_cast<sk_managed_png_chunk_reader_t*>(d);
}

static sk_managed_png_chunk_reader_procs_t gProcs;

bool readChunk(SkManaged_Png_Chunk_Reader* d, void* context, const char tag[], const void* data, size_t length) {
    if (!gProcs.fReadChunk) return false;
    return gProcs.fReadChunk(ToManaged_Png_Chunk_Reader(d), context, tag, data, length);
}

void destroy(SkManaged_Png_Chunk_Reader* d, void* context) {
    if (!gProcs.fDestroy) return;
    gProcs.fDestroy(ToManaged_Png_Chunk_Reader(d), context);
}

sk_managed_png_chunk_reader_t* sk_managed_png_chunk_reader_new(void* context) {
    return ToManaged_Png_Chunk_Reader(new SkManaged_Png_Chunk_Reader(context));
}

void sk_managed_png_chunk_reader_delete(sk_managed_png_chunk_reader_t* d) {
    delete AsManaged_Png_Chunk_Reader(d);
}

void sk_managed_png_chunk_reader_set_procs(sk_managed_png_chunk_reader_procs_t procs) {
    gProcs = procs;

    SkManaged_Png_Chunk_Reader::Procs p;
    p.fReadChunk = readChunk;
    p.fDestroy = destroy;

    SkManaged_Png_Chunk_Reader::setProcs(p);
}
