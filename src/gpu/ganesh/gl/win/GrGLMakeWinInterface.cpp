/*
 * Copyright 2024 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkTypes.h"
#include "src/base/SkLeanWindows.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "src/gpu/ganesh/gl/GrGLUtil.h"

#include <memory>
#include <type_traits>

using wglGetCurrentContextProc = HGLRC (WINAPI *)();
using wglGetProcAddressProc = PROC (WINAPI *)(LPCSTR);

namespace GrGLInterfaces {
sk_sp<const GrGLInterface> MakeWin() {
    struct FreeModule { void operator()(HMODULE m) { (void)FreeLibrary(m); } };
    std::unique_ptr<typename std::remove_pointer<HMODULE>::type, FreeModule> module(
            LoadLibraryExA("opengl32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!module) {
        return nullptr;
    }
    static wglGetCurrentContextProc wglGetCurrentContext_ =
            (wglGetCurrentContextProc)GetProcAddress(module.get(), "wglGetCurrentContext");
    static wglGetProcAddressProc wglGetProcAddress_ =
            (wglGetProcAddressProc)GetProcAddress(module.get(), "wglGetProcAddress");
    if (!wglGetCurrentContext_ || !wglGetProcAddress_) {
        return nullptr;
    }
    if (nullptr == wglGetCurrentContext_()) {
        return nullptr;
    }
    const GrGLGetProc win_get_gl_proc = [](void* ctx, const char* name) {
        SkASSERT(wglGetCurrentContext_());
        if (GrGLFuncPtr p = (GrGLFuncPtr)GetProcAddress((HMODULE)ctx, name)) {
            return p;
        }
        if (GrGLFuncPtr p = (GrGLFuncPtr)wglGetProcAddress_(name)) {
            return p;
        }
        return (GrGLFuncPtr)nullptr;
    };

    GrGLGetStringFn* getString =
        (GrGLGetStringFn*)win_get_gl_proc((void*)module.get(), "glGetString");
    if (!getString) {
        return nullptr;
    }
    const char* verStr = reinterpret_cast<const char*>(getString(GR_GL_VERSION));
    GrGLStandard standard = GrGLGetStandardInUseFromString(verStr);

    if (GR_IS_GR_GL_ES(standard)) {
        return GrGLMakeAssembledGLESInterface((void*)module.get(), win_get_gl_proc);
    } else if (GR_IS_GR_GL(standard)) {
        return GrGLMakeAssembledGLInterface((void*)module.get(), win_get_gl_proc);
    }
    return nullptr;
}
}  // namespace GrGLInterfaces
