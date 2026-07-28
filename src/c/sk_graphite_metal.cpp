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

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_metal(
    const sk_graphite_mtl_backend_context_init_t* init,
    const sk_graphite_context_options_t* opts)
{
    // sk_ret_cfp CFRetains the caller's handles. The local mbc holds the
    // retains for the duration of this call; on success MakeMetal takes its
    // own retains into the resulting Context, so the local releases at
    // scope-exit are correct whether MakeMetal succeeded or failed.
    gr::MtlBackendContext mbc;
    mbc.fDevice = sk_ret_cfp(static_cast<CFTypeRef>(init->fDevice));
    mbc.fQueue  = sk_ret_cfp(static_cast<CFTypeRef>(init->fQueue));

    gr::ContextOptions gopts;
    if (!sk_graphite_make_context_options(opts, &gopts)) return nullptr;
    auto context = gr::ContextFactory::MakeMetal(mbc, gopts);
    return ToGraphiteContext(context.release());
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_mtl_backend_texture_new(
    int32_t width, int32_t height, void* mtlTexture)
{
    auto bt = gr::BackendTextures::MakeMetal(SkISize::Make(width, height),
                                              static_cast<CFTypeRef>(mtlTexture));
    auto* heap = new gr::BackendTexture(bt);
    return ToGraphiteBackendTexture(heap);
}

#else  // !(SK_GRAPHITE && SK_METAL)

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_metal(const sk_graphite_mtl_backend_context_init_t*, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_mtl_backend_texture_new(int32_t, int32_t, void*) { return nullptr; }

#endif  // SK_GRAPHITE && SK_METAL
