/*
 * Forwarding visitor
 *
 * Copyright Red Hat, Inc. 2021
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef FORWARD_VISITOR_H
#define FORWARD_VISITOR_H

#include "qapi/visitor.h"

typedef struct ForwardFieldVisitor ForwardFieldVisitor;

/*
 * The forwarding visitor only expects a single name, @from, to be passed for
 * toplevel fields.  It is converted to @to and forwarded to the @target visitor.
 * Calls within a struct are forwarded without changing the name.
 */
Visitor *visitor_forward_field(Visitor *target, const char *from, const char *to);

#endif
