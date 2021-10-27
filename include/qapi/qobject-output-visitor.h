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

#ifndef QOBJECT_OUTPUT_VISITOR_H
#define QOBJECT_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct QObjectOutputVisitor QObjectOutputVisitor;

/**
 * Create a QObject output visitor for @obj
 *
 * A QObject output visitor visit builds a QObject from QAPI Object.
 * This simultaneously walks the QAPI object and the QObject being
 * built.  The latter walk starts at @obj.
 *
 * visit_type_FOO() creates a QObject for QAPI type FOO.  It creates a
 * QDict for struct/union types, a QList for list types, QString for
 * type 'str' and enumeration types, QNum for integer and float
 * types, QBool for type 'bool'.  For type 'any', it increments the
 * QObject's reference count.  For QAPI alternate types, it creates
 * the QObject for the member that is in use.
 *
 * visit_start_struct() ... visit_end_struct() visits a QAPI
 * struct/union and creates a QDict.  Visits in between visit the
 * members.  visit_optional() is true when the struct/union has this
 * member.  visit_check_struct() does nothing.
 *
 * visit_start_list() ... visit_end_list() visits a QAPI list and
 * creates a QList.  Visits in between visit list members, one after
 * the other.  visit_next_list() returns NULL when all QAPI list
 * members have been visited.  visit_check_list() does nothing.
 *
 * visit_start_alternate() ... visit_end_alternate() visits a QAPI
 * alternate.  The visit in between creates the QObject for the
 * alternate member that is in use.
 *
 * Errors are not expected to happen.
 *
 * The caller is responsible for freeing the visitor with
 * visit_free().
 */
Visitor *qobject_output_visitor_new(QObject **result);

#endif
