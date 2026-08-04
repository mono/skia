/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef gr_vk_device_lost_DEFINED
#define gr_vk_device_lost_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Single fault-address record from VK_EXT_device_fault. Fields mirror
// VkDeviceFaultAddressInfoEXT verbatim (see vulkan_core.h). Empty when the
// extension is not enabled or the driver reports no address info.
typedef struct {
    int32_t   fAddressType;         // VkDeviceFaultAddressTypeEXT
    uint64_t  fReportedAddress;     // VkDeviceAddress
    uint64_t  fAddressPrecision;    // VkDeviceSize
} gr_vk_device_fault_address_info_t;

// Single vendor-specific fault record from VK_EXT_device_fault. Mirrors
// VkDeviceFaultVendorInfoEXT (see vulkan_core.h). The description buffer is
// fixed-size (VK_MAX_DESCRIPTION_SIZE = 256) and NUL-terminated by the driver.
typedef struct {
    char      fDescription[256];    // VK_MAX_DESCRIPTION_SIZE
    uint64_t  fVendorFaultCode;
    uint64_t  fVendorFaultData;
} gr_vk_device_fault_vendor_info_t;

// Everything Skia's VulkanDeviceLostProc hands the caller, marshalled into a
// C-ABI-friendly struct. All pointers are valid only for the duration of the
// callback — copy anything you want to keep before returning. Vectors that were
// empty on the C++ side land here as (nullptr, 0).
typedef struct {
    const char*                                 fDescription;           // NUL-terminated
    const gr_vk_device_fault_address_info_t*    fAddressInfos;
    int32_t                                     fAddressInfoCount;
    const gr_vk_device_fault_vendor_info_t*     fVendorInfos;
    int32_t                                     fVendorInfoCount;
    const void*                                 fVendorBinaryData;      // raw bytes
    size_t                                      fVendorBinaryDataSize;
} gr_vk_device_lost_info_t;

// Fires when Skia detects VK_ERROR_DEVICE_LOST. `info` is valid only during the
// call; copy anything you need to keep. When VK_EXT_device_fault is not enabled
// the address/vendor arrays and binary data are empty; the description is still
// populated with Skia's message.
typedef void (*gr_vk_device_lost_proc)(void* userData, const gr_vk_device_lost_info_t* info);

// Build a bridge object that routes Skia's device-lost callback to the caller's
// proc + userData. Ownership: the returned handle is caller-owned; pass it to
// gr_vk_backendcontext_t.fDeviceLostHandler / sk_graphite_vk_backend_context_init_t
// .fDeviceLostHandler and, after the associated Context has been destroyed, free
// it with gr_vk_device_lost_handler_delete. Skia stores the callback pointer
// non-owning inside the Context, so the bridge must outlive the Context — do NOT
// delete the handle before the Context is deleted.
SK_C_API gr_vk_device_lost_handler_t* gr_vk_device_lost_handler_new(
    gr_vk_device_lost_proc proc,
    void* userData);

SK_C_API void gr_vk_device_lost_handler_delete(
    gr_vk_device_lost_handler_t* handler);

SK_C_PLUS_PLUS_END_GUARD

#endif // gr_vk_device_lost_DEFINED
