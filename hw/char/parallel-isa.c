/*
 * QEMU Parallel PORT (ISA bus helpers)
 *
 * These functions reside in a separate file since they also might be
 * required for linking when compiling QEMU without CONFIG_PARALLEL.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "hw/char/parallel.h"

static void parallel_init(ISABus *bus, int index, Chardev *chr)
{
    DeviceState *dev;
    ISADevice *isadev;

    isadev = isa_create(bus, "isa-parallel");
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "index", index);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
}

void parallel_hds_isa_init(ISABus *bus, int n)
{
    int i;

    assert(n <= MAX_PARALLEL_PORTS);

    for (i = 0; i < n; i++) {
        if (parallel_hds[i]) {
            parallel_init(bus, i, parallel_hds[i]);
        }
    }
}
