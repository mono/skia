/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/c/sk_graphite.h"

#include "include/core/SkTypes.h"  // pulls SK_GRAPHITE / SK_VULKAN / SK_METAL / SK_DAWN

#if defined(SK_GRAPHITE)

#include "include/core/SkImage.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/ImageProvider.h"
#include "include/gpu/graphite/Surface.h"

// Pulls in the Context/Recorder/Recording/BackendTexture/TextureInfo headers
// and the matching As/To helpers via the DEF_MAP_WITH_NS block guarded by
// SK_GRAPHITE in sk_types_priv.h.
#include "src/c/sk_types_priv.h"

#include <chrono>
#include <memory>
#include <utility>

namespace gr = skgpu::graphite;

// Backend availability — runtime-queried, per-backend compile-time gated.
extern "C" SK_C_API bool sk_graphite_backend_is_available(sk_graphite_backend_t backend) {
    switch (backend) {
        case VULKAN_SK_GRAPHITE_BACKEND:
#if defined(SK_VULKAN)
            return true;
#else
            return false;
#endif
        case METAL_SK_GRAPHITE_BACKEND:
#if defined(SK_METAL)
            return true;
#else
            return false;
#endif
        case DAWN_SK_GRAPHITE_BACKEND:
#if defined(SK_DAWN)
            return true;
#else
            return false;
#endif
    }
    return false;
}

// ContextOptions

extern "C" SK_C_API void sk_graphite_context_options_init_defaults(sk_graphite_context_options_t* out) {
    if (!out) return;
    gr::ContextOptions defaults;
    out->fDisableDriverCorrectnessWorkarounds = defaults.fDisableDriverCorrectnessWorkarounds;
    out->fInternalMultisampleCount            = static_cast<int32_t>(defaults.fInternalMultisampleCount);
    out->fGpuBudgetInBytes                    = static_cast<int64_t>(defaults.fGpuBudgetInBytes);
    out->fRequireOrderedRecordings            = defaults.fRequireOrderedRecordings;
    out->fSetBackendLabels                    = defaults.fSetBackendLabels;
}

// Translate the public sample-count integer to the SampleCount enum.
// Returns true on success; *out is left unchanged on failure so the caller
// can supply a default or surface the validation error. Used by
// sk_graphite_make_context_options below, which propagates the bool up.
static bool ToGraphiteSampleCount(int32_t count, gr::SampleCount* out) {
    switch (count) {
        case 1:  *out = gr::SampleCount::k1;  return true;
        case 2:  *out = gr::SampleCount::k2;  return true;
        case 4:  *out = gr::SampleCount::k4;  return true;
        case 8:  *out = gr::SampleCount::k8;  return true;
        case 16: *out = gr::SampleCount::k16; return true;
    }
    return false;
}

// ImageProvider bridge — routes Graphite's "I have a non-Graphite SkImage,
// please convert it" hook to a function pointer + userData supplied by the
// managed (or any C) caller. Held by sk_sp inside ContextOptions; owned by
// the Context after ContextFactory::Make* succeeds.
namespace {
class FfiImageProvider : public gr::ImageProvider {
public:
    FfiImageProvider(sk_graphite_image_provider_proc_t proc, void* userData)
        : fProc(proc), fUserData(userData) {}

    sk_sp<SkImage> findOrCreate(gr::Recorder* recorder,
                                 const SkImage* image,
                                 SkImage::RequiredProperties props) override {
        if (!fProc || !recorder || !image) return nullptr;
        // The bridge consumes a +1 reference: the callback returns a fresh
        // sk_image_t* (heap-owned by Skia's sk_sp machinery). We adopt it
        // into an sk_sp so it is decremented exactly once when this scope —
        // or whoever Skia hands the result to next — releases it.
        sk_image_t* raw = fProc(
            fUserData,
            ToGraphiteRecorder(recorder),
            ToImage(const_cast<SkImage*>(image)),  // const-correctness shim; Skia's hook is non-const
            props.fMipmapped ? 1 : 0);
        return sk_sp<SkImage>(AsImage(raw));
    }

private:
    sk_graphite_image_provider_proc_t fProc;
    void*                             fUserData;
};
}  // namespace

// Heap wrapper around sk_sp<gr::ImageProvider> so the C ABI can hand out a
// stable pointer. We keep the sp as a member rather than naked-AddRef so the
// destructor releases the underlying ref-count exactly once when the wrapper
// is freed via sk_graphite_image_provider_delete.
struct sk_graphite_image_provider_t {
    sk_sp<gr::ImageProvider> sp;
};

extern "C" SK_C_API sk_graphite_image_provider_t* sk_graphite_image_provider_new(
    sk_graphite_image_provider_proc_t proc, void* userData)
{
    if (!proc) return nullptr;
    auto* w = new sk_graphite_image_provider_t;
    w->sp = sk_make_sp<FfiImageProvider>(proc, userData);
    return w;
}

extern "C" SK_C_API void sk_graphite_image_provider_delete(sk_graphite_image_provider_t* w) {
    delete w;
}

extern "C" SK_C_API sk_image_t* sk_graphite_image_make_texture(
    sk_graphite_recorder_t* recorder,
    const sk_image_t* image,
    int32_t mipmapped)
{
    if (!recorder || !image) return nullptr;
    SkImage::RequiredProperties props;
    props.fMipmapped = (mipmapped != 0);
    auto out = SkImages::TextureFromImage(AsGraphiteRecorder(recorder), AsImage(image), props);
    return ToImage(out.release());
}

// Public helper used by per-backend factories in sibling translation units.
// Translate the C-ABI options struct to a Skia ContextOptions. Returns false
// if any field carries an invalid value (currently: only fInternalMultisampleCount
// being something other than 1/2/4/8/16). On failure *out is left in a
// well-defined "Skia defaults" state and the per-backend factory should bail
// with nullptr so the managed caller can surface ArgumentException.
bool sk_graphite_make_context_options(const sk_graphite_context_options_t* opts, gr::ContextOptions* out) {
    if (!out) return false;
    *out = gr::ContextOptions{};
    if (!opts) return true;  // null = use Skia defaults
    // fInternalMultisampleCount: 0 means "leave Skia's default in place", same
    // shape as the fGpuBudgetInBytes < 0 sentinel below. Lets a caller pass
    // default-constructed C-side opts without tripping the validator.
    if (opts->fInternalMultisampleCount != 0 &&
        !ToGraphiteSampleCount(opts->fInternalMultisampleCount, &out->fInternalMultisampleCount)) {
        return false;
    }
    out->fDisableDriverCorrectnessWorkarounds = opts->fDisableDriverCorrectnessWorkarounds;
    if (opts->fGpuBudgetInBytes >= 0) {
        out->fGpuBudgetInBytes = static_cast<size_t>(opts->fGpuBudgetInBytes);
    }
    out->fRequireOrderedRecordings = opts->fRequireOrderedRecordings;
    out->fSetBackendLabels         = opts->fSetBackendLabels;
    return true;
}

// Context

extern "C" SK_C_API void sk_graphite_context_delete(sk_graphite_context_t* h) {
    delete AsGraphiteContext(h);
}

extern "C" SK_C_API sk_graphite_backend_t sk_graphite_context_get_backend(const sk_graphite_context_t* h) {
    if (!h) return UNKNOWN_SK_GRAPHITE_BACKEND;
    switch (AsGraphiteContext(h)->backend()) {
        case skgpu::BackendApi::kDawn:        return DAWN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMetal:       return METAL_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kVulkan:      return VULKAN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMock:
        case skgpu::BackendApi::kUnsupported: return UNKNOWN_SK_GRAPHITE_BACKEND;
    }
    SkUNREACHABLE;
}

extern "C" SK_C_API bool    sk_graphite_context_is_device_lost(const sk_graphite_context_t* h)              { return h ? AsGraphiteContext(h)->isDeviceLost() : true; }
extern "C" SK_C_API int32_t sk_graphite_context_get_max_texture_size(const sk_graphite_context_t* h)        { return h ? AsGraphiteContext(h)->maxTextureSize() : 0; }
extern "C" SK_C_API bool    sk_graphite_context_supports_protected_content(const sk_graphite_context_t* h) { return h ? AsGraphiteContext(h)->supportsProtectedContent() : false; }
extern "C" SK_C_API int64_t sk_graphite_context_get_current_budgeted_bytes(const sk_graphite_context_t* h)  { return h ? static_cast<int64_t>(AsGraphiteContext(h)->currentBudgetedBytes()) : 0; }
extern "C" SK_C_API int64_t sk_graphite_context_get_max_budgeted_bytes(const sk_graphite_context_t* h)      { return h ? static_cast<int64_t>(AsGraphiteContext(h)->maxBudgetedBytes()) : 0; }
extern "C" SK_C_API void    sk_graphite_context_set_max_budgeted_bytes(sk_graphite_context_t* h, int64_t b) { if (h && b >= 0) AsGraphiteContext(h)->setMaxBudgetedBytes(static_cast<size_t>(b)); }
extern "C" SK_C_API void    sk_graphite_context_free_gpu_resources(sk_graphite_context_t* h)                { if (h) AsGraphiteContext(h)->freeGpuResources(); }

extern "C" SK_C_API void sk_graphite_context_perform_deferred_cleanup(sk_graphite_context_t* h, int64_t milliseconds) {
    if (h && milliseconds >= 0) {
        AsGraphiteContext(h)->performDeferredCleanup(std::chrono::milliseconds(milliseconds));
    }
}

extern "C" SK_C_API sk_graphite_recorder_t* sk_graphite_context_make_recorder(sk_graphite_context_t* h, int64_t recorderBudgetBytes) {
    return sk_graphite_context_make_recorder_with_image_provider(h, recorderBudgetBytes, nullptr);
}

extern "C" SK_C_API sk_graphite_recorder_t* sk_graphite_context_make_recorder_with_image_provider(
    sk_graphite_context_t* h, int64_t recorderBudgetBytes, sk_graphite_image_provider_t* imageProvider)
{
    if (!h) return nullptr;
    gr::RecorderOptions opts;
    if (recorderBudgetBytes >= 0) {
        opts.fGpuBudgetInBytes = static_cast<size_t>(recorderBudgetBytes);
    }
    if (imageProvider) {
        // The recorder takes its own ref on the underlying ImageProvider via the sp copy.
        // Caller's wrapper sp keeps its own ref until sk_graphite_image_provider_delete is
        // invoked — typically right after this call returns.
        opts.fImageProvider = imageProvider->sp;
    }
    auto recorder = AsGraphiteContext(h)->makeRecorder(opts);
    return ToGraphiteRecorder(recorder.release());
}

static sk_graphite_insert_status_t ToInsertStatus(gr::InsertStatus::V v) {
    switch (v) {
        case gr::InsertStatus::kSuccess:                         return SUCCESS_SK_GRAPHITE_INSERT_STATUS;
        case gr::InsertStatus::kInvalidRecording:                return INVALID_RECORDING_SK_GRAPHITE_INSERT_STATUS;
        case gr::InsertStatus::kPromiseImageInstantiationFailed: return PROMISE_INSTANTIATION_FAILED_SK_GRAPHITE_INSERT_STATUS;
        case gr::InsertStatus::kAddCommandsFailed:               return ADD_COMMANDS_FAILED_SK_GRAPHITE_INSERT_STATUS;
        case gr::InsertStatus::kAsyncShaderCompilesFailed:       return ASYNC_SHADER_COMPILES_FAILED_SK_GRAPHITE_INSERT_STATUS;
        case gr::InsertStatus::kOutOfOrderRecording:             return OUT_OF_ORDER_RECORDING_SK_GRAPHITE_INSERT_STATUS;
    }
    SkUNREACHABLE;
}

extern "C" SK_C_API sk_graphite_insert_status_t sk_graphite_context_insert_recording(sk_graphite_context_t* h, const sk_graphite_insert_recording_info_t* info) {
    if (!h || !info || !info->fRecording) {
        return INVALID_RECORDING_SK_GRAPHITE_INSERT_STATUS;
    }
    gr::InsertRecordingInfo iri;
    iri.fRecording = AsGraphiteRecording(info->fRecording);
    iri.fTargetSurface = AsSurface(info->fTargetSurface);
    iri.fTargetTranslation.fX = info->fTargetTranslationX;
    iri.fTargetTranslation.fY = info->fTargetTranslationY;
    iri.fTargetClip = *AsIRect(&info->fTargetClip);
    auto status = AsGraphiteContext(h)->insertRecording(iri);
    return ToInsertStatus(status);
}

extern "C" SK_C_API bool sk_graphite_context_submit(sk_graphite_context_t* h, const sk_graphite_submit_info_t* info) {
    if (!h) return false;
    gr::SubmitInfo si;
    if (info) {
        si.fSync          = (info->fSync == YES_SK_GRAPHITE_SYNC_TO_CPU) ? gr::SyncToCpu::kYes : gr::SyncToCpu::kNo;
        si.fMarkBoundary  = (info->fMarkBoundary == YES_SK_GRAPHITE_MARK_FRAME_BOUNDARY) ? gr::MarkFrameBoundary::kYes : gr::MarkFrameBoundary::kNo;
        si.fFrameID       = info->fFrameID;
    }
    return AsGraphiteContext(h)->submit(si);
}

// Recorder

extern "C" SK_C_API void sk_graphite_recorder_delete(sk_graphite_recorder_t* h) {
    delete AsGraphiteRecorder(h);
}

extern "C" SK_C_API sk_graphite_backend_t sk_graphite_recorder_get_backend(const sk_graphite_recorder_t* h) {
    if (!h) return UNKNOWN_SK_GRAPHITE_BACKEND;
    switch (AsGraphiteRecorder(h)->backend()) {
        case skgpu::BackendApi::kDawn:        return DAWN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMetal:       return METAL_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kVulkan:      return VULKAN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMock:
        case skgpu::BackendApi::kUnsupported: return UNKNOWN_SK_GRAPHITE_BACKEND;
    }
    SkUNREACHABLE;
}

extern "C" SK_C_API int32_t sk_graphite_recorder_get_max_texture_size(const sk_graphite_recorder_t* h) {
    return h ? AsGraphiteRecorder(h)->maxTextureSize() : 0;
}

extern "C" SK_C_API sk_graphite_recording_t* sk_graphite_recorder_snap(sk_graphite_recorder_t* h) {
    if (!h) return nullptr;
    auto recording = AsGraphiteRecorder(h)->snap();
    return ToGraphiteRecording(recording.release());
}

// Recording

extern "C" SK_C_API void sk_graphite_recording_delete(sk_graphite_recording_t* h) {
    delete AsGraphiteRecording(h);
}

// Surface (Graphite-backed)

extern "C" SK_C_API sk_surface_t* sk_graphite_surface_make_render_target(
    sk_graphite_recorder_t* recorder,
    const sk_imageinfo_t*   cinfo,
    int32_t                 mipmapped,
    const sk_surfaceprops_t* props)
{
    if (!recorder || !cinfo) return nullptr;
    SkImageInfo info = AsImageInfo(cinfo);
    skgpu::Mipmapped mips = (mipmapped != 0) ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo;
    auto surface = SkSurfaces::RenderTarget(AsGraphiteRecorder(recorder), info, mips, AsSurfaceProps(props));
    return ToSurface(surface.release());
}

extern "C" SK_C_API sk_surface_t* sk_graphite_surface_wrap_backend_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* backendTexture,
    sk_colortype_t          colorType,
    sk_colorspace_t*        colorSpace,
    const sk_surfaceprops_t* props,
    sk_graphite_release_proc_t releaseProc,
    void* releaseContext)
{
    if (!recorder || !backendTexture) return nullptr;
    auto surface = SkSurfaces::WrapBackendTexture(
        AsGraphiteRecorder(recorder),
        *AsGraphiteBackendTexture(backendTexture),
        static_cast<SkColorType>(colorType),
        sk_ref_sp(AsColorSpace(colorSpace)),
        AsSurfaceProps(props),
        reinterpret_cast<SkSurfaces::TextureReleaseProc>(releaseProc),
        releaseContext);
    return ToSurface(surface.release());
}

extern "C" SK_C_API sk_image_t* sk_graphite_image_wrap_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* backendTexture,
    sk_colortype_t  colorType,
    sk_alphatype_t  alphaType,
    sk_colorspace_t* colorSpace,
    sk_graphite_release_proc_t releaseProc,
    void* releaseContext)
{
    if (!recorder || !backendTexture) return nullptr;
    auto image = SkImages::WrapTexture(
        AsGraphiteRecorder(recorder),
        *AsGraphiteBackendTexture(backendTexture),
        static_cast<SkColorType>(colorType),
        static_cast<SkAlphaType>(alphaType),
        sk_ref_sp(AsColorSpace(colorSpace)),
        reinterpret_cast<SkImages::TextureReleaseProc>(releaseProc),
        releaseContext);
    return ToImage(image.release());
}

extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_recorder_create_backend_texture(
    sk_graphite_recorder_t* recorder,
    int32_t width, int32_t height,
    const sk_graphite_texture_info_t* info)
{
    if (!recorder || !info || width <= 0 || height <= 0) return nullptr;
    auto bt = AsGraphiteRecorder(recorder)->createBackendTexture(
        SkISize::Make(width, height),
        *AsGraphiteTextureInfo(info));
    if (!bt.isValid()) return nullptr;
    return ToGraphiteBackendTexture(new gr::BackendTexture(bt));
}

extern "C" SK_C_API void sk_graphite_recorder_delete_backend_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* tex)
{
    if (recorder && tex)
        AsGraphiteRecorder(recorder)->deleteBackendTexture(*AsGraphiteBackendTexture(tex));
}

extern "C" SK_C_API void sk_graphite_context_delete_backend_texture(
    sk_graphite_context_t* context,
    const sk_graphite_backend_texture_t* tex)
{
    if (context && tex)
        AsGraphiteContext(context)->deleteBackendTexture(*AsGraphiteBackendTexture(tex));
}

// BackendTexture handle: heap wrapper around the C++ value type.

extern "C" SK_C_API void sk_graphite_backend_texture_delete(sk_graphite_backend_texture_t* h) {
    delete AsGraphiteBackendTexture(h);
}
extern "C" SK_C_API bool sk_graphite_backend_texture_is_valid(const sk_graphite_backend_texture_t* h) {
    return h ? AsGraphiteBackendTexture(h)->isValid() : false;
}
extern "C" SK_C_API sk_graphite_backend_t sk_graphite_backend_texture_get_backend(const sk_graphite_backend_texture_t* h) {
    if (!h) return UNKNOWN_SK_GRAPHITE_BACKEND;
    switch (AsGraphiteBackendTexture(h)->backend()) {
        case skgpu::BackendApi::kDawn:        return DAWN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMetal:       return METAL_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kVulkan:      return VULKAN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMock:
        case skgpu::BackendApi::kUnsupported: return UNKNOWN_SK_GRAPHITE_BACKEND;
    }
    SkUNREACHABLE;
}
extern "C" SK_C_API void sk_graphite_backend_texture_get_dimensions(const sk_graphite_backend_texture_t* h, int32_t* outW, int32_t* outH) {
    if (!h || !outW || !outH) return;
    auto d = AsGraphiteBackendTexture(h)->dimensions();
    *outW = d.fWidth;
    *outH = d.fHeight;
}

// TextureInfo handle.

extern "C" SK_C_API void sk_graphite_texture_info_delete(sk_graphite_texture_info_t* h) {
    delete AsGraphiteTextureInfo(h);
}
extern "C" SK_C_API bool sk_graphite_texture_info_is_valid(const sk_graphite_texture_info_t* h) {
    return h ? AsGraphiteTextureInfo(h)->isValid() : false;
}
extern "C" SK_C_API sk_graphite_backend_t sk_graphite_texture_info_get_backend(const sk_graphite_texture_info_t* h) {
    if (!h) return UNKNOWN_SK_GRAPHITE_BACKEND;
    switch (AsGraphiteTextureInfo(h)->backend()) {
        case skgpu::BackendApi::kDawn:        return DAWN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMetal:       return METAL_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kVulkan:      return VULKAN_SK_GRAPHITE_BACKEND;
        case skgpu::BackendApi::kMock:
        case skgpu::BackendApi::kUnsupported: return UNKNOWN_SK_GRAPHITE_BACKEND;
    }
    SkUNREACHABLE;
}
extern "C" SK_C_API int32_t sk_graphite_texture_info_get_sample_count(const sk_graphite_texture_info_t* h) {
    if (!h) return 1;
    switch (AsGraphiteTextureInfo(h)->sampleCount()) {
        case gr::SampleCount::k1:  return 1;
        case gr::SampleCount::k2:  return 2;
        case gr::SampleCount::k4:  return 4;
        case gr::SampleCount::k8:  return 8;
        case gr::SampleCount::k16: return 16;
    }
    SkUNREACHABLE;
}
extern "C" SK_C_API int32_t sk_graphite_texture_info_get_mipmapped(const sk_graphite_texture_info_t* h) {
    return (h && AsGraphiteTextureInfo(h)->mipmapped() == skgpu::Mipmapped::kYes) ? 1 : 0;
}

// Async readback — pass-through wrapping of Context::asyncRescaleAndReadPixels.
// The C user provides a function-pointer callback + context void*. Our adapter
// receives the unique_ptr<AsyncReadResult> from Skia, hands the user a non-owning
// pointer to it, and lets the unique_ptr destruct when the callback returns.
namespace {
struct AsyncReadCallbackBridge {
    sk_graphite_async_read_pixels_proc_t userCallback;
    void* userContext;
};

void asyncReadCallbackAdapter(SkImage::ReadPixelsContext rawBridge,
                               std::unique_ptr<const SkImage::AsyncReadResult> result) {
    auto* bridge = static_cast<AsyncReadCallbackBridge*>(rawBridge);
    bridge->userCallback(
        bridge->userContext,
        reinterpret_cast<const sk_graphite_async_read_result_t*>(result.get()));
    delete bridge;
    // unique_ptr destroys result here as it goes out of scope — pointer the user
    // saw is now invalid. Documented in sk_graphite.h.
}
}  // namespace

extern "C" SK_C_API void sk_graphite_context_async_rescale_and_read_pixels_surface(
    sk_graphite_context_t* h,
    const sk_surface_t* surface,
    const sk_imageinfo_t* cdstInfo,
    const sk_irect_t* csrcRect,
    sk_graphite_rescale_gamma_t rescaleGamma,
    sk_graphite_rescale_mode_t  rescaleMode,
    sk_graphite_async_read_pixels_proc_t callback,
    void* callbackContext)
{
    if (!h || !surface || !cdstInfo || !csrcRect || !callback) {
        if (callback) callback(callbackContext, nullptr);  // signal failure synchronously
        return;
    }
    auto* bridge = new AsyncReadCallbackBridge{callback, callbackContext};
    AsGraphiteContext(h)->asyncRescaleAndReadPixels(
        AsSurface(surface),
        AsImageInfo(cdstInfo),
        *AsIRect(csrcRect),
        static_cast<SkImage::RescaleGamma>(rescaleGamma),
        static_cast<SkImage::RescaleMode>(rescaleMode),
        asyncReadCallbackAdapter,
        bridge);
}

extern "C" SK_C_API void sk_graphite_context_check_async_work_completion(sk_graphite_context_t* h) {
    if (h) AsGraphiteContext(h)->checkAsyncWorkCompletion();
}

// AsyncReadResult accessors. The opaque sk_graphite_async_read_result_t* maps
// directly to const SkImage::AsyncReadResult*.
static inline const SkImage::AsyncReadResult* AsAsyncReadResult(const sk_graphite_async_read_result_t* h) {
    return reinterpret_cast<const SkImage::AsyncReadResult*>(h);
}

extern "C" SK_C_API int32_t sk_graphite_async_read_result_get_count(const sk_graphite_async_read_result_t* h) {
    return h ? AsAsyncReadResult(h)->count() : 0;
}

extern "C" SK_C_API const void* sk_graphite_async_read_result_get_data(const sk_graphite_async_read_result_t* h, int32_t planeIndex) {
    if (!h || planeIndex < 0 || planeIndex >= AsAsyncReadResult(h)->count()) return nullptr;
    return AsAsyncReadResult(h)->data(planeIndex);
}

extern "C" SK_C_API size_t sk_graphite_async_read_result_get_row_bytes(const sk_graphite_async_read_result_t* h, int32_t planeIndex) {
    if (!h || planeIndex < 0 || planeIndex >= AsAsyncReadResult(h)->count()) return 0;
    return AsAsyncReadResult(h)->rowBytes(planeIndex);
}

#else  // !SK_GRAPHITE — stubs so the C ABI surface is consistent regardless of build config

extern "C" SK_C_API bool                          sk_graphite_backend_is_available(sk_graphite_backend_t) { return false; }
extern "C" SK_C_API void                          sk_graphite_context_options_init_defaults(sk_graphite_context_options_t*) {}
extern "C" SK_C_API void                          sk_graphite_context_delete(sk_graphite_context_t*) {}
extern "C" SK_C_API sk_graphite_backend_t         sk_graphite_context_get_backend(const sk_graphite_context_t*) { return VULKAN_SK_GRAPHITE_BACKEND; }
extern "C" SK_C_API bool                          sk_graphite_context_is_device_lost(const sk_graphite_context_t*) { return true; }
extern "C" SK_C_API int32_t                       sk_graphite_context_get_max_texture_size(const sk_graphite_context_t*) { return 0; }
extern "C" SK_C_API bool                          sk_graphite_context_supports_protected_content(const sk_graphite_context_t*) { return false; }
extern "C" SK_C_API int64_t                       sk_graphite_context_get_current_budgeted_bytes(const sk_graphite_context_t*) { return 0; }
extern "C" SK_C_API int64_t                       sk_graphite_context_get_max_budgeted_bytes(const sk_graphite_context_t*) { return 0; }
extern "C" SK_C_API void                          sk_graphite_context_set_max_budgeted_bytes(sk_graphite_context_t*, int64_t) {}
extern "C" SK_C_API void                          sk_graphite_context_free_gpu_resources(sk_graphite_context_t*) {}
extern "C" SK_C_API void                          sk_graphite_context_perform_deferred_cleanup(sk_graphite_context_t*, int64_t) {}
extern "C" SK_C_API sk_graphite_recorder_t*       sk_graphite_context_make_recorder(sk_graphite_context_t*, int64_t) { return nullptr; }
extern "C" SK_C_API sk_graphite_recorder_t*       sk_graphite_context_make_recorder_with_image_provider(sk_graphite_context_t*, int64_t, sk_graphite_image_provider_t*) { return nullptr; }
extern "C" SK_C_API sk_graphite_insert_status_t   sk_graphite_context_insert_recording(sk_graphite_context_t*, const sk_graphite_insert_recording_info_t*) { return INVALID_RECORDING_SK_GRAPHITE_INSERT_STATUS; }
extern "C" SK_C_API bool                          sk_graphite_context_submit(sk_graphite_context_t*, const sk_graphite_submit_info_t*) { return false; }
extern "C" SK_C_API void                          sk_graphite_recorder_delete(sk_graphite_recorder_t*) {}
extern "C" SK_C_API sk_graphite_backend_t         sk_graphite_recorder_get_backend(const sk_graphite_recorder_t*) { return VULKAN_SK_GRAPHITE_BACKEND; }
extern "C" SK_C_API int32_t                       sk_graphite_recorder_get_max_texture_size(const sk_graphite_recorder_t*) { return 0; }
extern "C" SK_C_API sk_graphite_recording_t*      sk_graphite_recorder_snap(sk_graphite_recorder_t*) { return nullptr; }
extern "C" SK_C_API void                          sk_graphite_recording_delete(sk_graphite_recording_t*) {}
extern "C" SK_C_API sk_surface_t*                 sk_graphite_surface_make_render_target(sk_graphite_recorder_t*, const sk_imageinfo_t*, int32_t, const sk_surfaceprops_t*) { return nullptr; }
extern "C" SK_C_API void                          sk_graphite_context_async_rescale_and_read_pixels_surface(sk_graphite_context_t*, const sk_surface_t*, const sk_imageinfo_t*, const sk_irect_t*, sk_graphite_rescale_gamma_t, sk_graphite_rescale_mode_t, sk_graphite_async_read_pixels_proc_t cb, void* ctx) { if (cb) cb(ctx, nullptr); }
extern "C" SK_C_API void                          sk_graphite_context_check_async_work_completion(sk_graphite_context_t*) {}
extern "C" SK_C_API int32_t                       sk_graphite_async_read_result_get_count(const sk_graphite_async_read_result_t*) { return 0; }
extern "C" SK_C_API const void*                   sk_graphite_async_read_result_get_data(const sk_graphite_async_read_result_t*, int32_t) { return nullptr; }
extern "C" SK_C_API size_t                        sk_graphite_async_read_result_get_row_bytes(const sk_graphite_async_read_result_t*, int32_t) { return 0; }
extern "C" SK_C_API sk_surface_t*                 sk_graphite_surface_wrap_backend_texture(sk_graphite_recorder_t*, const sk_graphite_backend_texture_t*, sk_colortype_t, sk_colorspace_t*, const sk_surfaceprops_t*, sk_graphite_release_proc_t, void*) { return nullptr; }
extern "C" SK_C_API void                          sk_graphite_backend_texture_delete(sk_graphite_backend_texture_t*) {}
extern "C" SK_C_API bool                          sk_graphite_backend_texture_is_valid(const sk_graphite_backend_texture_t*) { return false; }
extern "C" SK_C_API sk_graphite_backend_t         sk_graphite_backend_texture_get_backend(const sk_graphite_backend_texture_t*) { return VULKAN_SK_GRAPHITE_BACKEND; }
extern "C" SK_C_API void                          sk_graphite_backend_texture_get_dimensions(const sk_graphite_backend_texture_t*, int32_t*, int32_t*) {}
extern "C" SK_C_API void                          sk_graphite_texture_info_delete(sk_graphite_texture_info_t*) {}
extern "C" SK_C_API bool                          sk_graphite_texture_info_is_valid(const sk_graphite_texture_info_t*) { return false; }
extern "C" SK_C_API sk_graphite_backend_t         sk_graphite_texture_info_get_backend(const sk_graphite_texture_info_t*) { return VULKAN_SK_GRAPHITE_BACKEND; }
extern "C" SK_C_API int32_t                       sk_graphite_texture_info_get_sample_count(const sk_graphite_texture_info_t*) { return 1; }
extern "C" SK_C_API int32_t                       sk_graphite_texture_info_get_mipmapped(const sk_graphite_texture_info_t*) { return 0; }
extern "C" SK_C_API sk_image_t*                   sk_graphite_image_wrap_texture(sk_graphite_recorder_t*, const sk_graphite_backend_texture_t*, sk_colortype_t, sk_alphatype_t, sk_colorspace_t*, sk_graphite_release_proc_t, void*) { return nullptr; }
extern "C" SK_C_API sk_graphite_backend_texture_t* sk_graphite_recorder_create_backend_texture(sk_graphite_recorder_t*, int32_t, int32_t, const sk_graphite_texture_info_t*) { return nullptr; }
extern "C" SK_C_API void                          sk_graphite_recorder_delete_backend_texture(sk_graphite_recorder_t*, const sk_graphite_backend_texture_t*) {}
extern "C" SK_C_API void                          sk_graphite_context_delete_backend_texture(sk_graphite_context_t*, const sk_graphite_backend_texture_t*) {}
extern "C" SK_C_API sk_graphite_image_provider_t* sk_graphite_image_provider_new(sk_graphite_image_provider_proc_t, void*) { return nullptr; }
extern "C" SK_C_API void                          sk_graphite_image_provider_delete(sk_graphite_image_provider_t*) {}
extern "C" SK_C_API sk_image_t*                   sk_graphite_image_make_texture(sk_graphite_recorder_t*, const sk_image_t*, int32_t) { return nullptr; }

#endif  // SK_GRAPHITE
