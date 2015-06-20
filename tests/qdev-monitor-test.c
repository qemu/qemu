/*
 * qdev-monitor.c test cases
 *
 * Copyright (C) 2013 Red Hat Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <string.h>
#include <glib.h>
#include "libqtest.h"
#include "qapi/qmp/qjson.h"

static void test_device_add(void)
{
    QDict *response;
    QDict *error;

    qtest_start("-drive if=none,id=drive0");

    /* Make device_add fail.  If this leaks the virtio-blk-pci device then a
     * reference to drive0 will also be held (via qdev properties).
     */
    response = qmp("{\"execute\": \"device_add\","
                   " \"arguments\": {"
                   "   \"driver\": \"virtio-blk-pci\","
                   "   \"drive\": \"drive0\""
                   "}}");
    g_assert(response);
    error = qdict_get_qdict(response, "error");
    g_assert(!strcmp(qdict_get_try_str(error, "class") ?: "",
                     "GenericError"));
    g_assert(!strcmp(qdict_get_try_str(error, "desc") ?: "",
                     "Device initialization failed."));
    QDECREF(response);

    /* Delete the drive */
    response = qmp("{\"execute\": \"human-monitor-command\","
                   " \"arguments\": {"
                   "   \"command-line\": \"drive_del drive0\""
                   "}}");
    g_assert(response);
    g_assert(!strcmp(qdict_get_try_str(response, "return") ?: "(null)", ""));
    QDECREF(response);

    /* Try to re-add the drive.  This fails with duplicate IDs if a leaked
     * virtio-blk-pci exists that holds a reference to the old drive0.
     */
    response = qmp("{\"execute\": \"human-monitor-command\","
                   " \"arguments\": {"
                   "   \"command-line\": \"drive_add pci-addr=auto if=none,id=drive0\""
                   "}}");
    g_assert(response);
    g_assert(!strcmp(qdict_get_try_str(response, "return") ?: "",
                     "OK\r\n"));
    QDECREF(response);

    qtest_end();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    /* Check architecture */
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86\n");
        return 0;
    }

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qmp/device_add", test_device_add);

    return g_test_run();
}
