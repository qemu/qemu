/*
 * Input Visitor
 *
 * Copyright (C) 2017 Red Hat, Inc.
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

#include "qapi/qapi-types-compat.h"
#include "qapi/visitor.h"

typedef struct QObjectInputVisitor QObjectInputVisitor;

/*
 * Create a QObject input visitor for @obj
 *
 * A QObject input visitor visit builds a QAPI object from a QObject.
 * This simultaneously walks the QAPI object being built and the
 * QObject.  The latter walk starts at @obj.
 *
 * visit_type_FOO() creates an instance of QAPI type FOO.  The visited
 * QObject must match FOO.  QDict matches struct/union types, QList
 * matches list types, QString matches type 'str' and enumeration
 * types, QNum matches integer and float types, QBool matches type
 * 'bool'.  Type 'any' is matched by QObject.  A QAPI alternate type
 * is matched when one of its member types is.
 *
 * visit_start_struct() ... visit_end_struct() visits a QDict and
 * creates a QAPI struct/union.  Visits in between visit the
 * dictionary members.  visit_optional() is true when the QDict has
 * this member.  visit_check_struct() fails if unvisited members
 * remain.
 *
 * visit_start_list() ... visit_end_list() visits a QList and creates
 * a QAPI list.  Visits in between visit list members, one after the
 * other.  visit_next_list() returns NULL when all QList members have
 * been visited.  visit_check_list() fails if unvisited members
 * remain.
 *
 * visit_start_alternate() ... visit_end_alternate() visits a QObject
 * and creates a QAPI alternate.  The visit in between visits the same
 * QObject and initializes the alternate member that is in use.
 *
 * Error messages refer to parts of @obj in JavaScript/Python syntax.
 * For example, 'a.b[2]' refers to the second member of the QList
 * member 'b' of the QDict member 'a' of QDict @obj.
 *
 * The caller is responsible for freeing the visitor with
 * visit_free().
 */
Visitor *qobject_input_visitor_new(QObject *obj);

void qobject_input_visitor_set_policy(Visitor *v,
                                      CompatPolicyInput deprecated);

/*
 * Create a QObject input visitor for @obj for use with keyval_parse()
 *
 * This is like qobject_input_visitor_new(), except scalars are all
 * QString, and error messages refer to parts of @obj in the syntax
 * keyval_parse() uses for KEYs.
 */
Visitor *qobject_input_visitor_new_keyval(QObject *obj);

/*
 * Create a QObject input visitor for parsing @str.
 *
 * If @str looks like JSON, parse it as JSON, else as KEY=VALUE,...
 * @implied_key applies to KEY=VALUE, and works as in keyval_parse().
 * On failure, store an error through @errp and return NULL.
 * On success, return a new QObject input visitor for the parse.
 */
Visitor *qobject_input_visitor_new_str(const char *str,
                                       const char *implied_key,
                                       Error **errp);

#endif
