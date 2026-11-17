/*
 * Copyright 2024 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkMesh.h"
#include "include/core/SkString.h"
#include "include/core/SkData.h"
#include "include/core/SkColorSpace.h"
#include "include/effects/SkRuntimeEffect.h"
#if defined(SK_GANESH)
#include "include/gpu/ganesh/SkMeshGanesh.h"
#endif

#include "include/c/sk_mesh.h"
#include "include/c/sk_types.h"

#include "src/c/sk_types_priv.h"


// SkMeshSpecification

sk_meshspecification_t* sk_meshspecification_make(
    const sk_meshspecification_attribute_t* attributes, size_t attributeCount,
    size_t vertexStride,
    const sk_meshspecification_varying_t* varyings, size_t varyingCount,
    const sk_string_t* vs, const sk_string_t* fs,
    sk_colorspace_t* cs, sk_alphatype_t at,
    sk_string_t* error) {

    std::vector<SkMeshSpecification::Attribute> skAttrs(attributeCount);
    for (size_t i = 0; i < attributeCount; i++) {
        skAttrs[i].type = (SkMeshSpecification::Attribute::Type)attributes[i].fType;
        skAttrs[i].offset = attributes[i].fOffset;
        skAttrs[i].name = SkString(attributes[i].fName);
    }

    std::vector<SkMeshSpecification::Varying> skVaryings(varyingCount);
    for (size_t i = 0; i < varyingCount; i++) {
        skVaryings[i].type = (SkMeshSpecification::Varying::Type)varyings[i].fType;
        skVaryings[i].name = SkString(varyings[i].fName);
    }

    auto result = SkMeshSpecification::Make(
        SkSpan(skAttrs),
        vertexStride,
        SkSpan(skVaryings),
        AsString(*vs),
        AsString(*fs),
        cs ? sk_ref_sp(AsColorSpace(cs)) : nullptr,
        (SkAlphaType)at);

    if (error && result.error.size() > 0)
        AsString(error)->swap(result.error);

    return ToMeshSpecification(result.specification.release());
}

void sk_meshspecification_ref(sk_meshspecification_t* spec) {
    SkSafeRef(AsMeshSpecification(spec));
}

void sk_meshspecification_unref(sk_meshspecification_t* spec) {
    SkSafeUnref(AsMeshSpecification(spec));
}

size_t sk_meshspecification_get_stride(const sk_meshspecification_t* spec) {
    return AsMeshSpecification(spec)->stride();
}

size_t sk_meshspecification_get_uniform_byte_size(const sk_meshspecification_t* spec) {
    return AsMeshSpecification(spec)->uniformSize();
}

sk_colorspace_t* sk_meshspecification_get_colorspace(const sk_meshspecification_t* spec) {
    return ToColorSpace(sk_ref_sp(AsMeshSpecification(spec)->colorSpace()).release());
}

size_t sk_meshspecification_get_uniforms_size(const sk_meshspecification_t* spec) {
    return AsMeshSpecification(spec)->uniforms().size();
}

void sk_meshspecification_get_uniform_name(const sk_meshspecification_t* spec, int index, sk_string_t* name) {
    auto uniforms = AsMeshSpecification(spec)->uniforms();
    AsString(name)->set(uniforms[index].name);
}

void sk_meshspecification_get_uniform_from_index(const sk_meshspecification_t* spec, int index, sk_runtimeeffect_uniform_t* cuniform) {
    auto uniforms = AsMeshSpecification(spec)->uniforms();
    auto uniform = uniforms.begin() + index;
    *cuniform = *ToRuntimeEffectUniform(uniform);
}

size_t sk_meshspecification_get_children_size(const sk_meshspecification_t* spec) {
    return AsMeshSpecification(spec)->children().size();
}

void sk_meshspecification_get_child_name(const sk_meshspecification_t* spec, int index, sk_string_t* name) {
    auto children = AsMeshSpecification(spec)->children();
    AsString(name)->set(children[index].name);
}

void sk_meshspecification_get_child_from_index(const sk_meshspecification_t* spec, int index, sk_runtimeeffect_child_t* cchild) {
    auto children = AsMeshSpecification(spec)->children();
    auto child = children.begin() + index;
    *cchild = *ToRuntimeEffectChild(child);
}

// Buffers

sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_make(const void* data, size_t size) {
    if (!size)
        return nullptr;
    return ToMeshVertexBuffer(SkMeshes::MakeVertexBuffer(data, size).release());
}

sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_make_ganesh(gr_direct_context_t* context, const void* data, size_t size) {
    if (!size)
        return nullptr;
    return SK_ONLY_GPU(ToMeshVertexBuffer(SkMeshes::MakeVertexBuffer(AsGrDirectContext(context), data, size).release()), nullptr);
}

sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_copy(const sk_mesh_vertex_buffer_t* buffer) {
    return ToMeshVertexBuffer(SkMeshes::CopyVertexBuffer(sk_ref_sp(AsMeshVertexBuffer(buffer))).release());
}

sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_copy_ganesh(gr_direct_context_t* context, const sk_mesh_vertex_buffer_t* buffer) {
    return SK_ONLY_GPU(ToMeshVertexBuffer(SkMeshes::CopyVertexBuffer(AsGrDirectContext(context), sk_ref_sp(AsMeshVertexBuffer(buffer))).release()), nullptr);
}

bool sk_mesh_vertex_buffer_update(sk_mesh_vertex_buffer_t* buffer, gr_direct_context_t* context, const void* data, size_t offset, size_t size) {
    return AsMeshVertexBuffer(buffer)->update(SK_ONLY_GPU(AsGrDirectContext(context), nullptr), data, offset, size);
}

size_t sk_mesh_vertex_buffer_get_size(const sk_mesh_vertex_buffer_t* buffer) {
    return AsMeshVertexBuffer(buffer)->size();
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_make(const void* data, size_t size) {
    if (!size)
        return nullptr;
    return ToMeshIndexBuffer(SkMeshes::MakeIndexBuffer(data, size).release());
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_make_ganesh(gr_direct_context_t* context, const void* data, size_t size) {
    if (!size)
        return nullptr;
    return SK_ONLY_GPU(ToMeshIndexBuffer(SkMeshes::MakeIndexBuffer(AsGrDirectContext(context), data, size).release()), nullptr);
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_copy(const sk_mesh_index_buffer_t* buffer) {
    return ToMeshIndexBuffer(SkMeshes::CopyIndexBuffer(sk_ref_sp(AsMeshIndexBuffer(buffer))).release());
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_copy_ganesh(gr_direct_context_t* context, const sk_mesh_index_buffer_t* buffer) {
    return SK_ONLY_GPU(ToMeshIndexBuffer(SkMeshes::CopyIndexBuffer(AsGrDirectContext(context), sk_ref_sp(AsMeshIndexBuffer(buffer))).release()), nullptr);
}

bool sk_mesh_index_buffer_update(sk_mesh_index_buffer_t* buffer, gr_direct_context_t* context, const void* data, size_t offset, size_t size) {
    return AsMeshIndexBuffer(buffer)->update(SK_ONLY_GPU(AsGrDirectContext(context), nullptr), data, offset, size);
}

size_t sk_mesh_index_buffer_get_size(const sk_mesh_index_buffer_t* buffer) {
    return AsMeshIndexBuffer(buffer)->size();
}

// SkMesh

static std::vector<SkRuntimeEffect::ChildPtr> make_children(sk_flattenable_t** children, size_t childCount) {
    if (!children || !childCount)
        return {};
    std::vector<SkRuntimeEffect::ChildPtr> vec(childCount);
    for (size_t i = 0; i < childCount; i++) {
        vec[i] = sk_ref_sp(AsFlattenable(children[i]));
    }
    return vec;
}

static sk_sp<const SkData> make_uniforms(const void* data, size_t size) {
    if (!data || !size)
        return SkData::MakeEmpty();
    return SkData::MakeWithCopy(data, size);
}

sk_mesh_t* sk_mesh_make(
    sk_meshspecification_t* spec,
    const sk_mesh_make_args_t* args,
    sk_string_t* error) {

    if (!args)
        return nullptr;

    auto childVec = make_children(args->fChildren, args->fChildCount);
    auto result = SkMesh::Make(
        sk_ref_sp(AsMeshSpecification(spec)),
        (SkMesh::Mode)args->fMode,
        sk_ref_sp(AsMeshVertexBuffer(args->fVB)),
        args->fVCount,
        args->fVOffset,
        make_uniforms(args->fUniformData, args->fUniformSize),
        SkSpan(childVec.data(), childVec.size()),
        AsRect(args->fBounds));

    if (error && result.error.size() > 0)
        AsString(error)->swap(result.error);

    if (!result.mesh.isValid())
        return nullptr;

    return ToMesh(new SkMesh(std::move(result.mesh)));
}

sk_mesh_t* sk_mesh_make_indexed(
    sk_meshspecification_t* spec,
    const sk_mesh_make_indexed_args_t* args,
    sk_string_t* error) {

    if (!args)
        return nullptr;

    auto childVec = make_children(args->fChildren, args->fChildCount);
    auto result = SkMesh::MakeIndexed(
        sk_ref_sp(AsMeshSpecification(spec)),
        (SkMesh::Mode)args->fMode,
        sk_ref_sp(AsMeshVertexBuffer(args->fVB)),
        args->fVCount,
        args->fVOffset,
        sk_ref_sp(AsMeshIndexBuffer(args->fIB)),
        args->fICount,
        args->fIOffset,
        make_uniforms(args->fUniformData, args->fUniformSize),
        SkSpan(childVec.data(), childVec.size()),
        AsRect(args->fBounds));

    if (error && result.error.size() > 0)
        AsString(error)->swap(result.error);

    if (!result.mesh.isValid())
        return nullptr;

    return ToMesh(new SkMesh(std::move(result.mesh)));
}

void sk_mesh_delete(sk_mesh_t* mesh) {
    delete AsMesh(mesh);
}

bool sk_mesh_get_is_valid(const sk_mesh_t* mesh) {
    return AsMesh(mesh)->isValid();
}

sk_mesh_mode_t sk_mesh_get_mode(const sk_mesh_t* mesh) {
    return (sk_mesh_mode_t)AsMesh(mesh)->mode();
}

size_t sk_mesh_get_vertex_count(const sk_mesh_t* mesh) {
    return AsMesh(mesh)->vertexCount();
}

size_t sk_mesh_get_vertex_offset(const sk_mesh_t* mesh) {
    return AsMesh(mesh)->vertexOffset();
}

size_t sk_mesh_get_index_count(const sk_mesh_t* mesh) {
    return AsMesh(mesh)->indexCount();
}

size_t sk_mesh_get_index_offset(const sk_mesh_t* mesh) {
    return AsMesh(mesh)->indexOffset();
}

void sk_mesh_get_bounds(const sk_mesh_t* mesh, sk_rect_t* bounds) {
    *bounds = ToRect(AsMesh(mesh)->bounds());
}
