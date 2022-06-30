/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/xamarin/SkManaged_Png_Chunk_Reader.h"

SkManaged_Png_Chunk_Reader::Procs SkManaged_Png_Chunk_Reader::fProcs;

void SkManaged_Png_Chunk_Reader::setProcs(SkManaged_Png_Chunk_Reader::Procs procs) {
    fProcs = procs;
}

SkManaged_Png_Chunk_Reader::SkManaged_Png_Chunk_Reader(void* context) {
    fContext = context;
}

SkManaged_Png_Chunk_Reader::~SkManaged_Png_Chunk_Reader() {
    if (fProcs.fDestroy) {
        fProcs.fDestroy(this, fContext);
    }
}

bool SkManaged_Png_Chunk_Reader::readChunk(const char tag[], const void* data, size_t length) {
    if (!fProcs.fReadChunk) return false;
    return fProcs.fReadChunk(this, fContext, tag, data, length);
}
