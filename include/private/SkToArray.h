/*
 * Copyright 2026 Microsoft Corporation.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkToArray_DEFINED
#define SkToArray_DEFINED

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace skia_private {

template <typename T, size_t N, size_t... Is>
constexpr std::array<std::remove_cv_t<T>, N> to_array(
        T (&&array)[N], std::index_sequence<Is...>) {
    return {{std::move(array[Is])...}};
}

}  // namespace skia_private

template <typename T, size_t N>
constexpr std::array<std::remove_cv_t<T>, N> SkToArray(T (&&array)[N]) {
    // Tizen's GCC 9.2 standard library does not provide std::to_array.
    return skia_private::to_array(std::move(array), std::make_index_sequence<N>{});
}

#endif  // SkToArray_DEFINED
