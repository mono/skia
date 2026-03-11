/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBlender.h"
#include "include/effects/SkBlenders.h"

#include "include/c/sk_blender.h"

#include "src/c/sk_types_priv.h"

void sk_blender_ref(sk_blender_t* blender) {
    SkSafeRef(AsBlender(blender));
}

void sk_blender_unref(sk_blender_t* blender) {
    SkSafeUnref(AsBlender(blender));
}

sk_blender_t* sk_blender_new_mode(sk_blendmode_t mode) {
    return ToBlender(SkBlender::Mode((SkBlendMode)mode).release());
}

sk_blender_t* sk_blender_new_arithmetic(float k1, float k2, float k3, float k4, bool enforcePremul) {
    return ToBlender(SkBlenders::Arithmetic(k1, k2, k3, k4, enforcePremul).release());
}
