/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManagedPngChunkReader.h"

SkManagedPngChunkReader::Procs SkManagedPngChunkReader::fProcs;

void SkManagedPngChunkReader::setProcs(SkManagedPngChunkReader::Procs procs) {
    fProcs = procs;
}

SkManagedPngChunkReader::SkManagedPngChunkReader(void* context) {
    fContext = context;
}

SkManagedPngChunkReader::~SkManagedPngChunkReader() {
    if (!fProcs.fDestroy) return;
    fProcs.fDestroy(this, fContext);
}

bool SkManagedPngChunkReader::readChunk(const char tag[], const void* data, size_t length) {
    if (!fProcs.fReadChunk) return false;
    return fProcs.fReadChunk(this, fContext, tag, data, length);
}
