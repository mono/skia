#include "modules/skresources/include/SkResources.h"
#include "include/c/skresources_resource_provider.h"

#include "src/c/sk_types_priv.h"

void skresources_resource_provider_ref(skresources_resource_provider_t* instance) {
    SkSafeRef(AsSkResourcesResourceProvider(instance));
}
void skresources_resource_provider_unref(skresources_resource_provider_t* instance) {
    SkSafeUnref(AsSkResourcesResourceProvider(instance));
}
void skresources_resource_provider_delete(skresources_resource_provider_t *instance) {
    delete AsSkResourcesResourceProvider(instance);
}

skresources_resource_provider_t* skresources_file_resource_provider_make(const char* base_dir, size_t length, bool predecode){
    return ToSkResourcesResourceProvider(skresources::FileResourceProvider::Make(SkString(base_dir, length), predecode).release());
}
skresources_resource_provider_t* skresources_caching_resource_provider_proxy_make(skresources_resource_provider_t* rp) {
    return ToSkResourcesResourceProvider(skresources::CachingResourceProvider::Make(sk_ref_sp(AsSkResourcesResourceProvider(rp))).release());
}
skresources_resource_provider_t* skresources_data_uri_resource_provider_proxy_make(skresources_resource_provider_t* rp, bool predecode) {
    return ToSkResourcesResourceProvider(skresources::DataURIResourceProviderProxy::Make(sk_ref_sp(AsSkResourcesResourceProvider(rp)), predecode).release());
}
