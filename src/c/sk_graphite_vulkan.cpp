/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/sk_graphite_vulkan.h"

#include "include/core/SkTypes.h"

#if defined(SK_GRAPHITE) && defined(SK_VULKAN)

#include "include/gpu/GpuTypes.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"

// Pulls in Context/BackendTexture/TextureInfo + the matching As/To helpers
// via the SK_GRAPHITE block.
#include "src/c/sk_types_priv.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <memory>
#include <cstring>

namespace gr = skgpu::graphite;

// Forward-declared in sk_graphite.cpp; reused here for option translation.
// Returns false if opts carries an invalid value (e.g. out-of-range sample count).
extern bool sk_graphite_make_context_options(const sk_graphite_context_options_t* opts, gr::ContextOptions* out);

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan(
    const sk_graphite_vk_backend_context_init_t init,
    const sk_graphite_context_options_t* opts)
{
    skgpu::VulkanBackendContext vkbc;
    vkbc.fInstance            = reinterpret_cast<VkInstance>(init.fInstance);
    vkbc.fPhysicalDevice      = reinterpret_cast<VkPhysicalDevice>(init.fPhysicalDevice);
    vkbc.fDevice              = reinterpret_cast<VkDevice>(init.fDevice);
    vkbc.fQueue               = reinterpret_cast<VkQueue>(init.fQueue);
    vkbc.fGraphicsQueueIndex  = init.fGraphicsQueueIndex;
    vkbc.fMaxAPIVersion       = init.fMaxAPIVersion;
    vkbc.fProtectedContext    = init.fProtectedContext ? skgpu::Protected::kYes : skgpu::Protected::kNo;

    if (init.fGetProc) {
        // Capture by value so the lambda is independent of the init struct's lifetime.
        sk_graphite_vk_get_proc getProc = init.fGetProc;
        void* userData = init.fGetProcUserData;
        vkbc.fGetProc = [getProc, userData](const char* name, VkInstance instance, VkDevice device) -> PFN_vkVoidFunction {
            return reinterpret_cast<PFN_vkVoidFunction>(
                getProc(userData, name, reinterpret_cast<vk_instance_t*>(instance), reinterpret_cast<vk_device_t*>(device)));
        };
    }

    // Graphite's Vulkan path does NOT auto-create a memory allocator (unlike Ganesh's
    // GrVkGpu); the caller must supply one. Until we expose that as part of the C API
    // (deferred to a follow-up), build the default VMA-backed allocator here.
    if (!vkbc.fMemoryAllocator) {
        vkbc.fMemoryAllocator = skgpu::VulkanMemoryAllocators::Make(vkbc, skgpu::ThreadSafe::kNo);
        if (!vkbc.fMemoryAllocator) {
            return nullptr;
        }
    }

    gr::ContextOptions gopts;
    if (!sk_graphite_make_context_options(opts, &gopts)) return nullptr;
    auto context = gr::ContextFactory::MakeVulkan(vkbc, gopts);
    return ToGraphiteContext(context.release());
}

// Vulkan TextureInfo + BackendTexture factories.

namespace {
gr::VulkanTextureInfo MakeNativeVkTextureInfo(const sk_graphite_vk_texture_info_t& info) {
    auto sampleBits = static_cast<VkSampleCountFlagBits>(info.fSampleCount > 0 ? info.fSampleCount : 1);
    auto mipmapped  = info.fMipmapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo;
    return gr::VulkanTextureInfo(
        sampleBits,
        mipmapped,
        static_cast<VkImageCreateFlags>(info.fFlags),
        static_cast<VkFormat>(info.fFormat),
        static_cast<VkImageTiling>(info.fImageTiling),
        static_cast<VkImageUsageFlags>(info.fImageUsageFlags),
        static_cast<VkSharingMode>(info.fSharingMode),
        static_cast<VkImageAspectFlags>(info.fAspectMask),
        skgpu::VulkanYcbcrConversionInfo{});
}

template <typename T>
T MakeNonDispatchableHandle(uint64_t value) {
    static_assert(sizeof(T) == sizeof(value));
    T handle;
    std::memcpy(&handle, &value, sizeof(handle));
    return handle;
}
}  // namespace

extern "C" SK_C_API sk_graphite_texture_info_t* sk_graphite_vk_texture_info_new(const sk_graphite_vk_texture_info_t* info) {
    auto* ti = new gr::TextureInfo(gr::TextureInfos::MakeVulkan(MakeNativeVkTextureInfo(*info)));
    return ToGraphiteTextureInfo(ti);
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new(
    int32_t width, int32_t height,
    const sk_graphite_vk_texture_info_t* info,
    int32_t imageLayout,
    uint32_t queueFamilyIndex,
    void* vkImage)
{
    auto vkti = MakeNativeVkTextureInfo(*info);
    auto bt = gr::BackendTextures::MakeVulkan(
        SkISize::Make(width, height),
        vkti,
        static_cast<VkImageLayout>(imageLayout),
        queueFamilyIndex,
        reinterpret_cast<VkImage>(vkImage),
        skgpu::VulkanAlloc{});
    auto* heap = new gr::BackendTexture(bt);
    return ToGraphiteBackendTexture(heap);
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new_uint64(
    int32_t width, int32_t height,
    const sk_graphite_vk_texture_info_t* info,
    int32_t imageLayout,
    uint32_t queueFamilyIndex,
    uint64_t vkImage)
{
    auto vkti = MakeNativeVkTextureInfo(*info);
    auto bt = gr::BackendTextures::MakeVulkan(
        SkISize::Make(width, height),
        vkti,
        static_cast<VkImageLayout>(imageLayout),
        queueFamilyIndex,
        MakeNonDispatchableHandle<VkImage>(vkImage),
        skgpu::VulkanAlloc{});
    return ToGraphiteBackendTexture(new gr::BackendTexture(bt));
}

extern "C" SK_C_API sk_graphite_backend_semaphore_t* sk_graphite_vk_backend_semaphore_new(
    uint64_t vkSemaphore)
{
    auto semaphore = gr::BackendSemaphores::MakeVulkan(
        MakeNonDispatchableHandle<VkSemaphore>(vkSemaphore));
    return ToGraphiteBackendSemaphore(new gr::BackendSemaphore(semaphore));
}

extern "C" SK_C_API sk_graphite_mutable_texture_state_t*
sk_graphite_vk_mutable_texture_state_new(
    int32_t imageLayout,
    uint32_t queueFamilyIndex)
{
    auto state = skgpu::MutableTextureStates::MakeVulkan(
        static_cast<VkImageLayout>(imageLayout), queueFamilyIndex);
    return ToGraphiteMutableTextureState(new skgpu::MutableTextureState(state));
}

#else  // !(SK_GRAPHITE && SK_VULKAN)

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan(const sk_graphite_vk_backend_context_init_t, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_texture_info_t* sk_graphite_vk_texture_info_new(const sk_graphite_vk_texture_info_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new(int32_t, int32_t, const sk_graphite_vk_texture_info_t*, int32_t, uint32_t, void*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new_uint64(int32_t, int32_t, const sk_graphite_vk_texture_info_t*, int32_t, uint32_t, uint64_t) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_semaphore_t* sk_graphite_vk_backend_semaphore_new(uint64_t) { return nullptr; }
extern "C" SK_C_API sk_graphite_mutable_texture_state_t* sk_graphite_vk_mutable_texture_state_new(int32_t, uint32_t) { return nullptr; }

#endif  // SK_GRAPHITE && SK_VULKAN
