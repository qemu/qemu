/*
 * Rasperry Pi 2 emulation ARM control logic module.
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * Based on bcm2835_ic.c (Raspberry Pi emulation) (c) 2012 Gregory Estrade
 *
 * At present, only implements interrupt routing, and mailboxes (i.e.,
 * not PMU interrupt, or AXI counters).
 *
 * ARM Local Timer IRQ Copyright (c) 2019. Zoltán Baldaszti
 *
 * Ref:
 * https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2836/QA7_rev3.4.pdf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/intc/bcm2836_control.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define REG_GPU_ROUTE           0x0c
#define REG_LOCALTIMERROUTING   0x24
#define REG_LOCALTIMERCONTROL   0x34
#define REG_LOCALTIMERACK       0x38
#define REG_TIMERCONTROL        0x40
#define REG_MBOXCONTROL         0x50
#define REG_IRQSRC              0x60
#define REG_FIQSRC              0x70
#define REG_MBOX0_WR            0x80
#define REG_MBOX0_RDCLR         0xc0
#define REG_LIMIT              0x100

#define IRQ_BIT(cntrl, num) (((cntrl) & (1 << (num))) != 0)
#define FIQ_BIT(cntrl, num) (((cntrl) & (1 << ((num) + 4))) != 0)

#define IRQ_CNTPSIRQ    0
#define IRQ_CNTPNSIRQ   1
#define IRQ_CNTHPIRQ    2
#define IRQ_CNTVIRQ     3
#define IRQ_MAILBOX0    4
#define IRQ_MAILBOX1    5
#define IRQ_MAILBOX2    6
#define IRQ_MAILBOX3    7
#define IRQ_GPU         8
#define IRQ_PMU         9
#define IRQ_AXI         10
#define IRQ_TIMER       11
#define IRQ_MAX         IRQ_TIMER

#define LOCALTIMER_FREQ      38400000
#define LOCALTIMER_INTFLAG   (1 << 31)
#define LOCALTIMER_RELOAD    (1 << 30)
#define LOCALTIMER_INTENABLE (1 << 29)
#define LOCALTIMER_ENABLE    (1 << 28)
#define LOCALTIMER_VALUE(x)  ((x) & 0xfffffff)

static void deliver_local(BCM2836ControlState *s, uint8_t core, uint8_t irq,
                          uint32_t controlreg, uint8_t controlidx)
{
    if (FIQ_BIT(controlreg, controlidx)) {
        /* deliver a FIQ */
        s->fiqsrc[core] |= (uint32_t)1 << irq;
    } else if (IRQ_BIT(controlreg, controlidx)) {
        /* deliver an IRQ */
        s->irqsrc[core] |= (uint32_t)1 << irq;
    } else {
        /* the interrupt is masked */
    }
}

/* Update interrupts.  */
static void bcm2836_control_update(BCM2836ControlState *s)
{
    int i, j;

    /* reset pending IRQs/FIQs */
    for (i = 0; i < BCM2836_NCORES; i++) {
        s->irqsrc[i] = s->fiqsrc[i] = 0;
    }

    /* apply routing logic, update status regs */
    if (s->gpu_irq) {
        assert(s->route_gpu_irq < BCM2836_NCORES);
        s->irqsrc[s->route_gpu_irq] |= (uint32_t)1 << IRQ_GPU;
    }

    if (s->gpu_fiq) {
        assert(s->route_gpu_fiq < BCM2836_NCORES);
        s->fiqsrc[s->route_gpu_fiq] |= (uint32_t)1 << IRQ_GPU;
    }

    /*
     * handle the control module 'local timer' interrupt for one of the
     * cores' IRQ/FIQ;  this is distinct from the per-CPU timer
     * interrupts handled below.
     */
    if ((s->local_timer_control & LOCALTIMER_INTENABLE) &&
        (s->local_timer_control & LOCALTIMER_INTFLAG)) {
        if (s->route_localtimer & 4) {
            s->fiqsrc[(s->route_localtimer & 3)] |= (uint32_t)1 << IRQ_TIMER;
        } else {
            s->irqsrc[(s->route_localtimer & 3)] |= (uint32_t)1 << IRQ_TIMER;
        }
    }

    for (i = 0; i < BCM2836_NCORES; i++) {
        /* handle local timer interrupts for this core */
        if (s->timerirqs[i]) {
            assert(s->timerirqs[i] < (1 << (IRQ_CNTVIRQ + 1))); /* sane mask? */
            for (j = 0; j <= IRQ_CNTVIRQ; j++) {
                if ((s->timerirqs[i] & (1 << j)) != 0) {
                    /* local interrupt j is set */
                    deliver_local(s, i, j, s->timercontrol[i], j);
                }
            }
        }

        /* handle mailboxes for this core */
        for (j = 0; j < BCM2836_MBPERCORE; j++) {
            if (s->mailboxes[i * BCM2836_MBPERCORE + j] != 0) {
                /* mailbox j is set */
                deliver_local(s, i, j + IRQ_MAILBOX0, s->mailboxcontrol[i], j);
            }
        }
    }

    /* call set_irq appropriately for each output */
    for (i = 0; i < BCM2836_NCORES; i++) {
        qemu_set_irq(s->irq[i], s->irqsrc[i] != 0);
        qemu_set_irq(s->fiq[i], s->fiqsrc[i] != 0);
    }
}

static void bcm2836_control_set_local_irq(void *opaque, int core, int local_irq,
                                          int level)
{
    BCM2836ControlState *s = opaque;

    assert(core >= 0 && core < BCM2836_NCORES);
    assert(local_irq >= 0 && local_irq <= IRQ_CNTVIRQ);

    s->timerirqs[core] = deposit32(s->timerirqs[core], local_irq, 1, !!level);

    bcm2836_control_update(s);
}

/* XXX: the following wrapper functions are a kludgy workaround,
 * needed because I can't seem to pass useful information in the "irq"
 * parameter when using named interrupts. Feel free to clean this up!
 */

static void bcm2836_control_set_local_irq0(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, IRQ_CNTPSIRQ, level);
}

static void bcm2836_control_set_local_irq1(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, IRQ_CNTPNSIRQ, level);
}

static void bcm2836_control_set_local_irq2(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, IRQ_CNTHPIRQ, level);
}

static void bcm2836_control_set_local_irq3(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, IRQ_CNTVIRQ, level);
}

static void bcm2836_control_set_gpu_irq(void *opaque, int irq, int level)
{
    BCM2836ControlState *s = opaque;

    s->gpu_irq = level;

    bcm2836_control_update(s);
}

static void bcm2836_control_set_gpu_fiq(void *opaque, int irq, int level)
{
    BCM2836ControlState *s = opaque;

    s->gpu_fiq = level;

    bcm2836_control_update(s);
}

static void bcm2836_control_local_timer_set_next(void *opaque)
{
    BCM2836ControlState *s = opaque;
    uint64_t next_event;

    assert(LOCALTIMER_VALUE(s->local_timer_control) > 0);

    next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        muldiv64(LOCALTIMER_VALUE(s->local_timer_control),
            NANOSECONDS_PER_SECOND, LOCALTIMER_FREQ);
    timer_mod(&s->timer, next_event);
}

static void bcm2836_control_local_timer_tick(void *opaque)
{
    BCM2836ControlState *s = opaque;

    bcm2836_control_local_timer_set_next(s);

    s->local_timer_control |= LOCALTIMER_INTFLAG;
    bcm2836_control_update(s);
}

static void bcm2836_control_local_timer_control(void *opaque, uint32_t val)
{
    BCM2836ControlState *s = opaque;

    s->local_timer_control = val;
    if (val & LOCALTIMER_ENABLE) {
        bcm2836_control_local_timer_set_next(s);
    } else {
        timer_del(&s->timer);
    }
}

static void bcm2836_control_local_timer_ack(void *opaque, uint32_t val)
{
    BCM2836ControlState *s = opaque;

    if (val & LOCALTIMER_INTFLAG) {
        s->local_timer_control &= ~LOCALTIMER_INTFLAG;
    }
    if ((val & LOCALTIMER_RELOAD) &&
        (s->local_timer_control & LOCALTIMER_ENABLE)) {
            bcm2836_control_local_timer_set_next(s);
    }
}

static uint64_t bcm2836_control_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2836ControlState *s = opaque;

    if (offset == REG_GPU_ROUTE) {
        assert(s->route_gpu_fiq < BCM2836_NCORES
               && s->route_gpu_irq < BCM2836_NCORES);
        return ((uint32_t)s->route_gpu_fiq << 2) | s->route_gpu_irq;
    } else if (offset == REG_LOCALTIMERROUTING) {
        return s->route_localtimer;
    } else if (offset == REG_LOCALTIMERCONTROL) {
        return s->local_timer_control;
    } else if (offset == REG_LOCALTIMERACK) {
        return 0;
    } else if (offset >= REG_TIMERCONTROL && offset < REG_MBOXCONTROL) {
        return s->timercontrol[(offset - REG_TIMERCONTROL) >> 2];
    } else if (offset >= REG_MBOXCONTROL && offset < REG_IRQSRC) {
        return s->mailboxcontrol[(offset - REG_MBOXCONTROL) >> 2];
    } else if (offset >= REG_IRQSRC && offset < REG_FIQSRC) {
        return s->irqsrc[(offset - REG_IRQSRC) >> 2];
    } else if (offset >= REG_FIQSRC && offset < REG_MBOX0_WR) {
        return s->fiqsrc[(offset - REG_FIQSRC) >> 2];
    } else if (offset >= REG_MBOX0_RDCLR && offset < REG_LIMIT) {
        return s->mailboxes[(offset - REG_MBOX0_RDCLR) >> 2];
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }
}

static void bcm2836_control_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    BCM2836ControlState *s = opaque;

    if (offset == REG_GPU_ROUTE) {
        s->route_gpu_irq = val & 0x3;
        s->route_gpu_fiq = (val >> 2) & 0x3;
    } else if (offset == REG_LOCALTIMERROUTING) {
        s->route_localtimer = val & 7;
    } else if (offset == REG_LOCALTIMERCONTROL) {
        bcm2836_control_local_timer_control(s, val);
    } else if (offset == REG_LOCALTIMERACK) {
        bcm2836_control_local_timer_ack(s, val);
    } else if (offset >= REG_TIMERCONTROL && offset < REG_MBOXCONTROL) {
        s->timercontrol[(offset - REG_TIMERCONTROL) >> 2] = val & 0xff;
    } else if (offset >= REG_MBOXCONTROL && offset < REG_IRQSRC) {
        s->mailboxcontrol[(offset - REG_MBOXCONTROL) >> 2] = val & 0xff;
    } else if (offset >= REG_MBOX0_WR && offset < REG_MBOX0_RDCLR) {
        s->mailboxes[(offset - REG_MBOX0_WR) >> 2] |= val;
    } else if (offset >= REG_MBOX0_RDCLR && offset < REG_LIMIT) {
        s->mailboxes[(offset - REG_MBOX0_RDCLR) >> 2] &= ~val;
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx
                                 " value 0x%"PRIx64"\n",
                      __func__, offset, val);
        return;
    }

    bcm2836_control_update(s);
}

static const MemoryRegionOps bcm2836_control_ops = {
    .read = bcm2836_control_read,
    .write = bcm2836_control_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void bcm2836_control_reset(DeviceState *d)
{
    BCM2836ControlState *s = BCM2836_CONTROL(d);
    int i;

    s->route_gpu_irq = s->route_gpu_fiq = 0;

    timer_del(&s->timer);
    s->route_localtimer = 0;
    s->local_timer_control = 0;

    for (i = 0; i < BCM2836_NCORES; i++) {
        s->timercontrol[i] = 0;
        s->mailboxcontrol[i] = 0;
    }

    for (i = 0; i < BCM2836_NCORES * BCM2836_MBPERCORE; i++) {
        s->mailboxes[i] = 0;
    }
}

static void bcm2836_control_init(Object *obj)
{
    BCM2836ControlState *s = BCM2836_CONTROL(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2836_control_ops, s,
                          TYPE_BCM2836_CONTROL, REG_LIMIT);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    /* inputs from each CPU core */
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq0, "cntpsirq",
                            BCM2836_NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq1, "cntpnsirq",
                            BCM2836_NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq2, "cnthpirq",
                            BCM2836_NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq3, "cntvirq",
                            BCM2836_NCORES);

    /* IRQ and FIQ inputs from upstream bcm2835 controller */
    qdev_init_gpio_in_named(dev, bcm2836_control_set_gpu_irq, "gpu-irq", 1);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_gpu_fiq, "gpu-fiq", 1);

    /* outputs to CPU cores */
    qdev_init_gpio_out_named(dev, s->irq, "irq", BCM2836_NCORES);
    qdev_init_gpio_out_named(dev, s->fiq, "fiq", BCM2836_NCORES);

    /* create a qemu virtual timer */
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL,
                  bcm2836_control_local_timer_tick, s);
}

static const VMStateDescription vmstate_bcm2836_control = {
    .name = TYPE_BCM2836_CONTROL,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(mailboxes, BCM2836ControlState,
                             BCM2836_NCORES * BCM2836_MBPERCORE),
        VMSTATE_UINT8(route_gpu_irq, BCM2836ControlState),
        VMSTATE_UINT8(route_gpu_fiq, BCM2836ControlState),
        VMSTATE_UINT32_ARRAY(timercontrol, BCM2836ControlState, BCM2836_NCORES),
        VMSTATE_UINT32_ARRAY(mailboxcontrol, BCM2836ControlState,
                             BCM2836_NCORES),
        VMSTATE_TIMER_V(timer, BCM2836ControlState, 2),
        VMSTATE_UINT32_V(local_timer_control, BCM2836ControlState, 2),
        VMSTATE_UINT8_V(route_localtimer, BCM2836ControlState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2836_control_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2836_control_reset;
    dc->vmsd = &vmstate_bcm2836_control;
}

static TypeInfo bcm2836_control_info = {
    .name          = TYPE_BCM2836_CONTROL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2836ControlState),
    .class_init    = bcm2836_control_class_init,
    .instance_init = bcm2836_control_init,
};

static void bcm2836_control_register_types(void)
{
    type_register_static(&bcm2836_control_info);
}

type_init(bcm2836_control_register_types)
