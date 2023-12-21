/*
 * ASPEED Real Time Clock
 * Joel Stanley <joel@jms.id.au>
 *
 * Copyright 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/rtc/aspeed_rtc.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "sysemu/rtc.h"

#include "trace.h"

#define COUNTER1        (0x00 / 4)
#define COUNTER2        (0x04 / 4)
#define ALARM           (0x08 / 4)
#define CONTROL         (0x10 / 4)
#define ALARM_STATUS    (0x14 / 4)

#define RTC_UNLOCKED    BIT(1)
#define RTC_ENABLED     BIT(0)

static void aspeed_rtc_calc_offset(AspeedRtcState *rtc)
{
    struct tm tm;
    uint32_t year, cent;
    uint32_t reg1 = rtc->reg[COUNTER1];
    uint32_t reg2 = rtc->reg[COUNTER2];

    tm.tm_mday = (reg1 >> 24) & 0x1f;
    tm.tm_hour = (reg1 >> 16) & 0x1f;
    tm.tm_min = (reg1 >> 8) & 0x3f;
    tm.tm_sec = (reg1 >> 0) & 0x3f;

    cent = (reg2 >> 16) & 0x1f;
    year = (reg2 >> 8) & 0x7f;
    tm.tm_mon = ((reg2 >>  0) & 0x0f) - 1;
    tm.tm_year = year + (cent * 100) - 1900;

    rtc->offset = qemu_timedate_diff(&tm);
}

static uint32_t aspeed_rtc_get_counter(AspeedRtcState *rtc, int r)
{
    uint32_t year, cent;
    struct tm now;

    qemu_get_timedate(&now, rtc->offset);

    switch (r) {
    case COUNTER1:
        return (now.tm_mday << 24) | (now.tm_hour << 16) |
            (now.tm_min << 8) | now.tm_sec;
    case COUNTER2:
        cent = (now.tm_year + 1900) / 100;
        year = now.tm_year % 100;
        return ((cent & 0x1f) << 16) | ((year & 0x7f) << 8) |
            ((now.tm_mon + 1) & 0xf);
    default:
        g_assert_not_reached();
    }
}

static uint64_t aspeed_rtc_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    AspeedRtcState *rtc = opaque;
    uint64_t val;
    uint32_t r = addr >> 2;

    switch (r) {
    case COUNTER1:
    case COUNTER2:
        if (rtc->reg[CONTROL] & RTC_ENABLED) {
            rtc->reg[r] = aspeed_rtc_get_counter(rtc, r);
        }
        /* fall through */
    case CONTROL:
        val = rtc->reg[r];
        break;
    case ALARM:
    case ALARM_STATUS:
    default:
        qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    trace_aspeed_rtc_read(addr, val);

    return val;
}

static void aspeed_rtc_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    AspeedRtcState *rtc = opaque;
    uint32_t r = addr >> 2;

    switch (r) {
    case COUNTER1:
    case COUNTER2:
        if (!(rtc->reg[CONTROL] & RTC_UNLOCKED)) {
            break;
        }
        /* fall through */
    case CONTROL:
        rtc->reg[r] = val;
        aspeed_rtc_calc_offset(rtc);
        break;
    case ALARM:
    case ALARM_STATUS:
    default:
        qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    trace_aspeed_rtc_write(addr, val);
}

static void aspeed_rtc_reset(DeviceState *d)
{
    AspeedRtcState *rtc = ASPEED_RTC(d);

    rtc->offset = 0;
    memset(rtc->reg, 0, sizeof(rtc->reg));
}

static const MemoryRegionOps aspeed_rtc_ops = {
    .read = aspeed_rtc_read,
    .write = aspeed_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_aspeed_rtc = {
    .name = TYPE_ASPEED_RTC,
    .version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, AspeedRtcState, 0x18),
        VMSTATE_INT64(offset, AspeedRtcState),
        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_rtc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedRtcState *s = ASPEED_RTC(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_rtc_ops, s,
                          "aspeed-rtc", 0x18ULL);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_rtc_realize;
    dc->vmsd = &vmstate_aspeed_rtc;
    dc->reset = aspeed_rtc_reset;
}

static const TypeInfo aspeed_rtc_info = {
    .name          = TYPE_ASPEED_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedRtcState),
    .class_init    = aspeed_rtc_class_init,
};

static void aspeed_rtc_register_types(void)
{
    type_register_static(&aspeed_rtc_info);
}

type_init(aspeed_rtc_register_types)
