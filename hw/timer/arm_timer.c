/*
 * ARM PrimeCell Timer modules.
 *
 * Copyright (c) 2005-2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"

/* Common timer implementation.  */

#define TIMER_CTRL_ONESHOT      (1 << 0)
#define TIMER_CTRL_32BIT        (1 << 1)
#define TIMER_CTRL_DIV1         (0 << 2)
#define TIMER_CTRL_DIV16        (1 << 2)
#define TIMER_CTRL_DIV256       (2 << 2)
#define TIMER_CTRL_IE           (1 << 5)
#define TIMER_CTRL_PERIODIC     (1 << 6)
#define TIMER_CTRL_ENABLE       (1 << 7)

typedef struct {
    ptimer_state *timer;
    uint32_t control;
    uint32_t limit;
    int freq;
    int int_level;
    qemu_irq irq;
} arm_timer_state;

/* Check all active timers, and schedule the next timer interrupt.  */

static void arm_timer_update(arm_timer_state *s)
{
    /* Update interrupts.  */
    if (s->int_level && (s->control & TIMER_CTRL_IE)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t arm_timer_read(void *opaque, hwaddr offset)
{
    arm_timer_state *s = (arm_timer_state *)opaque;

    switch (offset >> 2) {
    case 0: /* TimerLoad */
    case 6: /* TimerBGLoad */
        return s->limit;
    case 1: /* TimerValue */
        return ptimer_get_count(s->timer);
    case 2: /* TimerControl */
        return s->control;
    case 4: /* TimerRIS */
        return s->int_level;
    case 5: /* TimerMIS */
        if ((s->control & TIMER_CTRL_IE) == 0)
            return 0;
        return s->int_level;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int)offset);
        return 0;
    }
}

/* Reset the timer limit after settings have changed.  */
static void arm_timer_recalibrate(arm_timer_state *s, int reload)
{
    uint32_t limit;

    if ((s->control & (TIMER_CTRL_PERIODIC | TIMER_CTRL_ONESHOT)) == 0) {
        /* Free running.  */
        if (s->control & TIMER_CTRL_32BIT)
            limit = 0xffffffff;
        else
            limit = 0xffff;
    } else {
          /* Periodic.  */
          limit = s->limit;
    }
    ptimer_set_limit(s->timer, limit, reload);
}

static void arm_timer_write(void *opaque, hwaddr offset,
                            uint32_t value)
{
    arm_timer_state *s = (arm_timer_state *)opaque;
    int freq;

    switch (offset >> 2) {
    case 0: /* TimerLoad */
        s->limit = value;
        arm_timer_recalibrate(s, 1);
        break;
    case 1: /* TimerValue */
        /* ??? Linux seems to want to write to this readonly register.
           Ignore it.  */
        break;
    case 2: /* TimerControl */
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            ptimer_stop(s->timer);
        }
        s->control = value;
        freq = s->freq;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch ((value >> 2) & 3) {
        case 1: freq >>= 4; break;
        case 2: freq >>= 8; break;
        }
        arm_timer_recalibrate(s, s->control & TIMER_CTRL_ENABLE);
        ptimer_set_freq(s->timer, freq);
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Restart the timer if still enabled.  */
            ptimer_run(s->timer, (s->control & TIMER_CTRL_ONESHOT) != 0);
        }
        break;
    case 3: /* TimerIntClr */
        s->int_level = 0;
        break;
    case 6: /* TimerBGLoad */
        s->limit = value;
        arm_timer_recalibrate(s, 0);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int)offset);
    }
    arm_timer_update(s);
}

static void arm_timer_tick(void *opaque)
{
    arm_timer_state *s = (arm_timer_state *)opaque;
    s->int_level = 1;
    arm_timer_update(s);
}

static const VMStateDescription vmstate_arm_timer = {
    .name = "arm_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, arm_timer_state),
        VMSTATE_UINT32(limit, arm_timer_state),
        VMSTATE_INT32(int_level, arm_timer_state),
        VMSTATE_PTIMER(timer, arm_timer_state),
        VMSTATE_END_OF_LIST()
    }
};

static arm_timer_state *arm_timer_init(uint32_t freq)
{
    arm_timer_state *s;
    QEMUBH *bh;

    s = (arm_timer_state *)g_malloc0(sizeof(arm_timer_state));
    s->freq = freq;
    s->control = TIMER_CTRL_IE;

    bh = qemu_bh_new(arm_timer_tick, s);
    s->timer = ptimer_init(bh);
    vmstate_register(NULL, -1, &vmstate_arm_timer, s);
    return s;
}

/* ARM PrimeCell SP804 dual timer module.
 * Docs at
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0271d/index.html
*/

#define TYPE_SP804 "sp804"
#define SP804(obj) OBJECT_CHECK(SP804State, (obj), TYPE_SP804)

typedef struct SP804State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    arm_timer_state *timer[2];
    uint32_t freq0, freq1;
    int level[2];
    qemu_irq irq;
} SP804State;

static const uint8_t sp804_ids[] = {
    /* Timer ID */
    0x04, 0x18, 0x14, 0,
    /* PrimeCell ID */
    0xd, 0xf0, 0x05, 0xb1
};

/* Merge the IRQs from the two component devices.  */
static void sp804_set_irq(void *opaque, int irq, int level)
{
    SP804State *s = (SP804State *)opaque;

    s->level[irq] = level;
    qemu_set_irq(s->irq, s->level[0] || s->level[1]);
}

static uint64_t sp804_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    SP804State *s = (SP804State *)opaque;

    if (offset < 0x20) {
        return arm_timer_read(s->timer[0], offset);
    }
    if (offset < 0x40) {
        return arm_timer_read(s->timer[1], offset - 0x20);
    }

    /* TimerPeriphID */
    if (offset >= 0xfe0 && offset <= 0xffc) {
        return sp804_ids[(offset - 0xfe0) >> 2];
    }

    switch (offset) {
    /* Integration Test control registers, which we won't support */
    case 0xf00: /* TimerITCR */
    case 0xf04: /* TimerITOP (strictly write only but..) */
        qemu_log_mask(LOG_UNIMP,
                      "%s: integration test registers unimplemented\n",
                      __func__);
        return 0;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Bad offset %x\n", __func__, (int)offset);
    return 0;
}

static void sp804_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    SP804State *s = (SP804State *)opaque;

    if (offset < 0x20) {
        arm_timer_write(s->timer[0], offset, value);
        return;
    }

    if (offset < 0x40) {
        arm_timer_write(s->timer[1], offset - 0x20, value);
        return;
    }

    /* Technically we could be writing to the Test Registers, but not likely */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %x\n",
                  __func__, (int)offset);
}

static const MemoryRegionOps sp804_ops = {
    .read = sp804_read,
    .write = sp804_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_sp804 = {
    .name = "sp804",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_ARRAY(level, SP804State, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void sp804_init(Object *obj)
{
    SP804State *s = SP804(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &sp804_ops, s,
                          "sp804", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sp804_realize(DeviceState *dev, Error **errp)
{
    SP804State *s = SP804(dev);

    s->timer[0] = arm_timer_init(s->freq0);
    s->timer[1] = arm_timer_init(s->freq1);
    s->timer[0]->irq = qemu_allocate_irq(sp804_set_irq, s, 0);
    s->timer[1]->irq = qemu_allocate_irq(sp804_set_irq, s, 1);
}

/* Integrator/CP timer module.  */

#define TYPE_INTEGRATOR_PIT "integrator_pit"
#define INTEGRATOR_PIT(obj) \
    OBJECT_CHECK(icp_pit_state, (obj), TYPE_INTEGRATOR_PIT)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    arm_timer_state *timer[3];
} icp_pit_state;

static uint64_t icp_pit_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    icp_pit_state *s = (icp_pit_state *)opaque;
    int n;

    /* ??? Don't know the PrimeCell ID for this device.  */
    n = offset >> 8;
    if (n > 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad timer %d\n", __func__, n);
        return 0;
    }

    return arm_timer_read(s->timer[n], offset & 0xff);
}

static void icp_pit_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    icp_pit_state *s = (icp_pit_state *)opaque;
    int n;

    n = offset >> 8;
    if (n > 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad timer %d\n", __func__, n);
        return;
    }

    arm_timer_write(s->timer[n], offset & 0xff, value);
}

static const MemoryRegionOps icp_pit_ops = {
    .read = icp_pit_read,
    .write = icp_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void icp_pit_init(Object *obj)
{
    icp_pit_state *s = INTEGRATOR_PIT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    /* Timer 0 runs at the system clock speed (40MHz).  */
    s->timer[0] = arm_timer_init(40000000);
    /* The other two timers run at 1MHz.  */
    s->timer[1] = arm_timer_init(1000000);
    s->timer[2] = arm_timer_init(1000000);

    sysbus_init_irq(dev, &s->timer[0]->irq);
    sysbus_init_irq(dev, &s->timer[1]->irq);
    sysbus_init_irq(dev, &s->timer[2]->irq);

    memory_region_init_io(&s->iomem, obj, &icp_pit_ops, s,
                          "icp_pit", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    /* This device has no state to save/restore.  The component timers will
       save themselves.  */
}

static const TypeInfo icp_pit_info = {
    .name          = TYPE_INTEGRATOR_PIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(icp_pit_state),
    .instance_init = icp_pit_init,
};

static Property sp804_properties[] = {
    DEFINE_PROP_UINT32("freq0", SP804State, freq0, 1000000),
    DEFINE_PROP_UINT32("freq1", SP804State, freq1, 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void sp804_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = sp804_realize;
    k->props = sp804_properties;
    k->vmsd = &vmstate_sp804;
}

static const TypeInfo sp804_info = {
    .name          = TYPE_SP804,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SP804State),
    .instance_init = sp804_init,
    .class_init    = sp804_class_init,
};

static void arm_timer_register_types(void)
{
    type_register_static(&icp_pit_info);
    type_register_static(&sp804_info);
}

type_init(arm_timer_register_types)
