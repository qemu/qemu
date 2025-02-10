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
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

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

static void process_device_remove(QTestState *qtest, const char *id)
{
    /*
     * Request device removal. As the guest is not running, the request won't
     * be processed. However during system reset, the removal will be
     * handled, removing the device.
     */
    qtest_qmp_device_del_send(qtest, id);
    qtest_system_reset_nowait(qtest);
    wait_device_deleted_event(qtest, id);
}

static void test_pci_unplug_request(void)
{
    QTestState *qtest;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!qtest_has_device("virtio-mouse-pci")) {
        g_test_skip("Device virtio-mouse-pci not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    qtest = qtest_initf("%s -device virtio-mouse-pci,id=dev0",
                        machine_addition);

    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_q35_pci_unplug_request(void)
{
    QTestState *qtest;

    if (!qtest_has_device("virtio-mouse-pci")) {
        g_test_skip("Device virtio-mouse-pci not available");
        return;
    }

    qtest = qtest_initf("-machine q35 "
                        "-device pcie-root-port,id=p1 "
                        "-device pcie-pci-bridge,bus=p1,id=b1 "
                        "-device virtio-mouse-pci,bus=b1,id=dev0");

    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_pci_unplug_json_request(void)
{
    QTestState *qtest;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!qtest_has_device("virtio-mouse-pci")) {
        g_test_skip("Device virtio-mouse-pci not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    qtest = qtest_initf(
        "%s -device \"{'driver': 'virtio-mouse-pci', 'id': 'dev0'}\"",
        machine_addition);

    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_q35_pci_unplug_json_request(void)
{
    QTestState *qtest;
    const char *port = "-device \"{'driver': 'pcie-root-port', "
                                  "'id': 'p1'}\"";

    const char *bridge = "-device \"{'driver': 'pcie-pci-bridge', "
                                    "'id': 'b1', "
                                    "'bus': 'p1'}\"";

    const char *device = "-device \"{'driver': 'virtio-mouse-pci', "
                                    "'bus': 'b1', "
                                    "'id': 'dev0'}\"";

    if (!qtest_has_device("virtio-mouse-pci")) {
        g_test_skip("Device virtio-mouse-pci not available");
        return;
    }

    qtest = qtest_initf("-machine q35 %s %s %s", port, bridge, device);

    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_ccw_unplug(void)
{
    QTestState *qtest;

    if (!qtest_has_device("virtio-balloon-ccw")) {
        g_test_skip("Device virtio-balloon-ccw not available");
        return;
    }

    qtest = qtest_initf("-device virtio-balloon-ccw,id=dev0");

    qtest_qmp_device_del_send(qtest, "dev0");
    wait_device_deleted_event(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_cpu_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-cpu power9_v2.2 -smp 1,maxcpus=2 "
                        "-device power9_v2.2-spapr-cpu-core,core-id=1,id=dev0");

    /* similar to test_pci_unplug_request */
    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_memory_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-m 256M,slots=1,maxmem=768M "
                        "-object memory-backend-ram,id=mem0,size=512M "
                        "-device pc-dimm,id=dev0,memdev=mem0");

    /* similar to test_pci_unplug_request */
    process_device_remove(qtest, "dev0");

    qtest_quit(qtest);
}

static void test_spapr_phb_unplug_request(void)
{
    QTestState *qtest;

    qtest = qtest_initf("-device spapr-pci-host-bridge,index=1,id=dev0");

    /* similar to test_pci_unplug_request */
    process_device_remove(qtest, "dev0");

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

    if (!strcmp(arch, "x86_64") && qtest_has_machine("q35")) {
        qtest_add_func("/device-plug/q35-pci-unplug-request",
                   test_q35_pci_unplug_request);
        qtest_add_func("/device-plug/q35-pci-unplug-json-request",
                   test_q35_pci_unplug_json_request);
    }

    return g_test_run();
}
