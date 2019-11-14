/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTypes.h" // required to make sure SK_SUPPORT_GPU is defined

#define SK_SKIP_ARG__(keep, skip, ...) skip
#define SK_SKIP_ARG_(args) SK_SKIP_ARG__ args
#define SK_SKIP_ARG(...) SK_SKIP_ARG_((__VA_ARGS__, ))

#define SK_FIRST_ARG__(keep, skip, ...) keep
#define SK_FIRST_ARG_(args) SK_FIRST_ARG__ args
#define SK_FIRST_ARG(...) SK_FIRST_ARG_((__VA_ARGS__, ))

#if SK_SUPPORT_GPU
#    include "GrContext.h"
#    include "GrBackendSurface.h"
#    include "gl/GrGLInterface.h"
#    include "gl/GrGLAssembleInterface.h"
#    define SK_ONLY_GPU(...) SK_FIRST_ARG(__VA_ARGS__)
#    if SK_VULKAN
#        include "vk/GrVkBackendContext.h"
#        define SK_ONLY_VULKAN(...) SK_FIRST_ARG(__VA_ARGS__)
#    else
#        define SK_ONLY_VULKAN(...) SK_SKIP_ARG(__VA_ARGS__)
#    endif
#else
#    define SK_ONLY_GPU(...) SK_SKIP_ARG(__VA_ARGS__)
#    define SK_ONLY_VULKAN(...) SK_SKIP_ARG(__VA_ARGS__)
#endif

#include "gr_context.h"

#include "sk_types_priv.h"

// GrContext

gr_context_t* gr_context_make_gl(const gr_glinterface_t* glInterface) {
    return SK_ONLY_GPU(ToGrContext(GrContext::MakeGL(sk_ref_sp(AsGrGLInterface(glInterface))).release()), nullptr);
}

gr_context_t* gr_context_make_vulkan(const gr_vkbackendcontext_t* vkBackendContext) {
    return SK_ONLY_VULKAN(ToGrContext(GrContext::MakeVulkan(sk_ref_sp(AsGrVkBackendContext(vkBackendContext))).release()), nullptr);
}

void gr_context_unref(gr_context_t* context) {
    SK_ONLY_GPU(SkSafeUnref(AsGrContext(context)));
}

void gr_context_abandon_context(gr_context_t* context) {
    SK_ONLY_GPU(AsGrContext(context)->abandonContext());
}

void gr_context_release_resources_and_abandon_context(gr_context_t* context) {
    SK_ONLY_GPU(AsGrContext(context)->releaseResourcesAndAbandonContext());
}

void gr_context_get_resource_cache_limits(gr_context_t* context, int* maxResources, size_t* maxResourceBytes) {
    SK_ONLY_GPU(AsGrContext(context)->getResourceCacheLimits(maxResources, maxResourceBytes));
}

void gr_context_set_resource_cache_limits(gr_context_t* context, int maxResources, size_t maxResourceBytes) {
    SK_ONLY_GPU(AsGrContext(context)->setResourceCacheLimits(maxResources, maxResourceBytes));
}

void gr_context_get_resource_cache_usage(gr_context_t* context, int* maxResources, size_t* maxResourceBytes) {
    SK_ONLY_GPU(AsGrContext(context)->getResourceCacheUsage(maxResources, maxResourceBytes));
}

int gr_context_get_max_surface_sample_count_for_color_type(gr_context_t* context, sk_colortype_t colorType) {
    return SK_ONLY_GPU(AsGrContext(context)->maxSurfaceSampleCountForColorType((SkColorType)colorType), 0);
}

void gr_context_flush(gr_context_t* context) {
    SK_ONLY_GPU(AsGrContext(context)->flush());
}

void gr_context_reset_context(gr_context_t* context, uint32_t state) {
    SK_ONLY_GPU(AsGrContext(context)->resetContext(state));
}

gr_backend_t gr_context_get_backend(gr_context_t* context) {
    return SK_ONLY_GPU((gr_backend_t)AsGrContext(context)->backend(), (gr_backend_t)0);
}


// GrGLInterface

const gr_glinterface_t* gr_glinterface_create_native_interface() {
    return SK_ONLY_GPU(ToGrGLInterface(GrGLMakeNativeInterface().release()), nullptr);
}

const gr_glinterface_t* gr_glinterface_assemble_interface(void* ctx, gr_gl_get_proc get) {
    return SK_ONLY_GPU(ToGrGLInterface(GrGLMakeAssembledInterface(ctx, get).release()), nullptr);
}

const gr_glinterface_t* gr_glinterface_assemble_gl_interface(void* ctx, gr_gl_get_proc get) {
    return SK_ONLY_GPU(ToGrGLInterface(GrGLMakeAssembledGLInterface(ctx, get).release()), nullptr);
}

const gr_glinterface_t* gr_glinterface_assemble_gles_interface(void* ctx, gr_gl_get_proc get) {
    return SK_ONLY_GPU(ToGrGLInterface(GrGLMakeAssembledGLESInterface(ctx, get).release()), nullptr);
}

void gr_glinterface_unref(const gr_glinterface_t* glInterface) {
    SK_ONLY_GPU(SkSafeUnref(AsGrGLInterface(glInterface)));
}

bool gr_glinterface_validate(const gr_glinterface_t* glInterface) {
    return SK_ONLY_GPU(AsGrGLInterface(glInterface)->validate(), false);
}

bool gr_glinterface_has_extension(const gr_glinterface_t* glInterface, const char* extension) {
    return SK_ONLY_GPU(AsGrGLInterface(glInterface)->hasExtension(extension), false);
}


// GrVkInstance

gr_vkinterface_t* gr_vkinterface_make(vk_getinstanceprocaddr_t* vkGetInstanceProcAddr,
                                      vk_getdeviceprocaddr_t* vkGetDeviceProcAddr,
                                      vk_instance_t* vkInstance,
                                      vk_device_t* vkDevice,
                                      uint32_t extensionFlags) {
    SK_ONLY_VULKAN(
        sk_sp<GrVkInterface> grVkInterface(new GrVkInterface(
            GrVkInterface::GetInstanceProc(reinterpret_cast<PFN_vkVoidFunction(*)(VkInstance, const char*)>(vkGetInstanceProcAddr)),
            GrVkInterface::GetDeviceProc(reinterpret_cast<PFN_vkVoidFunction(*)(VkDevice, const char*)>(vkGetDeviceProcAddr)),
            reinterpret_cast<VkInstance>(vkInstance),
            reinterpret_cast<VkDevice>(vkDevice),
            extensionFlags
        ));
    )

    return SK_ONLY_VULKAN(ToGrVkInterface(grVkInterface.release()), nullptr);
}

void gr_vkinterface_unref(gr_vkinterface_t* grVkInterface) {
    SK_ONLY_VULKAN(SkSafeUnref(AsGrVkInterface(grVkInterface)));
}

bool gr_vkinterface_validate(const gr_vkinterface_t* grVkInterface, uint32_t extensionsFlags)
{
    return SK_ONLY_VULKAN(AsGrVkInterface(grVkInterface)->validate(extensionsFlags), false);
}

// GrVkBackendContext

gr_vkbackendcontext_t* gr_vkbackendcontext_assemble(vk_instance_t* vkInstance,
                                                    vk_physical_device_t* vkPhysicalDevice,
                                                    vk_device_t* vkDevice,
                                                    vk_queue_t* vkQueue,
                                                    uint32_t graphicsQueueIndex,
                                                    uint32_t minAPIVersion,
                                                    uint32_t extensions,
                                                    uint32_t features,
                                                    gr_vkinterface_t* grVkInterface) {
    SK_ONLY_VULKAN(
        sk_sp<GrVkBackendContext> grVkBackendContext(new GrVkBackendContext());

        sk_sp<GrVkInterface> sharedGrVkInterface(AsGrVkInterface(grVkInterface));

        grVkBackendContext->fInstance = reinterpret_cast<VkInstance>(vkInstance);
        grVkBackendContext->fPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(vkPhysicalDevice);
        grVkBackendContext->fDevice = reinterpret_cast<VkDevice>(vkDevice);
        grVkBackendContext->fQueue = reinterpret_cast<VkQueue>(vkQueue);
        grVkBackendContext->fGraphicsQueueIndex = graphicsQueueIndex;
        grVkBackendContext->fMinAPIVersion = minAPIVersion;
        grVkBackendContext->fExtensions = extensions;
        grVkBackendContext->fFeatures = features;
        grVkBackendContext->fOwnsInstanceAndDevice = false;
        grVkBackendContext->fInterface = sharedGrVkInterface;

        sharedGrVkInterface.release();
    )

    return SK_ONLY_VULKAN(ToGrVkBackendContext(grVkBackendContext.release()), nullptr);
}

void gr_vkbackendcontext_unref(gr_vkbackendcontext_t* grVkBackendContext) {
    SK_ONLY_VULKAN(SkSafeUnref(AsGrVkBackendContext(grVkBackendContext)));
}

// GrBackendTexture

gr_backendtexture_t* gr_backendtexture_new_gl(int width, int height, bool mipmapped, const gr_gl_textureinfo_t* glInfo) {
    return SK_ONLY_GPU(ToGrBackendTexture(new GrBackendTexture(width, height, (GrMipMapped)mipmapped, *AsGrGLTextureInfo(glInfo))), nullptr);
}

gr_backendtexture_t* gr_backendtexture_new_vulkan(int width, int height, const gr_vk_imageinfo_t* vkInfo) {
    return SK_ONLY_VULKAN(ToGrBackendTexture(new GrBackendTexture(width, height, *AsGrVkImageInfo(vkInfo))), nullptr);
}

void gr_backendtexture_delete(gr_backendtexture_t* texture) {
    SK_ONLY_GPU(delete AsGrBackendTexture(texture));
}

bool gr_backendtexture_is_valid(const gr_backendtexture_t* texture) {
    return SK_ONLY_GPU(AsGrBackendTexture(texture)->isValid(), false);
}

int gr_backendtexture_get_width(const gr_backendtexture_t* texture) {
    return SK_ONLY_GPU(AsGrBackendTexture(texture)->width(), 0);
}

int gr_backendtexture_get_height(const gr_backendtexture_t* texture) {
    return SK_ONLY_GPU(AsGrBackendTexture(texture)->height(), 0);
}

bool gr_backendtexture_has_mipmaps(const gr_backendtexture_t* texture) {
    return SK_ONLY_GPU(AsGrBackendTexture(texture)->hasMipMaps(), false);
}

gr_backend_t gr_backendtexture_get_backend(const gr_backendtexture_t* texture) {
    return SK_ONLY_GPU((gr_backend_t)AsGrBackendTexture(texture)->backend(), (gr_backend_t)0);
}

bool gr_backendtexture_get_gl_textureinfo(const gr_backendtexture_t* texture, gr_gl_textureinfo_t* glInfo) {
    return SK_ONLY_GPU(AsGrBackendTexture(texture)->getGLTextureInfo(AsGrGLTextureInfo(glInfo)), false);
}


// GrBackendRenderTarget

gr_backendrendertarget_t* gr_backendrendertarget_new_gl(int width, int height, int samples, int stencils, const gr_gl_framebufferinfo_t* glInfo) {
    return SK_ONLY_GPU(ToGrBackendRenderTarget(new GrBackendRenderTarget(width, height, samples, stencils, *AsGrGLFramebufferInfo(glInfo))), nullptr);
}

gr_backendrendertarget_t* gr_backendrendertarget_new_vulkan(int width, int height, int samples, const gr_vk_imageinfo_t* vkImageInfo) {
    return SK_ONLY_VULKAN(ToGrBackendRenderTarget(new GrBackendRenderTarget(width, height, samples, *AsGrVkImageInfo(vkImageInfo))), nullptr);
}

void gr_backendrendertarget_delete(gr_backendrendertarget_t* rendertarget) {
    SK_ONLY_GPU(delete AsGrBackendRenderTarget(rendertarget));
}

bool gr_backendrendertarget_is_valid(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->isValid(), false);
}

int gr_backendrendertarget_get_width(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->width(), 0);
}

int gr_backendrendertarget_get_height(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->height(), 0);
}

int gr_backendrendertarget_get_samples(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->sampleCnt(), 0);
}

int gr_backendrendertarget_get_stencils(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->stencilBits(), 0);
}

gr_backend_t gr_backendrendertarget_get_backend(const gr_backendrendertarget_t* rendertarget) {
    return SK_ONLY_GPU((gr_backend_t)AsGrBackendRenderTarget(rendertarget)->backend(), (gr_backend_t)0);
}

bool gr_backendrendertarget_get_gl_framebufferinfo(const gr_backendrendertarget_t* rendertarget, gr_gl_framebufferinfo_t* glInfo) {
    return SK_ONLY_GPU(AsGrBackendRenderTarget(rendertarget)->getGLFramebufferInfo(AsGrGLFramebufferInfo(glInfo)), false);
}
