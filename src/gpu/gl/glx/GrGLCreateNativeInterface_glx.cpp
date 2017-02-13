/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "gl/GrGLInterface.h"
#include "gl/GrGLAssembleInterface.h"
#include "gl/GrGLUtil.h"
#include <dlfcn.h>
#include <GL/glx.h>

typedef void* (*GetCurrentContextFuncPtr)();
typedef GrGLFuncPtr (*GetGLProc)(const char name[]);
static void* libGLHandle = nullptr;
static GetGLProc pglXGetProcAddress = nullptr;
static GetCurrentContextFuncPtr pglXGetCurrentContext = nullptr;

static GrGLFuncPtr glx_get(void* ctx, const char name[]) {
    // Avoid calling glXGetProcAddress() for EGL procs.
    // We don't expect it to ever succeed, but somtimes it returns non-null anyway.
    if (0 == strncmp(name, "egl", 3)) {
        return nullptr;
    }

    SkASSERT(nullptr == ctx);
    SkASSERT(pglXGetCurrentContext());
    return pglXGetProcAddress(name);
}

const GrGLInterface* GrGLCreateNativeInterface() {
    if(libGLHandle == nullptr)
    {
        libGLHandle = dlopen("libGL.so.1", RTLD_LAZY);
        if(libGLHandle != nullptr)
            return nullptr;
        pglXGetProcAddress = (GetGLProc)dlsym(libGLHandle, "glXGetProcAddress");
        pglXGetCurrentContext = (GetCurrentContextFuncPtr)dlsym(libGLHandle, "glXGetCurrentContext");
    }
    if(pglXGetProcAddress == nullptr || pglXGetCurrentContext == nullptr)
        return nullptr;
    if (nullptr == pglXGetCurrentContext()) {
        return nullptr;
    }

    return GrGLAssembleInterface(nullptr, glx_get);
}
