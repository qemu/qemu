/*
 * String printing Visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef STRING_OUTPUT_VISITOR_H
#define STRING_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct StringOutputVisitor StringOutputVisitor;

/*
 * The string output visitor does not implement support for visiting
 * QAPI structs, alternates, or arbitrary QTypes.
 */
StringOutputVisitor *string_output_visitor_new(bool human);
void string_output_visitor_cleanup(StringOutputVisitor *v);

char *string_output_get_string(StringOutputVisitor *v);
Visitor *string_output_get_visitor(StringOutputVisitor *v);

#endif
