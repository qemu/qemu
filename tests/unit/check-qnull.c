/*
 * QNull unit-tests.
 *
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "qapi/qmp/qnull.h"
#include "qemu-common.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/error.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qnull_ref_test(void)
{
    QObject *obj;

    g_assert(qnull_.base.refcnt == 1);
    obj = QOBJECT(qnull());
    g_assert(obj);
    g_assert(obj == QOBJECT(&qnull_));
    g_assert(qnull_.base.refcnt == 2);
    g_assert(qobject_type(obj) == QTYPE_QNULL);
    qobject_unref(obj);
    g_assert(qnull_.base.refcnt == 1);
}

static void qnull_visit_test(void)
{
    QObject *obj;
    Visitor *v;
    QNull *null;

    /*
     * Most tests of interactions between QObject and visitors are in
     * test-qmp-*-visitor; but these tests live here because they
     * depend on layering violations to check qnull_ refcnt.
     */

    g_assert(qnull_.base.refcnt == 1);
    obj = QOBJECT(qnull());
    v = qobject_input_visitor_new(obj);
    qobject_unref(obj);
    visit_type_null(v, NULL, &null, &error_abort);
    g_assert(obj == QOBJECT(&qnull_));
    qobject_unref(null);
    visit_free(v);

    null = NULL;
    v = qobject_output_visitor_new(&obj);
    visit_type_null(v, NULL, &null, &error_abort);
    visit_complete(v, &obj);
    g_assert(obj == QOBJECT(&qnull_));
    qobject_unref(null);
    qobject_unref(obj);
    visit_free(v);

    g_assert(qnull_.base.refcnt == 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qnull_ref", qnull_ref_test);
    g_test_add_func("/public/qnull_visit", qnull_visit_test);

    return g_test_run();
}
