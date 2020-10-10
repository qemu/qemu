/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This peripheral is roughly divided into 3 main parts:
 *   - the PLLs
 *   - the PLL channels
 *   - the clock muxes
 *
 * A main oscillator (xosc) feeds all the PLLs. Each PLLs has one or more
 * channels. Those channel are then connected to the clock muxes. Each mux has
 * multiples sources (usually the xosc, some of the PLL channels and some "test
 * debug" clocks). A mux is configured to select a given source through its
 * control register. Each mux has one output clock that also goes out of the
 * CPRMAN. This output clock usually connects to another peripheral in the SoC
 * (so a given mux is dedicated to a peripheral).
 *
 * At each level (PLL, channel and mux), the clock can be altered through
 * dividers (and multipliers in case of the PLLs), and can be disabled (in this
 * case, the next levels see no clock).
 *
 * This can be sum-up as follows (this is an example and not the actual BCM2835
 * clock tree):
 *
 *          /-->[PLL]-|->[PLL channel]--...            [mux]--> to peripherals
 *          |         |->[PLL channel]  muxes takes    [mux]
 *          |         \->[PLL channel]  inputs from    [mux]
 *          |                           some channels  [mux]
 * [xosc]---|-->[PLL]-|->[PLL channel]  and other srcs [mux]
 *          |         \->[PLL channel]           ...-->[mux]
 *          |                                          [mux]
 *          \-->[PLL]--->[PLL channel]                 [mux]
 *
 * The page at https://elinux.org/The_Undocumented_Pi gives the actual clock
 * tree configuration.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/misc/bcm2835_cprman.h"
#include "hw/misc/bcm2835_cprman_internals.h"
#include "trace.h"

/* CPRMAN "top level" model */

static uint64_t cprman_read(void *opaque, hwaddr offset,
                            unsigned size)
{
    BCM2835CprmanState *s = CPRMAN(opaque);
    uint64_t r = 0;
    size_t idx = offset / sizeof(uint32_t);

    switch (idx) {
    default:
        r = s->regs[idx];
    }

    trace_bcm2835_cprman_read(offset, r);
    return r;
}

static void cprman_write(void *opaque, hwaddr offset,
                         uint64_t value, unsigned size)
{
    BCM2835CprmanState *s = CPRMAN(opaque);
    size_t idx = offset / sizeof(uint32_t);

    if (FIELD_EX32(value, CPRMAN, PASSWORD) != CPRMAN_PASSWORD) {
        trace_bcm2835_cprman_write_invalid_magic(offset, value);
        return;
    }

    value &= ~R_CPRMAN_PASSWORD_MASK;

    trace_bcm2835_cprman_write(offset, value);
    s->regs[idx] = value;

}

static const MemoryRegionOps cprman_ops = {
    .read = cprman_read,
    .write = cprman_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        /*
         * Although this hasn't been checked against real hardware, nor the
         * information can be found in a datasheet, it seems reasonable because
         * of the "PASSWORD" magic value found in every registers.
         */
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
    .impl = {
        .max_access_size = 4,
    },
};

static void cprman_reset(DeviceState *dev)
{
    BCM2835CprmanState *s = CPRMAN(dev);

    memset(s->regs, 0, sizeof(s->regs));

    clock_update_hz(s->xosc, s->xosc_freq);
}

static void cprman_init(Object *obj)
{
    BCM2835CprmanState *s = CPRMAN(obj);

    s->xosc = clock_new(obj, "xosc");

    memory_region_init_io(&s->iomem, obj, &cprman_ops,
                          s, "bcm2835-cprman", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription cprman_vmstate = {
    .name = TYPE_BCM2835_CPRMAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BCM2835CprmanState, CPRMAN_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static Property cprman_properties[] = {
    DEFINE_PROP_UINT32("xosc-freq-hz", BCM2835CprmanState, xosc_freq, 19200000),
    DEFINE_PROP_END_OF_LIST()
};

static void cprman_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = cprman_reset;
    dc->vmsd = &cprman_vmstate;
    device_class_set_props(dc, cprman_properties);
}

static const TypeInfo cprman_info = {
    .name = TYPE_BCM2835_CPRMAN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835CprmanState),
    .class_init = cprman_class_init,
    .instance_init = cprman_init,
};

static void cprman_register_types(void)
{
    type_register_static(&cprman_info);
}

type_init(cprman_register_types);
