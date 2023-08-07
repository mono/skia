/*
 * Copyright 2020 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkShader.h"
#include "include/effects/SkRuntimeEffect.h"

#include "include/c/sk_types.h"
#include "include/c/sk_runtimeeffect.h"

#include "src/c/sk_types_priv.h"

sk_runtimeeffect_t* sk_runtimeeffect_make_for_color_filter(sk_string_t* sksl, sk_string_t* error) {
    auto [effect, errorMessage] = SkRuntimeEffect::MakeForColorFilter(AsString(*sksl));
    if (error && errorMessage.size() > 0)
        AsString(error)->swap(errorMessage);
    return ToRuntimeEffect(effect.release());
}

sk_runtimeeffect_t* sk_runtimeeffect_make_for_shader(sk_string_t* sksl, sk_string_t* error) {
    auto [effect, errorMessage] = SkRuntimeEffect::MakeForShader(AsString(*sksl));
    if (error && errorMessage.size() > 0)
        AsString(error)->swap(errorMessage);
    return ToRuntimeEffect(effect.release());
}

void sk_runtimeeffect_unref(sk_runtimeeffect_t* effect) {
    SkSafeUnref(AsRuntimeEffect(effect));
}

sk_shader_t* sk_runtimeeffect_make_shader(sk_runtimeeffect_t* effect, sk_data_t* uniforms, sk_flattenable_t** children, size_t childCount, const sk_matrix_t* localMatrix) {
    std::vector<SkRuntimeEffect::ChildPtr> skChildren(childCount);
    for (size_t i = 0; i < childCount; i++) {
        skChildren[i] = sk_ref_sp(AsFlattenable(children[i]));
    }

    SkMatrix m;
    if (localMatrix)
        m = AsMatrix(localMatrix);
    
    sk_sp<SkShader> shader = AsRuntimeEffect(effect)->makeShader(
        sk_ref_sp(AsData(uniforms)),
        SkSpan(skChildren.data(), childCount),
        localMatrix ? &m : nullptr);

    return ToShader(shader.release());
}

sk_colorfilter_t* sk_runtimeeffect_make_color_filter(sk_runtimeeffect_t* effect, sk_data_t* uniforms, sk_flattenable_t** children, size_t childCount) {
    std::vector<SkRuntimeEffect::ChildPtr> skChildren(childCount);
    for (size_t i = 0; i < childCount; i++) {
        skChildren[i] = sk_ref_sp(AsFlattenable(children[i]));
    }

    sk_sp<SkColorFilter> shader = AsRuntimeEffect(effect)->makeColorFilter(
        sk_ref_sp(AsData(uniforms)),
        SkSpan(skChildren.data(), childCount));

    return ToColorFilter(shader.release());
}

size_t sk_runtimeeffect_get_uniform_byte_size(const sk_runtimeeffect_t* effect) {
    return AsRuntimeEffect(effect)->uniformSize();
}

size_t sk_runtimeeffect_get_uniforms_size(const sk_runtimeeffect_t* effect) {
    return AsRuntimeEffect(effect)->uniforms().size();
}

void sk_runtimeeffect_get_uniform_name(const sk_runtimeeffect_t* effect, int index, sk_string_t* name) {
    auto vector = AsRuntimeEffect(effect)->uniforms();
    auto item = vector[index];
    AsString(name)->set(item.name);
}

void sk_runtimeeffect_get_uniform_from_index(const sk_runtimeeffect_t* effect, int index, sk_runtimeeffect_uniform_t* cuniform) {
    auto uniforms = AsRuntimeEffect(effect)->uniforms();
    auto uniform = uniforms.begin() + index;
    *cuniform = *ToRuntimeEffectUniform(uniform);
    // return ToRuntimeEffectUniform(&(*uniform));
}

void sk_runtimeeffect_get_uniform_from_name(const sk_runtimeeffect_t* effect, const char* name, size_t len, sk_runtimeeffect_uniform_t* cuniform) {
    *cuniform = *ToRuntimeEffectUniform(AsRuntimeEffect(effect)->findUniform(std::string_view(name, len)));
}

size_t sk_runtimeeffect_get_children_size(const sk_runtimeeffect_t* effect) {
    return AsRuntimeEffect(effect)->children().size();
}

void sk_runtimeeffect_get_child_name(const sk_runtimeeffect_t* effect, int index, sk_string_t* name) {
    auto vector = AsRuntimeEffect(effect)->children();
    auto item = vector[index];
    AsString(name)->set(item.name);
}

void sk_runtimeeffect_get_child_from_index(const sk_runtimeeffect_t* effect, int index, sk_runtimeeffect_child_t* cchild) {
    auto children = AsRuntimeEffect(effect)->children();
    auto child = children.begin() + index;
    *cchild = *ToRuntimeEffectChild(child);
    // return ToRuntimeEffectUniform(&(*child));
}

void sk_runtimeeffect_get_child_from_name(const sk_runtimeeffect_t* effect, const char* name, size_t len, sk_runtimeeffect_child_t* cchild) {
    *cchild = *ToRuntimeEffectChild(AsRuntimeEffect(effect)->findChild(std::string_view(name, len)));
}
