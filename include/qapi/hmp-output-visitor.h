/*
 * HMP string output Visitor
 *
 * Copyright Yandex N.V., 2021
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HMP_OUTPUT_VISITOR_H
#define HMP_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct HMPOutputVisitor HMPOutputVisitor;

/**
 * Create a HMP string output visitor for @obj
 *
 * Flattens dicts/structures, only shows arrays borders.
 *
 * Errors are not expected to happen.
 *
 * The caller is responsible for freeing the visitor with
 * visit_free().
 */
Visitor *hmp_output_visitor_new(char **result);

#endif
