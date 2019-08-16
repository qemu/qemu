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

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(q, ...) qobject_unref(qtest_qmp(q, __VA_ARGS__))

static void drive_add(QTestState *qts)
{
    char *resp = qtest_hmp(qts, "drive_add 0 if=none,id=drive0");

    g_assert_cmpstr(resp, ==, "OK\r\n");
    g_free(resp);
}

static void drive_del(QTestState *qts)
{
    char *resp = qtest_hmp(qts, "drive_del drive0");

    g_assert_cmpstr(resp, ==, "");
    g_free(resp);
}

static void device_del(QTestState *qts)
{
    QDict *response;

    /* Complication: ignore DEVICE_DELETED event */
    qmp_discard_response(qts, "{'execute': 'device_del',"
                         " 'arguments': { 'id': 'dev0' } }");
    response = qtest_qmp_receive(qts);
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
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
    qmp_assert_error_class(response, "GenericError");

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
    device_del(qts);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/drive_del/without-dev", test_drive_without_dev);

    if (qvirtio_get_dev_type() != NULL) {
        qtest_add_func("/drive_del/after_failed_device_add",
                       test_after_failed_device_add);
        qtest_add_func("/blockdev/drive_del_device_del",
                       test_drive_del_device_del);
    }

    return g_test_run();
}
