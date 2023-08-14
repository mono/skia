/*
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/utils/SkTextUtils.h"
#include "include/xamarin/SkCompatPaint.h"

SkCompatPaint::SkCompatPaint()
    : fFont(SkFont())
    , fTextAlign(SkTextUtils::Align::kLeft_Align)
    , fTextEncoding(SkTextEncoding::kUTF8)
    , fFilterQuality(SkFilterQuality::None)
    , fLcdRenderText(false)
{
    fFont.setLinearMetrics(true);
    fFont.setEdging(SkFont::Edging::kAlias);
}

SkCompatPaint::SkCompatPaint(const SkCompatPaint& paint) = default;

SkCompatPaint::SkCompatPaint(const SkFont* font)
    : fFont(*font)
    , fTextAlign(SkTextUtils::Align::kLeft_Align)
    , fTextEncoding(SkTextEncoding::kUTF8)
    , fFilterQuality(SkFilterQuality::None)
    , fLcdRenderText(font->getEdging() == SkFont::Edging::kSubpixelAntiAlias)
{
    updateFontEdging();
}

SkCompatPaint::~SkCompatPaint() = default;

void SkCompatPaint::reset() {
    *this = SkCompatPaint();
}

SkFont* SkCompatPaint::makeFont() {
    return new SkFont(fFont);
}

SkFont* SkCompatPaint::getFont() {
    return &fFont;
}

void SkCompatPaint::setTextAlign(SkTextUtils::Align textAlign) {
    fTextAlign = textAlign;
}

SkTextUtils::Align SkCompatPaint::getTextAlign() const {
    return fTextAlign;
}

void SkCompatPaint::setTextEncoding(SkTextEncoding encoding) {
    fTextEncoding = encoding;
}

SkTextEncoding SkCompatPaint::getTextEncoding() const {
    return fTextEncoding;
}

void SkCompatPaint::setFilterQuality(SkFilterQuality quality) {
    fFilterQuality = quality;
}

SkFilterQuality SkCompatPaint::getFilterQuality() const {
    return fFilterQuality;
}

void SkCompatPaint::setLcdRenderText(bool lcdRenderText) {
    fLcdRenderText = lcdRenderText;
    updateFontEdging();
}

bool SkCompatPaint::getLcdRenderText() const {
    return fLcdRenderText;
}

void SkCompatPaint::setAntiAlias(bool aa) {
    SkPaint::setAntiAlias(aa);
    updateFontEdging();
}

void SkCompatPaint::updateFontEdging() {
    SkFont::Edging edging = SkFont::Edging::kAlias;
    if (isAntiAlias()) {
        edging = fLcdRenderText
            ? SkFont::Edging::kSubpixelAntiAlias
            : SkFont::Edging::kAntiAlias;
    }
    fFont.setEdging(edging);
}