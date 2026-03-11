/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_default_fontmgr_DEFINED
#define sk_default_fontmgr_DEFINED

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

// Creates a new platform-appropriate font manager instance.
sk_sp<SkFontMgr> sk_create_default_fontmgr();

// Returns a cached, thread-safe, platform-appropriate default font manager.
sk_sp<SkFontMgr> sk_get_default_fontmgr();

// Returns a default typeface from the cached default font manager.
inline sk_sp<SkTypeface> sk_get_default_typeface() {
    return sk_get_default_fontmgr()->legacyMakeTypeface("", SkFontStyle::Normal());
}

#endif // sk_default_fontmgr_DEFINED
