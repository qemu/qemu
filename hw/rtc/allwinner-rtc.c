/*
 * Allwinner Real Time Clock emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/rtc/allwinner-rtc.h"
#include "system/rtc.h"
#include "trace.h"

/* RTC registers */
enum {
    REG_LOSC = 1,        /* Low Oscillator Control */
    REG_YYMMDD,          /* RTC Year-Month-Day */
    REG_HHMMSS,          /* RTC Hour-Minute-Second */
    REG_ALARM1_WKHHMMSS, /* Alarm1 Week Hour-Minute-Second */
    REG_ALARM1_EN,       /* Alarm1 Enable */
    REG_ALARM1_IRQ_EN,   /* Alarm1 IRQ Enable */
    REG_ALARM1_IRQ_STA,  /* Alarm1 IRQ Status */
    REG_GP0,             /* General Purpose Register 0 */
    REG_GP1,             /* General Purpose Register 1 */
    REG_GP2,             /* General Purpose Register 2 */
    REG_GP3,             /* General Purpose Register 3 */

    /* sun4i registers */
    REG_ALARM1_DDHHMMSS, /* Alarm1 Day Hour-Minute-Second */
    REG_CPUCFG,          /* CPU Configuration Register */

    /* sun6i registers */
    REG_LOSC_AUTOSTA,    /* LOSC Auto Switch Status */
    REG_INT_OSC_PRE,     /* Internal OSC Clock Prescaler */
    REG_ALARM0_COUNTER,  /* Alarm0 Counter */
    REG_ALARM0_CUR_VLU,  /* Alarm0 Counter Current Value */
    REG_ALARM0_ENABLE,   /* Alarm0 Enable */
    REG_ALARM0_IRQ_EN,   /* Alarm0 IRQ Enable */
    REG_ALARM0_IRQ_STA,  /* Alarm0 IRQ Status */
    REG_ALARM_CONFIG,    /* Alarm Config */
    REG_LOSC_OUT_GATING, /* LOSC Output Gating Register */
    REG_GP4,             /* General Purpose Register 4 */
    REG_GP5,             /* General Purpose Register 5 */
    REG_GP6,             /* General Purpose Register 6 */
    REG_GP7,             /* General Purpose Register 7 */
    REG_RTC_DBG,         /* RTC Debug Register */
    REG_GPL_HOLD_OUT,    /* GPL Hold Output Register */
    REG_VDD_RTC,         /* VDD RTC Regulate Register */
    REG_IC_CHARA,        /* IC Characteristics Register */
};

/* RTC register flags */
enum {
    REG_LOSC_YMD   = (1 << 7),
    REG_LOSC_HMS   = (1 << 8),
};

/* RTC sun4i register map (offset to name) */
const uint8_t allwinner_rtc_sun4i_regmap[] = {
    [0x0000] = REG_LOSC,
    [0x0004] = REG_YYMMDD,
    [0x0008] = REG_HHMMSS,
    [0x000C] = REG_ALARM1_DDHHMMSS,
    [0x0010] = REG_ALARM1_WKHHMMSS,
    [0x0014] = REG_ALARM1_EN,
    [0x0018] = REG_ALARM1_IRQ_EN,
    [0x001C] = REG_ALARM1_IRQ_STA,
    [0x0020] = REG_GP0,
    [0x0024] = REG_GP1,
    [0x0028] = REG_GP2,
    [0x002C] = REG_GP3,
    [0x003C] = REG_CPUCFG,
};

/* RTC sun6i register map (offset to name) */
const uint8_t allwinner_rtc_sun6i_regmap[] = {
    [0x0000] = REG_LOSC,
    [0x0004] = REG_LOSC_AUTOSTA,
    [0x0008] = REG_INT_OSC_PRE,
    [0x0010] = REG_YYMMDD,
    [0x0014] = REG_HHMMSS,
    [0x0020] = REG_ALARM0_COUNTER,
    [0x0024] = REG_ALARM0_CUR_VLU,
    [0x0028] = REG_ALARM0_ENABLE,
    [0x002C] = REG_ALARM0_IRQ_EN,
    [0x0030] = REG_ALARM0_IRQ_STA,
    [0x0040] = REG_ALARM1_WKHHMMSS,
    [0x0044] = REG_ALARM1_EN,
    [0x0048] = REG_ALARM1_IRQ_EN,
    [0x004C] = REG_ALARM1_IRQ_STA,
    [0x0050] = REG_ALARM_CONFIG,
    [0x0060] = REG_LOSC_OUT_GATING,
    [0x0100] = REG_GP0,
    [0x0104] = REG_GP1,
    [0x0108] = REG_GP2,
    [0x010C] = REG_GP3,
    [0x0110] = REG_GP4,
    [0x0114] = REG_GP5,
    [0x0118] = REG_GP6,
    [0x011C] = REG_GP7,
    [0x0170] = REG_RTC_DBG,
    [0x0180] = REG_GPL_HOLD_OUT,
    [0x0190] = REG_VDD_RTC,
    [0x01F0] = REG_IC_CHARA,
};

static bool allwinner_rtc_sun4i_read(AwRtcState *s, uint32_t offset)
{
    /* no sun4i specific registers currently implemented */
    return false;
}

static bool allwinner_rtc_sun4i_write(AwRtcState *s, uint32_t offset,
                                      uint32_t data)
{
    /* no sun4i specific registers currently implemented */
    return false;
}

static bool allwinner_rtc_sun6i_read(AwRtcState *s, uint32_t offset)
{
    const AwRtcClass *c = AW_RTC_GET_CLASS(s);

    switch (c->regmap[offset]) {
    case REG_GP4:             /* General Purpose Register 4 */
    case REG_GP5:             /* General Purpose Register 5 */
    case REG_GP6:             /* General Purpose Register 6 */
    case REG_GP7:             /* General Purpose Register 7 */
        return true;
    default:
        break;
    }
    return false;
}

static bool allwinner_rtc_sun6i_write(AwRtcState *s, uint32_t offset,
                                      uint32_t data)
{
    const AwRtcClass *c = AW_RTC_GET_CLASS(s);

    switch (c->regmap[offset]) {
    case REG_GP4:             /* General Purpose Register 4 */
    case REG_GP5:             /* General Purpose Register 5 */
    case REG_GP6:             /* General Purpose Register 6 */
    case REG_GP7:             /* General Purpose Register 7 */
        return true;
    default:
        break;
    }
    return false;
}

static uint64_t allwinner_rtc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    AwRtcState *s = AW_RTC(opaque);
    const AwRtcClass *c = AW_RTC_GET_CLASS(s);
    uint64_t val = 0;

    if (offset >= c->regmap_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    if (!c->regmap[offset]) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid register 0x%04x\n",
                          __func__, (uint32_t)offset);
        return 0;
    }

    switch (c->regmap[offset]) {
    case REG_LOSC:       /* Low Oscillator Control */
        val = s->regs[REG_LOSC];
        s->regs[REG_LOSC] &= ~(REG_LOSC_YMD | REG_LOSC_HMS);
        break;
    case REG_YYMMDD:     /* RTC Year-Month-Day */
    case REG_HHMMSS:     /* RTC Hour-Minute-Second */
    case REG_GP0:        /* General Purpose Register 0 */
    case REG_GP1:        /* General Purpose Register 1 */
    case REG_GP2:        /* General Purpose Register 2 */
    case REG_GP3:        /* General Purpose Register 3 */
        val = s->regs[c->regmap[offset]];
        break;
    default:
        if (!c->read(s, offset)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%04x\n",
                          __func__, (uint32_t)offset);
        }
        val = s->regs[c->regmap[offset]];
        break;
    }

    trace_allwinner_rtc_read(offset, val);
    return val;
}

static void allwinner_rtc_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    AwRtcState *s = AW_RTC(opaque);
    const AwRtcClass *c = AW_RTC_GET_CLASS(s);

    if (offset >= c->regmap_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    if (!c->regmap[offset]) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid register 0x%04x\n",
                          __func__, (uint32_t)offset);
        return;
    }

    trace_allwinner_rtc_write(offset, val);

    switch (c->regmap[offset]) {
    case REG_YYMMDD:     /* RTC Year-Month-Day */
        s->regs[REG_YYMMDD] = val;
        s->regs[REG_LOSC]  |= REG_LOSC_YMD;
        break;
    case REG_HHMMSS:     /* RTC Hour-Minute-Second */
        s->regs[REG_HHMMSS] = val;
        s->regs[REG_LOSC]  |= REG_LOSC_HMS;
        break;
    case REG_GP0:        /* General Purpose Register 0 */
    case REG_GP1:        /* General Purpose Register 1 */
    case REG_GP2:        /* General Purpose Register 2 */
    case REG_GP3:        /* General Purpose Register 3 */
        s->regs[c->regmap[offset]] = val;
        break;
    default:
        if (!c->write(s, offset, val)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%04x\n",
                          __func__, (uint32_t)offset);
        }
        break;
    }
}

static const MemoryRegionOps allwinner_rtc_ops = {
    .read = allwinner_rtc_read,
    .write = allwinner_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_rtc_reset(DeviceState *dev)
{
    AwRtcState *s = AW_RTC(dev);
    struct tm now;

    /* Clear registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Get current datetime */
    qemu_get_timedate(&now, 0);

    /* Set RTC with current datetime */
    if (s->base_year > 1900) {
        s->regs[REG_YYMMDD] =  ((now.tm_year + 1900 - s->base_year) << 16) |
                               ((now.tm_mon + 1) << 8) |
                                 now.tm_mday;
        s->regs[REG_HHMMSS] = (((now.tm_wday + 6) % 7) << 29) |
                                  (now.tm_hour << 16) |
                                  (now.tm_min << 8) |
                                   now.tm_sec;
    }
}

static void allwinner_rtc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwRtcState *s = AW_RTC(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_rtc_ops, s,
                          TYPE_AW_RTC, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_rtc_vmstate = {
    .name = "allwinner-rtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwRtcState, AW_RTC_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static const Property allwinner_rtc_properties[] = {
    DEFINE_PROP_INT32("base-year", AwRtcState, base_year, 0),
};

static void allwinner_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_rtc_reset);
    dc->vmsd = &allwinner_rtc_vmstate;
    device_class_set_props(dc, allwinner_rtc_properties);
}

static void allwinner_rtc_sun4i_init(Object *obj)
{
    AwRtcState *s = AW_RTC(obj);
    s->base_year = 2010;
}

static void allwinner_rtc_sun4i_class_init(ObjectClass *klass, void *data)
{
    AwRtcClass *arc = AW_RTC_CLASS(klass);

    arc->regmap = allwinner_rtc_sun4i_regmap;
    arc->regmap_size = sizeof(allwinner_rtc_sun4i_regmap);
    arc->read = allwinner_rtc_sun4i_read;
    arc->write = allwinner_rtc_sun4i_write;
}

static void allwinner_rtc_sun6i_init(Object *obj)
{
    AwRtcState *s = AW_RTC(obj);
    s->base_year = 1970;
}

static void allwinner_rtc_sun6i_class_init(ObjectClass *klass, void *data)
{
    AwRtcClass *arc = AW_RTC_CLASS(klass);

    arc->regmap = allwinner_rtc_sun6i_regmap;
    arc->regmap_size = sizeof(allwinner_rtc_sun6i_regmap);
    arc->read = allwinner_rtc_sun6i_read;
    arc->write = allwinner_rtc_sun6i_write;
}

static void allwinner_rtc_sun7i_init(Object *obj)
{
    AwRtcState *s = AW_RTC(obj);
    s->base_year = 1970;
}

static void allwinner_rtc_sun7i_class_init(ObjectClass *klass, void *data)
{
    AwRtcClass *arc = AW_RTC_CLASS(klass);
    allwinner_rtc_sun4i_class_init(klass, arc);
}

static const TypeInfo allwinner_rtc_info = {
    .name          = TYPE_AW_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_rtc_init,
    .instance_size = sizeof(AwRtcState),
    .class_init    = allwinner_rtc_class_init,
    .class_size    = sizeof(AwRtcClass),
    .abstract      = true,
};

static const TypeInfo allwinner_rtc_sun4i_info = {
    .name          = TYPE_AW_RTC_SUN4I,
    .parent        = TYPE_AW_RTC,
    .class_init    = allwinner_rtc_sun4i_class_init,
    .instance_init = allwinner_rtc_sun4i_init,
};

static const TypeInfo allwinner_rtc_sun6i_info = {
    .name          = TYPE_AW_RTC_SUN6I,
    .parent        = TYPE_AW_RTC,
    .class_init    = allwinner_rtc_sun6i_class_init,
    .instance_init = allwinner_rtc_sun6i_init,
};

static const TypeInfo allwinner_rtc_sun7i_info = {
    .name          = TYPE_AW_RTC_SUN7I,
    .parent        = TYPE_AW_RTC,
    .class_init    = allwinner_rtc_sun7i_class_init,
    .instance_init = allwinner_rtc_sun7i_init,
};

static void allwinner_rtc_register(void)
{
    type_register_static(&allwinner_rtc_info);
    type_register_static(&allwinner_rtc_sun4i_info);
    type_register_static(&allwinner_rtc_sun6i_info);
    type_register_static(&allwinner_rtc_sun7i_info);
}

type_init(allwinner_rtc_register)
