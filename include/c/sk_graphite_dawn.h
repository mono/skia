/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_graphite_dawn_DEFINED
#define sk_graphite_dawn_DEFINED

#include "include/c/sk_types.h"
#include "include/c/sk_graphite.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Opaque heap wrapper around skgpu::graphite::DawnBackendContext.
typedef struct sk_graphite_dawn_backend_context_t sk_graphite_dawn_backend_context_t;

// Init struct: caller-owned WGPUInstance/Device/Queue raw handles. The shim
// AddRef's each at construction; the wrapper owns those references and
// releases them on delete.
//
// fNonYielding: when true, no DawnTickFunction is installed (Skia's
// "non-yielding context" mode). Required when running over Emscripten without
// -s ASYNCIFY. Native Dawn callers should leave it false — the shim installs
// DawnNativeProcessEventsFunction by default.
typedef struct {
    void* fInstance;       // WGPUInstance
    void* fDevice;         // WGPUDevice
    void* fQueue;          // WGPUQueue
    bool  fNonYielding;
} sk_graphite_dawn_backend_context_init_t;

SK_C_API sk_graphite_dawn_backend_context_t* sk_graphite_dawn_backend_context_new(const sk_graphite_dawn_backend_context_init_t* init);
SK_C_API void                                sk_graphite_dawn_backend_context_delete(sk_graphite_dawn_backend_context_t* bc);

SK_C_API sk_graphite_context_t* sk_graphite_context_make_dawn(
    const sk_graphite_dawn_backend_context_t* bc,
    const sk_graphite_context_options_t* opts /* nullable -> defaults */);

// Wrap an externally-allocated WGPUTexture as a Graphite BackendTexture. The
// shim queries width/height/format/usage directly from the texture, so this
// is the path for WebGPU swap-chain textures (canvas.getContext('webgpu')
// .getCurrentTexture()).
//
// Lifetime: the BackendTexture does NOT retain or release the WGPUTexture —
// caller keeps it alive for the wrapper's lifetime. Any SkSurface/SkImage that
// wraps this BackendTexture *will* retain it for its own lifetime, so caller
// can drop their reference once the Surface is built.
SK_C_API sk_graphite_backend_texture_t* sk_graphite_dawn_backend_texture_new(
    void* wgpuTexture);  // WGPUTexture

SK_C_PLUS_PLUS_END_GUARD

#endif  // sk_graphite_dawn_DEFINED
