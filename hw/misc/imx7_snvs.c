/*
 * IMX7 Secure Non-Volatile Storage
 *
 * Copyright (c) 2018, Impinj, Inc.
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Bare minimum emulation code needed to support being able to shut
 * down linux guest gracefully.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/misc/imx7_snvs.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "sysemu/sysemu.h"
#include "sysemu/rtc.h"
#include "sysemu/runstate.h"
#include "trace.h"

#define RTC_FREQ    32768ULL

static const VMStateDescription vmstate_imx7_snvs = {
    .name = TYPE_IMX7_SNVS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(tick_offset, IMX7SNVSState),
        VMSTATE_UINT64(lpcr, IMX7SNVSState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t imx7_snvs_get_count(IMX7SNVSState *s)
{
    uint64_t ticks = muldiv64(qemu_clock_get_ns(rtc_clock), RTC_FREQ,
                              NANOSECONDS_PER_SECOND);
    return s->tick_offset + ticks;
}

static uint64_t imx7_snvs_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX7SNVSState *s = IMX7_SNVS(opaque);
    uint64_t ret = 0;

    switch (offset) {
    case SNVS_LPSRTCMR:
        ret = extract64(imx7_snvs_get_count(s), 32, 15);
        break;
    case SNVS_LPSRTCLR:
        ret = extract64(imx7_snvs_get_count(s), 0, 32);
        break;
    case SNVS_LPCR:
        ret = s->lpcr;
        break;
    }

    trace_imx7_snvs_read(offset, ret, size);

    return ret;
}

static void imx7_snvs_reset(DeviceState *dev)
{
    IMX7SNVSState *s = IMX7_SNVS(dev);

    s->lpcr = 0;
}

static void imx7_snvs_write(void *opaque, hwaddr offset,
                            uint64_t v, unsigned size)
{
    trace_imx7_snvs_write(offset, v, size);

    IMX7SNVSState *s = IMX7_SNVS(opaque);

    uint64_t new_value = 0, snvs_count = 0;

    if (offset == SNVS_LPSRTCMR || offset == SNVS_LPSRTCLR) {
        snvs_count = imx7_snvs_get_count(s);
    }

    switch (offset) {
    case SNVS_LPSRTCMR:
        new_value = deposit64(snvs_count, 32, 32, v);
        break;
    case SNVS_LPSRTCLR:
        new_value = deposit64(snvs_count, 0, 32, v);
        break;
    case SNVS_LPCR: {
        s->lpcr = v;

        const uint32_t mask  = SNVS_LPCR_TOP | SNVS_LPCR_DP_EN;

        if ((v & mask) == mask) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        break;
    }
    }

    if (offset == SNVS_LPSRTCMR || offset == SNVS_LPSRTCLR) {
        s->tick_offset += new_value - snvs_count;
    }
}

static const struct MemoryRegionOps imx7_snvs_ops = {
    .read = imx7_snvs_read,
    .write = imx7_snvs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx7_snvs_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX7SNVSState *s = IMX7_SNVS(obj);
    struct tm tm;

    memory_region_init_io(&s->mmio, obj, &imx7_snvs_ops, s,
                          TYPE_IMX7_SNVS, 0x1000);

    sysbus_init_mmio(sd, &s->mmio);

    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm) -
        qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;
}

static void imx7_snvs_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = imx7_snvs_reset;
    dc->vmsd = &vmstate_imx7_snvs;
    dc->desc  = "i.MX7 Secure Non-Volatile Storage Module";
}

static const TypeInfo imx7_snvs_info = {
    .name          = TYPE_IMX7_SNVS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX7SNVSState),
    .instance_init = imx7_snvs_init,
    .class_init    = imx7_snvs_class_init,
};

static void imx7_snvs_register_type(void)
{
    type_register_static(&imx7_snvs_info);
}
type_init(imx7_snvs_register_type)
