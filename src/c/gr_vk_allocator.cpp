/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/gr_vk_allocator.h"

#include "include/core/SkTypes.h"  // pulls SK_VULKAN

#if defined(SK_VULKAN)

#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
// mono/skia: Google keeps the concrete VMA implementation in the private
// module `src/gpu/vk/vulkanmemoryallocator/`. The public interface
// (skgpu::VulkanMemoryAllocator) is stable, but the factory function
// `VulkanMemoryAllocators::Make(ctx, threadSafe)` lives on the private path
// because Google considers allocator policy an embedder concern. See
// mono/SkiaSharp#4567 for the discussion — the tradeoff is: (a) depend on this
// private header and get the same code Skia's own tools use, or (b) copy VMA
// into our own tree and maintain it separately. We pick (a). If upstream ever
// removes the private module, swap this for our own VMA adapter.
#include "src/gpu/GpuTypesPriv.h"    // for skgpu::ThreadSafe (private-header enum)
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#include "src/c/sk_types_priv.h"

// Translate the C-ABI options into a VulkanBackendContext shaped for
// VulkanMemoryAllocators::Make. Only the fields VMA reads are set — everything
// else (queue, graphics queue index, memory allocator itself) is irrelevant to
// allocator construction and left default-initialised.
static skgpu::VulkanBackendContext ToAllocatorBackendContext(
    const gr_vk_allocator_default_options_t& opts)
{
    skgpu::VulkanBackendContext ctx;
    ctx.fInstance       = AsVkInstance(opts.fInstance);
    ctx.fPhysicalDevice = AsVkPhysicalDevice(opts.fPhysicalDevice);
    ctx.fDevice         = AsVkDevice(opts.fDevice);
    ctx.fMaxAPIVersion  = opts.fMaxAPIVersion;
    if (opts.fGetProc) {
        // Copy the proc + userData by value so the lambda outlives the caller's
        // options struct — the allocator holds onto fGetProc for the duration
        // of its lifetime and calls it during teardown as well as setup.
        gr_vk_get_proc getProc = opts.fGetProc;
        void* userData         = opts.fGetProcUserData;
        ctx.fGetProc = [getProc, userData](const char* name, VkInstance instance, VkDevice device) -> PFN_vkVoidFunction {
            return reinterpret_cast<PFN_vkVoidFunction>(
                getProc(userData, name, ToVkInstance(instance), ToVkDevice(device)));
        };
    }
    ctx.fProtectedContext =
        opts.fProtectedContext ? skgpu::Protected::kYes : skgpu::Protected::kNo;
    return ctx;
}

extern "C" SK_C_API gr_vk_memory_allocator_t* gr_vk_memory_allocator_make_default(
    const gr_vk_allocator_default_options_t options)
{
    if (!options.fInstance || !options.fPhysicalDevice ||
        !options.fDevice || !options.fGetProc) {
        return nullptr;
    }

    auto backendCtx = ToAllocatorBackendContext(options);
    auto threadSafe = options.fThreadSafe ? skgpu::ThreadSafe::kYes
                                          : skgpu::ThreadSafe::kNo;
    // VulkanMemoryAllocators::Make returns a sk_sp; release() hands us the raw
    // refcounted pointer with the +1 retained so the C caller owns exactly one
    // reference. Null propagates unchanged.
    sk_sp<skgpu::VulkanMemoryAllocator> allocator =
        skgpu::VulkanMemoryAllocators::Make(backendCtx, threadSafe);
    return ToGrVkMemoryAllocator(allocator.release());
}

extern "C" SK_C_API void gr_vk_memory_allocator_ref(gr_vk_memory_allocator_t* allocator)
{
    if (allocator) {
        AsGrVkMemoryAllocator(allocator)->ref();
    }
}

extern "C" SK_C_API void gr_vk_memory_allocator_unref(gr_vk_memory_allocator_t* allocator)
{
    if (allocator) {
        AsGrVkMemoryAllocator(allocator)->unref();
    }
}

#else  // !SK_VULKAN

extern "C" SK_C_API gr_vk_memory_allocator_t* gr_vk_memory_allocator_make_default(
    const gr_vk_allocator_default_options_t) { return nullptr; }
extern "C" SK_C_API void gr_vk_memory_allocator_ref(gr_vk_memory_allocator_t*) {}
extern "C" SK_C_API void gr_vk_memory_allocator_unref(gr_vk_memory_allocator_t*) {}

#endif  // SK_VULKAN
