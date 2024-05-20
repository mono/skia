/*
 * Copyright 2014 Google Inc.
 * Copyright 2015 Xamarin Inc.
 * Copyright 2017 Microsoft Corporation. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sk_document_DEFINED
#define sk_document_DEFINED

#include "include/c/sk_types.h"

SK_C_PLUS_PLUS_BEGIN_GUARD

// SkDocument

SK_C_API void sk_document_unref(sk_document_t* document);

SK_C_API sk_document_t* sk_document_create_pdf_from_stream(sk_wstream_t* stream);
SK_C_API sk_document_t* sk_document_create_pdf_from_stream_with_metadata(sk_wstream_t* stream, const sk_pdf_metadata_t* metadata);

SK_C_API sk_document_t* sk_document_create_xps_from_stream(sk_wstream_t* stream, float dpi);

SK_C_API sk_canvas_t* sk_document_begin_page(sk_document_t* document, float width, float height, const sk_rect_t* content);
SK_C_API void sk_document_end_page(sk_document_t* document);
SK_C_API void sk_document_close(sk_document_t* document);
SK_C_API void sk_document_abort(sk_document_t* document);


// SkPDF::Metadata

SK_C_API sk_pdf_metadata_t* sk_pdf_metadata_new(void);
SK_C_API void sk_pdf_metadata_delete(sk_pdf_metadata_t* metadata);

SK_C_API void sk_pdf_metadata_set_title(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_author(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_subject(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_keywords(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_creator(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_producer(sk_pdf_metadata_t* metadata, sk_string_t* value);
SK_C_API void sk_pdf_metadata_set_creation(sk_pdf_metadata_t* metadata, sk_time_datetime_t* value);
SK_C_API void sk_pdf_metadata_set_modified(sk_pdf_metadata_t* metadata, sk_time_datetime_t* value);
SK_C_API void sk_pdf_metadata_set_raster_dpi(sk_pdf_metadata_t* metadata, float value);
SK_C_API void sk_pdf_metadata_set_pdfa(sk_pdf_metadata_t* metadata, bool value);
SK_C_API void sk_pdf_metadata_set_encoding_quality(sk_pdf_metadata_t* metadata, int value);
SK_C_API void sk_pdf_metadata_set_structure_element_tree_root(sk_pdf_metadata_t* metadata, sk_pdf_structure_element_t* value);
SK_C_API void sk_pdf_metadata_set_compression_level(sk_pdf_metadata_t* metadata, sk_pdf_compression_t value);


// SkPDF::StructureElementNode

SK_C_API sk_pdf_structure_element_t* sk_pdf_structure_element_new(void);
SK_C_API void sk_pdf_structure_element_delete(sk_pdf_structure_element_t* element);

SK_C_API void sk_pdf_structure_element_set_type_string(sk_pdf_structure_element_t* element, sk_string_t* value);
SK_C_API void sk_pdf_structure_element_add_child(sk_pdf_structure_element_t* element, sk_pdf_structure_element_t* value);
SK_C_API void sk_pdf_structure_element_set_node_id(sk_pdf_structure_element_t* element, int value);
SK_C_API void sk_pdf_structure_element_add_additional_node_id(sk_pdf_structure_element_t* element, int value);
SK_C_API void sk_pdf_structure_element_add_additional_node_ids(sk_pdf_structure_element_t* element, int* value, size_t count);

SK_C_API void sk_pdf_structure_element_add_int_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, int value);
SK_C_API void sk_pdf_structure_element_add_float_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, float value);
SK_C_API void sk_pdf_structure_element_add_name_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, const char* value);
SK_C_API void sk_pdf_structure_element_add_float_array_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, float* values, size_t count);
SK_C_API void sk_pdf_structure_element_add_node_id_array_attribute(sk_pdf_structure_element_t* element, const char* owner, const char* name, int* values, size_t count);

SK_C_API void sk_pdf_structure_element_set_alt(sk_pdf_structure_element_t* element, sk_string_t* value);
SK_C_API void sk_pdf_structure_element_set_lang(sk_pdf_structure_element_t* element, sk_string_t* value);


// SkPDF

SK_C_API void sk_canvas_draw_pdf_node_id_annotation(sk_canvas_t* t, int nodeId);

SK_C_PLUS_PLUS_END_GUARD

#endif
