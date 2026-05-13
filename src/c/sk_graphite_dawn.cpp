/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/sk_graphite_dawn.h"

#include "include/core/SkTypes.h"

#if defined(SK_GRAPHITE) && defined(SK_DAWN)

#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"

// Pulls in Context/BackendTexture + the matching As/To helpers via the
// SK_GRAPHITE block.
#include "src/c/sk_types_priv.h"

#include <memory>

namespace gr = skgpu::graphite;

// Forward-declared in sk_graphite.cpp; reused here for option translation.
// Returns false if opts carries an invalid value (e.g. out-of-range sample count).
extern bool sk_graphite_make_context_options(const sk_graphite_context_options_t* opts, gr::ContextOptions* out);

// Heap-allocated wrapper holding a DawnBackendContext value. The wgpu::Instance/
// Device/Queue smart pointers AddRef on construction-from-raw and Release on
// destruction, so caller's references remain unaffected by our wrapper's
// lifetime.
struct sk_graphite_dawn_backend_context_t {
    gr::DawnBackendContext dbc;
};

extern "C" SK_C_API sk_graphite_dawn_backend_context_t* sk_graphite_dawn_backend_context_new(const sk_graphite_dawn_backend_context_init_t* init) {
    if (!init || !init->fInstance || !init->fDevice || !init->fQueue) return nullptr;
    auto* bc = new sk_graphite_dawn_backend_context_t;
    // wgpu::Instance/Device/Queue construct-from-raw with AddRef semantics
    // (the wgpu C++ wrappers' default constructor takes a raw C handle and
    // adds a reference unless explicitly told to acquire).
    bc->dbc.fInstance = wgpu::Instance(static_cast<WGPUInstance>(init->fInstance));
    bc->dbc.fDevice   = wgpu::Device  (static_cast<WGPUDevice>  (init->fDevice));
    bc->dbc.fQueue    = wgpu::Queue   (static_cast<WGPUQueue>   (init->fQueue));
    if (init->fNonYielding) {
        bc->dbc.fTick = nullptr;
    }
    // else: leave the default (DawnNativeProcessEventsFunction on non-Emscripten).
    return bc;
}

extern "C" SK_C_API void sk_graphite_dawn_backend_context_delete(sk_graphite_dawn_backend_context_t* bc) {
    delete bc;  // wgpu::* destructors call Release
}

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_dawn(
    const sk_graphite_dawn_backend_context_t* bc,
    const sk_graphite_context_options_t* opts)
{
    if (!bc) return nullptr;
    gr::ContextOptions gopts;
    if (!sk_graphite_make_context_options(opts, &gopts)) return nullptr;
    auto context = gr::ContextFactory::MakeDawn(bc->dbc, gopts);
    return ToGraphiteContext(context.release());
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_dawn_backend_texture_new(void* wgpuTexture) {
    if (!wgpuTexture) return nullptr;
    auto bt = gr::BackendTextures::MakeDawn(static_cast<WGPUTexture>(wgpuTexture));
    if (!bt.isValid()) return nullptr;
    return ToGraphiteBackendTexture(new gr::BackendTexture(bt));
}

#else  // !(SK_GRAPHITE && SK_DAWN)

extern "C" SK_C_API sk_graphite_dawn_backend_context_t* sk_graphite_dawn_backend_context_new(const sk_graphite_dawn_backend_context_init_t*) { return nullptr; }
extern "C" SK_C_API void sk_graphite_dawn_backend_context_delete(sk_graphite_dawn_backend_context_t*) {}
extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_dawn(const sk_graphite_dawn_backend_context_t*, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_dawn_backend_texture_new(void*) { return nullptr; }

#endif  // SK_GRAPHITE && SK_DAWN
