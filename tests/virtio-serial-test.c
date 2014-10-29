/*
 * QTest testcase for VirtIO Serial
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

static void hotplug(void)
{
    QDict *response;

    response = qmp("{\"execute\": \"device_add\","
                   " \"arguments\": {"
                   "   \"driver\": \"virtserialport\","
                   "   \"id\": \"hp-port\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{\"execute\": \"device_del\","
                   " \"arguments\": {"
                   "   \"id\": \"hp-port\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
    QDECREF(response);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/serial/pci/nop", pci_nop);
    qtest_add_func("/virtio/serial/pci/hotplug", hotplug);

    qtest_start("-device virtio-serial-pci");
    ret = g_test_run();

    qtest_end();

    return ret;
}
