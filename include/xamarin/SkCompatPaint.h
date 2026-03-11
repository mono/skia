/*
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkCompatPaint_h
#define SkCompatPaint_h

#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/utils/SkTextUtils.h"


enum class SkFilterQuality
{
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3,
};

class SkCompatPaint : public SkPaint {
public:
    SkCompatPaint();
    SkCompatPaint(const SkCompatPaint& paint);
    SkCompatPaint(const SkFont* font);
    ~SkCompatPaint();

public:
    void reset();

    SkFont* makeFont();

    SkFont* getFont();

    void setTextAlign(SkTextUtils::Align textAlign);
    SkTextUtils::Align getTextAlign() const;

    void setTextEncoding(SkTextEncoding encoding);
    SkTextEncoding getTextEncoding() const;

    void setFilterQuality(SkFilterQuality quality);
    SkFilterQuality getFilterQuality() const;

    void setLcdRenderText(bool lcdRenderText);
    bool getLcdRenderText() const;

    void setAntiAlias(bool aa);

private:
    void updateFontEdging();

private:
    SkFont fFont;
    SkTextUtils::Align fTextAlign;
    SkTextEncoding fTextEncoding;
    SkFilterQuality fFilterQuality;
    bool fLcdRenderText;

    typedef SkPaint INHERITED;
};


#endif
