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
 * Create a new string output visitor.
 *
 * Using @human creates output that is a bit easier for humans to read
 * (for example, showing integer values in both decimal and hex).
 *
 * If everything else succeeds, pass @result to visit_complete() to
 * collect the result of the visit.
 *
 * The string output visitor does not implement support for visiting
 * QAPI structs, alternates, null, or arbitrary QTypes.  It also
 * requires a non-null list argument to visit_start_list().
 */
Visitor *string_output_visitor_new(bool human, char **result);

#endif
