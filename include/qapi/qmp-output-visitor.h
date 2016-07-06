/*
 * Output Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QMP_OUTPUT_VISITOR_H
#define QMP_OUTPUT_VISITOR_H

#include "qapi/visitor.h"
#include "qapi/qmp/qobject.h"

typedef struct QmpOutputVisitor QmpOutputVisitor;

/*
 * Create a new QMP output visitor.
 *
 * If everything else succeeds, pass @result to visit_complete() to
 * collect the result of the visit.
 */
Visitor *qmp_output_visitor_new(QObject **result);

#endif
