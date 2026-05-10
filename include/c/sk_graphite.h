/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_graphite_DEFINED
#define sk_graphite_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// Opaque handles

typedef struct sk_graphite_context_t            sk_graphite_context_t;
typedef struct sk_graphite_recorder_t           sk_graphite_recorder_t;
typedef struct sk_graphite_recording_t          sk_graphite_recording_t;
typedef struct sk_graphite_backend_texture_t    sk_graphite_backend_texture_t;
typedef struct sk_graphite_texture_info_t       sk_graphite_texture_info_t;
typedef struct sk_graphite_image_provider_t     sk_graphite_image_provider_t;

// Backend identification

typedef enum {
    DAWN_SK_GRAPHITE_BACKEND   = 0,
    METAL_SK_GRAPHITE_BACKEND  = 1,
    VULKAN_SK_GRAPHITE_BACKEND = 2,
} sk_graphite_backend_t;

// Returns true if the requested backend was compiled into this build of
// libSkiaSharp. Safe to call before any context is created and on any backend.
SK_C_API bool sk_graphite_backend_is_available(sk_graphite_backend_t backend);

// ContextOptions (POD, value type)

typedef struct {
    bool      fDisableDriverCorrectnessWorkarounds;
    int32_t   fInternalMultisampleCount;       // valid: 1, 2, 4, 8, 16
    int64_t   fGpuBudgetInBytes;               // -1 to use Skia's default
    bool      fRequireOrderedRecordings;
    bool      fSetBackendLabels;
} sk_graphite_context_options_t;

SK_C_API void sk_graphite_context_options_init_defaults(sk_graphite_context_options_t* out);

// Submission control

typedef enum {
    NO_SK_GRAPHITE_SYNC_TO_CPU  = 0,
    YES_SK_GRAPHITE_SYNC_TO_CPU = 1,
} sk_graphite_sync_to_cpu_t;

typedef enum {
    NO_SK_GRAPHITE_MARK_FRAME_BOUNDARY  = 0,
    YES_SK_GRAPHITE_MARK_FRAME_BOUNDARY = 1,
} sk_graphite_mark_frame_boundary_t;

typedef struct {
    sk_graphite_sync_to_cpu_t          fSync;
    sk_graphite_mark_frame_boundary_t  fMarkBoundary;
    uint64_t                           fFrameID;
} sk_graphite_submit_info_t;

// Recording insert / status

typedef enum {
    SUCCESS_SK_GRAPHITE_INSERT_STATUS                          = 0,
    INVALID_RECORDING_SK_GRAPHITE_INSERT_STATUS                = 1,
    PROMISE_INSTANTIATION_FAILED_SK_GRAPHITE_INSERT_STATUS     = 2,
    ADD_COMMANDS_FAILED_SK_GRAPHITE_INSERT_STATUS              = 3,
    ASYNC_SHADER_COMPILES_FAILED_SK_GRAPHITE_INSERT_STATUS     = 4,
    OUT_OF_ORDER_RECORDING_SK_GRAPHITE_INSERT_STATUS           = 5,
} sk_graphite_insert_status_t;

typedef struct {
    sk_graphite_recording_t*    fRecording;        // non-null
    sk_surface_t*               fTargetSurface;    // nullable; for deferred canvas targets
    int32_t                     fTargetTranslationX;
    int32_t                     fTargetTranslationY;
    sk_irect_t                  fTargetClip;
} sk_graphite_insert_recording_info_t;

// Release callback for caller-owned backend textures

typedef void (*sk_graphite_release_proc_t)(void* releaseContext);

// Context

SK_C_API void                          sk_graphite_context_delete(sk_graphite_context_t* context);
SK_C_API sk_graphite_backend_t         sk_graphite_context_get_backend(const sk_graphite_context_t* context);
SK_C_API bool                          sk_graphite_context_is_device_lost(const sk_graphite_context_t* context);
SK_C_API int32_t                       sk_graphite_context_get_max_texture_size(const sk_graphite_context_t* context);
SK_C_API bool                          sk_graphite_context_supports_protected_content(const sk_graphite_context_t* context);
SK_C_API int64_t                       sk_graphite_context_get_current_budgeted_bytes(const sk_graphite_context_t* context);
SK_C_API int64_t                       sk_graphite_context_get_max_budgeted_bytes(const sk_graphite_context_t* context);
SK_C_API void                          sk_graphite_context_set_max_budgeted_bytes(sk_graphite_context_t* context, int64_t bytes);
SK_C_API void                          sk_graphite_context_free_gpu_resources(sk_graphite_context_t* context);
SK_C_API void                          sk_graphite_context_perform_deferred_cleanup(sk_graphite_context_t* context, int64_t milliseconds);

// Recorder vending. recorderBudgetBytes < 0 uses the context default.
SK_C_API sk_graphite_recorder_t*       sk_graphite_context_make_recorder(sk_graphite_context_t* context, int64_t recorderBudgetBytes);

// Variant that attaches an ImageProvider to the new recorder. Ownership of
// the provider transfers to the recorder on success — caller must NOT call
// sk_graphite_image_provider_delete on it afterwards. Pass null to behave
// identically to sk_graphite_context_make_recorder. The provider can also
// be sourced from sk_graphite_image_provider_new.
SK_C_API sk_graphite_recorder_t*       sk_graphite_context_make_recorder_with_image_provider(
    sk_graphite_context_t* context,
    int64_t recorderBudgetBytes,
    sk_graphite_image_provider_t* imageProvider);

// Insert / submit. insert returns a status — non-success is not an error path.
SK_C_API sk_graphite_insert_status_t   sk_graphite_context_insert_recording(sk_graphite_context_t* context, const sk_graphite_insert_recording_info_t* info);
SK_C_API bool                          sk_graphite_context_submit(sk_graphite_context_t* context, const sk_graphite_submit_info_t* info /* nullable -> defaults */);

// Recorder

SK_C_API void                          sk_graphite_recorder_delete(sk_graphite_recorder_t* recorder);
SK_C_API sk_graphite_backend_t         sk_graphite_recorder_get_backend(const sk_graphite_recorder_t* recorder);
SK_C_API int32_t                       sk_graphite_recorder_get_max_texture_size(const sk_graphite_recorder_t* recorder);
// Snap: returns null if no recording has been recorded since the last snap().
SK_C_API sk_graphite_recording_t*      sk_graphite_recorder_snap(sk_graphite_recorder_t* recorder);

// Recording

SK_C_API void                          sk_graphite_recording_delete(sk_graphite_recording_t* recording);

// Surface factories (Graphite-backed)

SK_C_API sk_surface_t*                 sk_graphite_surface_make_render_target(
    sk_graphite_recorder_t* recorder,
    const sk_imageinfo_t*   info,
    int32_t                 mipmapped,           // 0 = no, 1 = yes
    const sk_surfaceprops_t* props /* nullable */);

// Wrap a caller-allocated GPU texture (described by an opaque
// sk_graphite_backend_texture_t) as an SkSurface so that drawing into it
// writes pixels into the original GPU resource. Caller retains ownership of
// the underlying GPU object — the wrapper does NOT free it on disposal.
//
// The release callback (if provided) fires when the wrapping surface is
// destroyed, giving the caller a hook to release the wrapped resource.
SK_C_API sk_surface_t*                 sk_graphite_surface_wrap_backend_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* backendTexture,
    sk_colortype_t          colorType,
    sk_colorspace_t*        colorSpace /* nullable */,
    const sk_surfaceprops_t* props /* nullable */,
    sk_graphite_release_proc_t releaseProc /* nullable */,
    void* releaseContext);

// Wrap a Graphite-allocated or caller-allocated GPU texture as an SkImage so
// it can be sampled by other draw operations (paint sources, image filters,
// shaders). Mirrors SkImages::WrapTexture in upstream Skia.
SK_C_API sk_image_t*                   sk_graphite_image_wrap_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* backendTexture,
    sk_colortype_t          colorType,
    sk_alphatype_t          alphaType,
    sk_colorspace_t*        colorSpace /* nullable */,
    sk_graphite_release_proc_t releaseProc /* nullable */,
    void* releaseContext);

// Recorder-allocated BackendTexture: lets Skia allocate a fresh GPU texture
// matching the supplied TextureInfo. Caller owns the wrapper; pair with
// sk_graphite_recorder_delete_backend_texture or sk_graphite_context_delete_backend_texture
// (the recorder/context releases the underlying GPU resource).
SK_C_API sk_graphite_backend_texture_t* sk_graphite_recorder_create_backend_texture(
    sk_graphite_recorder_t* recorder,
    int32_t                 width,
    int32_t                 height,
    const sk_graphite_texture_info_t* info);

SK_C_API void                          sk_graphite_recorder_delete_backend_texture(
    sk_graphite_recorder_t* recorder,
    const sk_graphite_backend_texture_t* tex);

SK_C_API void                          sk_graphite_context_delete_backend_texture(
    sk_graphite_context_t* context,
    const sk_graphite_backend_texture_t* tex);

// BackendTexture handle — opaque heap wrapper around skgpu::graphite::BackendTexture
// (which is a value type in C++; we heap-alloc to keep ABI stable across Skia revs).

SK_C_API void                          sk_graphite_backend_texture_delete       (sk_graphite_backend_texture_t* tex);
SK_C_API bool                          sk_graphite_backend_texture_is_valid     (const sk_graphite_backend_texture_t* tex);
SK_C_API sk_graphite_backend_t         sk_graphite_backend_texture_get_backend  (const sk_graphite_backend_texture_t* tex);
SK_C_API void                          sk_graphite_backend_texture_get_dimensions(const sk_graphite_backend_texture_t* tex, int32_t* outWidth, int32_t* outHeight);

// TextureInfo handle — describes a backend's texture format/sample/mipmap state
// without referring to a concrete GPU resource.

SK_C_API void                          sk_graphite_texture_info_delete         (sk_graphite_texture_info_t* info);
SK_C_API bool                          sk_graphite_texture_info_is_valid       (const sk_graphite_texture_info_t* info);
SK_C_API sk_graphite_backend_t         sk_graphite_texture_info_get_backend    (const sk_graphite_texture_info_t* info);
SK_C_API int32_t                       sk_graphite_texture_info_get_sample_count(const sk_graphite_texture_info_t* info);
SK_C_API int32_t                       sk_graphite_texture_info_get_mipmapped  (const sk_graphite_texture_info_t* info); // 0 = no, 1 = yes

// Asynchronous CPU readback — direct pass-through of upstream Skia's
// Context::asyncRescaleAndReadPixels. The legacy sk_surface_read_pixels does
// NOT work on Graphite-backed surfaces in production builds (Skia gates the
// implementation on GPU_TEST_UTILS — see src/gpu/graphite/Device.cpp).
//
// The callback is invoked exactly once. The result pointer is non-owning and
// is only valid for the duration of the callback invocation. A null result
// means failure (rect out of bounds, lost device, etc.).
//
// Drive completion by calling sk_graphite_context_check_async_work_completion
// from the same thread that owns the context. The callback fires on the
// thread that calls checkAsyncWorkCompletion.

typedef struct sk_graphite_async_read_result_t sk_graphite_async_read_result_t;

typedef enum {
    SRC_SK_GRAPHITE_RESCALE_GAMMA    = 0,
    LINEAR_SK_GRAPHITE_RESCALE_GAMMA = 1,
} sk_graphite_rescale_gamma_t;

typedef enum {
    NEAREST_SK_GRAPHITE_RESCALE_MODE         = 0,
    REPEATED_LINEAR_SK_GRAPHITE_RESCALE_MODE = 1,
    REPEATED_CUBIC_SK_GRAPHITE_RESCALE_MODE  = 2,
} sk_graphite_rescale_mode_t;

typedef void (*sk_graphite_async_read_pixels_proc_t)(
    void* callbackContext,
    const sk_graphite_async_read_result_t* result /* non-owning, valid only during callback */);

SK_C_API void sk_graphite_context_async_rescale_and_read_pixels_surface(
    sk_graphite_context_t* context,
    const sk_surface_t* surface,
    const sk_imageinfo_t* dstInfo,
    const sk_irect_t* srcRect,
    sk_graphite_rescale_gamma_t rescaleGamma,
    sk_graphite_rescale_mode_t  rescaleMode,
    sk_graphite_async_read_pixels_proc_t callback,
    void* callbackContext);

SK_C_API void sk_graphite_context_check_async_work_completion(sk_graphite_context_t* context);

// AsyncReadResult accessors — call only inside the async-read callback.
SK_C_API int32_t      sk_graphite_async_read_result_get_count    (const sk_graphite_async_read_result_t* result);
SK_C_API const void*  sk_graphite_async_read_result_get_data     (const sk_graphite_async_read_result_t* result, int32_t planeIndex);
SK_C_API size_t       sk_graphite_async_read_result_get_row_bytes(const sk_graphite_async_read_result_t* result, int32_t planeIndex);

// ImageProvider — bridge so a managed (or any external) caller can satisfy
// Graphite's "convert non-Graphite SkImage to Graphite-backed" hook without
// owning a C++ subclass of skgpu::graphite::ImageProvider.
//
// The callback fires on the recorder's owning thread. Return a Graphite-backed
// SkImage (e.g. obtained via sk_graphite_image_make_texture) that preserves
// the original image's dimensions and alpha type. Returning null causes the
// triggering draw to be dropped — same as if no provider were set.
//
// Ownership: the returned sk_image_t* is consumed by Skia; the bridge takes
// the +1 reference and decrements after Skia is done. Do NOT call sk_image_unref
// on it from your callback — return it directly.

typedef sk_image_t* (*sk_graphite_image_provider_proc_t)(
    void* userData,
    sk_graphite_recorder_t* recorder,
    const sk_image_t* image,
    int32_t mipmapped /* 0 = no, 1 = yes */);

// Build an ImageProvider that dispatches to the given callback. Caller retains
// ownership of the returned wrapper until it is passed to a Context via
// sk_graphite_context_options_t::fImageProvider; ownership transfers on
// successful Context creation.
SK_C_API sk_graphite_image_provider_t* sk_graphite_image_provider_new(
    sk_graphite_image_provider_proc_t proc,
    void* userData);

// Free an unused provider (e.g. when CreateVulkan returned null and the caller
// needs to clean up). Safe on null. Calling this AFTER the provider has been
// installed in a Context is undefined — the Context owns it from that point.
SK_C_API void sk_graphite_image_provider_delete(sk_graphite_image_provider_t* provider);

// Upload a raster (CPU-backed) SkImage to a Graphite-backed texture. This is
// the same operation Skia's SkImages::TextureFromImage performs; exposed so a
// C# implementation of sk_graphite_image_provider_proc_t can do the actual
// conversion the hook is asked for. Returns null if the recorder is null or
// the upload failed.
SK_C_API sk_image_t* sk_graphite_image_make_texture(
    sk_graphite_recorder_t* recorder,
    const sk_image_t* image,
    int32_t mipmapped /* 0 = no, 1 = yes */);

SK_C_PLUS_PLUS_END_GUARD

#endif  // sk_graphite_DEFINED
