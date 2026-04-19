/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkDocument.h"
#include "include/docs/SkPDFDocument.h"
#include "include/docs/SkPDFJpegHelpers.h"
#ifdef SK_BUILD_FOR_WIN
#include "include/docs/SkXPSDocument.h"
#include <XpsObjectModel.h>
#endif

#include "include/c/sk_document.h"

#include "src/c/sk_types_priv.h"

void sk_document_unref(sk_document_t* document) {
    SkSafeUnref(AsDocument(document));
}

sk_document_t* sk_document_create_pdf_from_stream(sk_wstream_t* stream) {
    SkPDF::Metadata metadata;
    metadata.jpegDecoder = SkPDF::JPEG::Decode;
    metadata.jpegEncoder = SkPDF::JPEG::Encode;
    return ToDocument(SkPDF::MakeDocument(AsWStream(stream), metadata).release());
}

sk_document_t* sk_document_create_pdf_from_stream_with_metadata(sk_wstream_t* stream, const sk_document_pdf_metadata_t* cmetadata) {
    SkPDF::Metadata metadata = AsDocumentPDFMetadata(cmetadata);
    metadata.jpegDecoder = SkPDF::JPEG::Decode;
    metadata.jpegEncoder = SkPDF::JPEG::Encode;
    return ToDocument(SkPDF::MakeDocument(AsWStream(stream), metadata).release());
}

sk_document_t* sk_document_create_xps_from_stream_with_options(sk_wstream_t* stream, const sk_document_xps_options_t* options) {
#ifdef SK_BUILD_FOR_WIN
    IXpsOMObjectFactory* factory = nullptr;
    if (!SUCCEEDED(CoCreateInstance(
            CLSID_XpsOMObjectFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        return nullptr;
    }
    SkXPS::Options opts;
    if (options) {
        opts.dpi = options->fDPI;
        opts.allowNoPngs = options->fAllowNoPngs;
    }
    return ToDocument(SkXPS::MakeDocument(AsWStream(stream), factory, opts).release());
#else
    return nullptr;
#endif
}

sk_document_t* sk_document_create_xps_from_stream(sk_wstream_t* stream, float dpi) {
    // Preserve the pre-m147 behavior: callers of the dpi-only overload get
    // allowNoPngs=true since the original API had no way to supply a png
    // encoder and upstream previously didn't enforce one.
    sk_document_xps_options_t opts = {};
    opts.fDPI = dpi;
    opts.fAllowNoPngs = true;
    return sk_document_create_xps_from_stream_with_options(stream, &opts);
}

sk_canvas_t* sk_document_begin_page(sk_document_t* document, float width, float height, const sk_rect_t* content) {
    return ToCanvas(AsDocument(document)->beginPage(width, height, AsRect(content)));
}

void sk_document_end_page(sk_document_t* document) {
    AsDocument(document)->endPage();
}

void sk_document_close(sk_document_t* document) {
    AsDocument(document)->close();
}

void sk_document_abort(sk_document_t* document) {
    AsDocument(document)->abort();
}
