/*
 * ARM IoTKit system control element
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system control element" which is part of the
 * Arm IoTKit and documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * Specifically, it implements the "system control register" blocks.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/iotkit-sysctl.h"

REG32(SECDBGSTAT, 0x0)
REG32(SECDBGSET, 0x4)
REG32(SECDBGCLR, 0x8)
REG32(RESET_SYNDROME, 0x100)
REG32(RESET_MASK, 0x104)
REG32(SWRESET, 0x108)
    FIELD(SWRESET, SWRESETREQ, 9, 1)
REG32(GRETREG, 0x10c)
REG32(INITSVRTOR0, 0x110)
REG32(CPUWAIT, 0x118)
REG32(BUSWAIT, 0x11c)
REG32(WICCTRL, 0x120)
REG32(PID4, 0xfd0)
REG32(PID5, 0xfd4)
REG32(PID6, 0xfd8)
REG32(PID7, 0xfdc)
REG32(PID0, 0xfe0)
REG32(PID1, 0xfe4)
REG32(PID2, 0xfe8)
REG32(PID3, 0xfec)
REG32(CID0, 0xff0)
REG32(CID1, 0xff4)
REG32(CID2, 0xff8)
REG32(CID3, 0xffc)

/* PID/CID values */
static const int sysctl_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x54, 0xb8, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static uint64_t iotkit_sysctl_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);
    uint64_t r;

    switch (offset) {
    case A_SECDBGSTAT:
        r = s->secure_debug;
        break;
    case A_RESET_SYNDROME:
        r = s->reset_syndrome;
        break;
    case A_RESET_MASK:
        r = s->reset_mask;
        break;
    case A_GRETREG:
        r = s->gretreg;
        break;
    case A_INITSVRTOR0:
        r = s->initsvrtor0;
        break;
    case A_CPUWAIT:
        r = s->cpuwait;
        break;
    case A_BUSWAIT:
        /* In IoTKit BUSWAIT is reserved, R/O, zero */
        r = 0;
        break;
    case A_WICCTRL:
        r = s->wicctrl;
        break;
    case A_PID4 ... A_CID3:
        r = sysctl_id[(offset - A_PID4) / 4];
        break;
    case A_SECDBGSET:
    case A_SECDBGCLR:
    case A_SWRESET:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl read: read of WO offset %x\n",
                      (int)offset);
        r = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    trace_iotkit_sysctl_read(offset, r, size);
    return r;
}

static void iotkit_sysctl_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);

    trace_iotkit_sysctl_write(offset, value, size);

    /*
     * Most of the state here has to do with control of reset and
     * similar kinds of power up -- for instance the guest can ask
     * what the reason for the last reset was, or forbid reset for
     * some causes (like the non-secure watchdog). Most of this is
     * not relevant to QEMU, which doesn't really model anything other
     * than a full power-on reset.
     * We just model the registers as reads-as-written.
     */

    switch (offset) {
    case A_RESET_SYNDROME:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SysCtl RESET_SYNDROME unimplemented\n");
        s->reset_syndrome = value;
        break;
    case A_RESET_MASK:
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl RESET_MASK unimplemented\n");
        s->reset_mask = value;
        break;
    case A_GRETREG:
        /*
         * General retention register, which is only reset by a power-on
         * reset. Technically this implementation is complete, since
         * QEMU only supports power-on resets...
         */
        s->gretreg = value;
        break;
    case A_INITSVRTOR0:
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl INITSVRTOR0 unimplemented\n");
        s->initsvrtor0 = value;
        break;
    case A_CPUWAIT:
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl CPUWAIT unimplemented\n");
        s->cpuwait = value;
        break;
    case A_WICCTRL:
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl WICCTRL unimplemented\n");
        s->wicctrl = value;
        break;
    case A_SECDBGSET:
        /* write-1-to-set */
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl SECDBGSET unimplemented\n");
        s->secure_debug |= value;
        break;
    case A_SECDBGCLR:
        /* write-1-to-clear */
        s->secure_debug &= ~value;
        break;
    case A_SWRESET:
        /* One w/o bit to request a reset; all other bits reserved */
        if (value & R_SWRESET_SWRESETREQ_MASK) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case A_BUSWAIT:        /* In IoTKit BUSWAIT is reserved, R/O, zero */
    case A_SECDBGSTAT:
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl write: write of RO offset %x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl write: bad offset %x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps iotkit_sysctl_ops = {
    .read = iotkit_sysctl_read,
    .write = iotkit_sysctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void iotkit_sysctl_reset(DeviceState *dev)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(dev);

    trace_iotkit_sysctl_reset();
    s->secure_debug = 0;
    s->reset_syndrome = 1;
    s->reset_mask = 0;
    s->gretreg = 0;
    s->initsvrtor0 = 0x10000000;
    s->cpuwait = 0;
    s->wicctrl = 0;
}

static void iotkit_sysctl_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IoTKitSysCtl *s = IOTKIT_SYSCTL(obj);

    memory_region_init_io(&s->iomem, obj, &iotkit_sysctl_ops,
                          s, "iotkit-sysctl", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription iotkit_sysctl_vmstate = {
    .name = "iotkit-sysctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(secure_debug, IoTKitSysCtl),
        VMSTATE_UINT32(reset_syndrome, IoTKitSysCtl),
        VMSTATE_UINT32(reset_mask, IoTKitSysCtl),
        VMSTATE_UINT32(gretreg, IoTKitSysCtl),
        VMSTATE_UINT32(initsvrtor0, IoTKitSysCtl),
        VMSTATE_UINT32(cpuwait, IoTKitSysCtl),
        VMSTATE_UINT32(wicctrl, IoTKitSysCtl),
        VMSTATE_END_OF_LIST()
    }
};

static void iotkit_sysctl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &iotkit_sysctl_vmstate;
    dc->reset = iotkit_sysctl_reset;
}

static const TypeInfo iotkit_sysctl_info = {
    .name = TYPE_IOTKIT_SYSCTL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IoTKitSysCtl),
    .instance_init = iotkit_sysctl_init,
    .class_init = iotkit_sysctl_class_init,
};

static void iotkit_sysctl_register_types(void)
{
    type_register_static(&iotkit_sysctl_info);
}

type_init(iotkit_sysctl_register_types);
