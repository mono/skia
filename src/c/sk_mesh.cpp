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
    return ToMeshVertexBuffer(SkMeshes::MakeVertexBuffer(data, size).release());
}

sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_copy(const sk_mesh_vertex_buffer_t* buffer) {
    return ToMeshVertexBuffer(SkMeshes::CopyVertexBuffer(sk_ref_sp(AsMeshVertexBuffer(buffer))).release());
}

size_t sk_mesh_vertex_buffer_get_size(const sk_mesh_vertex_buffer_t* buffer) {
    return AsMeshVertexBuffer(buffer)->size();
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_make(const void* data, size_t size) {
    return ToMeshIndexBuffer(SkMeshes::MakeIndexBuffer(data, size).release());
}

sk_mesh_index_buffer_t* sk_mesh_index_buffer_copy(const sk_mesh_index_buffer_t* buffer) {
    return ToMeshIndexBuffer(SkMeshes::CopyIndexBuffer(sk_ref_sp(AsMeshIndexBuffer(buffer))).release());
}

size_t sk_mesh_index_buffer_get_size(const sk_mesh_index_buffer_t* buffer) {
    return AsMeshIndexBuffer(buffer)->size();
}

// SkMesh — builder pattern

sk_mesh_t* sk_mesh_new(void) {
    return ToMeshBuilder(new SkMeshBuilder());
}

void sk_mesh_delete(sk_mesh_t* mesh) {
    delete AsMeshBuilder(mesh);
}

void sk_mesh_set_spec(sk_mesh_t* mesh, sk_meshspecification_t* spec) {
    AsMeshBuilder(mesh)->fSpec = sk_ref_sp(AsMeshSpecification(spec));
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_mode(sk_mesh_t* mesh, sk_mesh_mode_t mode) {
    AsMeshBuilder(mesh)->fMode = (SkMesh::Mode)mode;
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_vertex_buffer(sk_mesh_t* mesh, sk_mesh_vertex_buffer_t* vb, size_t vertexCount, size_t vertexOffset) {
    AsMeshBuilder(mesh)->fVB = sk_ref_sp(AsMeshVertexBuffer(vb));
    AsMeshBuilder(mesh)->fVCount = vertexCount;
    AsMeshBuilder(mesh)->fVOffset = vertexOffset;
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_index_buffer(sk_mesh_t* mesh, sk_mesh_index_buffer_t* ib, size_t indexCount, size_t indexOffset) {
    AsMeshBuilder(mesh)->fIB = ib ? sk_ref_sp(AsMeshIndexBuffer(ib)) : nullptr;
    AsMeshBuilder(mesh)->fICount = indexCount;
    AsMeshBuilder(mesh)->fIOffset = indexOffset;
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_uniforms(sk_mesh_t* mesh, sk_data_t* uniforms) {
    AsMeshBuilder(mesh)->fUniforms = uniforms ? sk_ref_sp(AsData(uniforms)) : nullptr;
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_children(sk_mesh_t* mesh, sk_flattenable_t** children, size_t childCount) {
    auto& vec = AsMeshBuilder(mesh)->fChildren;
    vec.resize(childCount);
    for (size_t i = 0; i < childCount; i++) {
        vec[i] = sk_ref_sp(AsFlattenable(children[i]));
    }
    AsMeshBuilder(mesh)->fValidated = false;
}

void sk_mesh_set_bounds(sk_mesh_t* mesh, const sk_rect_t* bounds) {
    AsMeshBuilder(mesh)->fBounds = *AsRect(bounds);
    AsMeshBuilder(mesh)->fValidated = false;
}

bool sk_mesh_validate(sk_mesh_t* mesh, sk_string_t* error) {
    auto builder = AsMeshBuilder(mesh);
    SkMesh::Result result;

    if (builder->fIB) {
        result = SkMesh::MakeIndexed(
            builder->fSpec,
            builder->fMode,
            builder->fVB,
            builder->fVCount,
            builder->fVOffset,
            builder->fIB,
            builder->fICount,
            builder->fIOffset,
            builder->fUniforms,
            SkSpan(builder->fChildren.data(), builder->fChildren.size()),
            builder->fBounds);
    } else {
        result = SkMesh::Make(
            builder->fSpec,
            builder->fMode,
            builder->fVB,
            builder->fVCount,
            builder->fVOffset,
            builder->fUniforms,
            SkSpan(builder->fChildren.data(), builder->fChildren.size()),
            builder->fBounds);
    }

    if (error && result.error.size() > 0)
        AsString(error)->swap(result.error);

    builder->fMesh = std::move(result.mesh);
    builder->fValidated = builder->fMesh.isValid();
    return builder->fValidated;
}

bool sk_mesh_get_is_valid(const sk_mesh_t* mesh) {
    return AsMeshBuilder(const_cast<sk_mesh_t*>(mesh))->fValidated &&
           AsMeshBuilder(const_cast<sk_mesh_t*>(mesh))->fMesh.isValid();
}
