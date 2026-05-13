/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/sk_graphite_metal.h"

#include "include/core/SkTypes.h"

#if defined(SK_GRAPHITE) && defined(SK_METAL)

#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/mtl/MtlBackendContext.h"
#include "include/gpu/graphite/mtl/MtlGraphiteTypes_cpp.h"
#include "include/ports/SkCFObject.h"

// Pulls in Context/BackendTexture + the matching As/To helpers via the
// SK_GRAPHITE block.
#include "src/c/sk_types_priv.h"

#include <memory>

namespace gr = skgpu::graphite;

// Forward-declared in sk_graphite.cpp; reused here for option translation.
// Returns false if opts carries an invalid value (e.g. out-of-range sample count).
extern bool sk_graphite_make_context_options(const sk_graphite_context_options_t* opts, gr::ContextOptions* out);

// Heap-allocated wrapper holding an MtlBackendContext value. The wrapper
// CFRetains the caller-supplied device/queue at construction (via sk_ret_cfp)
// and CFReleases them on delete (via sk_cfp's destructor).
struct sk_graphite_mtl_backend_context_t {
    gr::MtlBackendContext mbc;
};

extern "C" SK_C_API sk_graphite_mtl_backend_context_t* sk_graphite_mtl_backend_context_new(const sk_graphite_mtl_backend_context_init_t* init) {
    if (!init || !init->fDevice || !init->fQueue) return nullptr;
    auto* bc = new sk_graphite_mtl_backend_context_t;
    bc->mbc.fDevice = sk_ret_cfp(static_cast<CFTypeRef>(init->fDevice));
    bc->mbc.fQueue  = sk_ret_cfp(static_cast<CFTypeRef>(init->fQueue));
    return bc;
}

extern "C" SK_C_API void sk_graphite_mtl_backend_context_delete(sk_graphite_mtl_backend_context_t* bc) {
    delete bc;  // sk_cfp destructors call CFRelease
}

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_metal(
    const sk_graphite_mtl_backend_context_t* bc,
    const sk_graphite_context_options_t* opts)
{
    if (!bc) return nullptr;
    gr::ContextOptions gopts;
    if (!sk_graphite_make_context_options(opts, &gopts)) return nullptr;
    auto context = gr::ContextFactory::MakeMetal(bc->mbc, gopts);
    return ToGraphiteContext(context.release());
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_mtl_backend_texture_new(
    int32_t width, int32_t height, void* mtlTexture)
{
    if (width <= 0 || height <= 0 || !mtlTexture) return nullptr;
    auto bt = gr::BackendTextures::MakeMetal(SkISize::Make(width, height),
                                              static_cast<CFTypeRef>(mtlTexture));
    auto* heap = new gr::BackendTexture(bt);
    return ToGraphiteBackendTexture(heap);
}

#else  // !(SK_GRAPHITE && SK_METAL)

extern "C" SK_C_API sk_graphite_mtl_backend_context_t* sk_graphite_mtl_backend_context_new(const sk_graphite_mtl_backend_context_init_t*) { return nullptr; }
extern "C" SK_C_API void sk_graphite_mtl_backend_context_delete(sk_graphite_mtl_backend_context_t*) {}
extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_metal(const sk_graphite_mtl_backend_context_t*, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_mtl_backend_texture_new(int32_t, int32_t, void*) { return nullptr; }

#endif  // SK_GRAPHITE && SK_METAL
