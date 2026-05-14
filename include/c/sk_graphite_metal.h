/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_graphite_metal_DEFINED
#define sk_graphite_metal_DEFINED

#include "include/c/sk_types.h"
#include "include/c/sk_graphite.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Init struct: caller-owned id<MTLDevice> + id<MTLCommandQueue>. Both are
// passed as opaque void* to keep the C API ObjC-free; the shim treats them
// as CFTypeRef and CFRetain's during MakeMetal. Skia takes its own retains
// on success, so the caller may drop their references as soon as
// sk_graphite_context_make_metal returns.
typedef struct {
    void* fDevice;   // id<MTLDevice> (CFRetain-able)
    void* fQueue;    // id<MTLCommandQueue> (CFRetain-able)
} sk_graphite_mtl_backend_context_init_t;

// Build a Graphite Context for the Metal backend. Returns null on failure
// (Metal not built into libSkiaSharp, init invalid, or device rejected).
SK_C_API sk_graphite_context_t* sk_graphite_context_make_metal(
    const sk_graphite_mtl_backend_context_init_t* init,
    const sk_graphite_context_options_t* opts /* nullable -> defaults */);

// Wrap an externally-allocated id<MTLTexture> as a Graphite BackendTexture.
// The wrapper does NOT retain the texture — Skia's contract is that the
// caller keeps the texture alive for the BackendTexture's lifetime. (See
// upstream comment in MtlGraphiteTypes_cpp.h: "The BackendTexture will not
// call retain or release on the passed in CFTypeRef.")
SK_C_API sk_graphite_backend_texture_t* sk_graphite_mtl_backend_texture_new(
    int32_t width, int32_t height,
    void* mtlTexture);  // id<MTLTexture>

SK_C_PLUS_PLUS_END_GUARD

#endif  // sk_graphite_metal_DEFINED
