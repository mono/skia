/*
 * Copyright 2022 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkManagedPngChunkReader_h
#define SkManagedPngChunkReader_h

#include "include/core/SkPngChunkReader.h"
#include "include/core/SkTypes.h"

class SK_API SkManagedPngChunkReader;

// delegate declarations

class SkManagedPngChunkReader : public SkPngChunkReader {
public:
    SkManagedPngChunkReader(void* context);

    ~SkManagedPngChunkReader() override;

public:
    typedef bool       (*ReadChunkProc)          (SkManagedPngChunkReader* d, void* context, const char* tag, const void* data, size_t length);
    typedef void       (*DestroyProc)            (SkManagedPngChunkReader* d, void* context);

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
