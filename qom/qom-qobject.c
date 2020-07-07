/*
 * QEMU Object Model - QObject wrappers
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qom/qom-qobject.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

bool object_property_set_qobject(Object *obj,
                                 const char *name, QObject *value,
                                 Error **errp)
{
    Visitor *v;
    bool ok;

    v = qobject_input_visitor_new(value);
    ok = object_property_set(obj, name, v, errp);
    visit_free(v);
    return ok;
}

QObject *object_property_get_qobject(Object *obj, const char *name,
                                     Error **errp)
{
    QObject *ret = NULL;
    Visitor *v;

    v = qobject_output_visitor_new(&ret);
    if (object_property_get(obj, name, v, errp)) {
        visit_complete(v, &ret);
    }
    visit_free(v);
    return ret;
}
