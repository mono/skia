/*
 * Copyright 2024 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_mesh_DEFINED
#define sk_mesh_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// SkMeshSpecification

SK_C_API sk_meshspecification_t* sk_meshspecification_make(
    const sk_meshspecification_attribute_t* attributes, size_t attributeCount,
    size_t vertexStride,
    const sk_meshspecification_varying_t* varyings, size_t varyingCount,
    const sk_string_t* vs, const sk_string_t* fs,
    sk_colorspace_t* cs, sk_alphatype_t at,
    sk_string_t* error);
SK_C_API void sk_meshspecification_ref(sk_meshspecification_t* spec);
SK_C_API void sk_meshspecification_unref(sk_meshspecification_t* spec);
SK_C_API size_t sk_meshspecification_get_stride(const sk_meshspecification_t* spec);
SK_C_API size_t sk_meshspecification_get_uniform_byte_size(const sk_meshspecification_t* spec);
SK_C_API sk_colorspace_t* sk_meshspecification_get_colorspace(const sk_meshspecification_t* spec);

SK_C_API size_t sk_meshspecification_get_uniforms_size(const sk_meshspecification_t* spec);
SK_C_API void sk_meshspecification_get_uniform_name(const sk_meshspecification_t* spec, int index, sk_string_t* name);
SK_C_API void sk_meshspecification_get_uniform_from_index(const sk_meshspecification_t* spec, int index, sk_runtimeeffect_uniform_t* cuniform);

SK_C_API size_t sk_meshspecification_get_children_size(const sk_meshspecification_t* spec);
SK_C_API void sk_meshspecification_get_child_name(const sk_meshspecification_t* spec, int index, sk_string_t* name);
SK_C_API void sk_meshspecification_get_child_from_index(const sk_meshspecification_t* spec, int index, sk_runtimeeffect_child_t* cchild);

// Buffers

SK_C_API sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_make(const void* data, size_t size);
SK_C_API sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_make_ganesh(gr_direct_context_t* context, const void* data, size_t size);
SK_C_API sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_copy(const sk_mesh_vertex_buffer_t* buffer);
SK_C_API sk_mesh_vertex_buffer_t* sk_mesh_vertex_buffer_copy_ganesh(gr_direct_context_t* context, const sk_mesh_vertex_buffer_t* buffer);
SK_C_API bool sk_mesh_vertex_buffer_update(sk_mesh_vertex_buffer_t* buffer, gr_direct_context_t* context, const void* data, size_t offset, size_t size);
SK_C_API size_t sk_mesh_vertex_buffer_get_size(const sk_mesh_vertex_buffer_t* buffer);

SK_C_API sk_mesh_index_buffer_t* sk_mesh_index_buffer_make(const void* data, size_t size);
SK_C_API sk_mesh_index_buffer_t* sk_mesh_index_buffer_make_ganesh(gr_direct_context_t* context, const void* data, size_t size);
SK_C_API sk_mesh_index_buffer_t* sk_mesh_index_buffer_copy(const sk_mesh_index_buffer_t* buffer);
SK_C_API sk_mesh_index_buffer_t* sk_mesh_index_buffer_copy_ganesh(gr_direct_context_t* context, const sk_mesh_index_buffer_t* buffer);
SK_C_API bool sk_mesh_index_buffer_update(sk_mesh_index_buffer_t* buffer, gr_direct_context_t* context, const void* data, size_t offset, size_t size);
SK_C_API size_t sk_mesh_index_buffer_get_size(const sk_mesh_index_buffer_t* buffer);

// SkMesh

SK_C_API sk_mesh_t* sk_mesh_make(
    sk_meshspecification_t* spec,
    sk_mesh_mode_t mode,
    sk_mesh_vertex_buffer_t* vb,
    size_t vertexCount,
    size_t vertexOffset,
    const sk_data_t* uniforms,
    sk_flattenable_t** children,
    const sk_rect_t* bounds,
    sk_string_t* error);
SK_C_API sk_mesh_t* sk_mesh_make_indexed(
    sk_meshspecification_t* spec,
    sk_mesh_mode_t mode,
    sk_mesh_vertex_buffer_t* vb,
    size_t vertexCount,
    size_t vertexOffset,
    sk_mesh_index_buffer_t* ib,
    size_t indexCount,
    size_t indexOffset,
    const sk_data_t* uniforms,
    sk_flattenable_t** children,
    const sk_rect_t* bounds,
    sk_string_t* error);
SK_C_API void sk_mesh_delete(sk_mesh_t* mesh);
SK_C_API bool sk_mesh_get_is_valid(const sk_mesh_t* mesh);
SK_C_API sk_mesh_mode_t sk_mesh_get_mode(const sk_mesh_t* mesh);
SK_C_API size_t sk_mesh_get_vertex_count(const sk_mesh_t* mesh);
SK_C_API size_t sk_mesh_get_vertex_offset(const sk_mesh_t* mesh);
SK_C_API size_t sk_mesh_get_index_count(const sk_mesh_t* mesh);
SK_C_API size_t sk_mesh_get_index_offset(const sk_mesh_t* mesh);
SK_C_API void sk_mesh_get_bounds(const sk_mesh_t* mesh, sk_rect_t* bounds);

SK_C_PLUS_PLUS_END_GUARD

#endif
