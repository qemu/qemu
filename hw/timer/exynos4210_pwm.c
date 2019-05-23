/*
 * Samsung exynos4210 Pulse Width Modulation Timer
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/ptimer.h"

#include "hw/arm/exynos4210.h"

//#define DEBUG_PWM

#ifdef DEBUG_PWM
#define DPRINTF(fmt, ...) \
        do { fprintf(stdout, "PWM: [%24s:%5d] " fmt, __func__, __LINE__, \
                ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define     EXYNOS4210_PWM_TIMERS_NUM      5
#define     EXYNOS4210_PWM_REG_MEM_SIZE    0x50

#define     TCFG0        0x0000
#define     TCFG1        0x0004
#define     TCON         0x0008
#define     TCNTB0       0x000C
#define     TCMPB0       0x0010
#define     TCNTO0       0x0014
#define     TCNTB1       0x0018
#define     TCMPB1       0x001C
#define     TCNTO1       0x0020
#define     TCNTB2       0x0024
#define     TCMPB2       0x0028
#define     TCNTO2       0x002C
#define     TCNTB3       0x0030
#define     TCMPB3       0x0034
#define     TCNTO3       0x0038
#define     TCNTB4       0x003C
#define     TCNTO4       0x0040
#define     TINT_CSTAT   0x0044

#define     TCNTB(x)    (0xC * (x))
#define     TCMPB(x)    (0xC * (x) + 1)
#define     TCNTO(x)    (0xC * (x) + 2)

#define GET_PRESCALER(reg, x) (((reg) & (0xFF << (8 * (x)))) >> 8 * (x))
#define GET_DIVIDER(reg, x) (1 << (((reg) & (0xF << (4 * (x)))) >> (4 * (x))))

/*
 * Attention! Timer4 doesn't have OUTPUT_INVERTER,
 * so Auto Reload bit is not accessible by macros!
 */
#define     TCON_TIMER_BASE(x)          (((x) ? 1 : 0) * 4 + 4 * (x))
#define     TCON_TIMER_START(x)         (1 << (TCON_TIMER_BASE(x) + 0))
#define     TCON_TIMER_MANUAL_UPD(x)    (1 << (TCON_TIMER_BASE(x) + 1))
#define     TCON_TIMER_OUTPUT_INV(x)    (1 << (TCON_TIMER_BASE(x) + 2))
#define     TCON_TIMER_AUTO_RELOAD(x)   (1 << (TCON_TIMER_BASE(x) + 3))
#define     TCON_TIMER4_AUTO_RELOAD     (1 << 22)

#define     TINT_CSTAT_STATUS(x)        (1 << (5 + (x)))
#define     TINT_CSTAT_ENABLE(x)        (1 << (x))

/* timer struct */
typedef struct {
    uint32_t    id;             /* timer id */
    qemu_irq    irq;            /* local timer irq */
    uint32_t    freq;           /* timer frequency */

    /* use ptimer.c to represent count down timer */
    ptimer_state *ptimer;       /* timer  */

    /* registers */
    uint32_t    reg_tcntb;      /* counter register buffer */
    uint32_t    reg_tcmpb;      /* compare register buffer */

    struct Exynos4210PWMState *parent;

} Exynos4210PWM;

#define TYPE_EXYNOS4210_PWM "exynos4210.pwm"
#define EXYNOS4210_PWM(obj) \
    OBJECT_CHECK(Exynos4210PWMState, (obj), TYPE_EXYNOS4210_PWM)

typedef struct Exynos4210PWMState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t    reg_tcfg[2];
    uint32_t    reg_tcon;
    uint32_t    reg_tint_cstat;

    Exynos4210PWM timer[EXYNOS4210_PWM_TIMERS_NUM];

} Exynos4210PWMState;

/*** VMState ***/
static const VMStateDescription vmstate_exynos4210_pwm = {
    .name = "exynos4210.pwm.pwm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, Exynos4210PWM),
        VMSTATE_UINT32(freq, Exynos4210PWM),
        VMSTATE_PTIMER(ptimer, Exynos4210PWM),
        VMSTATE_UINT32(reg_tcntb, Exynos4210PWM),
        VMSTATE_UINT32(reg_tcmpb, Exynos4210PWM),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_pwm_state = {
    .name = "exynos4210.pwm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg_tcfg, Exynos4210PWMState, 2),
        VMSTATE_UINT32(reg_tcon, Exynos4210PWMState),
        VMSTATE_UINT32(reg_tint_cstat, Exynos4210PWMState),
        VMSTATE_STRUCT_ARRAY(timer, Exynos4210PWMState,
            EXYNOS4210_PWM_TIMERS_NUM, 0,
        vmstate_exynos4210_pwm, Exynos4210PWM),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * PWM update frequency
 */
static void exynos4210_pwm_update_freq(Exynos4210PWMState *s, uint32_t id)
{
    uint32_t freq;
    freq = s->timer[id].freq;
    if (id > 1) {
        s->timer[id].freq = 24000000 /
        ((GET_PRESCALER(s->reg_tcfg[0], 1) + 1) *
                (GET_DIVIDER(s->reg_tcfg[1], id)));
    } else {
        s->timer[id].freq = 24000000 /
        ((GET_PRESCALER(s->reg_tcfg[0], 0) + 1) *
                (GET_DIVIDER(s->reg_tcfg[1], id)));
    }

    if (freq != s->timer[id].freq) {
        ptimer_set_freq(s->timer[id].ptimer, s->timer[id].freq);
        DPRINTF("freq=%dHz\n", s->timer[id].freq);
    }
}

/*
 * Counter tick handler
 */
static void exynos4210_pwm_tick(void *opaque)
{
    Exynos4210PWM *s = (Exynos4210PWM *)opaque;
    Exynos4210PWMState *p = (Exynos4210PWMState *)s->parent;
    uint32_t id = s->id;
    bool cmp;

    DPRINTF("timer %d tick\n", id);

    /* set irq status */
    p->reg_tint_cstat |= TINT_CSTAT_STATUS(id);

    /* raise IRQ */
    if (p->reg_tint_cstat & TINT_CSTAT_ENABLE(id)) {
        DPRINTF("timer %d IRQ\n", id);
        qemu_irq_raise(p->timer[id].irq);
    }

    /* reload timer */
    if (id != 4) {
        cmp = p->reg_tcon & TCON_TIMER_AUTO_RELOAD(id);
    } else {
        cmp = p->reg_tcon & TCON_TIMER4_AUTO_RELOAD;
    }

    if (cmp) {
        DPRINTF("auto reload timer %d count to %x\n", id,
                p->timer[id].reg_tcntb);
        ptimer_set_count(p->timer[id].ptimer, p->timer[id].reg_tcntb);
        ptimer_run(p->timer[id].ptimer, 1);
    } else {
        /* stop timer, set status to STOP, see Basic Timer Operation */
        p->reg_tcon &= ~TCON_TIMER_START(id);
        ptimer_stop(p->timer[id].ptimer);
    }
}

/*
 * PWM Read
 */
static uint64_t exynos4210_pwm_read(void *opaque, hwaddr offset,
        unsigned size)
{
    Exynos4210PWMState *s = (Exynos4210PWMState *)opaque;
    uint32_t value = 0;
    int index;

    switch (offset) {
    case TCFG0: case TCFG1:
        index = (offset - TCFG0) >> 2;
        value = s->reg_tcfg[index];
        break;

    case TCON:
        value = s->reg_tcon;
        break;

    case TCNTB0: case TCNTB1:
    case TCNTB2: case TCNTB3: case TCNTB4:
        index = (offset - TCNTB0) / 0xC;
        value = s->timer[index].reg_tcntb;
        break;

    case TCMPB0: case TCMPB1:
    case TCMPB2: case TCMPB3:
        index = (offset - TCMPB0) / 0xC;
        value = s->timer[index].reg_tcmpb;
        break;

    case TCNTO0: case TCNTO1:
    case TCNTO2: case TCNTO3: case TCNTO4:
        index = (offset == TCNTO4) ? 4 : (offset - TCNTO0) / 0xC;
        value = ptimer_get_count(s->timer[index].ptimer);
        break;

    case TINT_CSTAT:
        value = s->reg_tint_cstat;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "exynos4210.pwm: bad read offset " TARGET_FMT_plx,
                      offset);
        break;
    }
    return value;
}

/*
 * PWM Write
 */
static void exynos4210_pwm_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    Exynos4210PWMState *s = (Exynos4210PWMState *)opaque;
    int index;
    uint32_t new_val;
    int i;

    switch (offset) {
    case TCFG0: case TCFG1:
        index = (offset - TCFG0) >> 2;
        s->reg_tcfg[index] = value;

        /* update timers frequencies */
        for (i = 0; i < EXYNOS4210_PWM_TIMERS_NUM; i++) {
            exynos4210_pwm_update_freq(s, s->timer[i].id);
        }
        break;

    case TCON:
        for (i = 0; i < EXYNOS4210_PWM_TIMERS_NUM; i++) {
            if ((value & TCON_TIMER_MANUAL_UPD(i)) >
            (s->reg_tcon & TCON_TIMER_MANUAL_UPD(i))) {
                /*
                 * TCNTB and TCMPB are loaded into TCNT and TCMP.
                 * Update timers.
                 */

                /* this will start timer to run, this ok, because
                 * during processing start bit timer will be stopped
                 * if needed */
                ptimer_set_count(s->timer[i].ptimer, s->timer[i].reg_tcntb);
                DPRINTF("set timer %d count to %x\n", i,
                        s->timer[i].reg_tcntb);
            }

            if ((value & TCON_TIMER_START(i)) >
            (s->reg_tcon & TCON_TIMER_START(i))) {
                /* changed to start */
                ptimer_run(s->timer[i].ptimer, 1);
                DPRINTF("run timer %d\n", i);
            }

            if ((value & TCON_TIMER_START(i)) <
                    (s->reg_tcon & TCON_TIMER_START(i))) {
                /* changed to stop */
                ptimer_stop(s->timer[i].ptimer);
                DPRINTF("stop timer %d\n", i);
            }
        }
        s->reg_tcon = value;
        break;

    case TCNTB0: case TCNTB1:
    case TCNTB2: case TCNTB3: case TCNTB4:
        index = (offset - TCNTB0) / 0xC;
        s->timer[index].reg_tcntb = value;
        break;

    case TCMPB0: case TCMPB1:
    case TCMPB2: case TCMPB3:
        index = (offset - TCMPB0) / 0xC;
        s->timer[index].reg_tcmpb = value;
        break;

    case TINT_CSTAT:
        new_val = (s->reg_tint_cstat & 0x3E0) + (0x1F & value);
        new_val &= ~(0x3E0 & value);

        for (i = 0; i < EXYNOS4210_PWM_TIMERS_NUM; i++) {
            if ((new_val & TINT_CSTAT_STATUS(i)) <
                    (s->reg_tint_cstat & TINT_CSTAT_STATUS(i))) {
                qemu_irq_lower(s->timer[i].irq);
            }
        }

        s->reg_tint_cstat = new_val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "exynos4210.pwm: bad write offset " TARGET_FMT_plx,
                      offset);
        break;

    }
}

/*
 * Set default values to timer fields and registers
 */
static void exynos4210_pwm_reset(DeviceState *d)
{
    Exynos4210PWMState *s = EXYNOS4210_PWM(d);
    int i;
    s->reg_tcfg[0] = 0x0101;
    s->reg_tcfg[1] = 0x0;
    s->reg_tcon = 0;
    s->reg_tint_cstat = 0;
    for (i = 0; i < EXYNOS4210_PWM_TIMERS_NUM; i++) {
        s->timer[i].reg_tcmpb = 0;
        s->timer[i].reg_tcntb = 0;

        exynos4210_pwm_update_freq(s, s->timer[i].id);
        ptimer_stop(s->timer[i].ptimer);
    }
}

static const MemoryRegionOps exynos4210_pwm_ops = {
    .read = exynos4210_pwm_read,
    .write = exynos4210_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * PWM timer initialization
 */
static void exynos4210_pwm_init(Object *obj)
{
    Exynos4210PWMState *s = EXYNOS4210_PWM(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    int i;
    QEMUBH *bh;

    for (i = 0; i < EXYNOS4210_PWM_TIMERS_NUM; i++) {
        bh = qemu_bh_new(exynos4210_pwm_tick, &s->timer[i]);
        sysbus_init_irq(dev, &s->timer[i].irq);
        s->timer[i].ptimer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
        s->timer[i].id = i;
        s->timer[i].parent = s;
    }

    memory_region_init_io(&s->iomem, obj, &exynos4210_pwm_ops, s,
                          "exynos4210-pwm", EXYNOS4210_PWM_REG_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void exynos4210_pwm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_pwm_reset;
    dc->vmsd = &vmstate_exynos4210_pwm_state;
}

static const TypeInfo exynos4210_pwm_info = {
    .name          = TYPE_EXYNOS4210_PWM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210PWMState),
    .instance_init = exynos4210_pwm_init,
    .class_init    = exynos4210_pwm_class_init,
};

static void exynos4210_pwm_register_types(void)
{
    type_register_static(&exynos4210_pwm_info);
}

type_init(exynos4210_pwm_register_types)
