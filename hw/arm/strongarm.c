/*
 * StrongARM SA-1100/SA-1110 emulation
 *
 * Copyright (C) 2011 Dmitry Eremin-Solenikov
 *
 * Largely based on StrongARM emulation:
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * UART code based on QEMU 16550A UART emulation
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "strongarm.h"
#include "qemu/error-report.h"
#include "hw/arm/arm.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "hw/ssi/ssi.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

//#define DEBUG

/*
 TODO
 - Implement cp15, c14 ?
 - Implement cp15, c15 !!! (idle used in L)
 - Implement idle mode handling/DIM
 - Implement sleep mode/Wake sources
 - Implement reset control
 - Implement memory control regs
 - PCMCIA handling
 - Maybe support MBGNT/MBREQ
 - DMA channels
 - GPCLK
 - IrDA
 - MCP
 - Enhance UART with modem signals
 */

#ifdef DEBUG
# define DPRINTF(format, ...) printf(format , ## __VA_ARGS__)
#else
# define DPRINTF(format, ...) do { } while (0)
#endif

static struct {
    hwaddr io_base;
    int irq;
} sa_serial[] = {
    { 0x80010000, SA_PIC_UART1 },
    { 0x80030000, SA_PIC_UART2 },
    { 0x80050000, SA_PIC_UART3 },
    { 0, 0 }
};

/* Interrupt Controller */

#define TYPE_STRONGARM_PIC "strongarm_pic"
#define STRONGARM_PIC(obj) \
    OBJECT_CHECK(StrongARMPICState, (obj), TYPE_STRONGARM_PIC)

typedef struct StrongARMPICState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq    irq;
    qemu_irq    fiq;

    uint32_t pending;
    uint32_t enabled;
    uint32_t is_fiq;
    uint32_t int_idle;
} StrongARMPICState;

#define ICIP    0x00
#define ICMR    0x04
#define ICLR    0x08
#define ICFP    0x10
#define ICPR    0x20
#define ICCR    0x0c

#define SA_PIC_SRCS     32


static void strongarm_pic_update(void *opaque)
{
    StrongARMPICState *s = opaque;

    /* FIXME: reflect DIM */
    qemu_set_irq(s->fiq, s->pending & s->enabled &  s->is_fiq);
    qemu_set_irq(s->irq, s->pending & s->enabled & ~s->is_fiq);
}

static void strongarm_pic_set_irq(void *opaque, int irq, int level)
{
    StrongARMPICState *s = opaque;

    if (level) {
        s->pending |= 1 << irq;
    } else {
        s->pending &= ~(1 << irq);
    }

    strongarm_pic_update(s);
}

static uint64_t strongarm_pic_mem_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    StrongARMPICState *s = opaque;

    switch (offset) {
    case ICIP:
        return s->pending & ~s->is_fiq & s->enabled;
    case ICMR:
        return s->enabled;
    case ICLR:
        return s->is_fiq;
    case ICCR:
        return s->int_idle == 0;
    case ICFP:
        return s->pending & s->is_fiq & s->enabled;
    case ICPR:
        return s->pending;
    default:
        printf("%s: Bad register offset 0x" TARGET_FMT_plx "\n",
                        __func__, offset);
        return 0;
    }
}

static void strongarm_pic_mem_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    StrongARMPICState *s = opaque;

    switch (offset) {
    case ICMR:
        s->enabled = value;
        break;
    case ICLR:
        s->is_fiq = value;
        break;
    case ICCR:
        s->int_idle = (value & 1) ? 0 : ~0;
        break;
    default:
        printf("%s: Bad register offset 0x" TARGET_FMT_plx "\n",
                        __func__, offset);
        break;
    }
    strongarm_pic_update(s);
}

static const MemoryRegionOps strongarm_pic_ops = {
    .read = strongarm_pic_mem_read,
    .write = strongarm_pic_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void strongarm_pic_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    StrongARMPICState *s = STRONGARM_PIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, strongarm_pic_set_irq, SA_PIC_SRCS);
    memory_region_init_io(&s->iomem, obj, &strongarm_pic_ops, s,
                          "pic", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
}

static int strongarm_pic_post_load(void *opaque, int version_id)
{
    strongarm_pic_update(opaque);
    return 0;
}

static VMStateDescription vmstate_strongarm_pic_regs = {
    .name = "strongarm_pic",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = strongarm_pic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(pending, StrongARMPICState),
        VMSTATE_UINT32(enabled, StrongARMPICState),
        VMSTATE_UINT32(is_fiq, StrongARMPICState),
        VMSTATE_UINT32(int_idle, StrongARMPICState),
        VMSTATE_END_OF_LIST(),
    },
};

static void strongarm_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "StrongARM PIC";
    dc->vmsd = &vmstate_strongarm_pic_regs;
}

static const TypeInfo strongarm_pic_info = {
    .name          = TYPE_STRONGARM_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMPICState),
    .instance_init = strongarm_pic_initfn,
    .class_init    = strongarm_pic_class_init,
};

/* Real-Time Clock */
#define RTAR 0x00 /* RTC Alarm register */
#define RCNR 0x04 /* RTC Counter register */
#define RTTR 0x08 /* RTC Timer Trim register */
#define RTSR 0x10 /* RTC Status register */

#define RTSR_AL (1 << 0) /* RTC Alarm detected */
#define RTSR_HZ (1 << 1) /* RTC 1Hz detected */
#define RTSR_ALE (1 << 2) /* RTC Alarm enable */
#define RTSR_HZE (1 << 3) /* RTC 1Hz enable */

/* 16 LSB of RTTR are clockdiv for internal trim logic,
 * trim delete isn't emulated, so
 * f = 32 768 / (RTTR_trim + 1) */

#define TYPE_STRONGARM_RTC "strongarm-rtc"
#define STRONGARM_RTC(obj) \
    OBJECT_CHECK(StrongARMRTCState, (obj), TYPE_STRONGARM_RTC)

typedef struct StrongARMRTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t rttr;
    uint32_t rtsr;
    uint32_t rtar;
    uint32_t last_rcnr;
    int64_t last_hz;
    QEMUTimer *rtc_alarm;
    QEMUTimer *rtc_hz;
    qemu_irq rtc_irq;
    qemu_irq rtc_hz_irq;
} StrongARMRTCState;

static inline void strongarm_rtc_int_update(StrongARMRTCState *s)
{
    qemu_set_irq(s->rtc_irq, s->rtsr & RTSR_AL);
    qemu_set_irq(s->rtc_hz_irq, s->rtsr & RTSR_HZ);
}

static void strongarm_rtc_hzupdate(StrongARMRTCState *s)
{
    int64_t rt = qemu_clock_get_ms(rtc_clock);
    s->last_rcnr += ((rt - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    s->last_hz = rt;
}

static inline void strongarm_rtc_timer_update(StrongARMRTCState *s)
{
    if ((s->rtsr & RTSR_HZE) && !(s->rtsr & RTSR_HZ)) {
        timer_mod(s->rtc_hz, s->last_hz + 1000);
    } else {
        timer_del(s->rtc_hz);
    }

    if ((s->rtsr & RTSR_ALE) && !(s->rtsr & RTSR_AL)) {
        timer_mod(s->rtc_alarm, s->last_hz +
                (((s->rtar - s->last_rcnr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15));
    } else {
        timer_del(s->rtc_alarm);
    }
}

static inline void strongarm_rtc_alarm_tick(void *opaque)
{
    StrongARMRTCState *s = opaque;
    s->rtsr |= RTSR_AL;
    strongarm_rtc_timer_update(s);
    strongarm_rtc_int_update(s);
}

static inline void strongarm_rtc_hz_tick(void *opaque)
{
    StrongARMRTCState *s = opaque;
    s->rtsr |= RTSR_HZ;
    strongarm_rtc_timer_update(s);
    strongarm_rtc_int_update(s);
}

static uint64_t strongarm_rtc_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    StrongARMRTCState *s = opaque;

    switch (addr) {
    case RTTR:
        return s->rttr;
    case RTSR:
        return s->rtsr;
    case RTAR:
        return s->rtar;
    case RCNR:
        return s->last_rcnr +
                ((qemu_clock_get_ms(rtc_clock) - s->last_hz) << 15) /
                (1000 * ((s->rttr & 0xffff) + 1));
    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
        return 0;
    }
}

static void strongarm_rtc_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    StrongARMRTCState *s = opaque;
    uint32_t old_rtsr;

    switch (addr) {
    case RTTR:
        strongarm_rtc_hzupdate(s);
        s->rttr = value;
        strongarm_rtc_timer_update(s);
        break;

    case RTSR:
        old_rtsr = s->rtsr;
        s->rtsr = (value & (RTSR_ALE | RTSR_HZE)) |
                  (s->rtsr & ~(value & (RTSR_AL | RTSR_HZ)));

        if (s->rtsr != old_rtsr) {
            strongarm_rtc_timer_update(s);
        }

        strongarm_rtc_int_update(s);
        break;

    case RTAR:
        s->rtar = value;
        strongarm_rtc_timer_update(s);
        break;

    case RCNR:
        strongarm_rtc_hzupdate(s);
        s->last_rcnr = value;
        strongarm_rtc_timer_update(s);
        break;

    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
    }
}

static const MemoryRegionOps strongarm_rtc_ops = {
    .read = strongarm_rtc_read,
    .write = strongarm_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void strongarm_rtc_init(Object *obj)
{
    StrongARMRTCState *s = STRONGARM_RTC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    struct tm tm;

    s->rttr = 0x0;
    s->rtsr = 0;

    qemu_get_timedate(&tm, 0);

    s->last_rcnr = (uint32_t) mktimegm(&tm);
    s->last_hz = qemu_clock_get_ms(rtc_clock);

    s->rtc_alarm = timer_new_ms(rtc_clock, strongarm_rtc_alarm_tick, s);
    s->rtc_hz = timer_new_ms(rtc_clock, strongarm_rtc_hz_tick, s);

    sysbus_init_irq(dev, &s->rtc_irq);
    sysbus_init_irq(dev, &s->rtc_hz_irq);

    memory_region_init_io(&s->iomem, obj, &strongarm_rtc_ops, s,
                          "rtc", 0x10000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void strongarm_rtc_pre_save(void *opaque)
{
    StrongARMRTCState *s = opaque;

    strongarm_rtc_hzupdate(s);
}

static int strongarm_rtc_post_load(void *opaque, int version_id)
{
    StrongARMRTCState *s = opaque;

    strongarm_rtc_timer_update(s);
    strongarm_rtc_int_update(s);

    return 0;
}

static const VMStateDescription vmstate_strongarm_rtc_regs = {
    .name = "strongarm-rtc",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = strongarm_rtc_pre_save,
    .post_load = strongarm_rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rttr, StrongARMRTCState),
        VMSTATE_UINT32(rtsr, StrongARMRTCState),
        VMSTATE_UINT32(rtar, StrongARMRTCState),
        VMSTATE_UINT32(last_rcnr, StrongARMRTCState),
        VMSTATE_INT64(last_hz, StrongARMRTCState),
        VMSTATE_END_OF_LIST(),
    },
};

static void strongarm_rtc_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "StrongARM RTC Controller";
    dc->vmsd = &vmstate_strongarm_rtc_regs;
}

static const TypeInfo strongarm_rtc_sysbus_info = {
    .name          = TYPE_STRONGARM_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMRTCState),
    .instance_init = strongarm_rtc_init,
    .class_init    = strongarm_rtc_sysbus_class_init,
};

/* GPIO */
#define GPLR 0x00
#define GPDR 0x04
#define GPSR 0x08
#define GPCR 0x0c
#define GRER 0x10
#define GFER 0x14
#define GEDR 0x18
#define GAFR 0x1c

#define TYPE_STRONGARM_GPIO "strongarm-gpio"
#define STRONGARM_GPIO(obj) \
    OBJECT_CHECK(StrongARMGPIOInfo, (obj), TYPE_STRONGARM_GPIO)

typedef struct StrongARMGPIOInfo StrongARMGPIOInfo;
struct StrongARMGPIOInfo {
    SysBusDevice busdev;
    MemoryRegion iomem;
    qemu_irq handler[28];
    qemu_irq irqs[11];
    qemu_irq irqX;

    uint32_t ilevel;
    uint32_t olevel;
    uint32_t dir;
    uint32_t rising;
    uint32_t falling;
    uint32_t status;
    uint32_t gafr;

    uint32_t prev_level;
};


static void strongarm_gpio_irq_update(StrongARMGPIOInfo *s)
{
    int i;
    for (i = 0; i < 11; i++) {
        qemu_set_irq(s->irqs[i], s->status & (1 << i));
    }

    qemu_set_irq(s->irqX, (s->status & ~0x7ff));
}

static void strongarm_gpio_set(void *opaque, int line, int level)
{
    StrongARMGPIOInfo *s = opaque;
    uint32_t mask;

    mask = 1 << line;

    if (level) {
        s->status |= s->rising & mask &
                ~s->ilevel & ~s->dir;
        s->ilevel |= mask;
    } else {
        s->status |= s->falling & mask &
                s->ilevel & ~s->dir;
        s->ilevel &= ~mask;
    }

    if (s->status & mask) {
        strongarm_gpio_irq_update(s);
    }
}

static void strongarm_gpio_handler_update(StrongARMGPIOInfo *s)
{
    uint32_t level, diff;
    int bit;

    level = s->olevel & s->dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ctz32(diff);
        qemu_set_irq(s->handler[bit], (level >> bit) & 1);
    }

    s->prev_level = level;
}

static uint64_t strongarm_gpio_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    StrongARMGPIOInfo *s = opaque;

    switch (offset) {
    case GPDR:        /* GPIO Pin-Direction registers */
        return s->dir;

    case GPSR:        /* GPIO Pin-Output Set registers */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "strongarm GPIO: read from write only register GPSR\n");
        return 0;

    case GPCR:        /* GPIO Pin-Output Clear registers */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "strongarm GPIO: read from write only register GPCR\n");
        return 0;

    case GRER:        /* GPIO Rising-Edge Detect Enable registers */
        return s->rising;

    case GFER:        /* GPIO Falling-Edge Detect Enable registers */
        return s->falling;

    case GAFR:        /* GPIO Alternate Function registers */
        return s->gafr;

    case GPLR:        /* GPIO Pin-Level registers */
        return (s->olevel & s->dir) |
               (s->ilevel & ~s->dir);

    case GEDR:        /* GPIO Edge Detect Status registers */
        return s->status;

    default:
        printf("%s: Bad offset 0x" TARGET_FMT_plx "\n", __func__, offset);
    }

    return 0;
}

static void strongarm_gpio_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    StrongARMGPIOInfo *s = opaque;

    switch (offset) {
    case GPDR:        /* GPIO Pin-Direction registers */
        s->dir = value;
        strongarm_gpio_handler_update(s);
        break;

    case GPSR:        /* GPIO Pin-Output Set registers */
        s->olevel |= value;
        strongarm_gpio_handler_update(s);
        break;

    case GPCR:        /* GPIO Pin-Output Clear registers */
        s->olevel &= ~value;
        strongarm_gpio_handler_update(s);
        break;

    case GRER:        /* GPIO Rising-Edge Detect Enable registers */
        s->rising = value;
        break;

    case GFER:        /* GPIO Falling-Edge Detect Enable registers */
        s->falling = value;
        break;

    case GAFR:        /* GPIO Alternate Function registers */
        s->gafr = value;
        break;

    case GEDR:        /* GPIO Edge Detect Status registers */
        s->status &= ~value;
        strongarm_gpio_irq_update(s);
        break;

    default:
        printf("%s: Bad offset 0x" TARGET_FMT_plx "\n", __func__, offset);
    }
}

static const MemoryRegionOps strongarm_gpio_ops = {
    .read = strongarm_gpio_read,
    .write = strongarm_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static DeviceState *strongarm_gpio_init(hwaddr base,
                DeviceState *pic)
{
    DeviceState *dev;
    int i;

    dev = qdev_create(NULL, TYPE_STRONGARM_GPIO);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    for (i = 0; i < 12; i++)
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                    qdev_get_gpio_in(pic, SA_PIC_GPIO0_EDGE + i));

    return dev;
}

static void strongarm_gpio_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    StrongARMGPIOInfo *s = STRONGARM_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    qdev_init_gpio_in(dev, strongarm_gpio_set, 28);
    qdev_init_gpio_out(dev, s->handler, 28);

    memory_region_init_io(&s->iomem, obj, &strongarm_gpio_ops, s,
                          "gpio", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
    for (i = 0; i < 11; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
    sysbus_init_irq(sbd, &s->irqX);
}

static const VMStateDescription vmstate_strongarm_gpio_regs = {
    .name = "strongarm-gpio",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ilevel, StrongARMGPIOInfo),
        VMSTATE_UINT32(olevel, StrongARMGPIOInfo),
        VMSTATE_UINT32(dir, StrongARMGPIOInfo),
        VMSTATE_UINT32(rising, StrongARMGPIOInfo),
        VMSTATE_UINT32(falling, StrongARMGPIOInfo),
        VMSTATE_UINT32(status, StrongARMGPIOInfo),
        VMSTATE_UINT32(gafr, StrongARMGPIOInfo),
        VMSTATE_UINT32(prev_level, StrongARMGPIOInfo),
        VMSTATE_END_OF_LIST(),
    },
};

static void strongarm_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "StrongARM GPIO controller";
    dc->vmsd = &vmstate_strongarm_gpio_regs;
}

static const TypeInfo strongarm_gpio_info = {
    .name          = TYPE_STRONGARM_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMGPIOInfo),
    .instance_init = strongarm_gpio_initfn,
    .class_init    = strongarm_gpio_class_init,
};

/* Peripheral Pin Controller */
#define PPDR 0x00
#define PPSR 0x04
#define PPAR 0x08
#define PSDR 0x0c
#define PPFR 0x10

#define TYPE_STRONGARM_PPC "strongarm-ppc"
#define STRONGARM_PPC(obj) \
    OBJECT_CHECK(StrongARMPPCInfo, (obj), TYPE_STRONGARM_PPC)

typedef struct StrongARMPPCInfo StrongARMPPCInfo;
struct StrongARMPPCInfo {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq handler[28];

    uint32_t ilevel;
    uint32_t olevel;
    uint32_t dir;
    uint32_t ppar;
    uint32_t psdr;
    uint32_t ppfr;

    uint32_t prev_level;
};

static void strongarm_ppc_set(void *opaque, int line, int level)
{
    StrongARMPPCInfo *s = opaque;

    if (level) {
        s->ilevel |= 1 << line;
    } else {
        s->ilevel &= ~(1 << line);
    }
}

static void strongarm_ppc_handler_update(StrongARMPPCInfo *s)
{
    uint32_t level, diff;
    int bit;

    level = s->olevel & s->dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ctz32(diff);
        qemu_set_irq(s->handler[bit], (level >> bit) & 1);
    }

    s->prev_level = level;
}

static uint64_t strongarm_ppc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    StrongARMPPCInfo *s = opaque;

    switch (offset) {
    case PPDR:        /* PPC Pin Direction registers */
        return s->dir | ~0x3fffff;

    case PPSR:        /* PPC Pin State registers */
        return (s->olevel & s->dir) |
               (s->ilevel & ~s->dir) |
               ~0x3fffff;

    case PPAR:
        return s->ppar | ~0x41000;

    case PSDR:
        return s->psdr;

    case PPFR:
        return s->ppfr | ~0x7f001;

    default:
        printf("%s: Bad offset 0x" TARGET_FMT_plx "\n", __func__, offset);
    }

    return 0;
}

static void strongarm_ppc_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    StrongARMPPCInfo *s = opaque;

    switch (offset) {
    case PPDR:        /* PPC Pin Direction registers */
        s->dir = value & 0x3fffff;
        strongarm_ppc_handler_update(s);
        break;

    case PPSR:        /* PPC Pin State registers */
        s->olevel = value & s->dir & 0x3fffff;
        strongarm_ppc_handler_update(s);
        break;

    case PPAR:
        s->ppar = value & 0x41000;
        break;

    case PSDR:
        s->psdr = value & 0x3fffff;
        break;

    case PPFR:
        s->ppfr = value & 0x7f001;
        break;

    default:
        printf("%s: Bad offset 0x" TARGET_FMT_plx "\n", __func__, offset);
    }
}

static const MemoryRegionOps strongarm_ppc_ops = {
    .read = strongarm_ppc_read,
    .write = strongarm_ppc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void strongarm_ppc_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    StrongARMPPCInfo *s = STRONGARM_PPC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, strongarm_ppc_set, 22);
    qdev_init_gpio_out(dev, s->handler, 22);

    memory_region_init_io(&s->iomem, obj, &strongarm_ppc_ops, s,
                          "ppc", 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_strongarm_ppc_regs = {
    .name = "strongarm-ppc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ilevel, StrongARMPPCInfo),
        VMSTATE_UINT32(olevel, StrongARMPPCInfo),
        VMSTATE_UINT32(dir, StrongARMPPCInfo),
        VMSTATE_UINT32(ppar, StrongARMPPCInfo),
        VMSTATE_UINT32(psdr, StrongARMPPCInfo),
        VMSTATE_UINT32(ppfr, StrongARMPPCInfo),
        VMSTATE_UINT32(prev_level, StrongARMPPCInfo),
        VMSTATE_END_OF_LIST(),
    },
};

static void strongarm_ppc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "StrongARM PPC controller";
    dc->vmsd = &vmstate_strongarm_ppc_regs;
}

static const TypeInfo strongarm_ppc_info = {
    .name          = TYPE_STRONGARM_PPC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMPPCInfo),
    .instance_init = strongarm_ppc_init,
    .class_init    = strongarm_ppc_class_init,
};

/* UART Ports */
#define UTCR0 0x00
#define UTCR1 0x04
#define UTCR2 0x08
#define UTCR3 0x0c
#define UTDR  0x14
#define UTSR0 0x1c
#define UTSR1 0x20

#define UTCR0_PE  (1 << 0) /* Parity enable */
#define UTCR0_OES (1 << 1) /* Even parity */
#define UTCR0_SBS (1 << 2) /* 2 stop bits */
#define UTCR0_DSS (1 << 3) /* 8-bit data */

#define UTCR3_RXE (1 << 0) /* Rx enable */
#define UTCR3_TXE (1 << 1) /* Tx enable */
#define UTCR3_BRK (1 << 2) /* Force Break */
#define UTCR3_RIE (1 << 3) /* Rx int enable */
#define UTCR3_TIE (1 << 4) /* Tx int enable */
#define UTCR3_LBM (1 << 5) /* Loopback */

#define UTSR0_TFS (1 << 0) /* Tx FIFO nearly empty */
#define UTSR0_RFS (1 << 1) /* Rx FIFO nearly full */
#define UTSR0_RID (1 << 2) /* Receiver Idle */
#define UTSR0_RBB (1 << 3) /* Receiver begin break */
#define UTSR0_REB (1 << 4) /* Receiver end break */
#define UTSR0_EIF (1 << 5) /* Error in FIFO */

#define UTSR1_RNE (1 << 1) /* Receive FIFO not empty */
#define UTSR1_TNF (1 << 2) /* Transmit FIFO not full */
#define UTSR1_PRE (1 << 3) /* Parity error */
#define UTSR1_FRE (1 << 4) /* Frame error */
#define UTSR1_ROR (1 << 5) /* Receive Over Run */

#define RX_FIFO_PRE (1 << 8)
#define RX_FIFO_FRE (1 << 9)
#define RX_FIFO_ROR (1 << 10)

#define TYPE_STRONGARM_UART "strongarm-uart"
#define STRONGARM_UART(obj) \
    OBJECT_CHECK(StrongARMUARTState, (obj), TYPE_STRONGARM_UART)

typedef struct StrongARMUARTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CharDriverState *chr;
    qemu_irq irq;

    uint8_t utcr0;
    uint16_t brd;
    uint8_t utcr3;
    uint8_t utsr0;
    uint8_t utsr1;

    uint8_t tx_fifo[8];
    uint8_t tx_start;
    uint8_t tx_len;
    uint16_t rx_fifo[12]; /* value + error flags in high bits */
    uint8_t rx_start;
    uint8_t rx_len;

    uint64_t char_transmit_time; /* time to transmit a char in ticks*/
    bool wait_break_end;
    QEMUTimer *rx_timeout_timer;
    QEMUTimer *tx_timer;
} StrongARMUARTState;

static void strongarm_uart_update_status(StrongARMUARTState *s)
{
    uint16_t utsr1 = 0;

    if (s->tx_len != 8) {
        utsr1 |= UTSR1_TNF;
    }

    if (s->rx_len != 0) {
        uint16_t ent = s->rx_fifo[s->rx_start];

        utsr1 |= UTSR1_RNE;
        if (ent & RX_FIFO_PRE) {
            s->utsr1 |= UTSR1_PRE;
        }
        if (ent & RX_FIFO_FRE) {
            s->utsr1 |= UTSR1_FRE;
        }
        if (ent & RX_FIFO_ROR) {
            s->utsr1 |= UTSR1_ROR;
        }
    }

    s->utsr1 = utsr1;
}

static void strongarm_uart_update_int_status(StrongARMUARTState *s)
{
    uint16_t utsr0 = s->utsr0 &
            (UTSR0_REB | UTSR0_RBB | UTSR0_RID);
    int i;

    if ((s->utcr3 & UTCR3_TXE) &&
                (s->utcr3 & UTCR3_TIE) &&
                s->tx_len <= 4) {
        utsr0 |= UTSR0_TFS;
    }

    if ((s->utcr3 & UTCR3_RXE) &&
                (s->utcr3 & UTCR3_RIE) &&
                s->rx_len > 4) {
        utsr0 |= UTSR0_RFS;
    }

    for (i = 0; i < s->rx_len && i < 4; i++)
        if (s->rx_fifo[(s->rx_start + i) % 12] & ~0xff) {
            utsr0 |= UTSR0_EIF;
            break;
        }

    s->utsr0 = utsr0;
    qemu_set_irq(s->irq, utsr0);
}

static void strongarm_uart_update_parameters(StrongARMUARTState *s)
{
    int speed, parity, data_bits, stop_bits, frame_size;
    QEMUSerialSetParams ssp;

    /* Start bit. */
    frame_size = 1;
    if (s->utcr0 & UTCR0_PE) {
        /* Parity bit. */
        frame_size++;
        if (s->utcr0 & UTCR0_OES) {
            parity = 'E';
        } else {
            parity = 'O';
        }
    } else {
            parity = 'N';
    }
    if (s->utcr0 & UTCR0_SBS) {
        stop_bits = 2;
    } else {
        stop_bits = 1;
    }

    data_bits = (s->utcr0 & UTCR0_DSS) ? 8 : 7;
    frame_size += data_bits + stop_bits;
    speed = 3686400 / 16 / (s->brd + 1);
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    s->char_transmit_time =  (NANOSECONDS_PER_SECOND / speed) * frame_size;
    if (s->chr) {
        qemu_chr_fe_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    }

    DPRINTF(stderr, "%s speed=%d parity=%c data=%d stop=%d\n", s->chr->label,
            speed, parity, data_bits, stop_bits);
}

static void strongarm_uart_rx_to(void *opaque)
{
    StrongARMUARTState *s = opaque;

    if (s->rx_len) {
        s->utsr0 |= UTSR0_RID;
        strongarm_uart_update_int_status(s);
    }
}

static void strongarm_uart_rx_push(StrongARMUARTState *s, uint16_t c)
{
    if ((s->utcr3 & UTCR3_RXE) == 0) {
        /* rx disabled */
        return;
    }

    if (s->wait_break_end) {
        s->utsr0 |= UTSR0_REB;
        s->wait_break_end = false;
    }

    if (s->rx_len < 12) {
        s->rx_fifo[(s->rx_start + s->rx_len) % 12] = c;
        s->rx_len++;
    } else
        s->rx_fifo[(s->rx_start + 11) % 12] |= RX_FIFO_ROR;
}

static int strongarm_uart_can_receive(void *opaque)
{
    StrongARMUARTState *s = opaque;

    if (s->rx_len == 12) {
        return 0;
    }
    /* It's best not to get more than 2/3 of RX FIFO, so advertise that much */
    if (s->rx_len < 8) {
        return 8 - s->rx_len;
    }
    return 1;
}

static void strongarm_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    StrongARMUARTState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        strongarm_uart_rx_push(s, buf[i]);
    }

    /* call the timeout receive callback in 3 char transmit time */
    timer_mod(s->rx_timeout_timer,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->char_transmit_time * 3);

    strongarm_uart_update_status(s);
    strongarm_uart_update_int_status(s);
}

static void strongarm_uart_event(void *opaque, int event)
{
    StrongARMUARTState *s = opaque;
    if (event == CHR_EVENT_BREAK) {
        s->utsr0 |= UTSR0_RBB;
        strongarm_uart_rx_push(s, RX_FIFO_FRE);
        s->wait_break_end = true;
        strongarm_uart_update_status(s);
        strongarm_uart_update_int_status(s);
    }
}

static void strongarm_uart_tx(void *opaque)
{
    StrongARMUARTState *s = opaque;
    uint64_t new_xmit_ts = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (s->utcr3 & UTCR3_LBM) /* loopback */ {
        strongarm_uart_receive(s, &s->tx_fifo[s->tx_start], 1);
    } else if (s->chr) {
        qemu_chr_fe_write(s->chr, &s->tx_fifo[s->tx_start], 1);
    }

    s->tx_start = (s->tx_start + 1) % 8;
    s->tx_len--;
    if (s->tx_len) {
        timer_mod(s->tx_timer, new_xmit_ts + s->char_transmit_time);
    }
    strongarm_uart_update_status(s);
    strongarm_uart_update_int_status(s);
}

static uint64_t strongarm_uart_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    StrongARMUARTState *s = opaque;
    uint16_t ret;

    switch (addr) {
    case UTCR0:
        return s->utcr0;

    case UTCR1:
        return s->brd >> 8;

    case UTCR2:
        return s->brd & 0xff;

    case UTCR3:
        return s->utcr3;

    case UTDR:
        if (s->rx_len != 0) {
            ret = s->rx_fifo[s->rx_start];
            s->rx_start = (s->rx_start + 1) % 12;
            s->rx_len--;
            strongarm_uart_update_status(s);
            strongarm_uart_update_int_status(s);
            return ret;
        }
        return 0;

    case UTSR0:
        return s->utsr0;

    case UTSR1:
        return s->utsr1;

    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
        return 0;
    }
}

static void strongarm_uart_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    StrongARMUARTState *s = opaque;

    switch (addr) {
    case UTCR0:
        s->utcr0 = value & 0x7f;
        strongarm_uart_update_parameters(s);
        break;

    case UTCR1:
        s->brd = (s->brd & 0xff) | ((value & 0xf) << 8);
        strongarm_uart_update_parameters(s);
        break;

    case UTCR2:
        s->brd = (s->brd & 0xf00) | (value & 0xff);
        strongarm_uart_update_parameters(s);
        break;

    case UTCR3:
        s->utcr3 = value & 0x3f;
        if ((s->utcr3 & UTCR3_RXE) == 0) {
            s->rx_len = 0;
        }
        if ((s->utcr3 & UTCR3_TXE) == 0) {
            s->tx_len = 0;
        }
        strongarm_uart_update_status(s);
        strongarm_uart_update_int_status(s);
        break;

    case UTDR:
        if ((s->utcr3 & UTCR3_TXE) && s->tx_len != 8) {
            s->tx_fifo[(s->tx_start + s->tx_len) % 8] = value;
            s->tx_len++;
            strongarm_uart_update_status(s);
            strongarm_uart_update_int_status(s);
            if (s->tx_len == 1) {
                strongarm_uart_tx(s);
            }
        }
        break;

    case UTSR0:
        s->utsr0 = s->utsr0 & ~(value &
                (UTSR0_REB | UTSR0_RBB | UTSR0_RID));
        strongarm_uart_update_int_status(s);
        break;

    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
    }
}

static const MemoryRegionOps strongarm_uart_ops = {
    .read = strongarm_uart_read,
    .write = strongarm_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void strongarm_uart_init(Object *obj)
{
    StrongARMUARTState *s = STRONGARM_UART(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &strongarm_uart_ops, s,
                          "uart", 0x10000);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    s->rx_timeout_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, strongarm_uart_rx_to, s);
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, strongarm_uart_tx, s);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr,
                        strongarm_uart_can_receive,
                        strongarm_uart_receive,
                        strongarm_uart_event,
                        s);
    }
}

static void strongarm_uart_reset(DeviceState *dev)
{
    StrongARMUARTState *s = STRONGARM_UART(dev);

    s->utcr0 = UTCR0_DSS; /* 8 data, no parity */
    s->brd = 23;    /* 9600 */
    /* enable send & recv - this actually violates spec */
    s->utcr3 = UTCR3_TXE | UTCR3_RXE;

    s->rx_len = s->tx_len = 0;

    strongarm_uart_update_parameters(s);
    strongarm_uart_update_status(s);
    strongarm_uart_update_int_status(s);
}

static int strongarm_uart_post_load(void *opaque, int version_id)
{
    StrongARMUARTState *s = opaque;

    strongarm_uart_update_parameters(s);
    strongarm_uart_update_status(s);
    strongarm_uart_update_int_status(s);

    /* tx and restart timer */
    if (s->tx_len) {
        strongarm_uart_tx(s);
    }

    /* restart rx timeout timer */
    if (s->rx_len) {
        timer_mod(s->rx_timeout_timer,
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->char_transmit_time * 3);
    }

    return 0;
}

static const VMStateDescription vmstate_strongarm_uart_regs = {
    .name = "strongarm-uart",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = strongarm_uart_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(utcr0, StrongARMUARTState),
        VMSTATE_UINT16(brd, StrongARMUARTState),
        VMSTATE_UINT8(utcr3, StrongARMUARTState),
        VMSTATE_UINT8(utsr0, StrongARMUARTState),
        VMSTATE_UINT8_ARRAY(tx_fifo, StrongARMUARTState, 8),
        VMSTATE_UINT8(tx_start, StrongARMUARTState),
        VMSTATE_UINT8(tx_len, StrongARMUARTState),
        VMSTATE_UINT16_ARRAY(rx_fifo, StrongARMUARTState, 12),
        VMSTATE_UINT8(rx_start, StrongARMUARTState),
        VMSTATE_UINT8(rx_len, StrongARMUARTState),
        VMSTATE_BOOL(wait_break_end, StrongARMUARTState),
        VMSTATE_END_OF_LIST(),
    },
};

static Property strongarm_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", StrongARMUARTState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void strongarm_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "StrongARM UART controller";
    dc->reset = strongarm_uart_reset;
    dc->vmsd = &vmstate_strongarm_uart_regs;
    dc->props = strongarm_uart_properties;
}

static const TypeInfo strongarm_uart_info = {
    .name          = TYPE_STRONGARM_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMUARTState),
    .instance_init = strongarm_uart_init,
    .class_init    = strongarm_uart_class_init,
};

/* Synchronous Serial Ports */

#define TYPE_STRONGARM_SSP "strongarm-ssp"
#define STRONGARM_SSP(obj) \
    OBJECT_CHECK(StrongARMSSPState, (obj), TYPE_STRONGARM_SSP)

typedef struct StrongARMSSPState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    SSIBus *bus;

    uint16_t sscr[2];
    uint16_t sssr;

    uint16_t rx_fifo[8];
    uint8_t rx_level;
    uint8_t rx_start;
} StrongARMSSPState;

#define SSCR0 0x60 /* SSP Control register 0 */
#define SSCR1 0x64 /* SSP Control register 1 */
#define SSDR  0x6c /* SSP Data register */
#define SSSR  0x74 /* SSP Status register */

/* Bitfields for above registers */
#define SSCR0_SPI(x)    (((x) & 0x30) == 0x00)
#define SSCR0_SSP(x)    (((x) & 0x30) == 0x10)
#define SSCR0_UWIRE(x)  (((x) & 0x30) == 0x20)
#define SSCR0_PSP(x)    (((x) & 0x30) == 0x30)
#define SSCR0_SSE       (1 << 7)
#define SSCR0_DSS(x)    (((x) & 0xf) + 1)
#define SSCR1_RIE       (1 << 0)
#define SSCR1_TIE       (1 << 1)
#define SSCR1_LBM       (1 << 2)
#define SSSR_TNF        (1 << 2)
#define SSSR_RNE        (1 << 3)
#define SSSR_TFS        (1 << 5)
#define SSSR_RFS        (1 << 6)
#define SSSR_ROR        (1 << 7)
#define SSSR_RW         0x0080

static void strongarm_ssp_int_update(StrongARMSSPState *s)
{
    int level = 0;

    level |= (s->sssr & SSSR_ROR);
    level |= (s->sssr & SSSR_RFS)  &&  (s->sscr[1] & SSCR1_RIE);
    level |= (s->sssr & SSSR_TFS)  &&  (s->sscr[1] & SSCR1_TIE);
    qemu_set_irq(s->irq, level);
}

static void strongarm_ssp_fifo_update(StrongARMSSPState *s)
{
    s->sssr &= ~SSSR_TFS;
    s->sssr &= ~SSSR_TNF;
    if (s->sscr[0] & SSCR0_SSE) {
        if (s->rx_level >= 4) {
            s->sssr |= SSSR_RFS;
        } else {
            s->sssr &= ~SSSR_RFS;
        }
        if (s->rx_level) {
            s->sssr |= SSSR_RNE;
        } else {
            s->sssr &= ~SSSR_RNE;
        }
        /* TX FIFO is never filled, so it is always in underrun
           condition if SSP is enabled */
        s->sssr |= SSSR_TFS;
        s->sssr |= SSSR_TNF;
    }

    strongarm_ssp_int_update(s);
}

static uint64_t strongarm_ssp_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    StrongARMSSPState *s = opaque;
    uint32_t retval;

    switch (addr) {
    case SSCR0:
        return s->sscr[0];
    case SSCR1:
        return s->sscr[1];
    case SSSR:
        return s->sssr;
    case SSDR:
        if (~s->sscr[0] & SSCR0_SSE) {
            return 0xffffffff;
        }
        if (s->rx_level < 1) {
            printf("%s: SSP Rx Underrun\n", __func__);
            return 0xffffffff;
        }
        s->rx_level--;
        retval = s->rx_fifo[s->rx_start++];
        s->rx_start &= 0x7;
        strongarm_ssp_fifo_update(s);
        return retval;
    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
        break;
    }
    return 0;
}

static void strongarm_ssp_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    StrongARMSSPState *s = opaque;

    switch (addr) {
    case SSCR0:
        s->sscr[0] = value & 0xffbf;
        if ((s->sscr[0] & SSCR0_SSE) && SSCR0_DSS(value) < 4) {
            printf("%s: Wrong data size: %i bits\n", __func__,
                   (int)SSCR0_DSS(value));
        }
        if (!(value & SSCR0_SSE)) {
            s->sssr = 0;
            s->rx_level = 0;
        }
        strongarm_ssp_fifo_update(s);
        break;

    case SSCR1:
        s->sscr[1] = value & 0x2f;
        if (value & SSCR1_LBM) {
            printf("%s: Attempt to use SSP LBM mode\n", __func__);
        }
        strongarm_ssp_fifo_update(s);
        break;

    case SSSR:
        s->sssr &= ~(value & SSSR_RW);
        strongarm_ssp_int_update(s);
        break;

    case SSDR:
        if (SSCR0_UWIRE(s->sscr[0])) {
            value &= 0xff;
        } else
            /* Note how 32bits overflow does no harm here */
            value &= (1 << SSCR0_DSS(s->sscr[0])) - 1;

        /* Data goes from here to the Tx FIFO and is shifted out from
         * there directly to the slave, no need to buffer it.
         */
        if (s->sscr[0] & SSCR0_SSE) {
            uint32_t readval;
            if (s->sscr[1] & SSCR1_LBM) {
                readval = value;
            } else {
                readval = ssi_transfer(s->bus, value);
            }

            if (s->rx_level < 0x08) {
                s->rx_fifo[(s->rx_start + s->rx_level++) & 0x7] = readval;
            } else {
                s->sssr |= SSSR_ROR;
            }
        }
        strongarm_ssp_fifo_update(s);
        break;

    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps strongarm_ssp_ops = {
    .read = strongarm_ssp_read,
    .write = strongarm_ssp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int strongarm_ssp_post_load(void *opaque, int version_id)
{
    StrongARMSSPState *s = opaque;

    strongarm_ssp_fifo_update(s);

    return 0;
}

static int strongarm_ssp_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    StrongARMSSPState *s = STRONGARM_SSP(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &strongarm_ssp_ops, s,
                          "ssp", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    s->bus = ssi_create_bus(dev, "ssi");
    return 0;
}

static void strongarm_ssp_reset(DeviceState *dev)
{
    StrongARMSSPState *s = STRONGARM_SSP(dev);

    s->sssr = 0x03; /* 3 bit data, SPI, disabled */
    s->rx_start = 0;
    s->rx_level = 0;
}

static const VMStateDescription vmstate_strongarm_ssp_regs = {
    .name = "strongarm-ssp",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = strongarm_ssp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_ARRAY(sscr, StrongARMSSPState, 2),
        VMSTATE_UINT16(sssr, StrongARMSSPState),
        VMSTATE_UINT16_ARRAY(rx_fifo, StrongARMSSPState, 8),
        VMSTATE_UINT8(rx_start, StrongARMSSPState),
        VMSTATE_UINT8(rx_level, StrongARMSSPState),
        VMSTATE_END_OF_LIST(),
    },
};

static void strongarm_ssp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = strongarm_ssp_init;
    dc->desc = "StrongARM SSP controller";
    dc->reset = strongarm_ssp_reset;
    dc->vmsd = &vmstate_strongarm_ssp_regs;
}

static const TypeInfo strongarm_ssp_info = {
    .name          = TYPE_STRONGARM_SSP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StrongARMSSPState),
    .class_init    = strongarm_ssp_class_init,
};

/* Main CPU functions */
StrongARMState *sa1110_init(MemoryRegion *sysmem,
                            unsigned int sdram_size, const char *rev)
{
    StrongARMState *s;
    int i;

    s = g_new0(StrongARMState, 1);

    if (!rev) {
        rev = "sa1110-b5";
    }

    if (strncmp(rev, "sa1110", 6)) {
        error_report("Machine requires a SA1110 processor.");
        exit(1);
    }

    s->cpu = cpu_arm_init(rev);

    if (!s->cpu) {
        error_report("Unable to find CPU definition");
        exit(1);
    }

    memory_region_allocate_system_memory(&s->sdram, NULL, "strongarm.sdram",
                                         sdram_size);
    memory_region_add_subregion(sysmem, SA_SDCS0, &s->sdram);

    s->pic = sysbus_create_varargs("strongarm_pic", 0x90050000,
                    qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ),
                    qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ),
                    NULL);

    sysbus_create_varargs("pxa25x-timer", 0x90000000,
                    qdev_get_gpio_in(s->pic, SA_PIC_OSTC0),
                    qdev_get_gpio_in(s->pic, SA_PIC_OSTC1),
                    qdev_get_gpio_in(s->pic, SA_PIC_OSTC2),
                    qdev_get_gpio_in(s->pic, SA_PIC_OSTC3),
                    NULL);

    sysbus_create_simple(TYPE_STRONGARM_RTC, 0x90010000,
                    qdev_get_gpio_in(s->pic, SA_PIC_RTC_ALARM));

    s->gpio = strongarm_gpio_init(0x90040000, s->pic);

    s->ppc = sysbus_create_varargs(TYPE_STRONGARM_PPC, 0x90060000, NULL);

    for (i = 0; sa_serial[i].io_base; i++) {
        DeviceState *dev = qdev_create(NULL, TYPE_STRONGARM_UART);
        qdev_prop_set_chr(dev, "chardev", serial_hds[i]);
        qdev_init_nofail(dev);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                sa_serial[i].io_base);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                qdev_get_gpio_in(s->pic, sa_serial[i].irq));
    }

    s->ssp = sysbus_create_varargs(TYPE_STRONGARM_SSP, 0x80070000,
                qdev_get_gpio_in(s->pic, SA_PIC_SSP), NULL);
    s->ssp_bus = (SSIBus *)qdev_get_child_bus(s->ssp, "ssi");

    return s;
}

static void strongarm_register_types(void)
{
    type_register_static(&strongarm_pic_info);
    type_register_static(&strongarm_rtc_sysbus_info);
    type_register_static(&strongarm_gpio_info);
    type_register_static(&strongarm_ppc_info);
    type_register_static(&strongarm_uart_info);
    type_register_static(&strongarm_ssp_info);
}

type_init(strongarm_register_types)
