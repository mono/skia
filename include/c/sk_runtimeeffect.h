/*
 * Copyright 2020 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_runtimeeffect_DEFINED
#define sk_runtimeeffect_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

SK_C_API sk_runtimeeffect_t* sk_runtimeeffect_make_for_color_filter(sk_string_t* sksl, sk_string_t* error);
SK_C_API sk_runtimeeffect_t* sk_runtimeeffect_make_for_shader(sk_string_t* sksl, sk_string_t* error);
SK_C_API void sk_runtimeeffect_unref(sk_runtimeeffect_t* effect);
SK_C_API sk_shader_t* sk_runtimeeffect_make_shader(sk_runtimeeffect_t* effect, sk_data_t* uniforms, sk_flattenable_t** children, size_t childCount, const sk_matrix_t* localMatrix);
SK_C_API sk_colorfilter_t* sk_runtimeeffect_make_color_filter(sk_runtimeeffect_t* effect, sk_data_t* uniforms, sk_flattenable_t** children, size_t childCount);
SK_C_API size_t sk_runtimeeffect_get_uniform_byte_size(const sk_runtimeeffect_t* effect);

SK_C_API size_t sk_runtimeeffect_get_uniforms_size(const sk_runtimeeffect_t* effect);
SK_C_API void sk_runtimeeffect_get_uniform_name(const sk_runtimeeffect_t* effect, int index, sk_string_t* name);
SK_C_API void sk_runtimeeffect_get_uniform_from_index(const sk_runtimeeffect_t* effect, int index, sk_runtimeeffect_uniform_t* cuniform);
SK_C_API void sk_runtimeeffect_get_uniform_from_name(const sk_runtimeeffect_t* effect, const char* name, size_t len, sk_runtimeeffect_uniform_t* cuniform);

SK_C_API size_t sk_runtimeeffect_get_children_size(const sk_runtimeeffect_t* effect);
SK_C_API void sk_runtimeeffect_get_child_name(const sk_runtimeeffect_t* effect, int index, sk_string_t* name);
SK_C_API void sk_runtimeeffect_get_child_from_index(const sk_runtimeeffect_t* effect, int index, sk_runtimeeffect_child_t* cchild);
SK_C_API void sk_runtimeeffect_get_child_from_name(const sk_runtimeeffect_t* effect, const char* name, size_t len, sk_runtimeeffect_child_t* cchild);

SK_C_PLUS_PLUS_END_GUARD

#endif
