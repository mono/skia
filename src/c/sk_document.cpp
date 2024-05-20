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
#include "include/docs/SkXPSDocument.h"

#include "include/c/sk_document.h"

#include "src/c/sk_types_priv.h"

// SkDocument

void sk_document_unref(sk_document_t* document) {
    SkSafeUnref(AsDocument(document));
}

sk_document_t* sk_document_create_pdf_from_stream(sk_wstream_t* stream) {
    return ToDocument(SkPDF::MakeDocument(AsWStream(stream)).release());
}

sk_document_t* sk_document_create_pdf_from_stream_with_metadata(sk_wstream_t* stream, const sk_pdf_metadata_t* cmetadata) {
    return ToDocument(SkPDF::MakeDocument(AsWStream(stream), *AsPDFMetadata(cmetadata)).release());
}

sk_document_t* sk_document_create_xps_from_stream(sk_wstream_t* stream, float dpi) {
    return ToDocument(SkXPS::MakeDocument(AsWStream(stream), dpi).release());
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

// SkPDF::Metadata

sk_pdf_metadata_t* sk_pdf_metadata_new(void) {
    return ToPDFMetadata(new SkPDF::Metadata());
}

void sk_pdf_metadata_delete(sk_pdf_metadata_t* metadata) {
    delete AsPDFMetadata(metadata);
}

void sk_pdf_metadata_set_title(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fTitle = AsOptionalString(value);
}

void sk_pdf_metadata_set_author(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fAuthor = AsOptionalString(value);
}

void sk_pdf_metadata_set_subject(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fSubject = AsOptionalString(value);
}

void sk_pdf_metadata_set_keywords(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fKeywords = AsOptionalString(value);
}

void sk_pdf_metadata_set_creator(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fCreator = AsOptionalString(value);
}

void sk_pdf_metadata_set_producer(sk_pdf_metadata_t* metadata, sk_string_t* value) {
    AsPDFMetadata(metadata)->fProducer = AsOptionalString(value);
}

void sk_pdf_metadata_set_creation(sk_pdf_metadata_t* metadata, sk_time_datetime_t* value) {
    AsPDFMetadata(metadata)->fCreation = AsOptionalTimestamp(value);
}

void sk_pdf_metadata_set_modified(sk_pdf_metadata_t* metadata, sk_time_datetime_t* value) {
    AsPDFMetadata(metadata)->fModified = AsOptionalTimestamp(value);
}

void sk_pdf_metadata_set_raster_dpi(sk_pdf_metadata_t* metadata, float value) {
    AsPDFMetadata(metadata)->fRasterDPI = value;
}

void sk_pdf_metadata_set_pdfa(sk_pdf_metadata_t* metadata, bool value) {
    AsPDFMetadata(metadata)->fPDFA = value;
}

void sk_pdf_metadata_set_encoding_quality(sk_pdf_metadata_t* metadata, int value) {
    AsPDFMetadata(metadata)->fEncodingQuality = value;
}

void sk_pdf_metadata_set_structure_element_tree_root(sk_pdf_metadata_t* metadata, sk_pdf_structure_element_t* value) {
    AsPDFMetadata(metadata)->fStructureElementTreeRoot = AsPDFStructureElementNode(value);
}

void sk_pdf_metadata_set_compression_level(sk_pdf_metadata_t* metadata, sk_pdf_compression_t value) {
    AsPDFMetadata(metadata)->fCompressionLevel = (SkPDF::Metadata::CompressionLevel)value;
}

// SkPDF::StructureElementNode

sk_pdf_structure_element_t* sk_pdf_structure_element_new(void) {
    return ToPDFStructureElementNode(new SkPDF::StructureElementNode());
}

void sk_pdf_structure_element_delete(sk_pdf_structure_element_t* element) {
    delete AsPDFStructureElementNode(element);
}

void sk_pdf_structure_element_set_type_string(sk_pdf_structure_element_t* element, sk_string_t* value) {
    AsPDFStructureElementNode(element)->fTypeString = AsOptionalString(value);
}

void sk_pdf_structure_element_add_child(sk_pdf_structure_element_t* element, sk_pdf_structure_element_t* value) {
    std::unique_ptr<SkPDF::StructureElementNode> v(AsPDFStructureElementNode(value));
    AsPDFStructureElementNode(element)->fChildVector.push_back(std::move(v));
}

void sk_pdf_structure_element_set_node_id(sk_pdf_structure_element_t* element, int value) {
    AsPDFStructureElementNode(element)->fNodeId = value;
}

void sk_pdf_structure_element_add_additional_node_id(sk_pdf_structure_element_t* element, int value) {
    AsPDFStructureElementNode(element)->fAdditionalNodeIds.push_back(value);
}

void sk_pdf_structure_element_add_additional_node_ids(sk_pdf_structure_element_t* element, int* value, size_t count) {
    auto& nodes = AsPDFStructureElementNode(element)->fAdditionalNodeIds;
    nodes.insert(nodes.end(), value, value + count);
}

void sk_pdf_structure_element_add_int_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, int value) {
    AsPDFStructureElementNode(element)->fAttributes.appendInt(owner, name, value);
}

void sk_pdf_structure_element_add_float_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, float value) {
    AsPDFStructureElementNode(element)->fAttributes.appendFloat(owner, name, value);
}

void sk_pdf_structure_element_add_name_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, const char* value) {
    AsPDFStructureElementNode(element)->fAttributes.appendName(owner, name, value);
}

void sk_pdf_structure_element_add_float_array_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, float* values, size_t count) {
    auto& attributes = AsPDFStructureElementNode(element)->fAttributes;
    auto vector = std::vector<float>(values, values + count);
    attributes.appendFloatArray(owner, name, vector);
}

void sk_pdf_structure_element_add_node_id_array_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, int* values, size_t count) {
    auto& attributes = AsPDFStructureElementNode(element)->fAttributes;
    auto vector = std::vector<int>(values, values + count);
    attributes.appendNodeIdArray(owner, name, vector);
}

void sk_pdf_structure_element_set_alt(sk_pdf_structure_element_t* element, sk_string_t* value) {
    AsPDFStructureElementNode(element)->fAlt = AsOptionalString(value);
}

void sk_pdf_structure_element_set_lang(sk_pdf_structure_element_t* element, sk_string_t* value) {
    AsPDFStructureElementNode(element)->fLang = AsOptionalString(value);
}
