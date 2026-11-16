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
#include "include/gpu/vk/VulkanTypes.h"           // skgpu::VulkanDeviceLostContext for gr_vk_device_lost_thunk
#include "include/c/gr_vk_device_lost.h"          // gr_vk_device_lost_handler_t
#include <string>                                  // std::string in gr_vk_device_lost_thunk signature
#include <vector>                                  // std::vector<> in gr_vk_device_lost_thunk signature

// Defined in gr_vk_device_lost.cpp — adapts Skia's std::string+vectors callback to
// the plain C proc stored in the handle.
extern "C" void gr_vk_device_lost_thunk(skgpu::VulkanDeviceLostContext userData,
                                        const std::string& description,
                                        const std::vector<VkDeviceFaultAddressInfoEXT>&,
                                        const std::vector<VkDeviceFaultVendorInfoEXT>&,
                                        const std::vector<std::byte>&);

// Pulls in Context/BackendTexture/TextureInfo + the matching As/To helpers
// via the SK_GRAPHITE block.
#include "src/c/sk_types_priv.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <memory>

namespace gr = skgpu::graphite;

// Forward-declared in sk_graphite.cpp; reused here for option translation.
// Returns false if opts carries an invalid value (e.g. out-of-range sample count).
extern bool sk_graphite_make_context_options(const sk_graphite_context_options_t* opts, gr::ContextOptions* out);

namespace {
// Shared body for both revisions of the Graphite Vulkan entry point. Taking the
// fields individually keeps the two ABI revisions of the init struct out of the
// implementation, and keeps the fGetProc lambda capturing values rather than a
// pointer into a caller struct.
sk_graphite_context_t* MakeVulkanContext(vk_instance_t* instance,
                                         vk_physical_device_t* physicalDevice,
                                         vk_device_t* device,
                                         vk_queue_t* queue,
                                         uint32_t graphicsQueueIndex,
                                         uint32_t maxAPIVersion,
                                         sk_graphite_vk_get_proc getProc,
                                         void* getProcUserData,
                                         bool protectedContext,
                                         gr_vk_device_lost_handler_t* deviceLostHandler,
                                         const sk_graphite_context_options_t* opts)
{
    skgpu::VulkanBackendContext vkbc;
    vkbc.fInstance            = reinterpret_cast<VkInstance>(instance);
    vkbc.fPhysicalDevice      = reinterpret_cast<VkPhysicalDevice>(physicalDevice);
    vkbc.fDevice              = reinterpret_cast<VkDevice>(device);
    vkbc.fQueue               = reinterpret_cast<VkQueue>(queue);
    vkbc.fGraphicsQueueIndex  = graphicsQueueIndex;
    vkbc.fMaxAPIVersion       = maxAPIVersion;
    vkbc.fProtectedContext    = protectedContext ? skgpu::Protected::kYes : skgpu::Protected::kNo;

    if (getProc) {
        // Capture by value so the lambda is independent of the init struct's lifetime.
        vkbc.fGetProc = [getProc, getProcUserData](const char* name, VkInstance vkInstance, VkDevice vkDevice) -> PFN_vkVoidFunction {
            return reinterpret_cast<PFN_vkVoidFunction>(
                getProc(getProcUserData, name, reinterpret_cast<vk_instance_t*>(vkInstance), reinterpret_cast<vk_device_t*>(vkDevice)));
        };
    }

    // Device-lost bridge. Skia stores fDeviceLostContext as a raw non-owning void*;
    // the bridge is never freed (see include/c/gr_vk_device_lost.h).
    if (deviceLostHandler) {
        vkbc.fDeviceLostContext = deviceLostHandler;
        vkbc.fDeviceLostProc = gr_vk_device_lost_thunk;
    }

    // The C init struct has no allocator field, so supply the default VMA allocator.
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
}  // namespace

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan(
    const sk_graphite_vk_backend_context_init_t init,
    const sk_graphite_context_options_t* opts)
{
    // Frozen revision: no device-lost handler in this struct.
    return MakeVulkanContext(init.fInstance, init.fPhysicalDevice, init.fDevice, init.fQueue,
                             init.fGraphicsQueueIndex, init.fMaxAPIVersion,
                             init.fGetProc, init.fGetProcUserData, init.fProtectedContext,
                             nullptr, opts);
}

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan_v2(
    const sk_graphite_vk_backend_context_init_v2_t init,
    const sk_graphite_context_options_t* opts)
{
    // Only read fields the caller's revision actually contains.
    gr_vk_device_lost_handler_t* handler =
        init.fStructSize >= sizeof(sk_graphite_vk_backend_context_init_v2_t) ? init.fDeviceLostHandler
                                                                            : nullptr;
    return MakeVulkanContext(init.fInstance, init.fPhysicalDevice, init.fDevice, init.fQueue,
                             init.fGraphicsQueueIndex, init.fMaxAPIVersion,
                             init.fGetProc, init.fGetProcUserData, init.fProtectedContext,
                             handler, opts);
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

#else  // !(SK_GRAPHITE && SK_VULKAN)

extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan(const sk_graphite_vk_backend_context_init_t, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan_v2(const sk_graphite_vk_backend_context_init_v2_t, const sk_graphite_context_options_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_texture_info_t* sk_graphite_vk_texture_info_new(const sk_graphite_vk_texture_info_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new(int32_t, int32_t, const sk_graphite_vk_texture_info_t*, int32_t, uint32_t, void*) { return nullptr; }

#endif  // SK_GRAPHITE && SK_VULKAN
