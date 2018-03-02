/*
 * ARM MPS2 AN505 FPGAIO emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the "FPGA system control and I/O" block found
 * in the AN505 FPGA image for the MPS2 devboard.
 * It is documented in AN505:
 * http://infocenter.arm.com/help/topic/com.arm.doc.dai0505b/index.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/mps2-fpgaio.h"

REG32(LED0, 0)
REG32(BUTTON, 8)
REG32(CLK1HZ, 0x10)
REG32(CLK100HZ, 0x14)
REG32(COUNTER, 0x18)
REG32(PRESCALE, 0x1c)
REG32(PSCNTR, 0x20)
REG32(MISC, 0x4c)

static uint64_t mps2_fpgaio_read(void *opaque, hwaddr offset, unsigned size)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(opaque);
    uint64_t r;

    switch (offset) {
    case A_LED0:
        r = s->led0;
        break;
    case A_BUTTON:
        /* User-pressable board buttons. We don't model that, so just return
         * zeroes.
         */
        r = 0;
        break;
    case A_PRESCALE:
        r = s->prescale;
        break;
    case A_MISC:
        r = s->misc;
        break;
    case A_CLK1HZ:
    case A_CLK100HZ:
    case A_COUNTER:
    case A_PSCNTR:
        /* These are all upcounters of various frequencies. */
        qemu_log_mask(LOG_UNIMP, "MPS2 FPGAIO: counters unimplemented\n");
        r = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 FPGAIO read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }

    trace_mps2_fpgaio_read(offset, r, size);
    return r;
}

static void mps2_fpgaio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(opaque);

    trace_mps2_fpgaio_write(offset, value, size);

    switch (offset) {
    case A_LED0:
        /* LED bits [1:0] control board LEDs. We don't currently have
         * a mechanism for displaying this graphically, so use a trace event.
         */
        trace_mps2_fpgaio_leds(value & 0x02 ? '*' : '.',
                               value & 0x01 ? '*' : '.');
        s->led0 = value & 0x3;
        break;
    case A_PRESCALE:
        s->prescale = value;
        break;
    case A_MISC:
        /* These are control bits for some of the other devices on the
         * board (SPI, CLCD, etc). We don't implement that yet, so just
         * make the bits read as written.
         */
        qemu_log_mask(LOG_UNIMP,
                      "MPS2 FPGAIO: MISC control bits unimplemented\n");
        s->misc = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 FPGAIO write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps mps2_fpgaio_ops = {
    .read = mps2_fpgaio_read,
    .write = mps2_fpgaio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mps2_fpgaio_reset(DeviceState *dev)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(dev);

    trace_mps2_fpgaio_reset();
    s->led0 = 0;
    s->prescale = 0;
    s->misc = 0;
}

static void mps2_fpgaio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MPS2FPGAIO *s = MPS2_FPGAIO(obj);

    memory_region_init_io(&s->iomem, obj, &mps2_fpgaio_ops, s,
                          "mps2-fpgaio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription mps2_fpgaio_vmstate = {
    .name = "mps2-fpgaio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(led0, MPS2FPGAIO),
        VMSTATE_UINT32(prescale, MPS2FPGAIO),
        VMSTATE_UINT32(misc, MPS2FPGAIO),
        VMSTATE_END_OF_LIST()
    }
};

static Property mps2_fpgaio_properties[] = {
    /* Frequency of the prescale counter */
    DEFINE_PROP_UINT32("prescale-clk", MPS2FPGAIO, prescale_clk, 20000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void mps2_fpgaio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &mps2_fpgaio_vmstate;
    dc->reset = mps2_fpgaio_reset;
    dc->props = mps2_fpgaio_properties;
}

static const TypeInfo mps2_fpgaio_info = {
    .name = TYPE_MPS2_FPGAIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPS2FPGAIO),
    .instance_init = mps2_fpgaio_init,
    .class_init = mps2_fpgaio_class_init,
};

static void mps2_fpgaio_register_types(void)
{
    type_register_static(&mps2_fpgaio_info);
}

type_init(mps2_fpgaio_register_types);
