/* hw/s3c24xx_timers.c
 *
 * Samsung S3C24XX PWM emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "exec/address-spaces.h" /* get_system_memory */
#include "qemu/timer.h"

#include "s3c24xx.h"

/* The S3c24xx timer peripheral has five seperate timers. The first four (0-3)
 * have physical external connections and can be used for PWM control. The
 * fifth has no external connection but can generate interrupts because of this
 * it is almost always used to generate the Operating system clock tick
 * interrupt.
 *
 * The timers can be fed from the peripheral clock (pclk) or from one of two
 * external inputs (tclk0 and 1). The external inputs are split so tclk0 is
 * used for timer 0 and 1 and tclk1 feeds the remaining three timers.
 *
 * The emulation presented here only iplements the fifth timer (timer 4) as
 * there is no sensible way to interpret the external physical PWM signals from
 * timers 0 to 4 yet.
 *
 * ticks_per_sec is ticks per second for the qemu clocks
 * TCLK1 is the assumed input for timer4
 * Thus, period in ticks of timer4 is:
 *
 * (timer4_period * ticks_per_sec) / TCLK1
 */

/* Timer configuration 0 */
#define S3C_TIMERS_TCFG0 0
/* Timer configuration 1 */
#define S3C_TIMERS_TCFG1 1
/* Timer control */
#define S3C_TIMERS_TCON 2
/* Timer count buffer 0 */
#define S3C_TIMERS_TCNTB0 3
/* Timer compare buffer 0 */
#define S3C_TIMERS_TCMPB0 4
/* Timer count observation 0 */
#define S3C_TIMERS_TCNTO0 5
/* Timer count buffer 1 */
#define S3C_TIMERS_TCNTB1 6
/* Timer compare buffer 1 */
#define S3C_TIMERS_TCMPB1 7
/* Timer count observation 1 */
#define S3C_TIMERS_TCNTO1 8
/* Timer count buffer 2 */
#define S3C_TIMERS_TCNTB2 9
/* Timer compare buffer 2 */
#define S3C_TIMERS_TCMPB2 10
/* Timer count observation 2 */
#define S3C_TIMERS_TCNTO2 11
/* Timer count buffer 3 */
#define S3C_TIMERS_TCNTB3 12
/* Timer compare buffer 3 */
#define S3C_TIMERS_TCMPB3 13
/* Timer count observation 3 */
#define S3C_TIMERS_TCNTO3 14
/* Timer count buffer 4 */
#define S3C_TIMERS_TCNTB4 15
/* Timer count observation 4 */
#define S3C_TIMERS_TCNTO4 16

/* timer controller state */
struct s3c24xx_timers_state_s {
    MemoryRegion mmio;
    uint32_t tclk0; /* first timer clock source frequency */
    uint32_t tclk1; /* second timer clock source frequency */

    uint32_t timers_reg[17]; /* registers */

    /* resources for each timer */
    QEMUTimer *timer[5];
    qemu_irq irqs[5];
    uint32_t timer_reload_value[5];
    int64_t timer_last_ticked[5];

};


static void
s3c24xx_schedule_timer(struct s3c24xx_timers_state_s *s, int num)
{
    s->timers_reg[S3C_TIMERS_TCNTB4] = s->timer_reload_value[num];
    s->timer_last_ticked[num] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(s->timer[num],
                   s->timer_last_ticked[num] +
                   ((s->timer_reload_value[num] * get_ticks_per_sec()) / s->tclk1));
}

static void
s3c24xx_timer4_tick(void *opaque)
{
    struct s3c24xx_timers_state_s *s = (struct s3c24xx_timers_state_s *)opaque;

    /* set IRQ */
    qemu_set_irq(s->irqs[4], 1);

    /* if auto reload is set rescedule the next tick */
    if (s->timers_reg[S3C_TIMERS_TCON] & (1<<22)) {
        s3c24xx_schedule_timer(s, 4);
    }
}

static void s3c24xx_timers_write_f(void *opaque, hwaddr addr_,
                                   uint64_t value, unsigned size)
{
    struct s3c24xx_timers_state_s *s = opaque;
    int addr = (addr_ >> 2) & 0x1f;

    s->timers_reg[addr] = value;

    if (addr == S3C_TIMERS_TCON) {
        if (value & (1 << 21)) {
            /* Timer4 manual update is set, copy in the reload value */
            s->timer_reload_value[4] = s->timers_reg[S3C_TIMERS_TCNTB4];
        } else {
            /* Timer4 manual update is not set */
            if (value & (1 << 20)) {
                /* The timer is supposed to be running so start it  */
                s3c24xx_schedule_timer(s, 4);
            }
        }
    }
}

static uint64_t
s3c24xx_timers_read_f(void *opaque, hwaddr addr_, unsigned size)
{
    struct s3c24xx_timers_state_s *s = opaque;
    int addr = (addr_ >> 2) & 0x1f;

    if (addr == S3C_TIMERS_TCNTO4 ) {
        return s->timer_reload_value[4] -
            (((qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->timer_last_ticked[4]) * s->tclk1) / get_ticks_per_sec());
    }
    return s->timers_reg[addr];
}

static const MemoryRegionOps s3c24xx_timers_ops = {
    .read = s3c24xx_timers_read_f,
    .write = s3c24xx_timers_write_f,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void s3c24xx_timers_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_timers_state_s *s = (struct s3c24xx_timers_state_s *)opaque;
    int i;

    for (i = 0; i < 17; i ++)
        qemu_put_be32s(f, &s->timers_reg[i]);
}

static int s3c24xx_timers_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_timers_state_s *s = (struct s3c24xx_timers_state_s *)opaque;
    int i;

    for (i = 0; i < 17; i ++)
        qemu_get_be32s(f, &s->timers_reg[i]);

    return 0;
}

/* S3c24xx timer initialisation */
struct s3c24xx_timers_state_s *
s3c24xx_timers_init(S3CState *soc, hwaddr base_addr, uint32_t tclk0, uint32_t tclk1)
{
    MemoryRegion *system_memory = get_system_memory();
    struct s3c24xx_timers_state_s *s;
    int i;

    s = g_malloc0(sizeof(struct s3c24xx_timers_state_s));

    memory_region_init_io(&s->mmio, OBJECT(s),
                          &s3c24xx_timers_ops, s, "s3c24xx-timers", 17 * 4);
    memory_region_add_subregion(system_memory, base_addr, &s->mmio);

    register_savevm(NULL, "s3c24xx_timers", 0, 0, s3c24xx_timers_save, s3c24xx_timers_load, s);

    s->tclk0 = tclk0;
    s->tclk1 = tclk1;

    /* set up per timer values */
    for (i = 0; i < 5; i++) {
        s->irqs[i] = s3c24xx_get_irq(soc->irq, 10 + i);
        s->timer_reload_value[i] = 0;
        s->timer_last_ticked[i] = 0;
    }

    s->timer[4] = timer_new_ns(QEMU_CLOCK_VIRTUAL, s3c24xx_timer4_tick, s);

    return s;
}
