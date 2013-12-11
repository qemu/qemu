/*
 * mxs_timrot.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * Implements the timer block for the mxs. Currently supports only the
 * 32khz based clock, and not all the of the options, nor the input counters,
 * PWM etc etc.
 * Basically, it supports enough for the linux kernel
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"

enum {
    TIMROT_ROTCTRL = 0,
    TIMROT_CTRL0 = 0x2,
    TIMROT_COUNT0 = 0x3,
    TIMROT_CTRL1 = 0x4,
    TIMROT_COUNT1 = 0x5,
    TIMROT_CTRL2 = 0x6,
    TIMROT_COUNT2 = 0x7,
    TIMROT_CTRL3 = 0x8,
    TIMROT_COUNT3 = 0x9,
    TIMROT_VERSION = 0xa,
};

enum {
    TIM_IRQ = 15,
    TIM_IRQ_EN = 14,
    TIM_UPDATE = 7,
    TIM_RELOAD = 6,
    TIM_PRESCALE = 4,
    TIM_SELECT = 0,
};

typedef struct mxs_tim_state {
    struct mxs_timrot_state * s;
    uint8_t tid;
    uint8_t fired;
    uint32_t control, count;
    qemu_irq irq;
    ptimer_state * timer;
} mxs_tim_state;

typedef struct mxs_timrot_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t rotctrl;

    mxs_tim_state t[4];
} mxs_timrot_state;

static void tim_set_count(mxs_tim_state * t, uint32_t count)
{
    if (count != (t->count & 0xffff) || t->fired) {
        t->count = (t->count & ~0xffff) | (count & 0xffff);
        ptimer_set_limit(t->timer, t->count & 0xffff, 1);
        if (t->count & 0xffff) {
            t->fired = 0;
            ptimer_run(t->timer, t->control & (1 << TIM_RELOAD) ? 0 : 1);
        }
    }
}

static void tim_set_control(mxs_tim_state * t, uint16_t control)
{
    uint32_t change = t->control ^ control;
    if (!change) {
        return;
    }

    uint32_t freq = 0;
    switch ((control >> TIM_SELECT) & 0xf) {
        case 0x8:
            freq = 32000;
            break;
        case 0x9:
            freq = 8000;
            break;
        case 0xa:
            freq = 4000;
            break;
        case 0xc:
            freq = 1000;
            break;
    }
    switch ((control >> TIM_PRESCALE) & 0x3) {
        /* TODO */
    }
    if (!(control & (1 << TIM_IRQ))) {
        qemu_irq_lower(t->irq);
    }
    if (freq == 0) {
        ptimer_stop(t->timer);
    } else if (change & 0xff) {
        printf("%s[%d] %04x freq %d\n", __func__, t->tid, control, (int) freq);
        ptimer_set_freq(t->timer, freq);
        ptimer_set_limit(t->timer, t->count & 0xffff, 1);
        if (t->count & 0xffff) {
            t->fired = 0;
            ptimer_run(t->timer, control & (1 << TIM_RELOAD) ? 0 : 1);
        }
    }
    t->control = control;
}

static uint32_t tim_get_count(mxs_tim_state * t)
{
    t->count &= 0xffff;
    t->count |= (ptimer_get_count(t->timer) << 16);
    return t->count;
}

static void mxs_timrot_timer_trigger(void *opaque)
{
    mxs_tim_state * t = opaque;
    t->fired = 1;
    t->control |= (1 << TIM_IRQ);
    if (t->control & (1 << TIM_IRQ_EN))
        qemu_irq_raise(t->irq);
}

static inline int tim_get_tid(hwaddr offset)
{
    return ((offset >> 4) - TIMROT_CTRL0) >> 1;
}

static uint64_t mxs_timrot_read(void *opaque, hwaddr offset,
        unsigned size)
{
    mxs_timrot_state *s = (mxs_timrot_state *) opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
        case TIMROT_ROTCTRL:
            res = s->rotctrl | (0xf << 25);
            break;
        case TIMROT_VERSION:
            res = 0x01010000;
            break;
        case TIMROT_CTRL0:
        case TIMROT_CTRL1:
        case TIMROT_CTRL2:
        case TIMROT_CTRL3:
            res = s->t[tim_get_tid(offset)].control;
            break;
        case TIMROT_COUNT0:
        case TIMROT_COUNT1:
        case TIMROT_COUNT2:
        case TIMROT_COUNT3:
            res = tim_get_count(&s->t[tim_get_tid(offset)]);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            return 0;
    }
    return res;
}

static void mxs_timrot_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    mxs_timrot_state *s = (mxs_timrot_state *) opaque;
    uint32_t * dst = NULL;
    uint32_t val = 0;
    uint32_t oldvalue = 0;

    switch (offset >> 4) {
        case TIMROT_ROTCTRL:
            dst = &s->rotctrl;
            break;
        case TIMROT_CTRL0:
        case TIMROT_CTRL1:
        case TIMROT_CTRL2:
        case TIMROT_CTRL3:
            val = s->t[tim_get_tid(offset)].control;
            dst = &val;
            break;
        case TIMROT_COUNT0:
        case TIMROT_COUNT1:
        case TIMROT_COUNT2:
        case TIMROT_COUNT3:
            val = s->t[tim_get_tid(offset)].count;
            dst = &val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            return;
    }
    if (!dst) {
        return;
    }
    oldvalue = mxs_write(dst, offset, value, size);

    switch (offset >> 4) {
        case TIMROT_ROTCTRL:
            if ((oldvalue ^ *dst) == 0x80000000 && !(oldvalue & 0x80000000)) {
            //    printf("%s reseting, anding clockgate\n", __func__);
                *dst |= 0x40000000;
            }
            *dst |= 0xf << 25; // 4 timers, no encoder
            break;
        case TIMROT_CTRL0:
        case TIMROT_CTRL1:
        case TIMROT_CTRL2:
        case TIMROT_CTRL3:
            tim_set_control(&s->t[tim_get_tid(offset)], val);
            break;
        case TIMROT_COUNT0:
        case TIMROT_COUNT1:
        case TIMROT_COUNT2:
        case TIMROT_COUNT3:
            tim_set_count(&s->t[tim_get_tid(offset)], val);
            break;
    }
}


static const MemoryRegionOps mxs_timrot_ops = {
    .read = mxs_timrot_read,
    .write = mxs_timrot_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mxs_timrot_init(SysBusDevice *dev)
{
    mxs_timrot_state *s = OBJECT_CHECK(mxs_timrot_state, dev, "mxs_timrot");
    int i;

    for (i = 0; i < 4; i++) {
        QEMUBH *bh = qemu_bh_new(mxs_timrot_timer_trigger, &s->t[i]);
        s->t[i].timer = ptimer_init(bh);
        sysbus_init_irq(dev, &s->t[i].irq);
        s->t[i].s = s;
        s->t[i].tid = i;
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_timrot_ops, s,
            "mxs_timrot", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

static void mxs_timrot_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_timrot_init;
}

static TypeInfo timrot_info = {
    .name          = "mxs_timrot",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_timrot_state),
    .class_init    = mxs_timrot_class_init,
};

static void mxs_timrot_register(void)
{
    type_register_static(&timrot_info);
}

type_init(mxs_timrot_register)
