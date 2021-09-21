/*
 * SiFive PWM
 *
 * Copyright (c) 2020 Western Digital
 *
 * Author:  Alistair Francis <alistair.francis@wdc.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "hw/irq.h"
#include "hw/timer/sifive_pwm.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define HAS_PWM_EN_BITS(cfg) ((cfg & R_CONFIG_ENONESHOT_MASK) || \
                              (cfg & R_CONFIG_ENALWAYS_MASK))

#define PWMCMP_MASK 0xFFFF
#define PWMCOUNT_MASK 0x7FFFFFFF

REG32(CONFIG,                   0x00)
    FIELD(CONFIG, SCALE,            0, 4)
    FIELD(CONFIG, STICKY,           8, 1)
    FIELD(CONFIG, ZEROCMP,          9, 1)
    FIELD(CONFIG, DEGLITCH,         10, 1)
    FIELD(CONFIG, ENALWAYS,         12, 1)
    FIELD(CONFIG, ENONESHOT,        13, 1)
    FIELD(CONFIG, CMP0CENTER,       16, 1)
    FIELD(CONFIG, CMP1CENTER,       17, 1)
    FIELD(CONFIG, CMP2CENTER,       18, 1)
    FIELD(CONFIG, CMP3CENTER,       19, 1)
    FIELD(CONFIG, CMP0GANG,         24, 1)
    FIELD(CONFIG, CMP1GANG,         25, 1)
    FIELD(CONFIG, CMP2GANG,         26, 1)
    FIELD(CONFIG, CMP3GANG,         27, 1)
    FIELD(CONFIG, CMP0IP,           28, 1)
    FIELD(CONFIG, CMP1IP,           29, 1)
    FIELD(CONFIG, CMP2IP,           30, 1)
    FIELD(CONFIG, CMP3IP,           31, 1)
REG32(COUNT,                    0x08)
REG32(PWMS,                     0x10)
REG32(PWMCMP0,                  0x20)
REG32(PWMCMP1,                  0x24)
REG32(PWMCMP2,                  0x28)
REG32(PWMCMP3,                  0x2C)

static inline uint64_t sifive_pwm_ns_to_ticks(SiFivePwmState *s,
                                                uint64_t time)
{
    return muldiv64(time, s->freq_hz, NANOSECONDS_PER_SECOND);
}

static inline uint64_t sifive_pwm_ticks_to_ns(SiFivePwmState *s,
                                                uint64_t ticks)
{
    return muldiv64(ticks, NANOSECONDS_PER_SECOND, s->freq_hz);
}

static inline uint64_t sifive_pwm_compute_scale(SiFivePwmState *s)
{
    return s->pwmcfg & R_CONFIG_SCALE_MASK;
}

static void sifive_pwm_set_alarms(SiFivePwmState *s)
{
    uint64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (HAS_PWM_EN_BITS(s->pwmcfg)) {
        /*
         * Subtract ticks from number of ticks when the timer was zero
         * and mask to the register width.
         */
        uint64_t pwmcount = (sifive_pwm_ns_to_ticks(s, now_ns) -
                             s->tick_offset) & PWMCOUNT_MASK;
        uint64_t scale = sifive_pwm_compute_scale(s);
        /* PWMs only contains PWMCMP_MASK bits starting at scale */
        uint64_t pwms = (pwmcount & (PWMCMP_MASK << scale)) >> scale;

        for (int i = 0; i < SIFIVE_PWM_CHANS; i++) {
            uint64_t pwmcmp = s->pwmcmp[i] & PWMCMP_MASK;
            uint64_t pwmcmp_ticks = pwmcmp << scale;

            /*
             * Per circuit diagram and spec, both cases raises corresponding
             * IP bit one clock cycle after time expires.
             */
            if (pwmcmp > pwms) {
                uint64_t offset = pwmcmp_ticks - pwmcount + 1;
                uint64_t when_to_fire = now_ns +
                                          sifive_pwm_ticks_to_ns(s, offset);

                trace_sifive_pwm_set_alarm(when_to_fire, now_ns);
                timer_mod(&s->timer[i], when_to_fire);
            } else {
                /* Schedule interrupt for next cycle */
                trace_sifive_pwm_set_alarm(now_ns + 1, now_ns);
                timer_mod(&s->timer[i], now_ns + 1);
            }

        }
    } else {
        /*
         * If timer incrementing disabled, just do pwms > pwmcmp check since
         * a write may have happened to PWMs.
         */
        uint64_t pwmcount = (s->tick_offset) & PWMCOUNT_MASK;
        uint64_t scale = sifive_pwm_compute_scale(s);
        uint64_t pwms = (pwmcount & (PWMCMP_MASK << scale)) >> scale;

        for (int i = 0; i < SIFIVE_PWM_CHANS; i++) {
            uint64_t pwmcmp = s->pwmcmp[i] & PWMCMP_MASK;

            if (pwms >= pwmcmp) {
                trace_sifive_pwm_set_alarm(now_ns + 1, now_ns);
                timer_mod(&s->timer[i], now_ns + 1);
            } else {
                /* Effectively disable timer by scheduling far in future. */
                trace_sifive_pwm_set_alarm(0xFFFFFFFFFFFFFF, now_ns);
                timer_mod(&s->timer[i], 0xFFFFFFFFFFFFFF);
            }
        }
    }
}

static void sifive_pwm_interrupt(SiFivePwmState *s, int num)
{
    uint64_t now = sifive_pwm_ns_to_ticks(s,
                                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    bool was_incrementing = HAS_PWM_EN_BITS(s->pwmcfg);

    trace_sifive_pwm_interrupt(num);

    s->pwmcfg |= R_CONFIG_CMP0IP_MASK << num;
    qemu_irq_raise(s->irqs[num]);

    /*
     * If the zerocmp is set and pwmcmp0 raised the interrupt
     * reset the zero ticks.
     */
    if ((s->pwmcfg & R_CONFIG_ZEROCMP_MASK) && (num == 0)) {
        /* If reset signal conditions, disable ENONESHOT. */
        s->pwmcfg &= ~R_CONFIG_ENONESHOT_MASK;

        if (was_incrementing) {
            /* If incrementing, time in ticks is when pwmcount is zero */
            s->tick_offset = now;
        } else {
            /* If not incrementing, pwmcount = 0 */
            s->tick_offset = 0;
        }
    }

    /*
     * If carryout bit set, which we discern via looking for overflow,
     * also reset ENONESHOT.
     */
    if (was_incrementing &&
        ((now & PWMCOUNT_MASK) < (s->tick_offset & PWMCOUNT_MASK))) {
        s->pwmcfg &= ~R_CONFIG_ENONESHOT_MASK;
    }

    /* Schedule or disable interrupts */
    sifive_pwm_set_alarms(s);

    /* If was enabled, and now not enabled, switch tick rep */
    if (was_incrementing && !HAS_PWM_EN_BITS(s->pwmcfg)) {
        s->tick_offset = (now - s->tick_offset) & PWMCOUNT_MASK;
    }
}

static void sifive_pwm_interrupt_0(void *opaque)
{
    SiFivePwmState *s = opaque;

    sifive_pwm_interrupt(s, 0);
}

static void sifive_pwm_interrupt_1(void *opaque)
{
    SiFivePwmState *s = opaque;

    sifive_pwm_interrupt(s, 1);
}

static void sifive_pwm_interrupt_2(void *opaque)
{
    SiFivePwmState *s = opaque;

    sifive_pwm_interrupt(s, 2);
}

static void sifive_pwm_interrupt_3(void *opaque)
{
    SiFivePwmState *s = opaque;

    sifive_pwm_interrupt(s, 3);
}

static uint64_t sifive_pwm_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    SiFivePwmState *s = opaque;
    uint64_t cur_time, scale;
    uint64_t now = sifive_pwm_ns_to_ticks(s,
                                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));

    trace_sifive_pwm_read(addr);

    switch (addr) {
    case A_CONFIG:
        return s->pwmcfg;
    case A_COUNT:
        cur_time = s->tick_offset;

        if (HAS_PWM_EN_BITS(s->pwmcfg)) {
            cur_time = now - cur_time;
        }

        /*
         * Return the value in the counter with bit 31 always 0
         * This is allowed to wrap around so we don't need to check that.
         */
        return cur_time & PWMCOUNT_MASK;
    case A_PWMS:
        cur_time = s->tick_offset;
        scale = sifive_pwm_compute_scale(s);

        if (HAS_PWM_EN_BITS(s->pwmcfg)) {
            cur_time = now - cur_time;
        }

        return ((cur_time & PWMCOUNT_MASK) >> scale) & PWMCMP_MASK;
    case A_PWMCMP0:
        return s->pwmcmp[0] & PWMCMP_MASK;
    case A_PWMCMP1:
        return s->pwmcmp[1] & PWMCMP_MASK;
    case A_PWMCMP2:
        return s->pwmcmp[2] & PWMCMP_MASK;
    case A_PWMCMP3:
        return s->pwmcmp[3] & PWMCMP_MASK;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return 0;
}

static void sifive_pwm_write(void *opaque, hwaddr addr,
                               uint64_t val64, unsigned int size)
{
    SiFivePwmState *s = opaque;
    uint32_t value = val64;
    uint64_t new_offset, scale;
    uint64_t now = sifive_pwm_ns_to_ticks(s,
                                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));

    trace_sifive_pwm_write(value, addr);

    switch (addr) {
    case A_CONFIG:
        if (value & (R_CONFIG_CMP0CENTER_MASK | R_CONFIG_CMP1CENTER_MASK |
                     R_CONFIG_CMP2CENTER_MASK | R_CONFIG_CMP3CENTER_MASK)) {
            qemu_log_mask(LOG_UNIMP, "%s: CMPxCENTER is not supported\n",
                          __func__);
        }

        if (value & (R_CONFIG_CMP0GANG_MASK | R_CONFIG_CMP1GANG_MASK |
                     R_CONFIG_CMP2GANG_MASK | R_CONFIG_CMP3GANG_MASK)) {
            qemu_log_mask(LOG_UNIMP, "%s: CMPxGANG is not supported\n",
                          __func__);
        }

        if (value & (R_CONFIG_CMP0IP_MASK | R_CONFIG_CMP1IP_MASK |
                     R_CONFIG_CMP2IP_MASK | R_CONFIG_CMP3IP_MASK)) {
            qemu_log_mask(LOG_UNIMP, "%s: CMPxIP is not supported\n",
                          __func__);
        }

        if (!(value & R_CONFIG_CMP0IP_MASK)) {
            qemu_irq_lower(s->irqs[0]);
        }

        if (!(value & R_CONFIG_CMP1IP_MASK)) {
            qemu_irq_lower(s->irqs[1]);
        }

        if (!(value & R_CONFIG_CMP2IP_MASK)) {
            qemu_irq_lower(s->irqs[2]);
        }

        if (!(value & R_CONFIG_CMP3IP_MASK)) {
            qemu_irq_lower(s->irqs[3]);
        }

        /*
         * If this write enables the timer increment
         * set the time when pwmcount was zero to be cur_time - pwmcount.
         * If this write disables the timer increment
         * convert back from pwmcount to the time in ticks
         * when pwmcount was zero.
         */
        if ((!HAS_PWM_EN_BITS(s->pwmcfg) && HAS_PWM_EN_BITS(value)) ||
            (HAS_PWM_EN_BITS(s->pwmcfg) && !HAS_PWM_EN_BITS(value))) {
            s->tick_offset = (now - s->tick_offset) & PWMCOUNT_MASK;
        }

        s->pwmcfg = value;
        break;
    case A_COUNT:
        /* The guest changed the counter, updated the offset value. */
        new_offset = value;

        if (HAS_PWM_EN_BITS(s->pwmcfg)) {
            new_offset = now - new_offset;
        }

        s->tick_offset = new_offset;
        break;
    case A_PWMS:
        scale = sifive_pwm_compute_scale(s);
        new_offset = (((value & PWMCMP_MASK) << scale) & PWMCOUNT_MASK);

        if (HAS_PWM_EN_BITS(s->pwmcfg)) {
            new_offset = now - new_offset;
        }

        s->tick_offset = new_offset;
        break;
    case A_PWMCMP0:
        s->pwmcmp[0] = value & PWMCMP_MASK;
        break;
    case A_PWMCMP1:
        s->pwmcmp[1] = value & PWMCMP_MASK;
        break;
    case A_PWMCMP2:
        s->pwmcmp[2] = value & PWMCMP_MASK;
        break;
    case A_PWMCMP3:
        s->pwmcmp[3] = value & PWMCMP_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }

    /* Update the alarms to reflect possible updated values */
    sifive_pwm_set_alarms(s);
}

static void sifive_pwm_reset(DeviceState *dev)
{
    SiFivePwmState *s = SIFIVE_PWM(dev);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->pwmcfg = 0x00000000;
    s->pwmcmp[0] = 0x00000000;
    s->pwmcmp[1] = 0x00000000;
    s->pwmcmp[2] = 0x00000000;
    s->pwmcmp[3] = 0x00000000;

    s->tick_offset = sifive_pwm_ns_to_ticks(s, now);
}

static const MemoryRegionOps sifive_pwm_ops = {
    .read = sifive_pwm_read,
    .write = sifive_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_sifive_pwm = {
    .name = TYPE_SIFIVE_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_ARRAY(timer, SiFivePwmState, 4),
        VMSTATE_UINT64(tick_offset, SiFivePwmState),
        VMSTATE_UINT32(pwmcfg, SiFivePwmState),
        VMSTATE_UINT32_ARRAY(pwmcmp, SiFivePwmState, 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property sifive_pwm_properties[] = {
    /* 0.5Ghz per spec after FSBL */
    DEFINE_PROP_UINT64("clock-frequency", struct SiFivePwmState,
                       freq_hz, 500000000ULL),
    DEFINE_PROP_END_OF_LIST(),
};

static void sifive_pwm_init(Object *obj)
{
    SiFivePwmState *s = SIFIVE_PWM(obj);
    int i;

    for (i = 0; i < SIFIVE_PWM_IRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irqs[i]);
    }

    memory_region_init_io(&s->mmio, obj, &sifive_pwm_ops, s,
                          TYPE_SIFIVE_PWM, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void sifive_pwm_realize(DeviceState *dev, Error **errp)
{
    SiFivePwmState *s = SIFIVE_PWM(dev);

    timer_init_ns(&s->timer[0], QEMU_CLOCK_VIRTUAL,
                  sifive_pwm_interrupt_0, s);

    timer_init_ns(&s->timer[1], QEMU_CLOCK_VIRTUAL,
                  sifive_pwm_interrupt_1, s);

    timer_init_ns(&s->timer[2], QEMU_CLOCK_VIRTUAL,
                  sifive_pwm_interrupt_2, s);

    timer_init_ns(&s->timer[3], QEMU_CLOCK_VIRTUAL,
                  sifive_pwm_interrupt_3, s);
}

static void sifive_pwm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = sifive_pwm_reset;
    device_class_set_props(dc, sifive_pwm_properties);
    dc->vmsd = &vmstate_sifive_pwm;
    dc->realize = sifive_pwm_realize;
}

static const TypeInfo sifive_pwm_info = {
    .name          = TYPE_SIFIVE_PWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFivePwmState),
    .instance_init = sifive_pwm_init,
    .class_init    = sifive_pwm_class_init,
};

static void sifive_pwm_register_types(void)
{
    type_register_static(&sifive_pwm_info);
}

type_init(sifive_pwm_register_types)
