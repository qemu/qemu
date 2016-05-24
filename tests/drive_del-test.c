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

static void drive_add(void)
{
    char *resp = hmp("drive_add 0 if=none,id=drive0");

    g_assert_cmpstr(resp, ==, "OK\r\n");
    g_free(resp);
}

static void drive_del(void)
{
    char *resp = hmp("drive_del drive0");

    g_assert_cmpstr(resp, ==, "");
    g_free(resp);
}

static void device_del(void)
{
    QDict *response;

    /* Complication: ignore DEVICE_DELETED event */
    qmp_discard_response("{'execute': 'device_del',"
                         " 'arguments': { 'id': 'dev0' } }");
    response = qmp_receive();
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);
}

static void test_drive_without_dev(void)
{
    /* Start with an empty drive */
    qtest_start("-drive if=none,id=drive0");

    /* Delete the drive */
    drive_del();

    /* Ensure re-adding the drive works - there should be no duplicate ID error
     * because the old drive must be gone.
     */
    drive_add();

    qtest_end();
}

static void test_after_failed_device_add(void)
{
    QDict *response;
    QDict *error;

    qtest_start("-drive if=none,id=drive0");

    /* Make device_add fail.  If this leaks the virtio-blk-pci device then a
     * reference to drive0 will also be held (via qdev properties).
     */
    response = qmp("{'execute': 'device_add',"
                   " 'arguments': {"
                   "   'driver': 'virtio-blk-pci',"
                   "   'drive': 'drive0'"
                   "}}");
    g_assert(response);
    error = qdict_get_qdict(response, "error");
    g_assert_cmpstr(qdict_get_try_str(error, "class"), ==, "GenericError");
    QDECREF(response);

    /* Delete the drive */
    drive_del();

    /* Try to re-add the drive.  This fails with duplicate IDs if a leaked
     * virtio-blk-pci exists that holds a reference to the old drive0.
     */
    drive_add();

    qtest_end();
}

static void test_drive_del_device_del(void)
{
    /* Start with a drive used by a device that unplugs instantaneously */
    qtest_start("-drive if=none,id=drive0,file=/dev/null,format=raw"
                " -device virtio-scsi-pci"
                " -device scsi-hd,drive=drive0,id=dev0");

    /*
     * Delete the drive, and then the device
     * Doing it in this order takes notoriously tricky special paths
     */
    drive_del();
    device_del();

    qtest_end();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/drive_del/without-dev", test_drive_without_dev);

    /* TODO I guess any arch with PCI would do */
    if (!strcmp(arch, "i386") || !strcmp(arch, "x86_64")) {
        qtest_add_func("/drive_del/after_failed_device_add",
                       test_after_failed_device_add);
        qtest_add_func("/blockdev/drive_del_device_del",
                       test_drive_del_device_del);
    }

    return g_test_run();
}
