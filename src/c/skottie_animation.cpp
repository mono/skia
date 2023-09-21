#include "modules/skottie/include/Skottie.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/sksg/include/SkSGInvalidationController.h"
#include "include/c/skottie_animation.h"

#include "src/c/sk_types_priv.h"

#include "src/utils/SkJSON.h"
#include "include/core/SkCubicMap.h"

/*
 * skottie::Animation
 */

void skottie_animation_keepalive(void) {
    // This function is needed on Tizen to ensure required types are kept alive
    // It is not meant to be executed.

    skjson::ObjectValue* a = nullptr;
    auto r = (*a)["tmp"].getType();

    SkCubicMap* b = nullptr;
    (*b).computeYFromX((int)r);
}

void skottie_animation_ref(skottie_animation_t* instance) {
    SkSafeRef(AsSkottieAnimation(instance));
}

void skottie_animation_unref(skottie_animation_t* instance) {
    SkSafeUnref(AsSkottieAnimation(instance));
}

skottie_animation_t* skottie_animation_make_from_string(const char* data, size_t length) {
    return ToSkottieAnimation(skottie::Animation::Make(data, length).release());
}

skottie_animation_t* skottie_animation_make_from_data(const char* data, size_t length) {
    return ToSkottieAnimation(skottie::Animation::Make(data, length).release());
}

skottie_animation_t* skottie_animation_make_from_stream(sk_stream_t* stream) {
    return ToSkottieAnimation(skottie::Animation::Make(AsStream(stream)).release());
}

skottie_animation_t* skottie_animation_make_from_file(const char* path) {
    return ToSkottieAnimation(skottie::Animation::MakeFromFile(path).release());
}

void skottie_animation_delete(skottie_animation_t* instance) {
    delete AsSkottieAnimation(instance);
}

void skottie_animation_render(skottie_animation_t *instance, sk_canvas_t *canvas, sk_rect_t *dst) {
    AsSkottieAnimation(instance)->render(AsCanvas(canvas), AsRect(dst));
}

void skottie_animation_render_with_flags(skottie_animation_t *instance, sk_canvas_t *canvas, sk_rect_t *dst, skottie_animation_renderflags_t flags) {
    AsSkottieAnimation(instance)->render(AsCanvas(canvas), AsRect(dst), (skottie::Animation::RenderFlags)(flags));
}

void skottie_animation_seek(skottie_animation_t *instance, float t, sksg_invalidation_controller_t *ic) {
    AsSkottieAnimation(instance)->seek(t, AsSksgInvalidationController(ic));
}

void skottie_animation_seek_frame(skottie_animation_t *instance, float t, sksg_invalidation_controller_t *ic) {
    AsSkottieAnimation(instance)->seekFrame(t, AsSksgInvalidationController(ic));
}

void skottie_animation_seek_frame_time(skottie_animation_t *instance, float t, sksg_invalidation_controller_t *ic) {
    AsSkottieAnimation(instance)->seekFrameTime(t, AsSksgInvalidationController(ic));
}

double skottie_animation_get_duration(skottie_animation_t *instance) {
    return AsSkottieAnimation(instance)->duration();
}

double skottie_animation_get_fps(skottie_animation_t *instance) {
    return AsSkottieAnimation(instance)->fps();
}

double skottie_animation_get_in_point(skottie_animation_t *instance) {
    return AsSkottieAnimation(instance)->inPoint();
}

double skottie_animation_get_out_point(skottie_animation_t *instance) {
    return AsSkottieAnimation(instance)->outPoint();
}

void skottie_animation_get_version(skottie_animation_t *instance, sk_string_t* version) {
    AsString(version)->set(AsSkottieAnimation(instance)->version());
}

void skottie_animation_get_size(skottie_animation_t *instance, sk_size_t* size) {
    *size = ToSize(AsSkottieAnimation(instance)->size());
}


/*
 * skottie::Animation::Builder
 */

skottie_animation_builder_t* skottie_animation_builder_new(skottie_animation_builder_flags_t flags) {
    return ToSkottieAnimationBuilder(new skottie::Animation::Builder((skottie::Animation::Builder::Flags)flags));
}

void skottie_animation_builder_delete(skottie_animation_builder_t *instance) {
    delete AsSkottieAnimationBuilder(instance);
}

void skottie_animation_builder_get_stats(skottie_animation_builder_t* instance, skottie_animation_builder_stats_t* stats) {
    *stats = ToSkottieAnimationBuilderStats(AsSkottieAnimationBuilder(instance)->getStats());
}

void skottie_animation_builder_set_resource_provider(skottie_animation_builder_t* instance, skottie_resource_provider_t* resourceProvider) {
    AsSkottieAnimationBuilder(instance)->setResourceProvider(sk_ref_sp(AsSkottieResourceProvider(resourceProvider)));
}

void skottie_animation_builder_set_font_manager(skottie_animation_builder_t* instance, sk_fontmgr_t* fontManager) {
    AsSkottieAnimationBuilder(instance)->setFontManager(sk_ref_sp(AsFontMgr(fontManager)));
}

skottie_animation_t* skottie_animation_builder_make_from_stream(skottie_animation_builder_t* instance, sk_stream_t* stream) {
    return ToSkottieAnimation(AsSkottieAnimationBuilder(instance)->make(AsStream(stream)).release());
}

skottie_animation_t* skottie_animation_builder_make_from_file(skottie_animation_builder_t* instance, const char* path) {
    return ToSkottieAnimation(AsSkottieAnimationBuilder(instance)->makeFromFile(path).release());
}

skottie_animation_t* skottie_animation_builder_make_from_string(skottie_animation_builder_t* instance, const char* data, size_t length) {
    return ToSkottieAnimation(AsSkottieAnimationBuilder(instance)->make(data, length).release());
}

skottie_animation_t* skottie_animation_builder_make_from_data(skottie_animation_builder_t* instance, const char* data, size_t length) {
    return ToSkottieAnimation(AsSkottieAnimationBuilder(instance)->make(data, length).release());
}
