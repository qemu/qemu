/*
 * Intel XScale PXA255/270 OS Timers.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2006 Thorsten Zitterell
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "hw/arm/pxa.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define OSMR0	0x00
#define OSMR1	0x04
#define OSMR2	0x08
#define OSMR3	0x0c
#define OSMR4	0x80
#define OSMR5	0x84
#define OSMR6	0x88
#define OSMR7	0x8c
#define OSMR8	0x90
#define OSMR9	0x94
#define OSMR10	0x98
#define OSMR11	0x9c
#define OSCR	0x10	/* OS Timer Count */
#define OSCR4	0x40
#define OSCR5	0x44
#define OSCR6	0x48
#define OSCR7	0x4c
#define OSCR8	0x50
#define OSCR9	0x54
#define OSCR10	0x58
#define OSCR11	0x5c
#define OSSR	0x14	/* Timer status register */
#define OWER	0x18
#define OIER	0x1c	/* Interrupt enable register  3-0 to E3-E0 */
#define OMCR4	0xc0	/* OS Match Control registers */
#define OMCR5	0xc4
#define OMCR6	0xc8
#define OMCR7	0xcc
#define OMCR8	0xd0
#define OMCR9	0xd4
#define OMCR10	0xd8
#define OMCR11	0xdc
#define OSNR	0x20

#define PXA25X_FREQ	3686400	/* 3.6864 MHz */
#define PXA27X_FREQ	3250000	/* 3.25 MHz */

static int pxa2xx_timer4_freq[8] = {
    [0] = 0,
    [1] = 32768,
    [2] = 1000,
    [3] = 1,
    [4] = 1000000,
    /* [5] is the "Externally supplied clock".  Assign if necessary.  */
    [5 ... 7] = 0,
};

#define TYPE_PXA2XX_TIMER "pxa2xx-timer"
OBJECT_DECLARE_SIMPLE_TYPE(PXA2xxTimerInfo, PXA2XX_TIMER)


typedef struct {
    uint32_t value;
    qemu_irq irq;
    QEMUTimer *qtimer;
    int num;
    PXA2xxTimerInfo *info;
} PXA2xxTimer0;

typedef struct {
    PXA2xxTimer0 tm;
    int32_t oldclock;
    int32_t clock;
    uint64_t lastload;
    uint32_t freq;
    uint32_t control;
} PXA2xxTimer4;

struct PXA2xxTimerInfo {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t flags;

    int32_t clock;
    int32_t oldclock;
    uint64_t lastload;
    uint32_t freq;
    PXA2xxTimer0 timer[4];
    uint32_t events;
    uint32_t irq_enabled;
    uint32_t reset3;
    uint32_t snapshot;

    qemu_irq irq4;
    PXA2xxTimer4 tm4[8];
};

#define PXA2XX_TIMER_HAVE_TM4	0

static inline int pxa2xx_timer_has_tm4(PXA2xxTimerInfo *s)
{
    return s->flags & (1 << PXA2XX_TIMER_HAVE_TM4);
}

static void pxa2xx_timer_update(void *opaque, uint64_t now_qemu)
{
    PXA2xxTimerInfo *s = (PXA2xxTimerInfo *) opaque;
    int i;
    uint32_t now_vm;
    uint64_t new_qemu;

    now_vm = s->clock +
            muldiv64(now_qemu - s->lastload, s->freq, NANOSECONDS_PER_SECOND);

    for (i = 0; i < 4; i ++) {
        new_qemu = now_qemu + muldiv64((uint32_t) (s->timer[i].value - now_vm),
                        NANOSECONDS_PER_SECOND, s->freq);
        timer_mod(s->timer[i].qtimer, new_qemu);
    }
}

static void pxa2xx_timer_update4(void *opaque, uint64_t now_qemu, int n)
{
    PXA2xxTimerInfo *s = (PXA2xxTimerInfo *) opaque;
    uint32_t now_vm;
    uint64_t new_qemu;
    static const int counters[8] = { 0, 0, 0, 0, 4, 4, 6, 6 };
    int counter;

    assert(n < ARRAY_SIZE(counters));
    if (s->tm4[n].control & (1 << 7))
        counter = n;
    else
        counter = counters[n];

    if (!s->tm4[counter].freq) {
        timer_del(s->tm4[n].tm.qtimer);
        return;
    }

    now_vm = s->tm4[counter].clock + muldiv64(now_qemu -
                    s->tm4[counter].lastload,
                    s->tm4[counter].freq, NANOSECONDS_PER_SECOND);

    new_qemu = now_qemu + muldiv64((uint32_t) (s->tm4[n].tm.value - now_vm),
                    NANOSECONDS_PER_SECOND, s->tm4[counter].freq);
    timer_mod(s->tm4[n].tm.qtimer, new_qemu);
}

static uint64_t pxa2xx_timer_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    PXA2xxTimerInfo *s = (PXA2xxTimerInfo *) opaque;
    int tm = 0;

    switch (offset) {
    case OSMR3:  tm ++;
        /* fall through */
    case OSMR2:  tm ++;
        /* fall through */
    case OSMR1:  tm ++;
        /* fall through */
    case OSMR0:
        return s->timer[tm].value;
    case OSMR11: tm ++;
        /* fall through */
    case OSMR10: tm ++;
        /* fall through */
    case OSMR9:  tm ++;
        /* fall through */
    case OSMR8:  tm ++;
        /* fall through */
    case OSMR7:  tm ++;
        /* fall through */
    case OSMR6:  tm ++;
        /* fall through */
    case OSMR5:  tm ++;
        /* fall through */
    case OSMR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        return s->tm4[tm].tm.value;
    case OSCR:
        return s->clock + muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                        s->lastload, s->freq, NANOSECONDS_PER_SECOND);
    case OSCR11: tm ++;
        /* fall through */
    case OSCR10: tm ++;
        /* fall through */
    case OSCR9:  tm ++;
        /* fall through */
    case OSCR8:  tm ++;
        /* fall through */
    case OSCR7:  tm ++;
        /* fall through */
    case OSCR6:  tm ++;
        /* fall through */
    case OSCR5:  tm ++;
        /* fall through */
    case OSCR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;

        if ((tm == 9 - 4 || tm == 11 - 4) && (s->tm4[tm].control & (1 << 9))) {
            if (s->tm4[tm - 1].freq)
                s->snapshot = s->tm4[tm - 1].clock + muldiv64(
                                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                                s->tm4[tm - 1].lastload,
                                s->tm4[tm - 1].freq, NANOSECONDS_PER_SECOND);
            else
                s->snapshot = s->tm4[tm - 1].clock;
        }

        if (!s->tm4[tm].freq)
            return s->tm4[tm].clock;
        return s->tm4[tm].clock +
            muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                     s->tm4[tm].lastload, s->tm4[tm].freq,
                     NANOSECONDS_PER_SECOND);
    case OIER:
        return s->irq_enabled;
    case OSSR:	/* Status register */
        return s->events;
    case OWER:
        return s->reset3;
    case OMCR11: tm ++;
        /* fall through */
    case OMCR10: tm ++;
        /* fall through */
    case OMCR9:  tm ++;
        /* fall through */
    case OMCR8:  tm ++;
        /* fall through */
    case OMCR7:  tm ++;
        /* fall through */
    case OMCR6:  tm ++;
        /* fall through */
    case OMCR5:  tm ++;
        /* fall through */
    case OMCR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        return s->tm4[tm].control;
    case OSNR:
        return s->snapshot;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unknown register 0x%02" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    badreg:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: incorrect register 0x%02" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return 0;
}

static void pxa2xx_timer_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    int i, tm = 0;
    PXA2xxTimerInfo *s = (PXA2xxTimerInfo *) opaque;

    switch (offset) {
    case OSMR3:  tm ++;
        /* fall through */
    case OSMR2:  tm ++;
        /* fall through */
    case OSMR1:  tm ++;
        /* fall through */
    case OSMR0:
        s->timer[tm].value = value;
        pxa2xx_timer_update(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case OSMR11: tm ++;
        /* fall through */
    case OSMR10: tm ++;
        /* fall through */
    case OSMR9:  tm ++;
        /* fall through */
    case OSMR8:  tm ++;
        /* fall through */
    case OSMR7:  tm ++;
        /* fall through */
    case OSMR6:  tm ++;
        /* fall through */
    case OSMR5:  tm ++;
        /* fall through */
    case OSMR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        s->tm4[tm].tm.value = value;
        pxa2xx_timer_update4(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tm);
        break;
    case OSCR:
        s->oldclock = s->clock;
        s->lastload = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->clock = value;
        pxa2xx_timer_update(s, s->lastload);
        break;
    case OSCR11: tm ++;
        /* fall through */
    case OSCR10: tm ++;
        /* fall through */
    case OSCR9:  tm ++;
        /* fall through */
    case OSCR8:  tm ++;
        /* fall through */
    case OSCR7:  tm ++;
        /* fall through */
    case OSCR6:  tm ++;
        /* fall through */
    case OSCR5:  tm ++;
        /* fall through */
    case OSCR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        s->tm4[tm].oldclock = s->tm4[tm].clock;
        s->tm4[tm].lastload = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->tm4[tm].clock = value;
        pxa2xx_timer_update4(s, s->tm4[tm].lastload, tm);
        break;
    case OIER:
        s->irq_enabled = value & 0xfff;
        break;
    case OSSR:	/* Status register */
        value &= s->events;
        s->events &= ~value;
        for (i = 0; i < 4; i ++, value >>= 1)
            if (value & 1)
                qemu_irq_lower(s->timer[i].irq);
        if (pxa2xx_timer_has_tm4(s) && !(s->events & 0xff0) && value)
            qemu_irq_lower(s->irq4);
        break;
    case OWER:	/* XXX: Reset on OSMR3 match? */
        s->reset3 = value;
        break;
    case OMCR7:  tm ++;
        /* fall through */
    case OMCR6:  tm ++;
        /* fall through */
    case OMCR5:  tm ++;
        /* fall through */
    case OMCR4:
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        s->tm4[tm].control = value & 0x0ff;
        /* XXX Stop if running (shouldn't happen) */
        if ((value & (1 << 7)) || tm == 0)
            s->tm4[tm].freq = pxa2xx_timer4_freq[value & 7];
        else {
            s->tm4[tm].freq = 0;
            pxa2xx_timer_update4(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tm);
        }
        break;
    case OMCR11: tm ++;
        /* fall through */
    case OMCR10: tm ++;
        /* fall through */
    case OMCR9:  tm ++;
        /* fall through */
    case OMCR8:  tm += 4;
        if (!pxa2xx_timer_has_tm4(s))
            goto badreg;
        s->tm4[tm].control = value & 0x3ff;
        /* XXX Stop if running (shouldn't happen) */
        if ((value & (1 << 7)) || !(tm & 1))
            s->tm4[tm].freq =
                    pxa2xx_timer4_freq[(value & (1 << 8)) ?  0 : (value & 7)];
        else {
            s->tm4[tm].freq = 0;
            pxa2xx_timer_update4(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tm);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unknown register 0x%02" HWADDR_PRIx " "
                      "(value 0x%08" PRIx64 ")\n",  __func__, offset, value);
        break;
    badreg:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: incorrect register 0x%02" HWADDR_PRIx " "
                      "(value 0x%08" PRIx64 ")\n", __func__, offset, value);
    }
}

static const MemoryRegionOps pxa2xx_timer_ops = {
    .read = pxa2xx_timer_read,
    .write = pxa2xx_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pxa2xx_timer_tick(void *opaque)
{
    PXA2xxTimer0 *t = (PXA2xxTimer0 *) opaque;
    PXA2xxTimerInfo *i = t->info;

    if (i->irq_enabled & (1 << t->num)) {
        i->events |= 1 << t->num;
        qemu_irq_raise(t->irq);
    }

    if (t->num == 3)
        if (i->reset3 & 1) {
            i->reset3 = 0;
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
}

static void pxa2xx_timer_tick4(void *opaque)
{
    PXA2xxTimer4 *t = (PXA2xxTimer4 *) opaque;
    PXA2xxTimerInfo *i = (PXA2xxTimerInfo *) t->tm.info;

    pxa2xx_timer_tick(&t->tm);
    if (t->control & (1 << 3))
        t->clock = 0;
    if (t->control & (1 << 6))
        pxa2xx_timer_update4(i, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), t->tm.num - 4);
    if (i->events & 0xff0)
        qemu_irq_raise(i->irq4);
}

static int pxa25x_timer_post_load(void *opaque, int version_id)
{
    PXA2xxTimerInfo *s = (PXA2xxTimerInfo *) opaque;
    int64_t now;
    int i;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    pxa2xx_timer_update(s, now);

    if (pxa2xx_timer_has_tm4(s))
        for (i = 0; i < 8; i ++)
            pxa2xx_timer_update4(s, now, i);

    return 0;
}

static void pxa2xx_timer_init(Object *obj)
{
    PXA2xxTimerInfo *s = PXA2XX_TIMER(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    s->irq_enabled = 0;
    s->oldclock = 0;
    s->clock = 0;
    s->lastload = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->reset3 = 0;

    memory_region_init_io(&s->iomem, obj, &pxa2xx_timer_ops, s,
                          "pxa2xx-timer", 0x00001000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void pxa2xx_timer_realize(DeviceState *dev, Error **errp)
{
    PXA2xxTimerInfo *s = PXA2XX_TIMER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    for (i = 0; i < 4; i ++) {
        s->timer[i].value = 0;
        sysbus_init_irq(sbd, &s->timer[i].irq);
        s->timer[i].info = s;
        s->timer[i].num = i;
        s->timer[i].qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          pxa2xx_timer_tick, &s->timer[i]);
    }

    if (s->flags & (1 << PXA2XX_TIMER_HAVE_TM4)) {
        sysbus_init_irq(sbd, &s->irq4);

        for (i = 0; i < 8; i ++) {
            s->tm4[i].tm.value = 0;
            s->tm4[i].tm.info = s;
            s->tm4[i].tm.num = i + 4;
            s->tm4[i].freq = 0;
            s->tm4[i].control = 0x0;
            s->tm4[i].tm.qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                               pxa2xx_timer_tick4, &s->tm4[i]);
        }
    }
}

static const VMStateDescription vmstate_pxa2xx_timer0_regs = {
    .name = "pxa2xx_timer0",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(value, PXA2xxTimer0),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_pxa2xx_timer4_regs = {
    .name = "pxa2xx_timer4",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(tm, PXA2xxTimer4, 1,
                        vmstate_pxa2xx_timer0_regs, PXA2xxTimer0),
        VMSTATE_INT32(oldclock, PXA2xxTimer4),
        VMSTATE_INT32(clock, PXA2xxTimer4),
        VMSTATE_UINT64(lastload, PXA2xxTimer4),
        VMSTATE_UINT32(freq, PXA2xxTimer4),
        VMSTATE_UINT32(control, PXA2xxTimer4),
        VMSTATE_END_OF_LIST(),
    },
};

static bool pxa2xx_timer_has_tm4_test(void *opaque, int version_id)
{
    return pxa2xx_timer_has_tm4(opaque);
}

static const VMStateDescription vmstate_pxa2xx_timer_regs = {
    .name = "pxa2xx_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pxa25x_timer_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(clock, PXA2xxTimerInfo),
        VMSTATE_INT32(oldclock, PXA2xxTimerInfo),
        VMSTATE_UINT64(lastload, PXA2xxTimerInfo),
        VMSTATE_STRUCT_ARRAY(timer, PXA2xxTimerInfo, 4, 1,
                        vmstate_pxa2xx_timer0_regs, PXA2xxTimer0),
        VMSTATE_UINT32(events, PXA2xxTimerInfo),
        VMSTATE_UINT32(irq_enabled, PXA2xxTimerInfo),
        VMSTATE_UINT32(reset3, PXA2xxTimerInfo),
        VMSTATE_UINT32(snapshot, PXA2xxTimerInfo),
        VMSTATE_STRUCT_ARRAY_TEST(tm4, PXA2xxTimerInfo, 8,
                        pxa2xx_timer_has_tm4_test, 0,
                        vmstate_pxa2xx_timer4_regs, PXA2xxTimer4),
        VMSTATE_END_OF_LIST(),
    }
};

static Property pxa25x_timer_dev_properties[] = {
    DEFINE_PROP_UINT32("freq", PXA2xxTimerInfo, freq, PXA25X_FREQ),
    DEFINE_PROP_BIT("tm4", PXA2xxTimerInfo, flags,
                    PXA2XX_TIMER_HAVE_TM4, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa25x_timer_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PXA25x timer";
    device_class_set_props(dc, pxa25x_timer_dev_properties);
}

static const TypeInfo pxa25x_timer_dev_info = {
    .name          = "pxa25x-timer",
    .parent        = TYPE_PXA2XX_TIMER,
    .instance_size = sizeof(PXA2xxTimerInfo),
    .class_init    = pxa25x_timer_dev_class_init,
};

static Property pxa27x_timer_dev_properties[] = {
    DEFINE_PROP_UINT32("freq", PXA2xxTimerInfo, freq, PXA27X_FREQ),
    DEFINE_PROP_BIT("tm4", PXA2xxTimerInfo, flags,
                    PXA2XX_TIMER_HAVE_TM4, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa27x_timer_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PXA27x timer";
    device_class_set_props(dc, pxa27x_timer_dev_properties);
}

static const TypeInfo pxa27x_timer_dev_info = {
    .name          = "pxa27x-timer",
    .parent        = TYPE_PXA2XX_TIMER,
    .instance_size = sizeof(PXA2xxTimerInfo),
    .class_init    = pxa27x_timer_dev_class_init,
};

static void pxa2xx_timer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize  = pxa2xx_timer_realize;
    dc->vmsd = &vmstate_pxa2xx_timer_regs;
}

static const TypeInfo pxa2xx_timer_type_info = {
    .name          = TYPE_PXA2XX_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxTimerInfo),
    .instance_init = pxa2xx_timer_init,
    .abstract      = true,
    .class_init    = pxa2xx_timer_class_init,
};

static void pxa2xx_timer_register_types(void)
{
    type_register_static(&pxa2xx_timer_type_info);
    type_register_static(&pxa25x_timer_dev_info);
    type_register_static(&pxa27x_timer_dev_info);
}

type_init(pxa2xx_timer_register_types)
