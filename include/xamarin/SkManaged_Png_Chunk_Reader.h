/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManaged_Png_Chunk_Reader_h
#define SkManaged_Png_Chunk_Reader_h

#include "include/core/SkPngChunkReader.h"
#include "include/core/SkTypes.h"

class SK_API SkManaged_Png_Chunk_Reader;

// delegate declarations

// managed _Png_Chunk_Reader
class SkManaged_Png_Chunk_Reader : public SkPngChunkReader {
public:
    SkManaged_Png_Chunk_Reader(void* context);

    ~SkManaged_Png_Chunk_Reader() override;

public:
    typedef bool       (*ReadChunkProc)          (SkManaged_Png_Chunk_Reader* d, void* context, const char* tag, const void* data, size_t length);
    typedef void       (*DestroyProc)            (SkManaged_Png_Chunk_Reader* d, void* context);

    struct Procs {
        ReadChunkProc fReadChunk = nullptr;
        DestroyProc fDestroy = nullptr;
    };

    static void setProcs(Procs procs);

protected:
    bool readChunk(const char tag[], const void* data, size_t length) override;

private:
    void* fContext;
    static Procs fProcs;

    typedef SkPngChunkReader INHERITED;
};


#endif
