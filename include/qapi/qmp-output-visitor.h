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

QmpOutputVisitor *qmp_output_visitor_new(void);
void qmp_output_visitor_cleanup(QmpOutputVisitor *v);

QObject *qmp_output_get_qobject(QmpOutputVisitor *v);
Visitor *qmp_output_get_visitor(QmpOutputVisitor *v);

#endif
