/*
 * Copyright 2026 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/c/sk_default_fontmgr.h"

// Platform-specific font manager includes (m132: SkFontMgr::MakeDefault removed)
#if defined(__EMSCRIPTEN__)
#include "include/ports/SkFontMgr_data.h"
struct SkEmbeddedResource { const uint8_t* data; size_t size; };
struct SkEmbeddedResourceHeader { const SkEmbeddedResource* entries; int count; };
extern "C" const SkEmbeddedResourceHeader SK_EMBEDDED_FONTS;
extern sk_sp<SkFontMgr> SkFontMgr_New_Custom_Embedded(const SkEmbeddedResourceHeader*);
#elif defined(SK_BUILD_FOR_ANDROID)
#include "include/ports/SkFontMgr_android.h"
#elif defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include "include/ports/SkFontMgr_mac_ct.h"
#elif defined(SK_BUILD_FOR_WIN)
#include "include/ports/SkTypeface_win.h"
#elif defined(SK_BUILD_FOR_UNIX)
#include "include/ports/SkFontMgr_fontconfig.h"
#else
#include "include/ports/SkFontMgr_empty.h"
#endif

static sk_sp<SkFontMgr> create_platform_fontmgr() {
#if defined(__EMSCRIPTEN__)
    return SkFontMgr_New_Custom_Embedded(&SK_EMBEDDED_FONTS);
#elif defined(SK_BUILD_FOR_ANDROID)
    return SkFontMgr_New_Android(nullptr);
#elif defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    return SkFontMgr_New_CoreText(nullptr);
#elif defined(SK_BUILD_FOR_WIN)
    return SkFontMgr_New_DirectWrite();
#elif defined(SK_BUILD_FOR_UNIX)
    return SkFontMgr_New_FontConfig(nullptr);
#else
    return SkFontMgr_New_Custom_Empty();
#endif
}

sk_sp<SkFontMgr> sk_create_default_fontmgr() {
    return create_platform_fontmgr();
}

sk_sp<SkFontMgr> sk_get_default_fontmgr() {
    static sk_sp<SkFontMgr> mgr = create_platform_fontmgr();
    return mgr;
}
