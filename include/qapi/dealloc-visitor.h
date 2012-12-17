/*
 * Dealloc Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_DEALLOC_VISITOR_H
#define QAPI_DEALLOC_VISITOR_H

#include "qapi/visitor.h"

typedef struct QapiDeallocVisitor QapiDeallocVisitor;

QapiDeallocVisitor *qapi_dealloc_visitor_new(void);
void qapi_dealloc_visitor_cleanup(QapiDeallocVisitor *d);

Visitor *qapi_dealloc_get_visitor(QapiDeallocVisitor *v);

#endif
