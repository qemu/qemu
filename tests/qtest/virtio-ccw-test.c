/*
 * QTest testcase for VirtIO CCW
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* Until we have a full libqos implementation of virtio-ccw (which requires
 * also to add support for I/O channels to qtest), we can only do simple
 * tests that initialize the devices.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/virtio.h"

static void virtio_balloon_nop(void)
{
    global_qtest = qtest_initf("-device virtio-balloon-ccw");
    qtest_end();
}

static void virtconsole_nop(void)
{
    global_qtest = qtest_initf("-device virtio-serial-ccw,id=vser0 "
                                "-device virtconsole,bus=vser0.0");
    qtest_end();
}

static void virtserialport_nop(void)
{
    global_qtest = qtest_initf("-device virtio-serial-ccw,id=vser0 "
                                "-device virtserialport,bus=vser0.0");
    qtest_end();
}

static void virtio_serial_nop(void)
{
    global_qtest = qtest_initf("-device virtio-serial-ccw");
    qtest_end();
}

static void virtio_serial_hotplug(void)
{
    QTestState *qts = qtest_initf("-device virtio-serial-ccw");

    qtest_qmp_device_add(qts, "virtserialport", "hp-port", "{}");
    qtest_qmp_device_del(qts, "hp-port");

    qtest_quit(qts);
}

static void virtio_blk_nop(void)
{
    global_qtest = qtest_initf("-drive if=none,id=drv0,file=null-co://,"
                               "file.read-zeroes=on,format=raw "
                                "-device virtio-blk-ccw,drive=drv0");
    qtest_end();
}

static void virtio_net_nop(void)
{
    global_qtest = qtest_initf("-device virtio-net-ccw");
    qtest_end();
}

static void virtio_rng_nop(void)
{
    global_qtest = qtest_initf("-device virtio-rng-ccw");
    qtest_end();
}

static void virtio_scsi_nop(void)
{
    global_qtest = qtest_initf("-device virtio-scsi-ccw");
    qtest_end();
}

static void virtio_scsi_hotplug(void)
{
    QTestState *s = qtest_initf("-drive if=none,id=drv0,file=null-co://,"
                                "file.read-zeroes=on,format=raw "
                                "-drive if=none,id=drv1,file=null-co://,"
                                "file.read-zeroes=on,format=raw "
                                "-device virtio-scsi-ccw "
                                "-device scsi-hd,drive=drv0");
    qtest_qmp_device_add(s, "scsi-hd", "scsihd", "{'drive': 'drv1'}");
    qtest_qmp_device_del(s, "scsihd");

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/balloon/nop", virtio_balloon_nop);
    qtest_add_func("/virtio/console/nop", virtconsole_nop);
    qtest_add_func("/virtio/serialport/nop", virtserialport_nop);
    qtest_add_func("/virtio/serial/nop", virtio_serial_nop);
    qtest_add_func("/virtio/serial/hotplug", virtio_serial_hotplug);
    qtest_add_func("/virtio/block/nop", virtio_blk_nop);
    qtest_add_func("/virtio/net/nop", virtio_net_nop);
    qtest_add_func("/virtio/rng/nop", virtio_rng_nop);
    qtest_add_func("/virtio/scsi/nop", virtio_scsi_nop);
    qtest_add_func("/virtio/scsi/hotplug", virtio_scsi_hotplug);

    ret = g_test_run();

    return ret;
}
