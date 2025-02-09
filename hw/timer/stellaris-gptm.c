/*
 * Luminary Micro Stellaris General Purpose Timer Module
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-clock.h"
#include "hw/timer/stellaris-gptm.h"

static void gptm_update_irq(gptm_state *s)
{
    int level;
    level = (s->state & s->mask) != 0;
    qemu_set_irq(s->irq, level);
}

static void gptm_stop(gptm_state *s, int n)
{
    timer_del(s->timer[n]);
}

static void gptm_reload(gptm_state *s, int n, int reset)
{
    int64_t tick;
    if (reset) {
        tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    } else {
        tick = s->tick[n];
    }

    if (s->config == 0) {
        /* 32-bit CountDown.  */
        uint32_t count;
        count = s->load[0] | (s->load[1] << 16);
        tick += clock_ticks_to_ns(s->clk, count);
    } else if (s->config == 1) {
        /* 32-bit RTC.  1Hz tick.  */
        tick += NANOSECONDS_PER_SECOND;
    } else if (s->mode[n] == 0xa) {
        /* PWM mode.  Not implemented.  */
    } else {
        qemu_log_mask(LOG_UNIMP,
                      "GPTM: 16-bit timer mode unimplemented: 0x%x\n",
                      s->mode[n]);
        return;
    }
    s->tick[n] = tick;
    timer_mod(s->timer[n], tick);
}

static void gptm_tick(void *opaque)
{
    gptm_state **p = (gptm_state **)opaque;
    gptm_state *s;
    int n;

    s = *p;
    n = p - s->opaque;
    if (s->config == 0) {
        s->state |= 1;
        if ((s->control & 0x20)) {
            /* Output trigger.  */
            qemu_irq_pulse(s->trigger);
        }
        if (s->mode[0] & 1) {
            /* One-shot.  */
            s->control &= ~1;
        } else {
            /* Periodic.  */
            gptm_reload(s, 0, 0);
        }
    } else if (s->config == 1) {
        /* RTC.  */
        uint32_t match;
        s->rtc++;
        match = s->match[0] | (s->match[1] << 16);
        if (s->rtc > match)
            s->rtc = 0;
        if (s->rtc == 0) {
            s->state |= 8;
        }
        gptm_reload(s, 0, 0);
    } else if (s->mode[n] == 0xa) {
        /* PWM mode.  Not implemented.  */
    } else {
        qemu_log_mask(LOG_UNIMP,
                      "GPTM: 16-bit timer mode unimplemented: 0x%x\n",
                      s->mode[n]);
    }
    gptm_update_irq(s);
}

static uint64_t gptm_read(void *opaque, hwaddr offset,
                          unsigned size)
{
    gptm_state *s = (gptm_state *)opaque;

    switch (offset) {
    case 0x00: /* CFG */
        return s->config;
    case 0x04: /* TAMR */
        return s->mode[0];
    case 0x08: /* TBMR */
        return s->mode[1];
    case 0x0c: /* CTL */
        return s->control;
    case 0x18: /* IMR */
        return s->mask;
    case 0x1c: /* RIS */
        return s->state;
    case 0x20: /* MIS */
        return s->state & s->mask;
    case 0x24: /* CR */
        return 0;
    case 0x28: /* TAILR */
        return s->load[0] | ((s->config < 4) ? (s->load[1] << 16) : 0);
    case 0x2c: /* TBILR */
        return s->load[1];
    case 0x30: /* TAMARCHR */
        return s->match[0] | ((s->config < 4) ? (s->match[1] << 16) : 0);
    case 0x34: /* TBMATCHR */
        return s->match[1];
    case 0x38: /* TAPR */
        return s->prescale[0];
    case 0x3c: /* TBPR */
        return s->prescale[1];
    case 0x40: /* TAPMR */
        return s->match_prescale[0];
    case 0x44: /* TBPMR */
        return s->match_prescale[1];
    case 0x48: /* TAR */
        if (s->config == 1) {
            return s->rtc;
        }
        qemu_log_mask(LOG_UNIMP,
                      "GPTM: read of TAR but timer read not supported\n");
        return 0;
    case 0x4c: /* TBR */
        qemu_log_mask(LOG_UNIMP,
                      "GPTM: read of TBR but timer read not supported\n");
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "GPTM: read at bad offset 0x02%" HWADDR_PRIx "\n",
                      offset);
        return 0;
    }
}

static void gptm_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{
    gptm_state *s = (gptm_state *)opaque;
    uint32_t oldval;

    /*
     * The timers should be disabled before changing the configuration.
     * We take advantage of this and defer everything until the timer
     * is enabled.
     */
    switch (offset) {
    case 0x00: /* CFG */
        s->config = value;
        break;
    case 0x04: /* TAMR */
        s->mode[0] = value;
        break;
    case 0x08: /* TBMR */
        s->mode[1] = value;
        break;
    case 0x0c: /* CTL */
        oldval = s->control;
        s->control = value;
        /* TODO: Implement pause.  */
        if ((oldval ^ value) & 1) {
            if (value & 1) {
                gptm_reload(s, 0, 1);
            } else {
                gptm_stop(s, 0);
            }
        }
        if (((oldval ^ value) & 0x100) && s->config >= 4) {
            if (value & 0x100) {
                gptm_reload(s, 1, 1);
            } else {
                gptm_stop(s, 1);
            }
        }
        break;
    case 0x18: /* IMR */
        s->mask = value & 0x77;
        gptm_update_irq(s);
        break;
    case 0x24: /* CR */
        s->state &= ~value;
        break;
    case 0x28: /* TAILR */
        s->load[0] = value & 0xffff;
        if (s->config < 4) {
            s->load[1] = value >> 16;
        }
        break;
    case 0x2c: /* TBILR */
        s->load[1] = value & 0xffff;
        break;
    case 0x30: /* TAMARCHR */
        s->match[0] = value & 0xffff;
        if (s->config < 4) {
            s->match[1] = value >> 16;
        }
        break;
    case 0x34: /* TBMATCHR */
        s->match[1] = value >> 16;
        break;
    case 0x38: /* TAPR */
        s->prescale[0] = value;
        break;
    case 0x3c: /* TBPR */
        s->prescale[1] = value;
        break;
    case 0x40: /* TAPMR */
        s->match_prescale[0] = value;
        break;
    case 0x44: /* TBPMR */
        s->match_prescale[0] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "GPTM: write at bad offset 0x02%" HWADDR_PRIx "\n",
                      offset);
    }
    gptm_update_irq(s);
}

static const MemoryRegionOps gptm_ops = {
    .read = gptm_read,
    .write = gptm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stellaris_gptm = {
    .name = "stellaris_gptm",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(config, gptm_state),
        VMSTATE_UINT32_ARRAY(mode, gptm_state, 2),
        VMSTATE_UINT32(control, gptm_state),
        VMSTATE_UINT32(state, gptm_state),
        VMSTATE_UINT32(mask, gptm_state),
        VMSTATE_UNUSED(8),
        VMSTATE_UINT32_ARRAY(load, gptm_state, 2),
        VMSTATE_UINT32_ARRAY(match, gptm_state, 2),
        VMSTATE_UINT32_ARRAY(prescale, gptm_state, 2),
        VMSTATE_UINT32_ARRAY(match_prescale, gptm_state, 2),
        VMSTATE_UINT32(rtc, gptm_state),
        VMSTATE_INT64_ARRAY(tick, gptm_state, 2),
        VMSTATE_TIMER_PTR_ARRAY(timer, gptm_state, 2),
        VMSTATE_CLOCK(clk, gptm_state),
        VMSTATE_END_OF_LIST()
    }
};

static void stellaris_gptm_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    gptm_state *s = STELLARIS_GPTM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out(dev, &s->trigger, 1);

    memory_region_init_io(&s->iomem, obj, &gptm_ops, s,
                          "gptm", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    s->opaque[0] = s->opaque[1] = s;

    /*
     * TODO: in an ideal world we would model the effects of changing
     * the input clock frequency while the countdown timer is active.
     * The best way to do this would be to convert the device to use
     * ptimer instead of hand-rolling its own timer. This would also
     * make it easy to implement reading the current count from the
     * TAR and TBR registers.
     */
    s->clk = qdev_init_clock_in(dev, "clk", NULL, NULL, 0);
}

static void stellaris_gptm_realize(DeviceState *dev, Error **errp)
{
    gptm_state *s = STELLARIS_GPTM(dev);

    if (!clock_has_source(s->clk)) {
        error_setg(errp, "stellaris-gptm: clk must be connected");
        return;
    }

    s->timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, gptm_tick, &s->opaque[0]);
    s->timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, gptm_tick, &s->opaque[1]);
}

static void stellaris_gptm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_stellaris_gptm;
    dc->realize = stellaris_gptm_realize;
}

static const TypeInfo stellaris_gptm_info = {
    .name          = TYPE_STELLARIS_GPTM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(gptm_state),
    .instance_init = stellaris_gptm_init,
    .class_init    = stellaris_gptm_class_init,
};

static void stellaris_gptm_register_types(void)
{
    type_register_static(&stellaris_gptm_info);
}

type_init(stellaris_gptm_register_types)
