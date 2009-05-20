/*
 * QEMU model of the Xilinx timer block.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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

#include "sysbus.h"
#include "sysemu.h"
#include "qemu-timer.h"

#define D(x)

#define R_TCSR     0
#define R_TLR      1
#define R_TCR      2
#define R_MAX      4

#define TCSR_MDT        (1<<0)
#define TCSR_UDT        (1<<1)
#define TCSR_GENT       (1<<2)
#define TCSR_CAPT       (1<<3)
#define TCSR_ARHT       (1<<4)
#define TCSR_LOAD       (1<<5)
#define TCSR_ENIT       (1<<6)
#define TCSR_ENT        (1<<7)
#define TCSR_TINT       (1<<8)
#define TCSR_PWMA       (1<<9)
#define TCSR_ENALL      (1<<10)

struct xlx_timer
{
    QEMUBH *bh;
    ptimer_state *ptimer;
    void *parent;
    int nr; /* for debug.  */

    unsigned long timer_div;

    uint32_t regs[R_MAX];
};

struct timerblock
{
    SysBusDevice busdev;
    qemu_irq irq;
    unsigned int nr_timers;
    struct xlx_timer *timers;
};

static inline unsigned int timer_from_addr(target_phys_addr_t addr)
{
    /* Timers get a 4x32bit control reg area each.  */
    return addr >> 2;
}

static void timer_update_irq(struct timerblock *t)
{
    unsigned int i, irq = 0;
    uint32_t csr;

    for (i = 0; i < t->nr_timers; i++) {
        csr = t->timers[i].regs[R_TCSR];
        irq |= (csr & TCSR_TINT) && (csr & TCSR_ENIT);
    }

    /* All timers within the same slave share a single IRQ line.  */
    qemu_set_irq(t->irq, !!irq);
}

static uint32_t timer_readl (void *opaque, target_phys_addr_t addr)
{
    struct timerblock *t = opaque;
    struct xlx_timer *xt;
    uint32_t r = 0;
    unsigned int timer;

    addr >>= 2;
    timer = timer_from_addr(addr);
    xt = &t->timers[timer];
    /* Further decoding to address a specific timers reg.  */
    addr &= 0x3;
    switch (addr)
    {
        case R_TCR:
                r = ptimer_get_count(xt->ptimer);
                if (!(xt->regs[R_TCSR] & TCSR_UDT))
                    r = ~r;
                D(qemu_log("xlx_timer t=%d read counter=%x udt=%d\n",
                         timer, r, xt->regs[R_TCSR] & TCSR_UDT));
            break;
        default:
            if (addr < ARRAY_SIZE(xt->regs))
                r = xt->regs[addr];
            break;

    }
    D(printf("%s timer=%d %x=%x\n", __func__, timer, addr * 4, r));
    return r;
}

static void timer_enable(struct xlx_timer *xt)
{
    uint64_t count;

    D(printf("%s timer=%d down=%d\n", __func__,
              xt->nr, xt->regs[R_TCSR] & TCSR_UDT));

    ptimer_stop(xt->ptimer);

    if (xt->regs[R_TCSR] & TCSR_UDT)
        count = xt->regs[R_TLR];
    else
        count = ~0 - xt->regs[R_TLR];
    ptimer_set_count(xt->ptimer, count);
    ptimer_run(xt->ptimer, 1);
}

static void
timer_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct timerblock *t = opaque;
    struct xlx_timer *xt;
    unsigned int timer;

    addr >>= 2;
    timer = timer_from_addr(addr);
    xt = &t->timers[timer];
    D(printf("%s addr=%x val=%x (timer=%d off=%d)\n",
             __func__, addr * 4, value, timer, addr & 3));
    /* Further decoding to address a specific timers reg.  */
    addr &= 3;
    switch (addr) 
    {
        case R_TCSR:
            if (value & TCSR_TINT)
                value &= ~TCSR_TINT;

            xt->regs[addr] = value;
            if (value & TCSR_ENT)
                timer_enable(xt);
            break;
 
        default:
            if (addr < ARRAY_SIZE(xt->regs))
                xt->regs[addr] = value;
            break;
    }
    timer_update_irq(t);
}

static CPUReadMemoryFunc *timer_read[] = {
    NULL, NULL,
    &timer_readl,
};

static CPUWriteMemoryFunc *timer_write[] = {
    NULL, NULL,
    &timer_writel,
};

static void timer_hit(void *opaque)
{
    struct xlx_timer *xt = opaque;
    struct timerblock *t = xt->parent;
    D(printf("%s %d\n", __func__, timer));
    xt->regs[R_TCSR] |= TCSR_TINT;

    if (xt->regs[R_TCSR] & TCSR_ARHT)
        timer_enable(xt);
    timer_update_irq(t);
}

static void xilinx_timer_init(SysBusDevice *dev)
{
    struct timerblock *t = FROM_SYSBUS(typeof (*t), dev);
    unsigned int i;
    int timer_regs, freq_hz;

    /* All timers share a single irq line.  */
    sysbus_init_irq(dev, &t->irq);

    /* Init all the ptimers.  */
    freq_hz = qdev_get_prop_int(&dev->qdev, "frequency", 2);
    t->nr_timers = qdev_get_prop_int(&dev->qdev, "nr-timers", 2);
    t->timers = qemu_mallocz(sizeof t->timers[0] * t->nr_timers);
    for (i = 0; i < t->nr_timers; i++) {
        struct xlx_timer *xt = &t->timers[i];

        xt->parent = t;
        xt->nr = i;
        xt->bh = qemu_bh_new(timer_hit, xt);
        xt->ptimer = ptimer_init(xt->bh);
        ptimer_set_freq(xt->ptimer, freq_hz);
    }

    timer_regs = cpu_register_io_memory(0, timer_read, timer_write, t);
    sysbus_init_mmio(dev, R_MAX * 4 * t->nr_timers, timer_regs);
}

static void xilinx_timer_register(void)
{
    sysbus_register_dev("xilinx,timer", sizeof (struct timerblock),
                        xilinx_timer_init);
}

device_init(xilinx_timer_register)
