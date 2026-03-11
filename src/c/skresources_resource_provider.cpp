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

sk_data_t* skresources_resource_provider_load(skresources_resource_provider_t *instance, const char* path, const char* name) {
    return ToData(AsSkResourcesResourceProvider(instance)->load(path, name).release());
}
skresources_image_asset_t* skresources_resource_provider_load_image_asset(skresources_resource_provider_t *instance, const char* path, const char* name, const char* id) {
    return ToSkResourcesImageAsset(AsSkResourcesResourceProvider(instance)->loadImageAsset(path, name, id).release());
}
skresources_external_track_asset_t* skresources_resource_provider_load_audio_asset(skresources_resource_provider_t *instance, const char* path, const char* name, const char* id) {
    return ToSkResourcesExternalTrackAsset(AsSkResourcesResourceProvider(instance)->loadAudioAsset(path, name, id).release());
}
sk_typeface_t* skresources_resource_provider_load_typeface(skresources_resource_provider_t *instance, const char* name, const char* url) {
    return ToTypeface(AsSkResourcesResourceProvider(instance)->loadTypeface(name, url).release());
}

skresources_resource_provider_t* skresources_file_resource_provider_make(sk_string_t* base_dir, bool predecode){
    return ToSkResourcesResourceProvider(skresources::FileResourceProvider::Make(AsString(*base_dir), predecode).release());
}
skresources_resource_provider_t* skresources_caching_resource_provider_proxy_make(skresources_resource_provider_t* rp) {
    return ToSkResourcesResourceProvider(skresources::CachingResourceProvider::Make(sk_ref_sp(AsSkResourcesResourceProvider(rp))).release());
}
skresources_resource_provider_t* skresources_data_uri_resource_provider_proxy_make(skresources_resource_provider_t* rp, bool predecode) {
    return ToSkResourcesResourceProvider(skresources::DataURIResourceProviderProxy::Make(sk_ref_sp(AsSkResourcesResourceProvider(rp)), predecode).release());
}
