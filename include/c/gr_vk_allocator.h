/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef gr_vk_allocator_DEFINED
#define gr_vk_allocator_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Vulkan memory allocator public C API. Reuses gr_vk_memory_allocator_t (defined
// in sk_types.h alongside the Ganesh backend-context slot); this header just
// adds create/ref/unref entry points so managed callers can supply an allocator
// to both Ganesh (gr_vk_backendcontext_t.fMemoryAllocator) and Graphite
// (sk_graphite_vk_backend_context_init_t.fMemoryAllocator) without depending
// on Skia's internal VMA header. See mono/SkiaSharp#4567.

// Inputs for building the default VMA-backed allocator. Mirrors the subset of
// skgpu::VulkanBackendContext that Skia's VulkanMemoryAllocators::Make actually
// reads (instance/physical-device/device, Vulkan API version, get-proc bridge,
// protected-content flag).
typedef struct {
    vk_instance_t*        fInstance;
    vk_physical_device_t* fPhysicalDevice;
    vk_device_t*          fDevice;
    uint32_t              fMaxAPIVersion;
    gr_vk_get_proc        fGetProc;               // required
    void*                 fGetProcUserData;
    bool                  fProtectedContext;
    // Enable internal mutexes so the allocator is safe to touch from multiple
    // threads. Match this to the Context's threading model (Ganesh: false;
    // Graphite non-ThreadSafe: false; Graphite ThreadSafe or any multi-recorder
    // parallel use: true).
    bool                  fThreadSafe;
} gr_vk_allocator_default_options_t;

// Build Skia's default VMA-backed allocator. Returns a +1-refcounted handle;
// caller MUST balance every non-null return with a call to
// gr_vk_memory_allocator_unref. Contexts that consume the handle
// (gr_direct_context_make_vulkan / sk_graphite_context_make_vulkan) take their
// own internal ref, so the recommended pattern is
//   allocator = make_default(opts);
//   ctx.fMemoryAllocator = allocator;                 // borrow while wiring
//   context = make_vulkan(...);                        // context takes its own ref
//   gr_vk_memory_allocator_unref(allocator);           // drop the caller ref
// Returns null if libSkiaSharp was built without SK_VULKAN or if the underlying
// VMA initialisation failed.
//
// NOTE: options is passed BY VALUE to match gr_direct_context_make_vulkan's
// backend-context convention. This lets the managed-side marshaller thunk the
// fGetProc delegate without needing to take the address of a managed struct.
SK_C_API gr_vk_memory_allocator_t* gr_vk_memory_allocator_make_default(
    const gr_vk_allocator_default_options_t options);

SK_C_API void gr_vk_memory_allocator_ref(gr_vk_memory_allocator_t* allocator);
SK_C_API void gr_vk_memory_allocator_unref(gr_vk_memory_allocator_t* allocator);

SK_C_PLUS_PLUS_END_GUARD

#endif // gr_vk_allocator_DEFINED
