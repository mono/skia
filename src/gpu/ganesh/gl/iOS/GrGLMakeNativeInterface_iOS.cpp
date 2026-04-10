/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkTypes.h"
#ifdef SK_BUILD_FOR_IOS

#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/ios/GrGLMakeIOSInterface.h"
#include "include/private/base/SkTemplates.h"

#include <dlfcn.h>

static GrGLFuncPtr ios_get_gl_proc(void* ctx, const char name[]) {
    return (GrGLFuncPtr) dlsym(RTLD_DEFAULT, name);
}

namespace GrGLInterfaces {
sk_sp<const GrGLInterface> MakeIOS() {
    return GrGLMakeAssembledGLESInterface(nullptr, ios_get_gl_proc);
}

}  // namespace GrGLInterfaces

#if !defined(SK_DISABLE_LEGACY_GL_MAKE_NATIVE_INTERFACE)
sk_sp<const GrGLInterface> GrGLMakeNativeInterface() {
    return GrGLInterfaces::MakeIOS();
}
#endif

#endif  // SK_BUILD_FOR_IOS
