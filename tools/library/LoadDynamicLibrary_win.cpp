/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_WIN)

#include "src/core/SkLeanWindows.h"
#include "tools/library/LoadDynamicLibrary.h"

#include <memory>

void* SkLoadDynamicLibrary(const char* libraryName) {
#ifdef SK_BUILD_FOR_WINRT
    int str_len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, libraryName, -1,
                                      nullptr, 0);
    if (str_len == 0) {
        return nullptr;
    }
    auto wideLibraryName = std::make_unique<wchar_t[]>(str_len);
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, libraryName, -1,
                             wideLibraryName.get(), str_len) != str_len) {
        return nullptr;
    }

    return LoadPackagedLibrary(wideLibraryName.get(), 0);
#else // SK_BUILD_FOR_WINRT
    return LoadLibraryA(libraryName);
#endif // SK_BUILD_FOR_WINRT
}

void* SkGetProcedureAddress(void* library, const char* functionName) {
    return reinterpret_cast<void*>(::GetProcAddress((HMODULE)library, functionName));
}

bool SkFreeDynamicLibrary(void* library) {
    return FreeLibrary((HMODULE)library);
}

#endif//defined(SK_BUILD_FOR_WIN)
