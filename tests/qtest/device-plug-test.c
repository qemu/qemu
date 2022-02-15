/*
 * QEMU device plug/unplug handling
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

static void device_del(QTestState *qtest, const char *id)
{
    QDict *resp;

    resp = qtest_qmp(qtest,
                     "{'execute': 'device_del', 'arguments': { 'id': %s } }", id);

    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static void system_reset(QTestState *qtest)
{
    QDict *resp;

    resp = qtest_qmp(qtest, "{'execute': 'system_reset'}");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static void wait_device_deleted_event(QTestState *qtest, const char *id)
{
    QDict *resp, *data;
    QString *qstr;

    /*
     * Other devices might get removed along with the removed device. Skip
     * these. The device of interest will be the last one.
     */
    for (;;) {
        resp = qtest_qmp_eventwait_ref(qtest, "DEVICE_DELETED");
        data = qdict_get_qdict(resp, "data");
        if (!data || !qdict_get(data, "device")) {
            qobject_unref(resp);
            continue;
        }
        qstr = qobject_to(QString, qdict_get(data, "device"));
        g_assert(qstr);
        if (!strcmp(qstring_get_str(qstr), id)) {
            qobject_unref(resp);
            break;
        }
        qobject_unref(resp);
    }
}

static void test_pci_unplug_request(void)
{
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    QTestState *qtest = qtest_initf("%s -device virtio-mouse-pci,id=dev0",
                                    machine_addition);

    /*
     * Request device removal. As the guest is not running, the request won't
     * be processed. However during system reset, the removal will be
     * handled, removing the device.
     */
    device_del(qtest, "dev0");
    system_reset(qtest);
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_pci_unplug_json_request(void)
{
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    QTestState *qtest = qtest_initf(
        "%s -device '{\"driver\": \"virtio-mouse-pci\", \"id\": \"dev0\"}'",
        machine_addition);

    /*
     * Request device removal. As the guest is not running, the request won't
     * be processed. However during system reset, the removal will be
     * handled, removing the device.
     */
    device_del(qtest, "dev0");
    system_reset(qtest);
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_ccw_unplug(void)
{
    QTestState *qtest = qtest_initf("-device virtio-balloon-ccw,id=dev0");

    device_del(qtest, "dev0");
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_cpu_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-cpu power9_v2.0 -smp 1,maxcpus=2 "
                        "-device power9_v2.0-spapr-cpu-core,core-id=1,id=dev0");

    /* similar to test_pci_unplug_request */
    device_del(qtest, "dev0");
    system_reset(qtest);
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_memory_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-m 256M,slots=1,maxmem=768M "
                        "-object memory-backend-ram,id=mem0,size=512M "
                        "-device pc-dimm,id=dev0,memdev=mem0");

    /* similar to test_pci_unplug_request */
    device_del(qtest, "dev0");
    system_reset(qtest);
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_phb_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-device spapr-pci-host-bridge,index=1,id=dev0");

    /* similar to test_pci_unplug_request */
    device_del(qtest, "dev0");
    system_reset(qtest);
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    /*
     * We need a system that will process unplug requests during system resets
     * and does not do PCI surprise removal. This holds for x86 ACPI,
     * s390x and spapr.
     */
    qtest_add_func("/device-plug/pci-unplug-request",
                   test_pci_unplug_request);
    qtest_add_func("/device-plug/pci-unplug-json-request",
                   test_pci_unplug_json_request);

    if (!strcmp(arch, "s390x")) {
        qtest_add_func("/device-plug/ccw-unplug",
                       test_ccw_unplug);
    }

    if (!strcmp(arch, "ppc64")) {
        qtest_add_func("/device-plug/spapr-cpu-unplug-request",
                       test_spapr_cpu_unplug_request);
        qtest_add_func("/device-plug/spapr-memory-unplug-request",
                       test_spapr_memory_unplug_request);
        qtest_add_func("/device-plug/spapr-phb-unplug-request",
                       test_spapr_phb_unplug_request);
    }

    return g_test_run();
}
