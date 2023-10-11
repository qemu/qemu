/*
 * QTest testcase for virtio-net failover
 *
 * See docs/system/virtio-net-failover.rst
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "migration-helpers.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qjson.h"
#include "libqos/malloc-pc.h"
#include "libqos/virtio-pci.h"
#include "hw/pci/pci.h"

#define VIRTIO_NET_F_STANDBY    62

#define ACPI_PCIHP_ADDR_ICH9    0x0cc0
#define PCI_EJ_BASE             0x0008
#define PCI_SEL_BASE            0x0010

#define BASE_MACHINE "-M q35 -nodefaults " \
    "-device pcie-root-port,id=root0,addr=0x1,bus=pcie.0,chassis=1 " \
    "-device pcie-root-port,id=root1,addr=0x2,bus=pcie.0,chassis=2 "

#define MAC_PRIMARY0 "52:54:00:11:11:11"
#define MAC_STANDBY0 "52:54:00:22:22:22"
#define MAC_PRIMARY1 "52:54:00:33:33:33"
#define MAC_STANDBY1 "52:54:00:44:44:44"

static QGuestAllocator guest_malloc;
static QPCIBus *pcibus;

static QTestState *machine_start(const char *args, int numbus)
{
    QTestState *qts;
    QPCIDevice *dev;
    int bus;

    qts = qtest_init(args);

    pc_alloc_init(&guest_malloc, qts, 0);
    pcibus = qpci_new_pc(qts, &guest_malloc);
    g_assert(qpci_secondary_buses_init(pcibus) == numbus);

    for (bus = 1; bus <= numbus; bus++) {
        dev = qpci_device_find(pcibus, QPCI_DEVFN(bus, 0));
        g_assert_nonnull(dev);

        qpci_device_enable(dev);
        qpci_iomap(dev, 4, NULL);

        g_free(dev);
    }

    return qts;
}

static void machine_stop(QTestState *qts)
{
    qpci_free_pc(pcibus);
    alloc_destroy(&guest_malloc);
    qtest_quit(qts);
}

static void test_error_id(void)
{
    QTestState *qts;
    QDict *resp;
    QDict *err;

    qts = machine_start(BASE_MACHINE
                        "-device virtio-net,bus=root0,id=standby0,failover=on",
                        2);

    resp = qtest_qmp(qts, "{'execute': 'device_add',"
                          "'arguments': {"
                          "'driver': 'virtio-net',"
                          "'bus': 'root1',"
                          "'failover_pair_id': 'standby0'"
                          "} }");
    g_assert(qdict_haskey(resp, "error"));

    err = qdict_get_qdict(resp, "error");
    g_assert(qdict_haskey(err, "desc"));

    g_assert_cmpstr(qdict_get_str(err, "desc"), ==,
                    "Device with failover_pair_id needs to have id");

    qobject_unref(resp);

    machine_stop(qts);
}

static void test_error_pcie(void)
{
    QTestState *qts;
    QDict *resp;
    QDict *err;

    qts = machine_start(BASE_MACHINE
                        "-device virtio-net,bus=root0,id=standby0,failover=on",
                        2);

    resp = qtest_qmp(qts, "{'execute': 'device_add',"
                          "'arguments': {"
                          "'driver': 'virtio-net',"
                          "'id': 'primary0',"
                          "'bus': 'pcie.0',"
                          "'failover_pair_id': 'standby0'"
                          "} }");
    g_assert(qdict_haskey(resp, "error"));

    err = qdict_get_qdict(resp, "error");
    g_assert(qdict_haskey(err, "desc"));

    g_assert_cmpstr(qdict_get_str(err, "desc"), ==,
                    "Bus 'pcie.0' does not support hotplugging");

    qobject_unref(resp);

    machine_stop(qts);
}

static QDict *find_device(QDict *bus, const char *name)
{
    const QObject *obj;
    QList *devices;
    QList *list;

    devices = qdict_get_qlist(bus, "devices");
    if (devices == NULL) {
        return NULL;
    }

    list = qlist_copy(devices);
    while ((obj = qlist_pop(list))) {
        QDict *device;

        device = qobject_to(QDict, obj);

        if (qdict_haskey(device, "pci_bridge")) {
            QDict *bridge;
            QDict *bridge_device;

            bridge = qdict_get_qdict(device, "pci_bridge");

            if (qdict_haskey(bridge, "devices")) {
                bridge_device = find_device(bridge, name);
                if (bridge_device) {
                    qobject_unref(device);
                    qobject_unref(list);
                    return bridge_device;
                }
            }
        }

        if (!qdict_haskey(device, "qdev_id")) {
            qobject_unref(device);
            continue;
        }

        if (strcmp(qdict_get_str(device, "qdev_id"), name) == 0) {
            qobject_unref(list);
            return device;
        }
        qobject_unref(device);
    }
    qobject_unref(list);

    return NULL;
}

static QDict *get_bus(QTestState *qts, int num)
{
    QObject *obj;
    QDict *resp;
    QList *ret;

    resp = qtest_qmp(qts, "{ 'execute': 'query-pci' }");
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qlist(resp, "return");
    g_assert_nonnull(ret);

    while ((obj = qlist_pop(ret))) {
        QDict *bus;

        bus = qobject_to(QDict, obj);
        if (!qdict_haskey(bus, "bus")) {
            qobject_unref(bus);
            continue;
        }
        if (qdict_get_int(bus, "bus") == num) {
            qobject_unref(resp);
            return bus;
        }
        qobject_ref(bus);
    }
    qobject_unref(resp);

    return NULL;
}

static char *get_mac(QTestState *qts, const char *name)
{
    QDict *resp;
    char *mac;

    resp = qtest_qmp(qts, "{ 'execute': 'qom-get', "
                     "'arguments': { "
                     "'path': %s, "
                     "'property': 'mac' } }", name);

    g_assert(qdict_haskey(resp, "return"));

    mac = g_strdup(qdict_get_str(resp, "return"));

    qobject_unref(resp);

    return mac;
}

#define check_one_card(qts, present, id, mac)                   \
do {                                                            \
    QDict *device;                                              \
    QDict *bus;                                                 \
    char *addr;                                                 \
    bus = get_bus(qts, 0);                                      \
    device = find_device(bus, id);                              \
    if (present) {                                              \
        char *path;                                             \
        g_assert_nonnull(device);                               \
        qobject_unref(device);                                  \
        path = g_strdup_printf("/machine/peripheral/%s", id);   \
        addr = get_mac(qts, path);                              \
        g_free(path);                                           \
        g_assert_cmpstr(mac, ==, addr);                         \
        g_free(addr);                                           \
    } else {                                                    \
       g_assert_null(device);                                   \
    }                                                           \
    qobject_unref(bus);                                         \
} while (0)

static QDict *get_failover_negociated_event(QTestState *qts)
{
    QDict *resp;
    QDict *data;

    resp = qtest_qmp_eventwait_ref(qts, "FAILOVER_NEGOTIATED");
    g_assert(qdict_haskey(resp, "data"));

    data = qdict_get_qdict(resp, "data");
    g_assert(qdict_haskey(data, "device-id"));
    qobject_ref(data);
    qobject_unref(resp);

    return data;
}

static QVirtioPCIDevice *start_virtio_net_internal(QTestState *qts,
                                                   int bus, int slot,
                                                   uint64_t *features)
{
    QVirtioPCIDevice *dev;
    QPCIAddress addr;

    addr.devfn = QPCI_DEVFN((bus << 5) + slot, 0);
    dev = virtio_pci_new(pcibus, &addr);
    g_assert_nonnull(dev);
    qvirtio_pci_device_enable(dev);
    qvirtio_start_device(&dev->vdev);
    *features &= qvirtio_get_features(&dev->vdev);
    qvirtio_set_features(&dev->vdev, *features);
    qvirtio_set_driver_ok(&dev->vdev);
    return dev;
}

static QVirtioPCIDevice *start_virtio_net(QTestState *qts, int bus, int slot,
                                          const char *id, bool failover)
{
    QVirtioPCIDevice *dev;
    uint64_t features;

    features = ~(QVIRTIO_F_BAD_FEATURE |
                 (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                 (1ull << VIRTIO_RING_F_EVENT_IDX));

    dev = start_virtio_net_internal(qts, bus, slot, &features);

    g_assert(!!(features & (1ull << VIRTIO_NET_F_STANDBY)) == failover);

    if (failover) {
        QDict *resp;

        resp = get_failover_negociated_event(qts);
        g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, id);
        qobject_unref(resp);
    }

    return dev;
}

static void test_on(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                        "-netdev user,id=hs0 "
                        "-device virtio-net,bus=root0,id=standby0,"
                        "failover=on,netdev=hs0,mac="MAC_STANDBY0" "
                        "-netdev user,id=hs1 "
                        "-device virtio-net,bus=root1,id=primary0,"
                        "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY0,
                        2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    machine_stop(qts);
}

static void test_on_mismatch(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY0" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby1,netdev=hs1,mac="MAC_PRIMARY0,
                     2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_off(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=off,netdev=hs0,mac="MAC_STANDBY0" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY0,
                     2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", false);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_enabled(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY0" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY0" ",
                     2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_guest_off(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;
    uint64_t features;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY0" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY0" ",
                     2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    features = ~(QVIRTIO_F_BAD_FEATURE |
                 (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                 (1ull << VIRTIO_RING_F_EVENT_IDX) |
                 (1ull << VIRTIO_NET_F_STANDBY));

    vdev = start_virtio_net_internal(qts, 1, 0, &features);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_hotplug_1(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY0" "
                     "-netdev user,id=hs1 ", 2);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_hotplug_1_reverse(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY0" ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_hotplug_2(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_hotplug_2_reverse(void)
{
    QTestState *qts;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_STANDBY0"'}");

    /*
     * XXX: sounds like a bug:
     * The primary should be hidden until the virtio-net driver
     * negotiates the VIRTIO_NET_F_STANDBY feature by start_virtio_net()
     */
    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

#ifndef _WIN32
static QDict *migrate_status(QTestState *qts)
{
    QDict *resp, *ret;

    resp = qtest_qmp(qts, "{ 'execute': 'query-migrate' }");
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qdict(resp, "return");
    g_assert(qdict_haskey(ret, "status"));
    qobject_ref(ret);
    qobject_unref(resp);

    return ret;
}

static QDict *get_unplug_primary_event(QTestState *qts)
{
    QDict *resp;
    QDict *data;

    resp = qtest_qmp_eventwait_ref(qts, "UNPLUG_PRIMARY");
    g_assert(qdict_haskey(resp, "data"));

    data = qdict_get_qdict(resp, "data");
    g_assert(qdict_haskey(data, "device-id"));
    qobject_ref(data);
    qobject_unref(resp);

    return data;
}

static void test_migrate_out(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* the event is sent when QEMU asks the OS to unplug the card */
    resp = get_unplug_primary_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "primary0");
    qobject_unref(resp);

    /* wait the end of the migration setup phase */
    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "wait-unplug") == 0) {
            qobject_unref(ret);
            break;
        }

        /* The migration must not start if the card is not ejected */
        g_assert_cmpstr(status, !=, "active");
        g_assert_cmpstr(status, !=, "completed");
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");

        qobject_unref(ret);
    }

    if (g_test_slow()) {
        /* check we stay in wait-unplug while the card is not ejected */
        for (int i = 0; i < 5; i++) {
            sleep(1);
            ret = migrate_status(qts);
            status = qdict_get_str(ret, "status");
            g_assert_cmpstr(status, ==, "wait-unplug");
            qobject_unref(ret);
        }
    }

    /* OS unplugs the cards, QEMU can move from wait-unplug state */
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");
        qobject_unref(ret);
    }

    qtest_qmp_eventwait(qts, "STOP");

    /*
     * in fact, the card is ejected from the point of view of kernel
     * but not really from QEMU to be able to hotplug it back if
     * migration fails. So we can't check that:
     *   check_one_card(qts, true, "standby0", MAC_STANDBY0);
     *   check_one_card(qts, false, "primary0", MAC_PRIMARY0);
     */

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_migrate_in(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat %s", (gchar *)opaque);

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-incoming defer ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    migrate_incoming_qmp(qts, uri, "{}");

    resp = get_failover_negociated_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "standby0");
    qobject_unref(resp);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qtest_qmp_eventwait(qts, "RESUME");

    ret = migrate_status(qts);
    g_assert_cmpstr(qdict_get_str(ret, "status"), ==, "completed");
    qobject_unref(ret);

    machine_stop(qts);
}

static void test_off_migrate_out(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'off',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", false);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");
        qobject_unref(ret);
    }

    qtest_qmp_eventwait(qts, "STOP");

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_off_migrate_in(gconstpointer opaque)
{
    QTestState *qts;
    QDict *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat %s", (gchar *)opaque);

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-incoming defer ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'off',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    migrate_incoming_qmp(qts, uri, "{}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qtest_qmp_eventwait(qts, "RESUME");

    ret = migrate_status(qts);
    g_assert_cmpstr(qdict_get_str(ret, "status"), ==, "completed");
    qobject_unref(ret);

    machine_stop(qts);
}

static void test_guest_off_migrate_out(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;
    uint64_t features;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    features = ~(QVIRTIO_F_BAD_FEATURE |
                 (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                 (1ull << VIRTIO_RING_F_EVENT_IDX) |
                 (1ull << VIRTIO_NET_F_STANDBY));

    vdev = start_virtio_net_internal(qts, 1, 0, &features);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");
        qobject_unref(ret);
    }

    qtest_qmp_eventwait(qts, "STOP");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_guest_off_migrate_in(gconstpointer opaque)
{
    QTestState *qts;
    QDict *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat %s", (gchar *)opaque);

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-incoming defer ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    migrate_incoming_qmp(qts, uri, "{}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_eventwait(qts, "RESUME");

    ret = migrate_status(qts);
    g_assert_cmpstr(qdict_get_str(ret, "status"), ==, "completed");
    qobject_unref(ret);

    machine_stop(qts);
}

static void test_migrate_guest_off_abort(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;
    uint64_t features;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    features = ~(QVIRTIO_F_BAD_FEATURE |
                 (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                 (1ull << VIRTIO_RING_F_EVENT_IDX) |
                 (1ull << VIRTIO_NET_F_STANDBY));

    vdev = start_virtio_net_internal(qts, 1, 0, &features);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            g_test_skip("Failed to cancel the migration");
            qobject_unref(ret);
            goto out;
        }
        if (strcmp(status, "active") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        qobject_unref(ret);
    }

    resp = qtest_qmp(qts, "{ 'execute': 'migrate_cancel' }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);
        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            g_test_skip("Failed to cancel the migration");
            qobject_unref(ret);
            goto out;
        }
        if (strcmp(status, "cancelled") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "active");
        qobject_unref(ret);
    }

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

out:
    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_migrate_abort_wait_unplug(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* the event is sent when QEMU asks the OS to unplug the card */
    resp = get_unplug_primary_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "primary0");
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate_cancel' }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* migration has been cancelled while the unplug was in progress */

    /* while the card is not ejected, we must be in "cancelling" state */
    ret = migrate_status(qts);

    status = qdict_get_str(ret, "status");
    g_assert_cmpstr(status, ==, "cancelling");
    qobject_unref(ret);

    /* OS unplugs the cards, QEMU can move from wait-unplug state */
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "cancelled") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, ==, "cancelling");
        qobject_unref(ret);
    }

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_migrate_abort_active(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* the event is sent when QEMU asks the OS to unplug the card */
    resp = get_unplug_primary_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "primary0");
    qobject_unref(resp);

    /* OS unplugs the cards, QEMU can move from wait-unplug state */
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        g_assert_cmpstr(status, !=, "failed");
        if (strcmp(status, "wait-unplug") != 0) {
            qobject_unref(ret);
            break;
        }
        qobject_unref(ret);
    }

    resp = qtest_qmp(qts, "{ 'execute': 'migrate_cancel' }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            g_test_skip("Failed to cancel the migration");
            qobject_unref(ret);
            goto out;
        }
        if (strcmp(status, "cancelled") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "active");
        qobject_unref(ret);
    }

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

out:
    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_migrate_off_abort(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'off',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", false);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "active") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        qobject_unref(ret);
    }

    resp = qtest_qmp(qts, "{ 'execute': 'migrate_cancel' }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            g_test_skip("Failed to cancel the migration");
            qobject_unref(ret);
            goto out;
        }
        if (strcmp(status, "cancelled") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "active");
        qobject_unref(ret);
    }

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

out:
    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_migrate_abort_timeout(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status;
    int total;
    QVirtioPCIDevice *vdev;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 ",
                     2);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    vdev = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* the event is sent when QEMU asks the OS to unplug the card */
    resp = get_unplug_primary_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "primary0");
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate_cancel' }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* migration has been cancelled while the unplug was in progress */

    /* while the card is not ejected, we must be in "cancelling" state */

    total = 0;
    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "cancelled") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, ==, "cancelling");
        g_assert(qdict_haskey(ret, "total-time"));
        total = qdict_get_int(ret, "total-time");
        qobject_unref(ret);
    }

    /*
     * migration timeout in this case is 30 seconds
     * check we exit on the timeout (ms)
     */
    g_assert_cmpint(total, >, 30000);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);

    qos_object_destroy((QOSGraphObject *)vdev);
    machine_stop(qts);
}

static void test_multi_out(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    const gchar *status, *expected;
    QVirtioPCIDevice *vdev0, *vdev1;

    qts = machine_start(BASE_MACHINE
                "-device pcie-root-port,id=root2,addr=0x3,bus=pcie.0,chassis=3 "
                "-device pcie-root-port,id=root3,addr=0x4,bus=pcie.0,chassis=4 "
                "-netdev user,id=hs0 "
                "-netdev user,id=hs1 "
                "-netdev user,id=hs2 "
                "-netdev user,id=hs3 ",
                4);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    vdev0 = start_virtio_net(qts, 1, 0, "standby0", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "standby1",
                         "{'bus': 'root2',"
                         "'failover': 'on',"
                         "'netdev': 'hs2',"
                         "'mac': '"MAC_STANDBY1"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "primary1",
                         "{'bus': 'root3',"
                         "'failover_pair_id': 'standby1',"
                         "'netdev': 'hs3',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY1"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    vdev1 = start_virtio_net(qts, 3, 0, "standby1", true);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, true, "primary1", MAC_PRIMARY1);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* the event is sent when QEMU asks the OS to unplug the card */
    resp = get_unplug_primary_event(qts);
    if (strcmp(qdict_get_str(resp, "device-id"), "primary0") == 0) {
        expected = "primary1";
    } else if (strcmp(qdict_get_str(resp, "device-id"), "primary1") == 0) {
        expected = "primary0";
    } else {
        g_assert_not_reached();
    }
    qobject_unref(resp);

    resp = get_unplug_primary_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, expected);
    qobject_unref(resp);

    /* wait the end of the migration setup phase */
    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "wait-unplug") == 0) {
            qobject_unref(ret);
            break;
        }

        /* The migration must not start if the card is not ejected */
        g_assert_cmpstr(status, !=, "active");
        g_assert_cmpstr(status, !=, "completed");
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");

        qobject_unref(ret);
    }

    /* OS unplugs primary1, but we must wait the second */
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    ret = migrate_status(qts);
    status = qdict_get_str(ret, "status");
    g_assert_cmpstr(status, ==, "wait-unplug");
    qobject_unref(ret);

    if (g_test_slow()) {
        /* check we stay in wait-unplug while the card is not ejected */
        for (int i = 0; i < 5; i++) {
            sleep(1);
            ret = migrate_status(qts);
            status = qdict_get_str(ret, "status");
            g_assert_cmpstr(status, ==, "wait-unplug");
            qobject_unref(ret);
        }
    }

    /* OS unplugs primary0, QEMU can move from wait-unplug state */
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_SEL_BASE, 2);
    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    while (true) {
        ret = migrate_status(qts);

        status = qdict_get_str(ret, "status");
        if (strcmp(status, "completed") == 0) {
            qobject_unref(ret);
            break;
        }
        g_assert_cmpstr(status, !=, "failed");
        g_assert_cmpstr(status, !=, "cancelling");
        g_assert_cmpstr(status, !=, "cancelled");
        qobject_unref(ret);
    }

    qtest_qmp_eventwait(qts, "STOP");

    qos_object_destroy((QOSGraphObject *)vdev0);
    qos_object_destroy((QOSGraphObject *)vdev1);
    machine_stop(qts);
}

static void test_multi_in(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *ret;
    g_autofree gchar *uri = g_strdup_printf("exec: cat %s", (gchar *)opaque);

    qts = machine_start(BASE_MACHINE
                "-device pcie-root-port,id=root2,addr=0x3,bus=pcie.0,chassis=3 "
                "-device pcie-root-port,id=root3,addr=0x4,bus=pcie.0,chassis=4 "
                "-netdev user,id=hs0 "
                "-netdev user,id=hs1 "
                "-netdev user,id=hs2 "
                "-netdev user,id=hs3 "
                "-incoming defer ",
                4);

    check_one_card(qts, false, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY0"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, false, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "standby1",
                         "{'bus': 'root2',"
                         "'failover': 'on',"
                         "'netdev': 'hs2',"
                         "'mac': '"MAC_STANDBY1"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    qtest_qmp_device_add(qts, "virtio-net", "primary1",
                         "{'bus': 'root3',"
                         "'failover_pair_id': 'standby1',"
                         "'netdev': 'hs3',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY1"'}");

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, false, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, false, "primary1", MAC_PRIMARY1);

    migrate_incoming_qmp(qts, uri, "{}");

    resp = get_failover_negociated_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "standby0");
    qobject_unref(resp);

    resp = get_failover_negociated_event(qts);
    g_assert_cmpstr(qdict_get_str(resp, "device-id"), ==, "standby1");
    qobject_unref(resp);

    check_one_card(qts, true, "standby0", MAC_STANDBY0);
    check_one_card(qts, true, "primary0", MAC_PRIMARY0);
    check_one_card(qts, true, "standby1", MAC_STANDBY1);
    check_one_card(qts, true, "primary1", MAC_PRIMARY1);

    qtest_qmp_eventwait(qts, "RESUME");

    ret = migrate_status(qts);
    g_assert_cmpstr(qdict_get_str(ret, "status"), ==, "completed");
    qobject_unref(ret);

    machine_stop(qts);
}
#endif /* _WIN32 */

int main(int argc, char **argv)
{
    gchar *tmpfile;
    int ret;

    g_test_init(&argc, &argv, NULL);

    ret = g_file_open_tmp("failover_test_migrate-XXXXXX", &tmpfile, NULL);
    g_assert_true(ret >= 0);
    close(ret);

    /* parameters tests */
    qtest_add_func("failover-virtio-net/params/error/id", test_error_id);
    qtest_add_func("failover-virtio-net/params/error/pcie", test_error_pcie);
    qtest_add_func("failover-virtio-net/params/on", test_on);
    qtest_add_func("failover-virtio-net/params/on_mismatch",
                   test_on_mismatch);
    qtest_add_func("failover-virtio-net/params/off", test_off);
    qtest_add_func("failover-virtio-net/params/enabled", test_enabled);
    qtest_add_func("failover-virtio-net/params/guest_off", test_guest_off);

    /* hotplug tests */
    qtest_add_func("failover-virtio-net/hotplug/1", test_hotplug_1);
    qtest_add_func("failover-virtio-net/hotplug/1_reverse",
                   test_hotplug_1_reverse);
    qtest_add_func("failover-virtio-net/hotplug/2", test_hotplug_2);
    qtest_add_func("failover-virtio-net/hotplug/2_reverse",
                   test_hotplug_2_reverse);

#ifndef _WIN32
    /*
     * These migration tests cases use the exec migration protocol,
     * which is unsupported on Windows.
     */
    qtest_add_data_func("failover-virtio-net/migrate/on/out", tmpfile,
                        test_migrate_out);
    qtest_add_data_func("failover-virtio-net/migrate/on/in", tmpfile,
                        test_migrate_in);
    qtest_add_data_func("failover-virtio-net/migrate/off/out", tmpfile,
                        test_off_migrate_out);
    qtest_add_data_func("failover-virtio-net/migrate/off/in", tmpfile,
                        test_off_migrate_in);
    qtest_add_data_func("failover-virtio-net/migrate/off/abort", tmpfile,
                        test_migrate_off_abort);
    qtest_add_data_func("failover-virtio-net/migrate/guest_off/out", tmpfile,
                        test_guest_off_migrate_out);
    qtest_add_data_func("failover-virtio-net/migrate/guest_off/in", tmpfile,
                        test_guest_off_migrate_in);
    qtest_add_data_func("failover-virtio-net/migrate/guest_off/abort", tmpfile,
                        test_migrate_guest_off_abort);
    qtest_add_data_func("failover-virtio-net/migrate/abort/wait-unplug",
                        tmpfile, test_migrate_abort_wait_unplug);
    qtest_add_data_func("failover-virtio-net/migrate/abort/active", tmpfile,
                        test_migrate_abort_active);
    if (g_test_slow()) {
        qtest_add_data_func("failover-virtio-net/migrate/abort/timeout",
                            tmpfile, test_migrate_abort_timeout);
    }
    qtest_add_data_func("failover-virtio-net/migrate/multi/out",
                        tmpfile, test_multi_out);
    qtest_add_data_func("failover-virtio-net/migrate/multi/in",
                   tmpfile, test_multi_in);
#endif /* _WIN32 */

    ret = g_test_run();

    unlink(tmpfile);
    g_free(tmpfile);

    return ret;
}
