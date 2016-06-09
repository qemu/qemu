/*
 * Clone Visitor
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QAPI_CLONE_VISITOR_H
#define QAPI_CLONE_VISITOR_H

#include "qemu/typedefs.h"
#include "qapi/visitor.h"
#include "qapi-visit.h"

/*
 * The clone visitor is for direct use only by the QAPI_CLONE() macro;
 * it requires that the root visit occur on an object, list, or
 * alternate, and is not usable directly on built-in QAPI types.
 */
typedef struct QapiCloneVisitor QapiCloneVisitor;

void *qapi_clone(const void *src, void (*visit_type)(Visitor *, const char *,
                                                     void **, Error **));

/*
 * Deep-clone QAPI object @src of the given @type, and return the result.
 *
 * Not usable on QAPI scalars (integers, strings, enums), nor on a
 * QAPI object that references the 'any' type.  Safe when @src is NULL.
 */
#define QAPI_CLONE(type, src)                                           \
    ((type *)qapi_clone(src,                                            \
                        (void (*)(Visitor *, const char *, void**,      \
                                  Error **))visit_type_ ## type))

#endif
