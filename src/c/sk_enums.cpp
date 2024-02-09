/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/c/sk_types_priv.h"

#include "include/codec/SkEncodedImageFormat.h"
#include "include/codec/SkCodecAnimation.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkM44.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkRegion.h"
#include "include/core/SkRRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkVertices.h"
#include "include/effects/Sk1DPathEffect.h"
#include "include/effects/SkBlurMaskFilter.h"
#include "include/effects/SkHighContrastFilter.h"
#include "include/effects/SkTrimPathEffect.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"
#include "include/pathops/SkPathOps.h"
#include "include/utils/SkTextUtils.h"

#if defined(SK_GANESH)
#include "include/gpu/GpuTypes.h"
#include "include/gpu/GrTypes.h"
#include "include/gpu/GrContextOptions.h"
#endif

#if __cplusplus >= 199711L

#define ASSERT_MSG(SK, C) "ABI changed, you must write a enumeration mapper for " SK_TO_STRING(#SK) " to " SK_TO_STRING(#C) "."

// sk_font_style_slant_t
static_assert ((int)SkFontStyle::Slant::kUpright_Slant   == (int)UPRIGHT_SK_FONT_STYLE_SLANT,   ASSERT_MSG(SkFontStyle::Slant, sk_font_style_slant_t));
static_assert ((int)SkFontStyle::Slant::kItalic_Slant    == (int)ITALIC_SK_FONT_STYLE_SLANT,    ASSERT_MSG(SkFontStyle::Slant, sk_font_style_slant_t));
static_assert ((int)SkFontStyle::Slant::kOblique_Slant   == (int)OBLIQUE_SK_FONT_STYLE_SLANT,   ASSERT_MSG(SkFontStyle::Slant, sk_font_style_slant_t));

// sk_path_verb_t
static_assert ((int)SkPath::Verb::kMove_Verb    == (int)MOVE_SK_PATH_VERB,    ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kLine_Verb    == (int)LINE_SK_PATH_VERB,    ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kQuad_Verb    == (int)QUAD_SK_PATH_VERB,    ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kConic_Verb   == (int)CONIC_SK_PATH_VERB,   ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kCubic_Verb   == (int)CUBIC_SK_PATH_VERB,   ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kClose_Verb   == (int)CLOSE_SK_PATH_VERB,   ASSERT_MSG(SkPath::Verb, sk_path_verb_t));
static_assert ((int)SkPath::Verb::kDone_Verb    == (int)DONE_SK_PATH_VERB,    ASSERT_MSG(SkPath::Verb, sk_path_verb_t));

// sk_path_add_mode_t
static_assert ((int)SkPath::AddPathMode::kAppend_AddPathMode   == (int)APPEND_SK_PATH_ADD_MODE,   ASSERT_MSG(SkPath::AddPathMode, sk_path_add_mode_t));
static_assert ((int)SkPath::AddPathMode::kExtend_AddPathMode   == (int)EXTEND_SK_PATH_ADD_MODE,   ASSERT_MSG(SkPath::AddPathMode, sk_path_add_mode_t));

// sk_path_direction_t
static_assert ((int)SkPathDirection::kCCW   == (int)CCW_SK_PATH_DIRECTION,   ASSERT_MSG(SkPathDirection, sk_path_direction_t));
static_assert ((int)SkPathDirection::kCW    == (int)CW_SK_PATH_DIRECTION,    ASSERT_MSG(SkPathDirection, sk_path_direction_t));

// sk_path_arc_size_t
static_assert ((int)SkPath::ArcSize::kLarge_ArcSize   == (int)LARGE_SK_PATH_ARC_SIZE,   ASSERT_MSG(SkPath::ArcSize, sk_path_arc_size_t));
static_assert ((int)SkPath::ArcSize::kSmall_ArcSize   == (int)SMALL_SK_PATH_ARC_SIZE,   ASSERT_MSG(SkPath::ArcSize, sk_path_arc_size_t));

// sk_path_filltype_t
static_assert ((int)SkPathFillType::kWinding          == (int)WINDING_SK_PATH_FILLTYPE,           ASSERT_MSG(SkPathFillType, sk_path_filltype_t));
static_assert ((int)SkPathFillType::kEvenOdd          == (int)EVENODD_SK_PATH_FILLTYPE,           ASSERT_MSG(SkPathFillType, sk_path_filltype_t));
static_assert ((int)SkPathFillType::kInverseWinding   == (int)INVERSE_WINDING_SK_PATH_FILLTYPE,   ASSERT_MSG(SkPathFillType, sk_path_filltype_t));
static_assert ((int)SkPathFillType::kInverseEvenOdd   == (int)INVERSE_EVENODD_SK_PATH_FILLTYPE,   ASSERT_MSG(SkPathFillType, sk_path_filltype_t));

// sk_path_segment_mask_t
static_assert ((int)SkPath::SegmentMask::kLine_SegmentMask    == (int)LINE_SK_PATH_SEGMENT_MASK,    ASSERT_MSG(SkPath::SegmentMask, sk_path_segment_mask_t));
static_assert ((int)SkPath::SegmentMask::kQuad_SegmentMask    == (int)QUAD_SK_PATH_SEGMENT_MASK,    ASSERT_MSG(SkPath::SegmentMask, sk_path_segment_mask_t));
static_assert ((int)SkPath::SegmentMask::kConic_SegmentMask   == (int)CONIC_SK_PATH_SEGMENT_MASK,   ASSERT_MSG(SkPath::SegmentMask, sk_path_segment_mask_t));
static_assert ((int)SkPath::SegmentMask::kCubic_SegmentMask   == (int)CUBIC_SK_PATH_SEGMENT_MASK,   ASSERT_MSG(SkPath::SegmentMask, sk_path_segment_mask_t));

// sk_text_align_t
static_assert ((int)SkTextUtils::Align::kLeft_Align     == (int)LEFT_SK_TEXT_ALIGN,     ASSERT_MSG(SkTextUtils::Align, sk_text_align_t));
static_assert ((int)SkTextUtils::Align::kCenter_Align   == (int)CENTER_SK_TEXT_ALIGN,   ASSERT_MSG(SkTextUtils::Align, sk_text_align_t));
static_assert ((int)SkTextUtils::Align::kRight_Align    == (int)RIGHT_SK_TEXT_ALIGN,    ASSERT_MSG(SkTextUtils::Align, sk_text_align_t));

// sk_text_encoding_t
static_assert ((int)SkTextEncoding::kUTF8      == (int)UTF8_SK_TEXT_ENCODING,       ASSERT_MSG(SkTextEncoding, sk_text_encoding_t));
static_assert ((int)SkTextEncoding::kUTF16     == (int)UTF16_SK_TEXT_ENCODING,      ASSERT_MSG(SkTextEncoding, sk_text_encoding_t));
static_assert ((int)SkTextEncoding::kUTF32     == (int)UTF32_SK_TEXT_ENCODING,      ASSERT_MSG(SkTextEncoding, sk_text_encoding_t));
static_assert ((int)SkTextEncoding::kGlyphID   == (int)GLYPH_ID_SK_TEXT_ENCODING,   ASSERT_MSG(SkTextEncoding, sk_text_encoding_t));

// sk_filter_mode_t
static_assert ((int)SkFilterMode::kNearest   == (int)NEAREST_SK_FILTER_MODE,   ASSERT_MSG(SkFilterMode, sk_filter_mode_t));
static_assert ((int)SkFilterMode::kLinear    == (int)LINEAR_SK_FILTER_MODE,    ASSERT_MSG(SkFilterMode, sk_filter_mode_t));

// sk_mipmap_mode_t
static_assert ((int)SkMipmapMode::kNone      == (int)NONE_SK_MIPMAP_MODE,      ASSERT_MSG(SkMipmapMode, sk_mipmap_mode_t));
static_assert ((int)SkMipmapMode::kNearest   == (int)NEAREST_SK_MIPMAP_MODE,   ASSERT_MSG(SkMipmapMode, sk_mipmap_mode_t));
static_assert ((int)SkMipmapMode::kLinear    == (int)LINEAR_SK_MIPMAP_MODE,    ASSERT_MSG(SkMipmapMode, sk_mipmap_mode_t));

// sk_blendmode_t
static_assert ((int)SkBlendMode::kClear        == (int)CLEAR_SK_BLENDMODE,        ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSrc          == (int)SRC_SK_BLENDMODE,          ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDst          == (int)DST_SK_BLENDMODE,          ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSrcOver      == (int)SRCOVER_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDstOver      == (int)DSTOVER_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSrcIn        == (int)SRCIN_SK_BLENDMODE,        ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDstIn        == (int)DSTIN_SK_BLENDMODE,        ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSrcOut       == (int)SRCOUT_SK_BLENDMODE,       ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDstOut       == (int)DSTOUT_SK_BLENDMODE,       ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSrcATop      == (int)SRCATOP_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDstATop      == (int)DSTATOP_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kXor          == (int)XOR_SK_BLENDMODE,          ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kPlus         == (int)PLUS_SK_BLENDMODE,         ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kModulate     == (int)MODULATE_SK_BLENDMODE,     ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kScreen       == (int)SCREEN_SK_BLENDMODE,       ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kOverlay      == (int)OVERLAY_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDarken       == (int)DARKEN_SK_BLENDMODE,       ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kLighten      == (int)LIGHTEN_SK_BLENDMODE,      ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kColorDodge   == (int)COLORDODGE_SK_BLENDMODE,   ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kColorBurn    == (int)COLORBURN_SK_BLENDMODE,    ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kHardLight    == (int)HARDLIGHT_SK_BLENDMODE,    ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSoftLight    == (int)SOFTLIGHT_SK_BLENDMODE,    ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kDifference   == (int)DIFFERENCE_SK_BLENDMODE,   ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kExclusion    == (int)EXCLUSION_SK_BLENDMODE,    ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kMultiply     == (int)MULTIPLY_SK_BLENDMODE,     ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kHue          == (int)HUE_SK_BLENDMODE,          ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kSaturation   == (int)SATURATION_SK_BLENDMODE,   ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kColor        == (int)COLOR_SK_BLENDMODE,        ASSERT_MSG(SkBlendMode, sk_blendmode_t));
static_assert ((int)SkBlendMode::kLuminosity   == (int)LUMINOSITY_SK_BLENDMODE,   ASSERT_MSG(SkBlendMode, sk_blendmode_t));

// sk_colortype_t
static_assert ((int)SkColorType::kUnknown_SkColorType              == (int)UNKNOWN_SK_COLORTYPE,              ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kAlpha_8_SkColorType              == (int)ALPHA_8_SK_COLORTYPE,              ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGB_565_SkColorType              == (int)RGB_565_SK_COLORTYPE,              ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kARGB_4444_SkColorType            == (int)ARGB_4444_SK_COLORTYPE,            ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGBA_8888_SkColorType            == (int)RGBA_8888_SK_COLORTYPE,            ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kBGRA_8888_SkColorType            == (int)BGRA_8888_SK_COLORTYPE,            ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGB_888x_SkColorType             == (int)RGB_888X_SK_COLORTYPE,             ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGBA_1010102_SkColorType         == (int)RGBA_1010102_SK_COLORTYPE,         ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kBGRA_1010102_SkColorType         == (int)BGRA_1010102_SK_COLORTYPE,         ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGB_101010x_SkColorType          == (int)RGB_101010X_SK_COLORTYPE,          ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kBGR_101010x_SkColorType          == (int)BGR_101010X_SK_COLORTYPE,          ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kBGR_101010x_XR_SkColorType       == (int)BGR_101010X_XR_SK_COLORTYPE,       ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kGray_8_SkColorType               == (int)GRAY_8_SK_COLORTYPE,               ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGBA_F16Norm_SkColorType         == (int)RGBA_F16_NORM_SK_COLORTYPE,        ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGBA_F16_SkColorType             == (int)RGBA_F16_SK_COLORTYPE,             ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kRGBA_F32_SkColorType             == (int)RGBA_F32_SK_COLORTYPE,             ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kR8G8_unorm_SkColorType           == (int)R8G8_UNORM_SK_COLORTYPE,           ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kA16_unorm_SkColorType            == (int)A16_UNORM_SK_COLORTYPE,            ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kR16G16_unorm_SkColorType         == (int)R16G16_UNORM_SK_COLORTYPE,         ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kA16_float_SkColorType            == (int)A16_FLOAT_SK_COLORTYPE,            ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kR16G16_float_SkColorType         == (int)R16G16_FLOAT_SK_COLORTYPE,         ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kR16G16B16A16_unorm_SkColorType   == (int)R16G16B16A16_UNORM_SK_COLORTYPE,   ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kSRGBA_8888_SkColorType           == (int)SRGBA_8888_SK_COLORTYPE,           ASSERT_MSG(SkColorType, sk_colortype_t));
static_assert ((int)SkColorType::kR8_unorm_SkColorType             == (int)R8_UNORM_SK_COLORTYPE,             ASSERT_MSG(SkColorType, sk_colortype_t));

// sk_alphatype_t
static_assert ((int)SkAlphaType::kUnknown_SkAlphaType    == (int)UNKNOWN_SK_ALPHATYPE,    ASSERT_MSG(SkAlphaType, sk_alphatype_t));
static_assert ((int)SkAlphaType::kOpaque_SkAlphaType     == (int)OPAQUE_SK_ALPHATYPE,     ASSERT_MSG(SkAlphaType, sk_alphatype_t));
static_assert ((int)SkAlphaType::kPremul_SkAlphaType     == (int)PREMUL_SK_ALPHATYPE,     ASSERT_MSG(SkAlphaType, sk_alphatype_t));
static_assert ((int)SkAlphaType::kUnpremul_SkAlphaType   == (int)UNPREMUL_SK_ALPHATYPE,   ASSERT_MSG(SkAlphaType, sk_alphatype_t));

// sk_pixelgeometry_t
static_assert ((int)SkPixelGeometry::kUnknown_SkPixelGeometry   == (int)UNKNOWN_SK_PIXELGEOMETRY,   ASSERT_MSG(SkPixelGeometry, sk_pixelgeometry_t));
static_assert ((int)SkPixelGeometry::kRGB_H_SkPixelGeometry     == (int)RGB_H_SK_PIXELGEOMETRY,     ASSERT_MSG(SkPixelGeometry, sk_pixelgeometry_t));
static_assert ((int)SkPixelGeometry::kBGR_H_SkPixelGeometry     == (int)BGR_H_SK_PIXELGEOMETRY,     ASSERT_MSG(SkPixelGeometry, sk_pixelgeometry_t));
static_assert ((int)SkPixelGeometry::kRGB_V_SkPixelGeometry     == (int)RGB_V_SK_PIXELGEOMETRY,     ASSERT_MSG(SkPixelGeometry, sk_pixelgeometry_t));
static_assert ((int)SkPixelGeometry::kBGR_V_SkPixelGeometry     == (int)BGR_V_SK_PIXELGEOMETRY,     ASSERT_MSG(SkPixelGeometry, sk_pixelgeometry_t));

// sk_shader_tilemode_t
static_assert ((int)SkTileMode::kClamp    == (int)CLAMP_SK_SHADER_TILEMODE,    ASSERT_MSG(SkTileMode, sk_shader_tilemode_t));
static_assert ((int)SkTileMode::kRepeat   == (int)REPEAT_SK_SHADER_TILEMODE,   ASSERT_MSG(SkTileMode, sk_shader_tilemode_t));
static_assert ((int)SkTileMode::kMirror   == (int)MIRROR_SK_SHADER_TILEMODE,   ASSERT_MSG(SkTileMode, sk_shader_tilemode_t));
static_assert ((int)SkTileMode::kDecal    == (int)DECAL_SK_SHADER_TILEMODE,    ASSERT_MSG(SkTileMode, sk_shader_tilemode_t));

// sk_blurstyle_t
static_assert ((int)SkBlurStyle::kNormal_SkBlurStyle   == (int)NORMAL_SK_BLUR_STYLE,   ASSERT_MSG(SkBlurStyle, sk_blurstyle_t));
static_assert ((int)SkBlurStyle::kSolid_SkBlurStyle    == (int)SOLID_SK_BLUR_STYLE,    ASSERT_MSG(SkBlurStyle, sk_blurstyle_t));
static_assert ((int)SkBlurStyle::kOuter_SkBlurStyle    == (int)OUTER_SK_BLUR_STYLE,    ASSERT_MSG(SkBlurStyle, sk_blurstyle_t));
static_assert ((int)SkBlurStyle::kInner_SkBlurStyle    == (int)INNER_SK_BLUR_STYLE,    ASSERT_MSG(SkBlurStyle, sk_blurstyle_t));

// sk_stroke_cap_t
static_assert ((int)SkPaint::Cap::kButt_Cap     == (int)BUTT_SK_STROKE_CAP,     ASSERT_MSG(SkPaint::Cap, sk_stroke_cap_t));
static_assert ((int)SkPaint::Cap::kRound_Cap    == (int)ROUND_SK_STROKE_CAP,    ASSERT_MSG(SkPaint::Cap, sk_stroke_cap_t));
static_assert ((int)SkPaint::Cap::kSquare_Cap   == (int)SQUARE_SK_STROKE_CAP,   ASSERT_MSG(SkPaint::Cap, sk_stroke_cap_t));

// sk_stroke_join_t
static_assert ((int)SkPaint::Join::kMiter_Join   == (int)MITER_SK_STROKE_JOIN,   ASSERT_MSG(SkPaint::Join, sk_stroke_join_t));
static_assert ((int)SkPaint::Join::kRound_Join   == (int)ROUND_SK_STROKE_JOIN,   ASSERT_MSG(SkPaint::Join, sk_stroke_join_t));
static_assert ((int)SkPaint::Join::kBevel_Join   == (int)BEVEL_SK_STROKE_JOIN,   ASSERT_MSG(SkPaint::Join, sk_stroke_join_t));

// sk_region_op_t
static_assert ((int)SkRegion::Op::kDifference_Op          == (int)DIFFERENCE_SK_REGION_OP,           ASSERT_MSG(SkRegion::Op, sk_region_op_t));
static_assert ((int)SkRegion::Op::kIntersect_Op           == (int)INTERSECT_SK_REGION_OP,            ASSERT_MSG(SkRegion::Op, sk_region_op_t));
static_assert ((int)SkRegion::Op::kUnion_Op               == (int)UNION_SK_REGION_OP,                ASSERT_MSG(SkRegion::Op, sk_region_op_t));
static_assert ((int)SkRegion::Op::kXOR_Op                 == (int)XOR_SK_REGION_OP,                  ASSERT_MSG(SkRegion::Op, sk_region_op_t));
static_assert ((int)SkRegion::Op::kReverseDifference_Op   == (int)REVERSE_DIFFERENCE_SK_REGION_OP,   ASSERT_MSG(SkRegion::Op, sk_region_op_t));
static_assert ((int)SkRegion::Op::kReplace_Op             == (int)REPLACE_SK_REGION_OP,              ASSERT_MSG(SkRegion::Op, sk_region_op_t));

// sk_clipop_t
static_assert ((int)SkClipOp::kDifference   == (int)DIFFERENCE_SK_CLIPOP,           ASSERT_MSG(SkClipOp, sk_clipop_t));
static_assert ((int)SkClipOp::kIntersect    == (int)INTERSECT_SK_CLIPOP,            ASSERT_MSG(SkClipOp, sk_clipop_t));

// sk_encoded_image_format_t
static_assert ((int)SkEncodedImageFormat::kBMP       == (int)BMP_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kGIF       == (int)GIF_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kICO       == (int)ICO_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kJPEG      == (int)JPEG_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kPNG       == (int)PNG_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kWBMP      == (int)WBMP_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kWEBP      == (int)WEBP_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kPKM       == (int)PKM_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kKTX       == (int)KTX_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kASTC      == (int)ASTC_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kDNG       == (int)DNG_SK_ENCODED_FORMAT,       ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kHEIF      == (int)HEIF_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kAVIF      == (int)AVIF_SK_ENCODED_FORMAT,      ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));
static_assert ((int)SkEncodedImageFormat::kJPEGXL    == (int)JPEGXL_SK_ENCODED_FORMAT,    ASSERT_MSG(SkEncodedImageFormat, sk_encoded_image_format_t));

// SK_ENCODED_ORIGIN_t
static_assert ((int)SkEncodedOrigin::kTopLeft_SkEncodedOrigin       == (int)TOP_LEFT_SK_ENCODED_ORIGIN,         ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kTopRight_SkEncodedOrigin      == (int)TOP_RIGHT_SK_ENCODED_ORIGIN,        ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kBottomRight_SkEncodedOrigin   == (int)BOTTOM_RIGHT_SK_ENCODED_ORIGIN,     ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kBottomLeft_SkEncodedOrigin    == (int)BOTTOM_LEFT_SK_ENCODED_ORIGIN,      ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kLeftTop_SkEncodedOrigin       == (int)LEFT_TOP_SK_ENCODED_ORIGIN,         ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kRightTop_SkEncodedOrigin      == (int)RIGHT_TOP_SK_ENCODED_ORIGIN,        ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kRightBottom_SkEncodedOrigin   == (int)RIGHT_BOTTOM_SK_ENCODED_ORIGIN,     ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kLeftBottom_SkEncodedOrigin    == (int)LEFT_BOTTOM_SK_ENCODED_ORIGIN,      ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));
static_assert ((int)SkEncodedOrigin::kDefault_SkEncodedOrigin       == (int)DEFAULT_SK_ENCODED_ORIGIN,          ASSERT_MSG(SkEncodedOrigin, sk_encodedorigin_t));

// sk_codec_result_t
static_assert ((int)SkCodec::Result::kSuccess             == (int)SUCCESS_SK_CODEC_RESULT,              ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kIncompleteInput     == (int)INCOMPLETE_INPUT_SK_CODEC_RESULT,     ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kErrorInInput        == (int)ERROR_IN_INPUT_SK_CODEC_RESULT,       ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kInvalidConversion   == (int)INVALID_CONVERSION_SK_CODEC_RESULT,   ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kInvalidScale        == (int)INVALID_SCALE_SK_CODEC_RESULT,        ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kInvalidParameters   == (int)INVALID_PARAMETERS_SK_CODEC_RESULT,   ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kInvalidInput        == (int)INVALID_INPUT_SK_CODEC_RESULT,        ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kCouldNotRewind      == (int)COULD_NOT_REWIND_SK_CODEC_RESULT,     ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kInternalError       == (int)INTERNAL_ERROR_SK_CODEC_RESULT,       ASSERT_MSG(SkCodec::Result, sk_codec_result_t));
static_assert ((int)SkCodec::Result::kUnimplemented       == (int)UNIMPLEMENTED_SK_CODEC_RESULT,        ASSERT_MSG(SkCodec::Result, sk_codec_result_t));

// sk_codec_zero_initialized_t
static_assert ((int)SkCodec::ZeroInitialized::kYes_ZeroInitialized   == (int)YES_SK_CODEC_ZERO_INITIALIZED,   ASSERT_MSG(SkCodec::ZeroInitialized, sk_codec_zero_initialized_t));
static_assert ((int)SkCodec::ZeroInitialized::kNo_ZeroInitialized    == (int)NO_SK_CODEC_ZERO_INITIALIZED,    ASSERT_MSG(SkCodec::ZeroInitialized, sk_codec_zero_initialized_t));

// sk_codec_scanline_order_t
static_assert ((int)SkCodec::SkScanlineOrder::kTopDown_SkScanlineOrder    == (int)TOP_DOWN_SK_CODEC_SCANLINE_ORDER,    ASSERT_MSG(SkCodec::SkScanlineOrder, sk_codec_scanline_order_t));
static_assert ((int)SkCodec::SkScanlineOrder::kBottomUp_SkScanlineOrder   == (int)BOTTOM_UP_SK_CODEC_SCANLINE_ORDER,   ASSERT_MSG(SkCodec::SkScanlineOrder, sk_codec_scanline_order_t));

// sk_codecanimation_disposalmethod_t
static_assert ((int)SkCodecAnimation::DisposalMethod::kKeep              == (int)KEEP_SK_CODEC_ANIMATION_DISPOSAL_METHOD,               ASSERT_MSG(SkCodecAnimation::DisposalMethod, sk_codecanimation_disposalmethod_t));
static_assert ((int)SkCodecAnimation::DisposalMethod::kRestoreBGColor    == (int)RESTORE_BG_COLOR_SK_CODEC_ANIMATION_DISPOSAL_METHOD,   ASSERT_MSG(SkCodecAnimation::DisposalMethod, sk_codecanimation_disposalmethod_t));
static_assert ((int)SkCodecAnimation::DisposalMethod::kRestorePrevious   == (int)RESTORE_PREVIOUS_SK_CODEC_ANIMATION_DISPOSAL_METHOD,   ASSERT_MSG(SkCodecAnimation::DisposalMethod, sk_codecanimation_disposalmethod_t));

// sk_codecanimation_blend_t
static_assert ((int)SkCodecAnimation::Blend::kSrcOver   == (int)SRC_OVER_SK_CODEC_ANIMATION_BLEND,   ASSERT_MSG(SkCodecAnimation::Blend, sk_codecanimation_blend_t));
static_assert ((int)SkCodecAnimation::Blend::kSrc       == (int)SRC_SK_CODEC_ANIMATION_BLEND,        ASSERT_MSG(SkCodecAnimation::Blend, sk_codecanimation_blend_t));

// sk_path_effect_1d_style_t
static_assert ((int)SkPath1DPathEffect::Style::kTranslate_Style   == (int)TRANSLATE_SK_PATH_EFFECT_1D_STYLE,   ASSERT_MSG(SkPath1DPathEffect::Style, sk_path_effect_1d_style_t));
static_assert ((int)SkPath1DPathEffect::Style::kRotate_Style      == (int)ROTATE_SK_PATH_EFFECT_1D_STYLE,      ASSERT_MSG(SkPath1DPathEffect::Style, sk_path_effect_1d_style_t));
static_assert ((int)SkPath1DPathEffect::Style::kMorph_Style       == (int)MORPH_SK_PATH_EFFECT_1D_STYLE,       ASSERT_MSG(SkPath1DPathEffect::Style, sk_path_effect_1d_style_t));

// sk_path_effect_trim_mode_t
static_assert ((int)SkTrimPathEffect::Mode::kNormal     == (int)NORMAL_SK_PATH_EFFECT_TRIM_MODE,     ASSERT_MSG(SkTrimPathEffect::Mode, sk_path_effect_trim_mode_t));
static_assert ((int)SkTrimPathEffect::Mode::kInverted   == (int)INVERTED_SK_PATH_EFFECT_TRIM_MODE,   ASSERT_MSG(SkTrimPathEffect::Mode, sk_path_effect_trim_mode_t));

// sk_paint_style_t
static_assert ((int)SkPaint::Style::kFill_Style            == (int)FILL_SK_PAINT_STYLE,              ASSERT_MSG(SkPaint::Style, sk_paint_style_t));
static_assert ((int)SkPaint::Style::kStrokeAndFill_Style   == (int)STROKE_AND_FILL_SK_PAINT_STYLE,   ASSERT_MSG(SkPaint::Style, sk_paint_style_t));
static_assert ((int)SkPaint::Style::kStroke_Style          == (int)STROKE_SK_PAINT_STYLE,            ASSERT_MSG(SkPaint::Style, sk_paint_style_t));

// sk_font_hinting_t
static_assert ((int)SkFontHinting::kNone     == (int)NONE_SK_FONT_HINTING,     ASSERT_MSG(SkFontHinting, sk_font_hinting_t));
static_assert ((int)SkFontHinting::kSlight   == (int)SLIGHT_SK_FONT_HINTING,   ASSERT_MSG(SkFontHinting, sk_font_hinting_t));
static_assert ((int)SkFontHinting::kNormal   == (int)NORMAL_SK_FONT_HINTING,   ASSERT_MSG(SkFontHinting, sk_font_hinting_t));
static_assert ((int)SkFontHinting::kFull     == (int)FULL_SK_FONT_HINTING,     ASSERT_MSG(SkFontHinting, sk_font_hinting_t));

// sk_point_mode_t
static_assert ((int)SkCanvas::PointMode::kPoints_PointMode    == (int)POINTS_SK_POINT_MODE,    ASSERT_MSG(SkCanvas::PointMode, sk_point_mode_t));
static_assert ((int)SkCanvas::PointMode::kLines_PointMode     == (int)LINES_SK_POINT_MODE,     ASSERT_MSG(SkCanvas::PointMode, sk_point_mode_t));
static_assert ((int)SkCanvas::PointMode::kPolygon_PointMode   == (int)POLYGON_SK_POINT_MODE,   ASSERT_MSG(SkCanvas::PointMode, sk_point_mode_t));

// sk_surfaceprops_flags_t
static_assert ((int)SkSurfaceProps::Flags::kUseDeviceIndependentFonts_Flag   == (int)USE_DEVICE_INDEPENDENT_FONTS_SK_SURFACE_PROPS_FLAGS,   ASSERT_MSG(SkSurfaceProps::Flags, sk_surfaceprops_flags_t));

// sk_pathop_t
static_assert ((int)SkPathOp::kDifference_SkPathOp          == (int)DIFFERENCE_SK_PATHOP,           ASSERT_MSG(SkPathOp, sk_pathop_t));
static_assert ((int)SkPathOp::kIntersect_SkPathOp           == (int)INTERSECT_SK_PATHOP,            ASSERT_MSG(SkPathOp, sk_pathop_t));
static_assert ((int)SkPathOp::kUnion_SkPathOp               == (int)UNION_SK_PATHOP,                ASSERT_MSG(SkPathOp, sk_pathop_t));
static_assert ((int)SkPathOp::kXOR_SkPathOp                 == (int)XOR_SK_PATHOP,                  ASSERT_MSG(SkPathOp, sk_pathop_t));
static_assert ((int)SkPathOp::kReverseDifference_SkPathOp   == (int)REVERSE_DIFFERENCE_SK_PATHOP,   ASSERT_MSG(SkPathOp, sk_pathop_t));

// sk_lattice_flags_t
static_assert ((int)SkCanvas::Lattice::RectType::kDefault       == (int)DEFAULT_SK_LATTICE_RECT_TYPE,       ASSERT_MSG(SkCanvas::Lattice::Flags, sk_lattice_recttype_t));
static_assert ((int)SkCanvas::Lattice::RectType::kTransparent   == (int)TRANSPARENT_SK_LATTICE_RECT_TYPE,   ASSERT_MSG(SkCanvas::Lattice::Flags, sk_lattice_recttype_t));
static_assert ((int)SkCanvas::Lattice::RectType::kFixedColor    == (int)FIXED_COLOR_SK_LATTICE_RECT_TYPE,   ASSERT_MSG(SkCanvas::Lattice::Flags, sk_lattice_recttype_t));

// sk_pathmeasure_matrixflags_t
static_assert ((int)SkPathMeasure::MatrixFlags::kGetPosition_MatrixFlag    == (int)GET_POSITION_SK_PATHMEASURE_MATRIXFLAGS,      ASSERT_MSG(SkPathMeasure::MatrixFlags, sk_pathmeasure_matrixflags_t));
static_assert ((int)SkPathMeasure::MatrixFlags::kGetTangent_MatrixFlag     == (int)GET_TANGENT_SK_PATHMEASURE_MATRIXFLAGS,       ASSERT_MSG(SkPathMeasure::MatrixFlags, sk_pathmeasure_matrixflags_t));
static_assert ((int)SkPathMeasure::MatrixFlags::kGetPosAndTan_MatrixFlag   == (int)GET_POS_AND_TAN_SK_PATHMEASURE_MATRIXFLAGS,   ASSERT_MSG(SkPathMeasure::MatrixFlags, sk_pathmeasure_matrixflags_t));

// sk_vertices_vertex_mode_t
static_assert ((int)SkVertices::VertexMode::kTriangles_VertexMode       == (int)TRIANGLES_SK_VERTICES_VERTEX_MODE,        ASSERT_MSG(SkVertices::VertexMode, sk_vertices_vertex_mode_t));
static_assert ((int)SkVertices::VertexMode::kTriangleStrip_VertexMode   == (int)TRIANGLE_STRIP_SK_VERTICES_VERTEX_MODE,   ASSERT_MSG(SkVertices::VertexMode, sk_vertices_vertex_mode_t));
static_assert ((int)SkVertices::VertexMode::kTriangleFan_VertexMode     == (int)TRIANGLE_FAN_SK_VERTICES_VERTEX_MODE,     ASSERT_MSG(SkVertices::VertexMode, sk_vertices_vertex_mode_t));

// sk_image_caching_hint_t
static_assert ((int)SkImage::CachingHint::kAllow_CachingHint      == (int)ALLOW_SK_IMAGE_CACHING_HINT,      ASSERT_MSG(SkImage::CachingHint, sk_image_caching_hint_t));
static_assert ((int)SkImage::CachingHint::kDisallow_CachingHint   == (int)DISALLOW_SK_IMAGE_CACHING_HINT,   ASSERT_MSG(SkImage::CachingHint, sk_image_caching_hint_t));

// sk_highcontrastconfig_invertstyle_t
static_assert ((int)SkHighContrastConfig::InvertStyle::kNoInvert           == (int)NO_INVERT_SK_HIGH_CONTRAST_CONFIG_INVERT_STYLE,           ASSERT_MSG(SkHighContrastConfig::InvertStyle, sk_highcontrastconfig_invertstyle_t));
static_assert ((int)SkHighContrastConfig::InvertStyle::kInvertBrightness   == (int)INVERT_BRIGHTNESS_SK_HIGH_CONTRAST_CONFIG_INVERT_STYLE,   ASSERT_MSG(SkHighContrastConfig::InvertStyle, sk_highcontrastconfig_invertstyle_t));
static_assert ((int)SkHighContrastConfig::InvertStyle::kInvertLightness    == (int)INVERT_LIGHTNESS_SK_HIGH_CONTRAST_CONFIG_INVERT_STYLE,    ASSERT_MSG(SkHighContrastConfig::InvertStyle, sk_highcontrastconfig_invertstyle_t));

// sk_bitmap_allocflags_t
static_assert ((int)SkBitmap::AllocFlags::kZeroPixels_AllocFlag   == (int)ZERO_PIXELS_SK_BITMAP_ALLOC_FLAGS,   ASSERT_MSG(SkBitmap::AllocFlags, sk_bitmap_allocflags_t));

// sk_pngencoder_filterflags_t
static_assert ((int)SkPngEncoder::FilterFlag::kZero    == (int)ZERO_SK_PNGENCODER_FILTER_FLAGS,    ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kNone    == (int)NONE_SK_PNGENCODER_FILTER_FLAGS,    ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kSub     == (int)SUB_SK_PNGENCODER_FILTER_FLAGS,     ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kUp      == (int)UP_SK_PNGENCODER_FILTER_FLAGS,      ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kAvg     == (int)AVG_SK_PNGENCODER_FILTER_FLAGS,     ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kPaeth   == (int)PAETH_SK_PNGENCODER_FILTER_FLAGS,   ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));
static_assert ((int)SkPngEncoder::FilterFlag::kAll     == (int)ALL_SK_PNGENCODER_FILTER_FLAGS,     ASSERT_MSG(SkPngEncoder::FilterFlag, sk_pngencoder_filterflags_t));

// sk_jpegencoder_downsample_t
static_assert ((int)SkJpegEncoder::Downsample::k420   == (int)DOWNSAMPLE_420_SK_JPEGENCODER_DOWNSAMPLE,   ASSERT_MSG(SkJpegEncoder::Downsample, sk_jpegencoder_downsample_t));
static_assert ((int)SkJpegEncoder::Downsample::k422   == (int)DOWNSAMPLE_422_SK_JPEGENCODER_DOWNSAMPLE,   ASSERT_MSG(SkJpegEncoder::Downsample, sk_jpegencoder_downsample_t));
static_assert ((int)SkJpegEncoder::Downsample::k444   == (int)DOWNSAMPLE_444_SK_JPEGENCODER_DOWNSAMPLE,   ASSERT_MSG(SkJpegEncoder::Downsample, sk_jpegencoder_downsample_t));

// sk_jpegencoder_alphaoption_t
static_assert ((int)SkJpegEncoder::AlphaOption::kIgnore         == (int)IGNORE_SK_JPEGENCODER_ALPHA_OPTION,           ASSERT_MSG(SkJpegEncoder::AlphaOption, sk_jpegencoder_alphaoption_t));
static_assert ((int)SkJpegEncoder::AlphaOption::kBlendOnBlack   == (int)BLEND_ON_BLACK_SK_JPEGENCODER_ALPHA_OPTION,   ASSERT_MSG(SkJpegEncoder::AlphaOption, sk_jpegencoder_alphaoption_t));

// sk_webpencoder_compression_t
static_assert ((int)SkWebpEncoder::Compression::kLossy      == (int)LOSSY_SK_WEBPENCODER_COMPTRESSION,      ASSERT_MSG(SkWebpEncoder::Compression, sk_webpencoder_compression_t));
static_assert ((int)SkWebpEncoder::Compression::kLossless   == (int)LOSSLESS_SK_WEBPENCODER_COMPTRESSION,   ASSERT_MSG(SkWebpEncoder::Compression, sk_webpencoder_compression_t));

// sk_rrect_type_t
static_assert ((int)SkRRect::Type::kEmpty_Type       == (int)EMPTY_SK_RRECT_TYPE,        ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));
static_assert ((int)SkRRect::Type::kRect_Type        == (int)RECT_SK_RRECT_TYPE,         ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));
static_assert ((int)SkRRect::Type::kOval_Type        == (int)OVAL_SK_RRECT_TYPE,         ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));
static_assert ((int)SkRRect::Type::kSimple_Type      == (int)SIMPLE_SK_RRECT_TYPE,       ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));
static_assert ((int)SkRRect::Type::kNinePatch_Type   == (int)NINE_PATCH_SK_RRECT_TYPE,   ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));
static_assert ((int)SkRRect::Type::kComplex_Type     == (int)COMPLEX_SK_RRECT_TYPE,      ASSERT_MSG(SkRRect::Type, sk_rrect_type_t));

// sk_rrect_corner_t
static_assert ((int)SkRRect::Corner::kUpperLeft_Corner    == (int)UPPER_LEFT_SK_RRECT_CORNER,    ASSERT_MSG(SkRRect::Corner, sk_rrect_corner_t));
static_assert ((int)SkRRect::Corner::kUpperRight_Corner   == (int)UPPER_RIGHT_SK_RRECT_CORNER,   ASSERT_MSG(SkRRect::Corner, sk_rrect_corner_t));
static_assert ((int)SkRRect::Corner::kLowerRight_Corner   == (int)LOWER_RIGHT_SK_RRECT_CORNER,   ASSERT_MSG(SkRRect::Corner, sk_rrect_corner_t));
static_assert ((int)SkRRect::Corner::kLowerLeft_Corner    == (int)LOWER_LEFT_SK_RRECT_CORNER,    ASSERT_MSG(SkRRect::Corner, sk_rrect_corner_t));

// skottie_animation_renderflags_t
static_assert ((int)skottie::Animation::kSkipTopLevelIsolation      == (int)SKIP_TOP_LEVEL_ISOLATION,      ASSERT_MSG(skottie::Animation, skottie_animation_renderflags_t));
static_assert ((int)skottie::Animation::kDisableTopLevelClipping    == (int)DISABLE_TOP_LEVEL_CLIPPING,    ASSERT_MSG(skottie::Animation, skottie_animation_renderflags_t));

// skottie_animation_renderflags_t
static_assert ((int)skottie::Animation::Builder::Flags::kDeferImageLoading      == (int)DEFER_IMAGE_LOADING_SKOTTIE_ANIMATION_BUILDER_FLAGS,      ASSERT_MSG(skottie::Animation::Builder::Flags, skottie_animation_builder_flags_t));
static_assert ((int)skottie::Animation::Builder::Flags::kPreferEmbeddedFonts    == (int)PREFER_EMBEDDED_FONTS_SKOTTIE_ANIMATION_BUILDER_FLAGS,    ASSERT_MSG(skottie::Animation::Builder::Flags, skottie_animation_builder_flags_t));

// sk_runtimeeffect_uniform_type_t
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat      == (int)FLOAT_SK_RUNTIMEEFFECT_UNIFORM_TYPE,      ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat2     == (int)FLOAT2_SK_RUNTIMEEFFECT_UNIFORM_TYPE,     ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat3     == (int)FLOAT3_SK_RUNTIMEEFFECT_UNIFORM_TYPE,     ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat4     == (int)FLOAT4_SK_RUNTIMEEFFECT_UNIFORM_TYPE,     ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat2x2   == (int)FLOAT2X2_SK_RUNTIMEEFFECT_UNIFORM_TYPE,   ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat3x3   == (int)FLOAT3X3_SK_RUNTIMEEFFECT_UNIFORM_TYPE,   ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kFloat4x4   == (int)FLOAT4X4_SK_RUNTIMEEFFECT_UNIFORM_TYPE,   ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kInt        == (int)INT_SK_RUNTIMEEFFECT_UNIFORM_TYPE,        ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kInt2       == (int)INT2_SK_RUNTIMEEFFECT_UNIFORM_TYPE,       ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kInt3       == (int)INT3_SK_RUNTIMEEFFECT_UNIFORM_TYPE,       ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));
static_assert ((int)SkRuntimeEffect::Uniform::Type::kInt4       == (int)INT4_SK_RUNTIMEEFFECT_UNIFORM_TYPE,       ASSERT_MSG(SkRuntimeEffect::Uniform::Type, sk_runtimeeffect_uniform_type_t));

// sk_runtimeeffect_child_type_t
static_assert ((int)SkRuntimeEffect::ChildType::kShader        == (int)SHADER_SK_RUNTIMEEFFECT_CHILD_TYPE,         ASSERT_MSG(SkRuntimeEffect::ChildType, sk_runtimeeffect_child_type_t));
static_assert ((int)SkRuntimeEffect::ChildType::kColorFilter   == (int)COLOR_FILTER_SK_RUNTIMEEFFECT_CHILD_TYPE,   ASSERT_MSG(SkRuntimeEffect::ChildType, sk_runtimeeffect_child_type_t));
static_assert ((int)SkRuntimeEffect::ChildType::kBlender       == (int)BLENDER_SK_RUNTIMEEFFECT_CHILD_TYPE,        ASSERT_MSG(SkRuntimeEffect::ChildType, sk_runtimeeffect_child_type_t));

// sk_runtimeeffect_uniform_flags_t
static_assert ((int)0                                                      == (int)NONE_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,             ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));
static_assert ((int)SkRuntimeEffect::Uniform::Flags::kArray_Flag           == (int)ARRAY_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,            ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));
static_assert ((int)SkRuntimeEffect::Uniform::Flags::kColor_Flag           == (int)COLOR_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,            ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));
static_assert ((int)SkRuntimeEffect::Uniform::Flags::kVertex_Flag          == (int)VERTEX_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,           ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));
static_assert ((int)SkRuntimeEffect::Uniform::Flags::kFragment_Flag        == (int)FRAGMENT_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,         ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));
static_assert ((int)SkRuntimeEffect::Uniform::Flags::kHalfPrecision_Flag   == (int)HALF_PRECISION_SK_RUNTIMEEFFECT_UNIFORM_FLAGS,   ASSERT_MSG(SkRuntimeEffect::Uniform::Flags, sk_runtimeeffect_uniform_flags_t));

#if defined(SK_GANESH)

// gr_surfaceorigin_t
static_assert ((int)GrSurfaceOrigin::kTopLeft_GrSurfaceOrigin      == (int)TOP_LEFT_GR_SURFACE_ORIGIN,      ASSERT_MSG(GrSurfaceOrigin, gr_surfaceorigin_t));
static_assert ((int)GrSurfaceOrigin::kBottomLeft_GrSurfaceOrigin   == (int)BOTTOM_LEFT_GR_SURFACE_ORIGIN,   ASSERT_MSG(GrSurfaceOrigin, gr_surfaceorigin_t));

// gr_backend_t
static_assert ((int)GrBackendApi::kMetal      == (int)METAL_GR_BACKEND,      ASSERT_MSG(GrBackendApi, gr_backend_t));
static_assert ((int)GrBackendApi::kDawn       == (int)DAWN_GR_BACKEND,       ASSERT_MSG(GrBackendApi, gr_backend_t));
static_assert ((int)GrBackendApi::kOpenGL     == (int)OPENGL_GR_BACKEND,     ASSERT_MSG(GrBackendApi, gr_backend_t));
static_assert ((int)GrBackendApi::kVulkan     == (int)VULKAN_GR_BACKEND,     ASSERT_MSG(GrBackendApi, gr_backend_t));
static_assert ((int)GrBackendApi::kDirect3D   == (int)DIRECT3D_GR_BACKEND,   ASSERT_MSG(GrBackendApi, gr_backend_t));

#endif

#endif
