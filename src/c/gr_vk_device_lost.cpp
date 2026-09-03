/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/gr_vk_device_lost.h"

#include "include/core/SkTypes.h"  // pulls SK_VULKAN

#if defined(SK_VULKAN)

#include "include/gpu/vk/VulkanTypes.h"

#include <string>
#include <vector>

// Bridge that adapts Skia's beefy VulkanDeviceLostProc (std::string,
// std::vector<VkDeviceFaultAddressInfoEXT>, std::vector<VkDeviceFaultVendorInfoEXT>,
// std::vector<std::byte>) to a plain C callback that takes only the description.
// The v1 signature intentionally drops the fault-detail vectors — see
// gr_vk_device_lost.h for rationale.
struct gr_vk_device_lost_handler_t {
    gr_vk_device_lost_proc fProc;
    void*                  fUserData;
};

// Static thunk used as skgpu::VulkanBackendContext.fDeviceLostProc.
// `userData` there is a gr_vk_device_lost_handler_t* — installed by the shims
// that build a skgpu::VulkanBackendContext (AsGrVkBackendContext for Ganesh,
// sk_graphite_context_make_vulkan for Graphite). Stateless so it decays to a
// plain function pointer (skgpu::VulkanDeviceLostProc is a fn ptr, not a
// std::function).
//
// Marshalling: VkDeviceFaultAddressInfoEXT / VkDeviceFaultVendorInfoEXT are
// standard-layout POD in Vulkan.h, and our gr_vk_device_fault_*_info_t mirror
// their layout field-for-field, so we can memcpy the vector storage into caller-
// visible arrays without a per-element loop. std::byte is layout-compatible with
// unsigned char, so vendorBinaryData maps 1:1. Skia guarantees the vectors and
// description remain valid for the duration of this call, so pointing the info
// struct at their storage is safe — no allocation, no copies.
extern "C" void gr_vk_device_lost_thunk(skgpu::VulkanDeviceLostContext userData,
                                        const std::string& description,
                                        const std::vector<VkDeviceFaultAddressInfoEXT>& addressInfos,
                                        const std::vector<VkDeviceFaultVendorInfoEXT>& vendorInfos,
                                        const std::vector<std::byte>& vendorBinaryData)
{
    auto* handler = static_cast<gr_vk_device_lost_handler_t*>(userData);
    if (!handler || !handler->fProc) return;

    static_assert(sizeof(gr_vk_device_fault_address_info_t) == sizeof(VkDeviceFaultAddressInfoEXT),
                  "gr_vk_device_fault_address_info_t must match VkDeviceFaultAddressInfoEXT layout");
    static_assert(sizeof(gr_vk_device_fault_vendor_info_t) == sizeof(VkDeviceFaultVendorInfoEXT),
                  "gr_vk_device_fault_vendor_info_t must match VkDeviceFaultVendorInfoEXT layout");

    gr_vk_device_lost_info_t info;
    info.fDescription           = description.c_str();
    info.fAddressInfos          = reinterpret_cast<const gr_vk_device_fault_address_info_t*>(
                                      addressInfos.data());
    info.fAddressInfoCount      = static_cast<int32_t>(addressInfos.size());
    info.fVendorInfos           = reinterpret_cast<const gr_vk_device_fault_vendor_info_t*>(
                                      vendorInfos.data());
    info.fVendorInfoCount       = static_cast<int32_t>(vendorInfos.size());
    info.fVendorBinaryData      = vendorBinaryData.data();
    info.fVendorBinaryDataSize  = vendorBinaryData.size();

    handler->fProc(handler->fUserData, &info);
}

extern "C" SK_C_API gr_vk_device_lost_handler_t* gr_vk_device_lost_handler_new(
    gr_vk_device_lost_proc proc, void* userData)
{
    if (!proc) return nullptr;
    auto* handler = new gr_vk_device_lost_handler_t;
    handler->fProc = proc;
    handler->fUserData = userData;
    return handler;
}

extern "C" SK_C_API void gr_vk_device_lost_handler_delete(gr_vk_device_lost_handler_t* handler)
{
    delete handler;
}

#else  // !SK_VULKAN

extern "C" SK_C_API gr_vk_device_lost_handler_t* gr_vk_device_lost_handler_new(
    gr_vk_device_lost_proc, void*) { return nullptr; }
extern "C" SK_C_API void gr_vk_device_lost_handler_delete(gr_vk_device_lost_handler_t*) {}

#endif  // SK_VULKAN
