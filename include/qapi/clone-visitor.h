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

#include "qapi/error.h"
#include "qapi/visitor.h"

/*
 * The clone visitor is for direct use only by the QAPI_CLONE() macro;
 * it requires that the root visit occur on an object, list, or
 * alternate, and is not usable directly on built-in QAPI types.
 */
typedef struct QapiCloneVisitor QapiCloneVisitor;

Visitor *qapi_clone_visitor_new(void);
Visitor *qapi_clone_members_visitor_new(void);

/*
 * Deep-clone QAPI object @src of the given @type, and return the result.
 *
 * Not usable on QAPI scalars (integers, strings, enums), nor on a
 * QAPI object that references the 'any' type.  Safe when @src is NULL.
 */
#define QAPI_CLONE(type, src)                                   \
    ({                                                          \
        Visitor *v_;                                            \
        type *dst_ = (type *) (src); /* Cast away const */      \
                                                                \
        if (dst_) {                                             \
            v_ = qapi_clone_visitor_new();                      \
            visit_type_ ## type(v_, NULL, &dst_, &error_abort); \
            visit_free(v_);                                     \
        }                                                       \
        dst_;                                                   \
    })

/*
 * Copy deep clones of @type members from @src to @dst.
 *
 * Not usable on QAPI scalars (integers, strings, enums), nor on a
 * QAPI object that references the 'any' type.
 */
#define QAPI_CLONE_MEMBERS(type, dst, src)                                \
    ({                                                                    \
        Visitor *v_;                                                      \
                                                                          \
        v_ = qapi_clone_members_visitor_new();                            \
        *(type *)(dst) = *(src);                                          \
        visit_type_ ## type ## _members(v_, (type *)(dst), &error_abort); \
        visit_free(v_);                                                   \
    })

#endif
