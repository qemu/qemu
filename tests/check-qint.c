/*
 * QInt unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include <glib.h>

#include "qint.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qint_from_int_test(void)
{
    QInt *qi;
    const int value = -42;

    qi = qint_from_int(value);
    g_assert(qi != NULL);
    g_assert(qi->value == value);
    g_assert(qi->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qi)) == QTYPE_QINT);

    // destroy doesn't exit yet
    g_free(qi);
}

static void qint_destroy_test(void)
{
    QInt *qi = qint_from_int(0);
    QDECREF(qi);
}

static void qint_from_int64_test(void)
{
    QInt *qi;
    const int64_t value = 0x1234567890abcdefLL;

    qi = qint_from_int(value);
    g_assert((int64_t) qi->value == value);

    QDECREF(qi);
}

static void qint_get_int_test(void)
{
    QInt *qi;
    const int value = 123456;

    qi = qint_from_int(value);
    g_assert(qint_get_int(qi) == value);

    QDECREF(qi);
}

static void qobject_to_qint_test(void)
{
    QInt *qi;

    qi = qint_from_int(0);
    g_assert(qobject_to_qint(QOBJECT(qi)) == qi);

    QDECREF(qi);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/from_int", qint_from_int_test);
    g_test_add_func("/public/destroy", qint_destroy_test);
    g_test_add_func("/public/from_int64", qint_from_int64_test);
    g_test_add_func("/public/get_int", qint_get_int_test);
    g_test_add_func("/public/to_qint", qobject_to_qint_test);

    return g_test_run();
}
