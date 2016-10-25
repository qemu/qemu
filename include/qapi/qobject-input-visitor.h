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

#ifndef QOBJECT_INPUT_VISITOR_H
#define QOBJECT_INPUT_VISITOR_H

#include "qapi/visitor.h"
#include "qapi/qmp/qobject.h"

typedef struct QObjectInputVisitor QObjectInputVisitor;

/*
 * Return a new input visitor that converts a QObject to a QAPI object.
 *
 * Set @strict to reject a parse that doesn't consume all keys of a
 * dictionary; otherwise excess input is ignored.
 */
Visitor *qobject_input_visitor_new(QObject *obj, bool strict);

#endif
