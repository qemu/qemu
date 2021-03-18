/*
 * Policy for handling "funny" management interfaces
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QAPI_COMPAT_POLICY_H
#define QAPI_COMPAT_POLICY_H

#include "qapi/qapi-types-compat.h"

extern CompatPolicy compat_policy;

/*
 * Create a QObject input visitor for @obj for use with QMP
 *
 * This is like qobject_input_visitor_new(), except it obeys the
 * policy for handling deprecated management interfaces set with
 * -compat.
 */
Visitor *qobject_input_visitor_new_qmp(QObject *obj);

/*
 * Create a QObject output visitor for @obj for use with QMP
 *
 * This is like qobject_output_visitor_new(), except it obeys the
 * policy for handling deprecated management interfaces set with
 * -compat.
 */
Visitor *qobject_output_visitor_new_qmp(QObject **result);

#endif
