/*
 * blockdev.c test cases
 *
 * Copyright (C) 2013-2014 Red Hat Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/virtio.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"

static const char *qvirtio_get_dev_type(void);

static bool look_for_drive0(QTestState *qts, const char *command, const char *key)
{
    QDict *response;
    QList *ret;
    QListEntry *entry;
    bool found;

    response = qtest_qmp(qts, "{'execute': %s}", command);
    g_assert(response && qdict_haskey(response, "return"));
    ret = qdict_get_qlist(response, "return");

    found = false;
    QLIST_FOREACH_ENTRY(ret, entry) {
        QDict *entry_dict = qobject_to(QDict, entry->value);
        if (!strcmp(qdict_get_str(entry_dict, key), "drive0")) {
            found = true;
            break;
        }
    }

    qobject_unref(response);
    return found;
}

/*
 * This covers the possible absence of a device due to QEMU build
 * options.
 */
static bool has_device_builtin(const char *dev)
{
    gchar *device = g_strdup_printf("%s-%s", dev, qvirtio_get_dev_type());
    bool rc = qtest_has_device(device);

    g_free(device);
    return rc;
}

static bool has_drive(QTestState *qts)
{
    return look_for_drive0(qts, "query-block", "device");
}

static bool has_blockdev(QTestState *qts)
{
    return look_for_drive0(qts, "query-named-block-nodes", "node-name");
}

static void blockdev_add_with_media(QTestState *qts)
{
    QDict *response;

    response = qtest_qmp(qts,
                         "{ 'execute': 'blockdev-add',"
                         "  'arguments': {"
                         "      'driver': 'raw',"
                         "      'node-name': 'drive0',"
                         "      'file': {"
                         "          'driver': 'null-co',"
                         "          'read-zeroes': true"
                         "      }"
                         "  }"
                         "}");

    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
    g_assert(has_blockdev(qts));
}

static void drive_add(QTestState *qts)
{
    char *resp = qtest_hmp(qts, "drive_add 0 if=none,id=drive0");

    g_assert_cmpstr(resp, ==, "OK\r\n");
    g_assert(has_drive(qts));
    g_free(resp);
}

static void drive_add_with_media(QTestState *qts)
{
    char *resp = qtest_hmp(qts,
                           "drive_add 0 if=none,id=drive0,file=null-co://,"
                           "file.read-zeroes=on,format=raw");

    g_assert_cmpstr(resp, ==, "OK\r\n");
    g_assert(has_drive(qts));
    g_free(resp);
}

static void drive_del(QTestState *qts)
{
    char *resp;

    g_assert(has_drive(qts));
    resp = qtest_hmp(qts, "drive_del drive0");
    g_assert_cmpstr(resp, ==, "");
    g_assert(!has_drive(qts));
    g_free(resp);
}

/*
 * qvirtio_get_dev_type:
 * Returns: the preferred virtio bus/device type for the current architecture.
 * TODO: delete this
 */
static const char *qvirtio_get_dev_type(void)
{
    const char *arch = qtest_get_arch();

    if (g_str_equal(arch, "arm") || g_str_equal(arch, "aarch64")) {
        return "device";  /* for virtio-mmio */
    } else if (g_str_equal(arch, "s390x")) {
        return "ccw";
    } else {
        return "pci";
    }
}

static void device_add(QTestState *qts)
{
    g_autofree char *driver = g_strdup_printf("virtio-blk-%s",
                                              qvirtio_get_dev_type());
    QDict *response =
               qtest_qmp(qts, "{'execute': 'device_add',"
                              " 'arguments': {"
                              "   'driver': %s,"
                              "   'drive': 'drive0',"
                              "   'id': 'dev0'"
                              "}}", driver);
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void device_del(QTestState *qts, bool and_reset)
{
    QDict *response;

    qtest_qmp_device_del_send(qts, "dev0");

    if (and_reset) {
        response = qtest_qmp(qts, "{'execute': 'system_reset' }");
        g_assert(response);
        g_assert(qdict_haskey(response, "return"));
        qobject_unref(response);
    }

    qtest_qmp_eventwait(qts, "DEVICE_DELETED");
}

static void test_drive_without_dev(void)
{
    QTestState *qts;

    /* Start with an empty drive */
    qts = qtest_init("-drive if=none,id=drive0");

    /* Delete the drive */
    drive_del(qts);

    /* Ensure re-adding the drive works - there should be no duplicate ID error
     * because the old drive must be gone.
     */
    drive_add(qts);

    qtest_quit(qts);
}

static void test_after_failed_device_add(void)
{
    char driver[32];
    QDict *response;
    QTestState *qts;

    snprintf(driver, sizeof(driver), "virtio-blk-%s",
             qvirtio_get_dev_type());

    qts = qtest_init("-drive if=none,id=drive0");

    /* Make device_add fail. If this leaks the virtio-blk device then a
     * reference to drive0 will also be held (via qdev properties).
     */
    response = qtest_qmp(qts, "{'execute': 'device_add',"
                              " 'arguments': {"
                              "   'driver': %s,"
                              "   'drive': 'drive0'"
                              "}}", driver);
    g_assert(response);
    qmp_expect_error_and_unref(response, "GenericError");

    /* Delete the drive */
    drive_del(qts);

    /* Try to re-add the drive.  This fails with duplicate IDs if a leaked
     * virtio-blk device exists that holds a reference to the old drive0.
     */
    drive_add(qts);

    qtest_quit(qts);
}

static void test_drive_del_device_del(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-scsi")) {
        g_test_skip("Device virtio-scsi is not available");
        return;
    }

    /* Start with a drive used by a device that unplugs instantaneously */
    qts = qtest_initf("-drive if=none,id=drive0,file=null-co://,"
                      "file.read-zeroes=on,format=raw"
                      " -device virtio-scsi-%s"
                      " -device scsi-hd,drive=drive0,id=dev0",
                      qvirtio_get_dev_type());

    /*
     * Delete the drive, and then the device
     * Doing it in this order takes notoriously tricky special paths
     */
    drive_del(qts);
    device_del(qts, false);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_cli_device_del(void)
{
    QTestState *qts;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    /*
     * -drive/-device and device_del.  Start with a drive used by a
     * device that unplugs after reset.
     */
    qts = qtest_initf("%s -drive if=none,id=drive0,file=null-co://,"
                      "file.read-zeroes=on,format=raw"
                      " -device virtio-blk-%s,drive=drive0,id=dev0",
                      machine_addition,
                      qvirtio_get_dev_type());

    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_cli_device_del_q35(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    /*
     * -drive/-device and device_del.  Start with a drive used by a
     * device that unplugs after reset.
     */
    qts = qtest_initf("-drive if=none,id=drive0,file=null-co://,"
                      "file.read-zeroes=on,format=raw "
                      "-machine q35 -device pcie-root-port,id=p1 "
                      "-device pcie-pci-bridge,bus=p1,id=b1 "
                      "-device virtio-blk-%s,drive=drive0,bus=b1,id=dev0",
                      qvirtio_get_dev_type());

    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_empty_device_del(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-scsi")) {
        g_test_skip("Device virtio-scsi is not available");
        return;
    }

    /* device_del with no drive plugged.  */
    qts = qtest_initf("-device virtio-scsi-%s -device scsi-cd,id=dev0",
                      qvirtio_get_dev_type());

    device_del(qts, false);
    qtest_quit(qts);
}

static void test_device_add_and_del(void)
{
    QTestState *qts;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    /*
     * -drive/device_add and device_del.  Start with a drive used by a
     * device that unplugs after reset.
     */
    qts = qtest_initf("%s -drive if=none,id=drive0,file=null-co://,"
                     "file.read-zeroes=on,format=raw", machine_addition);

    device_add(qts);
    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void device_add_q35(QTestState *qts)
{
    g_autofree char *driver = g_strdup_printf("virtio-blk-%s",
                                              qvirtio_get_dev_type());
    QDict *response =
               qtest_qmp(qts, "{'execute': 'device_add',"
                              " 'arguments': {"
                              "   'driver': %s,"
                              "   'drive': 'drive0',"
                              "   'id': 'dev0',"
                              "   'bus': 'b1'"
                              "}}", driver);
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void test_device_add_and_del_q35(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    /*
     * -drive/device_add and device_del.  Start with a drive used by a
     * device that unplugs after reset.
     */
    qts = qtest_initf("-machine q35 -device pcie-root-port,id=p1 "
                     "-device pcie-pci-bridge,bus=p1,id=b1 "
                     "-drive if=none,id=drive0,file=null-co://,"
                     "file.read-zeroes=on,format=raw");

    device_add_q35(qts);
    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_drive_add_device_add_and_del(void)
{
    QTestState *qts;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    qts = qtest_init(machine_addition);

    /*
     * drive_add/device_add and device_del.  The drive is used by a
     * device that unplugs after reset.
     */
    drive_add_with_media(qts);
    device_add(qts);
    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_drive_add_device_add_and_del_q35(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    qts = qtest_init("-machine q35 -device pcie-root-port,id=p1 "
                     "-device pcie-pci-bridge,bus=p1,id=b1");

    /*
     * drive_add/device_add and device_del.  The drive is used by a
     * device that unplugs after reset.
     */
    drive_add_with_media(qts);
    device_add_q35(qts);
    device_del(qts, true);
    g_assert(!has_drive(qts));

    qtest_quit(qts);
}

static void test_blockdev_add_device_add_and_del(void)
{
    QTestState *qts;
    const char *arch = qtest_get_arch();
    const char *machine_addition = "";

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        machine_addition = "-machine pc";
    }

    qts = qtest_init(machine_addition);

    /*
     * blockdev_add/device_add and device_del. The drive is used by a
     * device that unplugs after reset, but it doesn't go away.
     */
    blockdev_add_with_media(qts);
    device_add(qts);
    device_del(qts, true);
    g_assert(has_blockdev(qts));

    qtest_quit(qts);
}

static void test_blockdev_add_device_add_and_del_q35(void)
{
    QTestState *qts;

    if (!has_device_builtin("virtio-blk")) {
        g_test_skip("Device virtio-blk is not available");
        return;
    }

    qts = qtest_init("-machine q35 -device pcie-root-port,id=p1 "
                     "-device pcie-pci-bridge,bus=p1,id=b1");

    /*
     * blockdev_add/device_add and device_del. The drive is used by a
     * device that unplugs after reset, but it doesn't go away.
     */
    blockdev_add_with_media(qts);
    device_add_q35(qts);
    device_del(qts, true);
    g_assert(has_blockdev(qts));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/drive_del/without-dev", test_drive_without_dev);

    if (qvirtio_get_dev_type() != NULL) {
        qtest_add_func("/drive_del/after_failed_device_add",
                       test_after_failed_device_add);
        qtest_add_func("/drive_del/drive_del_device_del",
                       test_drive_del_device_del);
        qtest_add_func("/device_del/drive/cli_device",
                       test_cli_device_del);
        qtest_add_func("/device_del/drive/device_add",
                       test_device_add_and_del);
        qtest_add_func("/device_del/drive/drive_add_device_add",
                       test_drive_add_device_add_and_del);
        qtest_add_func("/device_del/empty",
                       test_empty_device_del);
        qtest_add_func("/device_del/blockdev",
                       test_blockdev_add_device_add_and_del);

        if (qtest_has_machine("q35")) {
            qtest_add_func("/device_del/drive/cli_device_q35",
                           test_cli_device_del_q35);
            qtest_add_func("/device_del/drive/device_add_q35",
                           test_device_add_and_del_q35);
            qtest_add_func("/device_del/drive/drive_add_device_add_q35",
                           test_drive_add_device_add_and_del_q35);
            qtest_add_func("/device_del/blockdev_q35",
                           test_blockdev_add_device_add_and_del_q35);
        }
    }

    return g_test_run();
}
