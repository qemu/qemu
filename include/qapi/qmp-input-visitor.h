/*
 * Input Visitor
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

#ifndef QMP_INPUT_VISITOR_H
#define QMP_INPUT_VISITOR_H

#include "qapi/visitor.h"
#include "qapi/qmp/qobject.h"

typedef struct QmpInputVisitor QmpInputVisitor;

/*
 * Return a new input visitor that converts QMP to QAPI.
 *
 * Set @strict to reject a parse that doesn't consume all keys of a
 * dictionary; otherwise excess input is ignored.
 */
QmpInputVisitor *qmp_input_visitor_new(QObject *obj, bool strict);

void qmp_input_visitor_cleanup(QmpInputVisitor *v);

Visitor *qmp_input_get_visitor(QmpInputVisitor *v);

#endif
