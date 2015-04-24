/*
 * QTest testcase for VirtIO SCSI
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

static void qvirtio_scsi_start(const char *extra_opts)
{
    char *cmdline;

    cmdline = g_strdup_printf(
                "-drive id=drv0,if=none,file=/dev/null,format=raw "
                "-device virtio-scsi-pci,id=vs0 "
                "-device scsi-hd,bus=vs0.0,drive=drv0 %s",
                extra_opts ? : "");
    qtest_start(cmdline);
    g_free(cmdline);
}

static void qvirtio_scsi_stop(void)
{
    qtest_end();
}

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
    qvirtio_scsi_start(NULL);
    qvirtio_scsi_stop();
}

static void hotplug(void)
{
    QDict *response;

    qvirtio_scsi_start("-drive id=drv1,if=none,file=/dev/null,format=raw");
    response = qmp("{\"execute\": \"device_add\","
                   " \"arguments\": {"
                   "   \"driver\": \"scsi-hd\","
                   "   \"id\": \"scsi-hd\","
                   "   \"drive\": \"drv1\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{\"execute\": \"device_del\","
                   " \"arguments\": {"
                   "   \"id\": \"scsi-hd\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
    QDECREF(response);
    qvirtio_scsi_stop();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/scsi/pci/nop", pci_nop);
    qtest_add_func("/virtio/scsi/pci/hotplug", hotplug);

    ret = g_test_run();

    return ret;
}
