/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_graphite_vulkan_DEFINED
#define sk_graphite_vulkan_DEFINED

#include "include/c/sk_types.h"
#include "include/c/sk_graphite.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Opaque handle wrapping the C++ skgpu::VulkanBackendContext value.
// Heap-allocated so that callers can pass it across the FFI without worrying
// about layout differences in the C++ struct between Skia revisions.
typedef struct sk_graphite_vk_backend_context_t sk_graphite_vk_backend_context_t;

// Function-pointer getter signature: identical in shape to skgpu::VulkanGetProc.
// userData is the fGetProcUserData passed via the init struct.
typedef VKAPI_ATTR void (VKAPI_CALL *sk_graphite_vk_func_ptr)(void);
typedef sk_graphite_vk_func_ptr (*sk_graphite_vk_get_proc_t)(void* userData, const char* name, vk_instance_t* instance, vk_device_t* device);

typedef struct {
    vk_instance_t*              fInstance;
    vk_physical_device_t*       fPhysicalDevice;
    vk_device_t*                fDevice;
    vk_queue_t*                 fQueue;
    uint32_t                    fGraphicsQueueIndex;
    uint32_t                    fMaxAPIVersion;
    sk_graphite_vk_get_proc_t   fGetProc;
    void*                       fGetProcUserData;
    bool                        fProtectedContext;
} sk_graphite_vk_backend_context_init_t;

// Lifetime: caller owns; pair with sk_graphite_vk_backend_context_delete.
SK_C_API sk_graphite_vk_backend_context_t* sk_graphite_vk_backend_context_new(const sk_graphite_vk_backend_context_init_t* init);
SK_C_API void                              sk_graphite_vk_backend_context_delete(sk_graphite_vk_backend_context_t* bc);

// Build a Graphite Context for the Vulkan backend.
// Returns null on failure (Vulkan not built in, init invalid, or device rejected).
SK_C_API sk_graphite_context_t* sk_graphite_context_make_vulkan(
    const sk_graphite_vk_backend_context_t* bc,
    const sk_graphite_context_options_t* opts /* nullable -> defaults */);

// Vulkan-specific TextureInfo (POD). Field values are passed straight into
// skgpu::graphite::VulkanTextureInfo. YCbCr conversion not exposed in v1 —
// add a separate _ycbcr_t struct + factory if needed.
typedef struct {
    int32_t  fSampleCount;          // 1, 2, 4, 8, or 16
    int32_t  fMipmapped;            // 0 = no, 1 = yes
    uint32_t fFlags;                // VkImageCreateFlags
    int32_t  fFormat;               // VkFormat (e.g. VK_FORMAT_R8G8B8A8_UNORM)
    int32_t  fImageTiling;          // VkImageTiling
    uint32_t fImageUsageFlags;      // VkImageUsageFlags
    int32_t  fSharingMode;          // VkSharingMode
    uint32_t fAspectMask;           // VkImageAspectFlags
} sk_graphite_vk_texture_info_t;

// Build a backend-erased TextureInfo from Vulkan-specific fields.
// Caller owns the returned handle; pair with sk_graphite_texture_info_delete.
SK_C_API sk_graphite_texture_info_t* sk_graphite_vk_texture_info_new(
    const sk_graphite_vk_texture_info_t* info);

// Wrap an externally-allocated VkImage as a Graphite BackendTexture. The
// VkImage's memory must already be bound; the wrapper does NOT bind memory.
// Caller retains ownership of the VkImage — sk_graphite_backend_texture_delete
// only frees the wrapper, not the VkImage.
//
// imageLayout is the layout the image is in when handed to Skia (typically
// VK_IMAGE_LAYOUT_UNDEFINED for a fresh allocation).
SK_C_API sk_graphite_backend_texture_t* sk_graphite_vk_backend_texture_new(
    int32_t  width,
    int32_t  height,
    const sk_graphite_vk_texture_info_t* info,
    int32_t  imageLayout,           // VkImageLayout
    uint32_t queueFamilyIndex,
    void*    vkImage);              // VkImage

SK_C_PLUS_PLUS_END_GUARD

#endif  // sk_graphite_vulkan_DEFINED
