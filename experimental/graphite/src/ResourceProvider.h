/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_ResourceProvider_DEFINED
#define skgpu_ResourceProvider_DEFINED

#include "src/core/SkLRUCache.h"

#include "experimental/graphite/src/RenderPipelineDesc.h"

namespace skgpu {

class CommandBuffer;
class Gpu;
class RenderPipeline;

class ResourceProvider {
public:
    virtual ~ResourceProvider();

    virtual std::unique_ptr<CommandBuffer> createCommandBuffer() { return nullptr; }
    RenderPipeline* findOrCreateRenderPipeline(const RenderPipelineDesc&);

protected:
    ResourceProvider(const Gpu* gpu);

    virtual RenderPipeline* onCreateRenderPipeline(const RenderPipelineDesc&) {
        return nullptr;
    }

    const Gpu* fGpu;

private:
    class RenderPipelineCache {
    public:
        RenderPipelineCache(ResourceProvider* resourceProvider);
        ~RenderPipelineCache();

        void release();
        RenderPipeline* refPipeline(const RenderPipelineDesc&);

    private:
        struct Entry;

        struct DescHash {
            uint32_t operator()(const RenderPipelineDesc& desc) const {
                return SkOpts::hash_fn(desc.asKey(), desc.keyLength(), 0);
            }
        };

        SkLRUCache<const RenderPipelineDesc, std::unique_ptr<Entry>, DescHash> fMap;

        ResourceProvider* fResourceProvider;
    };

    // Cache of RenderPipelines
    std::unique_ptr<RenderPipelineCache> fRenderPipelineCache;
};

} // namespace skgpu

#endif // skgpu_ResourceProvider_DEFINED
