/*
 * QTest testcase for VirtIO Serial
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/virtio-serial.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void virtio_serial_nop(void *obj, void *data, QGuestAllocator *alloc)
{
    /* no operation */
}

static void serial_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    qtest_qmp_device_add("virtserialport", "hp-port", "{}");
    qtest_qmp_device_del("hp-port");
}

static void register_virtio_serial_test(void)
{
    QOSGraphTestOptions opts = { };

    opts.edge.before_cmd_line = "-device virtconsole,bus=vser0.0";
    qos_add_test("console-nop", "virtio-serial", virtio_serial_nop, &opts);

    opts.edge.before_cmd_line = "-device virtserialport,bus=vser0.0";
    qos_add_test("serialport-nop", "virtio-serial", virtio_serial_nop, &opts);

    qos_add_test("hotplug", "virtio-serial", serial_hotplug, NULL);
}
libqos_init(register_virtio_serial_test);
