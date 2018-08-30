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
#define qmp_discard_response(...) qobject_unref(qmp(__VA_ARGS__))

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
    qobject_unref(response);
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
    char driver[32];
    QDict *response;

    snprintf(driver, sizeof(driver), "virtio-blk-%s",
             qvirtio_get_dev_type());

    qtest_start("-drive if=none,id=drive0");

    /* Make device_add fail. If this leaks the virtio-blk device then a
     * reference to drive0 will also be held (via qdev properties).
     */
    response = qmp("{'execute': 'device_add',"
                   " 'arguments': {"
                   "   'driver': %s,"
                   "   'drive': 'drive0'"
                   "}}", driver);
    g_assert(response);
    qmp_assert_error_class(response, "GenericError");

    /* Delete the drive */
    drive_del();

    /* Try to re-add the drive.  This fails with duplicate IDs if a leaked
     * virtio-blk device exists that holds a reference to the old drive0.
     */
    drive_add();

    qtest_end();
}

static void test_drive_del_device_del(void)
{
    char *args;

    /* Start with a drive used by a device that unplugs instantaneously */
    args = g_strdup_printf("-drive if=none,id=drive0,file=null-co://,format=raw"
                           " -device virtio-scsi-%s"
                           " -device scsi-hd,drive=drive0,id=dev0",
                           qvirtio_get_dev_type());
    qtest_start(args);

    /*
     * Delete the drive, and then the device
     * Doing it in this order takes notoriously tricky special paths
     */
    drive_del();
    device_del();

    qtest_end();
    g_free(args);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/drive_del/without-dev", test_drive_without_dev);

    /* TODO I guess any arch with a hot-pluggable virtio bus would do */
    if (!strcmp(arch, "i386") || !strcmp(arch, "x86_64") ||
        !strcmp(arch, "ppc") || !strcmp(arch, "ppc64") ||
        !strcmp(arch, "s390x")) {
        qtest_add_func("/drive_del/after_failed_device_add",
                       test_after_failed_device_add);
        qtest_add_func("/blockdev/drive_del_device_del",
                       test_drive_del_device_del);
    }

    return g_test_run();
}
