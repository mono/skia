/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedPngChunkReader.h"

#include "include/xamarin/sk_managedpngchunkreader.h"
#include "src/c/sk_types_priv.h"

static inline SkManagedPngChunkReader* AsManagedPngChunkReader(sk_managedpngchunkreader_t* d) {
    return reinterpret_cast<SkManagedPngChunkReader*>(d);
}
static inline sk_managedpngchunkreader_t* ToManagedPngChunkReader(SkManagedPngChunkReader* d) {
    return reinterpret_cast<sk_managedpngchunkreader_t*>(d);
}

static sk_managedpngchunkreader_procs_t gProcs;

bool readChunk(SkManagedPngChunkReader* d, void* context, const char tag[], const void* data, size_t length) {
    if (!gProcs.fReadChunk) return false;
    return gProcs.fReadChunk(ToManagedPngChunkReader(d), context, tag, data, length);
}

void destroy(SkManagedPngChunkReader* d, void* context) {
    if (!gProcs.fDestroy) return;
    gProcs.fDestroy(ToManagedPngChunkReader(d), context);
}

sk_managedpngchunkreader_t* sk_managedpngchunkreader_new(void* context) {
    return ToManagedPngChunkReader(new SkManagedPngChunkReader(context));
}

void sk_managedpngchunkreader_delete(sk_managedpngchunkreader_t* d) {
    delete AsManagedPngChunkReader(d);
}

void sk_managedpngchunkreader_set_procs(sk_managedpngchunkreader_procs_t procs) {
    gProcs = procs;

    SkManagedPngChunkReader::Procs p;
    p.fReadChunk = readChunk;
    p.fDestroy = destroy;

    SkManagedPngChunkReader::setProcs(p);
}
