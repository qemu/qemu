/*
 * QTest testcase for PV Panic PCI device
 *
 * Copyright (C) 2020 Oracle
 *
 * Authors:
 *     Mihai Carabas <mihai.carabas@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/misc/pvpanic.h"
#include "hw/pci/pci_regs.h"

static void test_panic_nopause(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;

    qts = qtest_init("-device pvpanic-pci,addr=04.0 -action panic=none");
    pcibus = qpci_new_pc(qts, NULL);
    dev = qpci_device_find(pcibus, QPCI_DEVFN(0x4, 0x0));
    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_memread(dev, bar, 0, &val, sizeof(val));
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    val = 1;
    qpci_memwrite(dev, bar, 0, &val, sizeof(val));

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PANICKED");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "action"));
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "run");
    qobject_unref(response);

    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

static void test_panic(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;

    qts = qtest_init("-device pvpanic-pci,addr=04.0 -action panic=pause");
    pcibus = qpci_new_pc(qts, NULL);
    dev = qpci_device_find(pcibus, QPCI_DEVFN(0x4, 0x0));
    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_memread(dev, bar, 0, &val, sizeof(val));
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    val = 1;
    qpci_memwrite(dev, bar, 0, &val, sizeof(val));

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PANICKED");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "action"));
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "pause");
    qobject_unref(response);

    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

static void test_pvshutdown(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;

    qts = qtest_init("-device pvpanic-pci,addr=04.0");
    pcibus = qpci_new_pc(qts, NULL);
    dev = qpci_device_find(pcibus, QPCI_DEVFN(0x4, 0x0));
    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    qpci_memread(dev, bar, 0, &val, sizeof(val));
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    val = PVPANIC_SHUTDOWN;
    qpci_memwrite(dev, bar, 0, &val, sizeof(val));

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PVSHUTDOWN");
    qobject_unref(response);

    response = qtest_qmp_eventwait_ref(qts, "SHUTDOWN");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "guest"));
    g_assert(qdict_get_bool(data, "guest"));
    g_assert(qdict_haskey(data, "reason"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-shutdown");
    qobject_unref(response);

    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pvpanic-pci/panic", test_panic);
    qtest_add_func("/pvpanic-pci/panic-nopause", test_panic_nopause);
    qtest_add_func("/pvpanic-pci/pvshutdown", test_pvshutdown);

    return g_test_run();
}
