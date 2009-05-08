/*
 * TI OMAP processors emulation.
 *
 * Copyright (C) 2007-2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "arm-misc.h"
#include "omap.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "flash.h"
#include "soc_dma.h"
#include "audio/audio.h"

/* GP timers */
struct omap_gp_timer_s {
    qemu_irq irq;
    qemu_irq wkup;
    qemu_irq in;
    qemu_irq out;
    omap_clk clk;
    QEMUTimer *timer;
    QEMUTimer *match;
    struct omap_target_agent_s *ta;

    int in_val;
    int out_val;
    int64_t time;
    int64_t rate;
    int64_t ticks_per_sec;

    int16_t config;
    int status;
    int it_ena;
    int wu_ena;
    int enable;
    int inout;
    int capt2;
    int pt;
    enum {
        gpt_trigger_none, gpt_trigger_overflow, gpt_trigger_both
    } trigger;
    enum {
        gpt_capture_none, gpt_capture_rising,
        gpt_capture_falling, gpt_capture_both
    } capture;
    int scpwm;
    int ce;
    int pre;
    int ptv;
    int ar;
    int st;
    int posted;
    uint32_t val;
    uint32_t load_val;
    uint32_t capture_val[2];
    uint32_t match_val;
    int capt_num;

    uint16_t writeh;	/* LSB */
    uint16_t readh;	/* MSB */
};

#define GPT_TCAR_IT	(1 << 2)
#define GPT_OVF_IT	(1 << 1)
#define GPT_MAT_IT	(1 << 0)

static inline void omap_gp_timer_intr(struct omap_gp_timer_s *timer, int it)
{
    if (timer->it_ena & it) {
        if (!timer->status)
            qemu_irq_raise(timer->irq);

        timer->status |= it;
        /* Or are the status bits set even when masked?
         * i.e. is masking applied before or after the status register?  */
    }

    if (timer->wu_ena & it)
        qemu_irq_pulse(timer->wkup);
}

static inline void omap_gp_timer_out(struct omap_gp_timer_s *timer, int level)
{
    if (!timer->inout && timer->out_val != level) {
        timer->out_val = level;
        qemu_set_irq(timer->out, level);
    }
}

static inline uint32_t omap_gp_timer_read(struct omap_gp_timer_s *timer)
{
    uint64_t distance;

    if (timer->st && timer->rate) {
        distance = qemu_get_clock(vm_clock) - timer->time;
        distance = muldiv64(distance, timer->rate, timer->ticks_per_sec);

        if (distance >= 0xffffffff - timer->val)
            return 0xffffffff;
        else
            return timer->val + distance;
    } else
        return timer->val;
}

static inline void omap_gp_timer_sync(struct omap_gp_timer_s *timer)
{
    if (timer->st) {
        timer->val = omap_gp_timer_read(timer);
        timer->time = qemu_get_clock(vm_clock);
    }
}

static inline void omap_gp_timer_update(struct omap_gp_timer_s *timer)
{
    int64_t expires, matches;

    if (timer->st && timer->rate) {
        expires = muldiv64(0x100000000ll - timer->val,
                        timer->ticks_per_sec, timer->rate);
        qemu_mod_timer(timer->timer, timer->time + expires);

        if (timer->ce && timer->match_val >= timer->val) {
            matches = muldiv64(timer->match_val - timer->val,
                            timer->ticks_per_sec, timer->rate);
            qemu_mod_timer(timer->match, timer->time + matches);
        } else
            qemu_del_timer(timer->match);
    } else {
        qemu_del_timer(timer->timer);
        qemu_del_timer(timer->match);
        omap_gp_timer_out(timer, timer->scpwm);
    }
}

static inline void omap_gp_timer_trigger(struct omap_gp_timer_s *timer)
{
    if (timer->pt)
        /* TODO in overflow-and-match mode if the first event to
         * occur is the match, don't toggle.  */
        omap_gp_timer_out(timer, !timer->out_val);
    else
        /* TODO inverted pulse on timer->out_val == 1?  */
        qemu_irq_pulse(timer->out);
}

static void omap_gp_timer_tick(void *opaque)
{
    struct omap_gp_timer_s *timer = (struct omap_gp_timer_s *) opaque;

    if (!timer->ar) {
        timer->st = 0;
        timer->val = 0;
    } else {
        timer->val = timer->load_val;
        timer->time = qemu_get_clock(vm_clock);
    }

    if (timer->trigger == gpt_trigger_overflow ||
                    timer->trigger == gpt_trigger_both)
        omap_gp_timer_trigger(timer);

    omap_gp_timer_intr(timer, GPT_OVF_IT);
    omap_gp_timer_update(timer);
}

static void omap_gp_timer_match(void *opaque)
{
    struct omap_gp_timer_s *timer = (struct omap_gp_timer_s *) opaque;

    if (timer->trigger == gpt_trigger_both)
        omap_gp_timer_trigger(timer);

    omap_gp_timer_intr(timer, GPT_MAT_IT);
}

static void omap_gp_timer_input(void *opaque, int line, int on)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;
    int trigger;

    switch (s->capture) {
    default:
    case gpt_capture_none:
        trigger = 0;
        break;
    case gpt_capture_rising:
        trigger = !s->in_val && on;
        break;
    case gpt_capture_falling:
        trigger = s->in_val && !on;
        break;
    case gpt_capture_both:
        trigger = (s->in_val == !on);
        break;
    }
    s->in_val = on;

    if (s->inout && trigger && s->capt_num < 2) {
        s->capture_val[s->capt_num] = omap_gp_timer_read(s);

        if (s->capt2 == s->capt_num ++)
            omap_gp_timer_intr(s, GPT_TCAR_IT);
    }
}

static void omap_gp_timer_clk_update(void *opaque, int line, int on)
{
    struct omap_gp_timer_s *timer = (struct omap_gp_timer_s *) opaque;

    omap_gp_timer_sync(timer);
    timer->rate = on ? omap_clk_getrate(timer->clk) : 0;
    omap_gp_timer_update(timer);
}

static void omap_gp_timer_clk_setup(struct omap_gp_timer_s *timer)
{
    omap_clk_adduser(timer->clk,
                    qemu_allocate_irqs(omap_gp_timer_clk_update, timer, 1)[0]);
    timer->rate = omap_clk_getrate(timer->clk);
}

static void omap_gp_timer_reset(struct omap_gp_timer_s *s)
{
    s->config = 0x000;
    s->status = 0;
    s->it_ena = 0;
    s->wu_ena = 0;
    s->inout = 0;
    s->capt2 = 0;
    s->capt_num = 0;
    s->pt = 0;
    s->trigger = gpt_trigger_none;
    s->capture = gpt_capture_none;
    s->scpwm = 0;
    s->ce = 0;
    s->pre = 0;
    s->ptv = 0;
    s->ar = 0;
    s->st = 0;
    s->posted = 1;
    s->val = 0x00000000;
    s->load_val = 0x00000000;
    s->capture_val[0] = 0x00000000;
    s->capture_val[1] = 0x00000000;
    s->match_val = 0x00000000;
    omap_gp_timer_update(s);
}

static uint32_t omap_gp_timer_readw(void *opaque, target_phys_addr_t addr)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;

    switch (addr) {
    case 0x00:	/* TIDR */
        return 0x21;

    case 0x10:	/* TIOCP_CFG */
        return s->config;

    case 0x14:	/* TISTAT */
        /* ??? When's this bit reset? */
        return 1;						/* RESETDONE */

    case 0x18:	/* TISR */
        return s->status;

    case 0x1c:	/* TIER */
        return s->it_ena;

    case 0x20:	/* TWER */
        return s->wu_ena;

    case 0x24:	/* TCLR */
        return (s->inout << 14) |
                (s->capt2 << 13) |
                (s->pt << 12) |
                (s->trigger << 10) |
                (s->capture << 8) |
                (s->scpwm << 7) |
                (s->ce << 6) |
                (s->pre << 5) |
                (s->ptv << 2) |
                (s->ar << 1) |
                (s->st << 0);

    case 0x28:	/* TCRR */
        return omap_gp_timer_read(s);

    case 0x2c:	/* TLDR */
        return s->load_val;

    case 0x30:	/* TTGR */
        return 0xffffffff;

    case 0x34:	/* TWPS */
        return 0x00000000;	/* No posted writes pending.  */

    case 0x38:	/* TMAR */
        return s->match_val;

    case 0x3c:	/* TCAR1 */
        return s->capture_val[0];

    case 0x40:	/* TSICR */
        return s->posted << 2;

    case 0x44:	/* TCAR2 */
        return s->capture_val[1];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap_gp_timer_readh(void *opaque, target_phys_addr_t addr)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;
    uint32_t ret;

    if (addr & 2)
        return s->readh;
    else {
        ret = omap_gp_timer_readw(opaque, addr);
        s->readh = ret >> 16;
        return ret & 0xffff;
    }
}

static CPUReadMemoryFunc *omap_gp_timer_readfn[] = {
    omap_badwidth_read32,
    omap_gp_timer_readh,
    omap_gp_timer_readw,
};

static void omap_gp_timer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;

    switch (addr) {
    case 0x00:	/* TIDR */
    case 0x14:	/* TISTAT */
    case 0x34:	/* TWPS */
    case 0x3c:	/* TCAR1 */
    case 0x44:	/* TCAR2 */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* TIOCP_CFG */
        s->config = value & 0x33d;
        if (((value >> 3) & 3) == 3)				/* IDLEMODE */
            fprintf(stderr, "%s: illegal IDLEMODE value in TIOCP_CFG\n",
                            __FUNCTION__);
        if (value & 2)						/* SOFTRESET */
            omap_gp_timer_reset(s);
        break;

    case 0x18:	/* TISR */
        if (value & GPT_TCAR_IT)
            s->capt_num = 0;
        if (s->status && !(s->status &= ~value))
            qemu_irq_lower(s->irq);
        break;

    case 0x1c:	/* TIER */
        s->it_ena = value & 7;
        break;

    case 0x20:	/* TWER */
        s->wu_ena = value & 7;
        break;

    case 0x24:	/* TCLR */
        omap_gp_timer_sync(s);
        s->inout = (value >> 14) & 1;
        s->capt2 = (value >> 13) & 1;
        s->pt = (value >> 12) & 1;
        s->trigger = (value >> 10) & 3;
        if (s->capture == gpt_capture_none &&
                        ((value >> 8) & 3) != gpt_capture_none)
            s->capt_num = 0;
        s->capture = (value >> 8) & 3;
        s->scpwm = (value >> 7) & 1;
        s->ce = (value >> 6) & 1;
        s->pre = (value >> 5) & 1;
        s->ptv = (value >> 2) & 7;
        s->ar = (value >> 1) & 1;
        s->st = (value >> 0) & 1;
        if (s->inout && s->trigger != gpt_trigger_none)
            fprintf(stderr, "%s: GP timer pin must be an output "
                            "for this trigger mode\n", __FUNCTION__);
        if (!s->inout && s->capture != gpt_capture_none)
            fprintf(stderr, "%s: GP timer pin must be an input "
                            "for this capture mode\n", __FUNCTION__);
        if (s->trigger == gpt_trigger_none)
            omap_gp_timer_out(s, s->scpwm);
        /* TODO: make sure this doesn't overflow 32-bits */
        s->ticks_per_sec = ticks_per_sec << (s->pre ? s->ptv + 1 : 0);
        omap_gp_timer_update(s);
        break;

    case 0x28:	/* TCRR */
        s->time = qemu_get_clock(vm_clock);
        s->val = value;
        omap_gp_timer_update(s);
        break;

    case 0x2c:	/* TLDR */
        s->load_val = value;
        break;

    case 0x30:	/* TTGR */
        s->time = qemu_get_clock(vm_clock);
        s->val = s->load_val;
        omap_gp_timer_update(s);
        break;

    case 0x38:	/* TMAR */
        omap_gp_timer_sync(s);
        s->match_val = value;
        omap_gp_timer_update(s);
        break;

    case 0x40:	/* TSICR */
        s->posted = (value >> 2) & 1;
        if (value & 2)	/* How much exactly are we supposed to reset? */
            omap_gp_timer_reset(s);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static void omap_gp_timer_writeh(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;

    if (addr & 2)
        return omap_gp_timer_write(opaque, addr, (value << 16) | s->writeh);
    else
        s->writeh = (uint16_t) value;
}

static CPUWriteMemoryFunc *omap_gp_timer_writefn[] = {
    omap_badwidth_write32,
    omap_gp_timer_writeh,
    omap_gp_timer_write,
};

struct omap_gp_timer_s *omap_gp_timer_init(struct omap_target_agent_s *ta,
                qemu_irq irq, omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *)
            qemu_mallocz(sizeof(struct omap_gp_timer_s));

    s->ta = ta;
    s->irq = irq;
    s->clk = fclk;
    s->timer = qemu_new_timer(vm_clock, omap_gp_timer_tick, s);
    s->match = qemu_new_timer(vm_clock, omap_gp_timer_match, s);
    s->in = qemu_allocate_irqs(omap_gp_timer_input, s, 1)[0];
    omap_gp_timer_reset(s);
    omap_gp_timer_clk_setup(s);

    iomemtype = l4_register_io_memory(0, omap_gp_timer_readfn,
                    omap_gp_timer_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;
}

/* 32-kHz Sync Timer of the OMAP2 */
static uint32_t omap_synctimer_read(struct omap_synctimer_s *s) {
    return muldiv64(qemu_get_clock(vm_clock), 0x8000, ticks_per_sec);
}

static void omap_synctimer_reset(struct omap_synctimer_s *s)
{
    s->val = omap_synctimer_read(s);
}

static uint32_t omap_synctimer_readw(void *opaque, target_phys_addr_t addr)
{
    struct omap_synctimer_s *s = (struct omap_synctimer_s *) opaque;

    switch (addr) {
    case 0x00:	/* 32KSYNCNT_REV */
        return 0x21;

    case 0x10:	/* CR */
        return omap_synctimer_read(s) - s->val;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap_synctimer_readh(void *opaque, target_phys_addr_t addr)
{
    struct omap_synctimer_s *s = (struct omap_synctimer_s *) opaque;
    uint32_t ret;

    if (addr & 2)
        return s->readh;
    else {
        ret = omap_synctimer_readw(opaque, addr);
        s->readh = ret >> 16;
        return ret & 0xffff;
    }
}

static CPUReadMemoryFunc *omap_synctimer_readfn[] = {
    omap_badwidth_read32,
    omap_synctimer_readh,
    omap_synctimer_readw,
};

static void omap_synctimer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_BAD_REG(addr);
}

static CPUWriteMemoryFunc *omap_synctimer_writefn[] = {
    omap_badwidth_write32,
    omap_synctimer_write,
    omap_synctimer_write,
};

void omap_synctimer_init(struct omap_target_agent_s *ta,
                struct omap_mpu_state_s *mpu, omap_clk fclk, omap_clk iclk)
{
    struct omap_synctimer_s *s = &mpu->synctimer;

    omap_synctimer_reset(s);
    omap_l4_attach(ta, 0, l4_register_io_memory(0,
                      omap_synctimer_readfn, omap_synctimer_writefn, s));
}

/* General-Purpose Interface of OMAP2 */
struct omap2_gpio_s {
    qemu_irq irq[2];
    qemu_irq wkup;
    qemu_irq *in;
    qemu_irq handler[32];

    uint8_t config[2];
    uint32_t inputs;
    uint32_t outputs;
    uint32_t dir;
    uint32_t level[2];
    uint32_t edge[2];
    uint32_t mask[2];
    uint32_t wumask;
    uint32_t ints[2];
    uint32_t debounce;
    uint8_t delay;
};

static inline void omap_gpio_module_int_update(struct omap2_gpio_s *s,
                int line)
{
    qemu_set_irq(s->irq[line], s->ints[line] & s->mask[line]);
}

static void omap_gpio_module_wake(struct omap2_gpio_s *s, int line)
{
    if (!(s->config[0] & (1 << 2)))			/* ENAWAKEUP */
        return;
    if (!(s->config[0] & (3 << 3)))			/* Force Idle */
        return;
    if (!(s->wumask & (1 << line)))
        return;

    qemu_irq_raise(s->wkup);
}

static inline void omap_gpio_module_out_update(struct omap2_gpio_s *s,
                uint32_t diff)
{
    int ln;

    s->outputs ^= diff;
    diff &= ~s->dir;
    while ((ln = ffs(diff))) {
        ln --;
        qemu_set_irq(s->handler[ln], (s->outputs >> ln) & 1);
        diff &= ~(1 << ln);
    }
}

static void omap_gpio_module_level_update(struct omap2_gpio_s *s, int line)
{
    s->ints[line] |= s->dir &
            ((s->inputs & s->level[1]) | (~s->inputs & s->level[0]));
    omap_gpio_module_int_update(s, line);
}

static inline void omap_gpio_module_int(struct omap2_gpio_s *s, int line)
{
    s->ints[0] |= 1 << line;
    omap_gpio_module_int_update(s, 0);
    s->ints[1] |= 1 << line;
    omap_gpio_module_int_update(s, 1);
    omap_gpio_module_wake(s, line);
}

static void omap_gpio_module_set(void *opaque, int line, int level)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;

    if (level) {
        if (s->dir & (1 << line) & ((~s->inputs & s->edge[0]) | s->level[1]))
            omap_gpio_module_int(s, line);
        s->inputs |= 1 << line;
    } else {
        if (s->dir & (1 << line) & ((s->inputs & s->edge[1]) | s->level[0]))
            omap_gpio_module_int(s, line);
        s->inputs &= ~(1 << line);
    }
}

static void omap_gpio_module_reset(struct omap2_gpio_s *s)
{
    s->config[0] = 0;
    s->config[1] = 2;
    s->ints[0] = 0;
    s->ints[1] = 0;
    s->mask[0] = 0;
    s->mask[1] = 0;
    s->wumask = 0;
    s->dir = ~0;
    s->level[0] = 0;
    s->level[1] = 0;
    s->edge[0] = 0;
    s->edge[1] = 0;
    s->debounce = 0;
    s->delay = 0;
}

static uint32_t omap_gpio_module_read(void *opaque, target_phys_addr_t addr)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
        return 0x18;

    case 0x10:	/* GPIO_SYSCONFIG */
        return s->config[0];

    case 0x14:	/* GPIO_SYSSTATUS */
        return 0x01;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        return s->ints[0];

    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
        return s->mask[0];

    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
        return s->wumask;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        return s->ints[1];

    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        return s->mask[1];

    case 0x30:	/* GPIO_CTRL */
        return s->config[1];

    case 0x34:	/* GPIO_OE */
        return s->dir;

    case 0x38:	/* GPIO_DATAIN */
        return s->inputs;

    case 0x3c:	/* GPIO_DATAOUT */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        return s->outputs;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        return s->level[0];

    case 0x44:	/* GPIO_LEVELDETECT1 */
        return s->level[1];

    case 0x48:	/* GPIO_RISINGDETECT */
        return s->edge[0];

    case 0x4c:	/* GPIO_FALLINGDETECT */
        return s->edge[1];

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        return s->debounce;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        return s->delay;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpio_module_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap2_gpio_s *s = (struct omap2_gpio_s *) opaque;
    uint32_t diff;
    int ln;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
        if (((value >> 3) & 3) == 3)
            fprintf(stderr, "%s: bad IDLEMODE value\n", __FUNCTION__);
        if (value & 2)
            omap_gpio_module_reset(s);
        s->config[0] = value & 0x1d;
        break;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        if (s->ints[0] & value) {
            s->ints[0] &= ~value;
            omap_gpio_module_level_update(s, 0);
        }
        break;

    case 0x1c:	/* GPIO_IRQENABLE1 */
        s->mask[0] = value;
        omap_gpio_module_int_update(s, 0);
        break;

    case 0x20:	/* GPIO_WAKEUPENABLE */
        s->wumask = value;
        break;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        if (s->ints[1] & value) {
            s->ints[1] &= ~value;
            omap_gpio_module_level_update(s, 1);
        }
        break;

    case 0x2c:	/* GPIO_IRQENABLE2 */
        s->mask[1] = value;
        omap_gpio_module_int_update(s, 1);
        break;

    case 0x30:	/* GPIO_CTRL */
        s->config[1] = value & 7;
        break;

    case 0x34:	/* GPIO_OE */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            diff &= ~(1 <<-- ln);
            qemu_set_irq(s->handler[ln], (value >> ln) & 1);
        }

        omap_gpio_module_level_update(s, 0);
        omap_gpio_module_level_update(s, 1);
        break;

    case 0x3c:	/* GPIO_DATAOUT */
        omap_gpio_module_out_update(s, s->outputs ^ value);
        break;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        s->level[0] = value;
        omap_gpio_module_level_update(s, 0);
        omap_gpio_module_level_update(s, 1);
        break;

    case 0x44:	/* GPIO_LEVELDETECT1 */
        s->level[1] = value;
        omap_gpio_module_level_update(s, 0);
        omap_gpio_module_level_update(s, 1);
        break;

    case 0x48:	/* GPIO_RISINGDETECT */
        s->edge[0] = value;
        break;

    case 0x4c:	/* GPIO_FALLINGDETECT */
        s->edge[1] = value;
        break;

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        s->debounce = value;
        break;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        s->delay = value;
        break;

    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
        s->mask[0] &= ~value;
        omap_gpio_module_int_update(s, 0);
        break;

    case 0x64:	/* GPIO_SETIRQENABLE1 */
        s->mask[0] |= value;
        omap_gpio_module_int_update(s, 0);
        break;

    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
        s->mask[1] &= ~value;
        omap_gpio_module_int_update(s, 1);
        break;

    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        s->mask[1] |= value;
        omap_gpio_module_int_update(s, 1);
        break;

    case 0x80:	/* GPIO_CLEARWKUENA */
        s->wumask &= ~value;
        break;

    case 0x84:	/* GPIO_SETWKUENA */
        s->wumask |= value;
        break;

    case 0x90:	/* GPIO_CLEARDATAOUT */
        omap_gpio_module_out_update(s, s->outputs & value);
        break;

    case 0x94:	/* GPIO_SETDATAOUT */
        omap_gpio_module_out_update(s, ~s->outputs & value);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static uint32_t omap_gpio_module_readp(void *opaque, target_phys_addr_t addr)
{
    return omap_gpio_module_readp(opaque, addr) >> ((addr & 3) << 3);
}

static void omap_gpio_module_writep(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    uint32_t cur = 0;
    uint32_t mask = 0xffff;

    switch (addr & ~3) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x30:	/* GPIO_CTRL */
    case 0x34:	/* GPIO_OE */
    case 0x3c:	/* GPIO_DATAOUT */
    case 0x40:	/* GPIO_LEVELDETECT0 */
    case 0x44:	/* GPIO_LEVELDETECT1 */
    case 0x48:	/* GPIO_RISINGDETECT */
    case 0x4c:	/* GPIO_FALLINGDETECT */
    case 0x50:	/* GPIO_DEBOUNCENABLE */
    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        cur = omap_gpio_module_read(opaque, addr & ~3) &
                ~(mask << ((addr & 3) << 3));

        /* Fall through.  */
    case 0x18:	/* GPIO_IRQSTATUS1 */
    case 0x28:	/* GPIO_IRQSTATUS2 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        value <<= (addr & 3) << 3;
        omap_gpio_module_write(opaque, addr, cur | value);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_gpio_module_readfn[] = {
    omap_gpio_module_readp,
    omap_gpio_module_readp,
    omap_gpio_module_read,
};

static CPUWriteMemoryFunc *omap_gpio_module_writefn[] = {
    omap_gpio_module_writep,
    omap_gpio_module_writep,
    omap_gpio_module_write,
};

static void omap_gpio_module_init(struct omap2_gpio_s *s,
                struct omap_target_agent_s *ta, int region,
                qemu_irq mpu, qemu_irq dsp, qemu_irq wkup,
                omap_clk fclk, omap_clk iclk)
{
    int iomemtype;

    s->irq[0] = mpu;
    s->irq[1] = dsp;
    s->wkup = wkup;
    s->in = qemu_allocate_irqs(omap_gpio_module_set, s, 32);

    iomemtype = l4_register_io_memory(0, omap_gpio_module_readfn,
                    omap_gpio_module_writefn, s);
    omap_l4_attach(ta, region, iomemtype);
}

struct omap_gpif_s {
    struct omap2_gpio_s module[5];
    int modules;

    int autoidle;
    int gpo;
};

static void omap_gpif_reset(struct omap_gpif_s *s)
{
    int i;

    for (i = 0; i < s->modules; i ++)
        omap_gpio_module_reset(s->module + i);

    s->autoidle = 0;
    s->gpo = 0;
}

static uint32_t omap_gpif_top_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_gpif_s *s = (struct omap_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
        return 0x18;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        return s->autoidle;

    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
        return 0x01;

    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
        return 0x00;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        return s->gpo;

    case 0x50:	/* IPGENERICOCPSPL_GPI */
        return 0x00;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpif_top_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gpif_s *s = (struct omap_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
    case 0x50:	/* IPGENERICOCPSPL_GPI */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        if (value & (1 << 1))					/* SOFTRESET */
            omap_gpif_reset(s);
        s->autoidle = value & 1;
        break;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        s->gpo = value & 1;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_gpif_top_readfn[] = {
    omap_gpif_top_read,
    omap_gpif_top_read,
    omap_gpif_top_read,
};

static CPUWriteMemoryFunc *omap_gpif_top_writefn[] = {
    omap_gpif_top_write,
    omap_gpif_top_write,
    omap_gpif_top_write,
};

struct omap_gpif_s *omap2_gpio_init(struct omap_target_agent_s *ta,
                qemu_irq *irq, omap_clk *fclk, omap_clk iclk, int modules)
{
    int iomemtype, i;
    struct omap_gpif_s *s = (struct omap_gpif_s *)
            qemu_mallocz(sizeof(struct omap_gpif_s));
    int region[4] = { 0, 2, 4, 5 };

    s->modules = modules;
    for (i = 0; i < modules; i ++)
        omap_gpio_module_init(s->module + i, ta, region[i],
                        irq[i], 0, 0, fclk[i], iclk);

    omap_gpif_reset(s);

    iomemtype = l4_register_io_memory(0, omap_gpif_top_readfn,
                    omap_gpif_top_writefn, s);
    omap_l4_attach(ta, 1, iomemtype);

    return s;
}

qemu_irq *omap2_gpio_in_get(struct omap_gpif_s *s, int start)
{
    if (start >= s->modules * 32 || start < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, start);
    return s->module[start >> 5].in + (start & 31);
}

void omap2_gpio_out_set(struct omap_gpif_s *s, int line, qemu_irq handler)
{
    if (line >= s->modules * 32 || line < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, line);
    s->module[line >> 5].handler[line & 31] = handler;
}

/* Multichannel SPI */
struct omap_mcspi_s {
    qemu_irq irq;
    int chnum;

    uint32_t sysconfig;
    uint32_t systest;
    uint32_t irqst;
    uint32_t irqen;
    uint32_t wken;
    uint32_t control;

    struct omap_mcspi_ch_s {
        qemu_irq txdrq;
        qemu_irq rxdrq;
        uint32_t (*txrx)(void *opaque, uint32_t, int);
        void *opaque;

        uint32_t tx;
        uint32_t rx;

        uint32_t config;
        uint32_t status;
        uint32_t control;
    } ch[4];
};

static inline void omap_mcspi_interrupt_update(struct omap_mcspi_s *s)
{
    qemu_set_irq(s->irq, s->irqst & s->irqen);
}

static inline void omap_mcspi_dmarequest_update(struct omap_mcspi_ch_s *ch)
{
    qemu_set_irq(ch->txdrq,
                    (ch->control & 1) &&		/* EN */
                    (ch->config & (1 << 14)) &&		/* DMAW */
                    (ch->status & (1 << 1)) &&		/* TXS */
                    ((ch->config >> 12) & 3) != 1);	/* TRM */
    qemu_set_irq(ch->rxdrq,
                    (ch->control & 1) &&		/* EN */
                    (ch->config & (1 << 15)) &&		/* DMAW */
                    (ch->status & (1 << 0)) &&		/* RXS */
                    ((ch->config >> 12) & 3) != 2);	/* TRM */
}

static void omap_mcspi_transfer_run(struct omap_mcspi_s *s, int chnum)
{
    struct omap_mcspi_ch_s *ch = s->ch + chnum;

    if (!(ch->control & 1))				/* EN */
        return;
    if ((ch->status & (1 << 0)) &&			/* RXS */
                    ((ch->config >> 12) & 3) != 2 &&	/* TRM */
                    !(ch->config & (1 << 19)))		/* TURBO */
        goto intr_update;
    if ((ch->status & (1 << 1)) &&			/* TXS */
                    ((ch->config >> 12) & 3) != 1)	/* TRM */
        goto intr_update;

    if (!(s->control & 1) ||				/* SINGLE */
                    (ch->config & (1 << 20))) {		/* FORCE */
        if (ch->txrx)
            ch->rx = ch->txrx(ch->opaque, ch->tx,	/* WL */
                            1 + (0x1f & (ch->config >> 7)));
    }

    ch->tx = 0;
    ch->status |= 1 << 2;				/* EOT */
    ch->status |= 1 << 1;				/* TXS */
    if (((ch->config >> 12) & 3) != 2)			/* TRM */
        ch->status |= 1 << 0;				/* RXS */

intr_update:
    if ((ch->status & (1 << 0)) &&			/* RXS */
                    ((ch->config >> 12) & 3) != 2 &&	/* TRM */
                    !(ch->config & (1 << 19)))		/* TURBO */
        s->irqst |= 1 << (2 + 4 * chnum);		/* RX_FULL */
    if ((ch->status & (1 << 1)) &&			/* TXS */
                    ((ch->config >> 12) & 3) != 1)	/* TRM */
        s->irqst |= 1 << (0 + 4 * chnum);		/* TX_EMPTY */
    omap_mcspi_interrupt_update(s);
    omap_mcspi_dmarequest_update(ch);
}

static void omap_mcspi_reset(struct omap_mcspi_s *s)
{
    int ch;

    s->sysconfig = 0;
    s->systest = 0;
    s->irqst = 0;
    s->irqen = 0;
    s->wken = 0;
    s->control = 4;

    for (ch = 0; ch < 4; ch ++) {
        s->ch[ch].config = 0x060000;
        s->ch[ch].status = 2;				/* TXS */
        s->ch[ch].control = 0;

        omap_mcspi_dmarequest_update(s->ch + ch);
    }

    omap_mcspi_interrupt_update(s);
}

static uint32_t omap_mcspi_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    int ch = 0;
    uint32_t ret;

    switch (addr) {
    case 0x00:	/* MCSPI_REVISION */
        return 0x91;

    case 0x10:	/* MCSPI_SYSCONFIG */
        return s->sysconfig;

    case 0x14:	/* MCSPI_SYSSTATUS */
        return 1;					/* RESETDONE */

    case 0x18:	/* MCSPI_IRQSTATUS */
        return s->irqst;

    case 0x1c:	/* MCSPI_IRQENABLE */
        return s->irqen;

    case 0x20:	/* MCSPI_WAKEUPENABLE */
        return s->wken;

    case 0x24:	/* MCSPI_SYST */
        return s->systest;

    case 0x28:	/* MCSPI_MODULCTRL */
        return s->control;

    case 0x68: ch ++;
    case 0x54: ch ++;
    case 0x40: ch ++;
    case 0x2c:	/* MCSPI_CHCONF */
        return s->ch[ch].config;

    case 0x6c: ch ++;
    case 0x58: ch ++;
    case 0x44: ch ++;
    case 0x30:	/* MCSPI_CHSTAT */
        return s->ch[ch].status;

    case 0x70: ch ++;
    case 0x5c: ch ++;
    case 0x48: ch ++;
    case 0x34:	/* MCSPI_CHCTRL */
        return s->ch[ch].control;

    case 0x74: ch ++;
    case 0x60: ch ++;
    case 0x4c: ch ++;
    case 0x38:	/* MCSPI_TX */
        return s->ch[ch].tx;

    case 0x78: ch ++;
    case 0x64: ch ++;
    case 0x50: ch ++;
    case 0x3c:	/* MCSPI_RX */
        s->ch[ch].status &= ~(1 << 0);			/* RXS */
        ret = s->ch[ch].rx;
        omap_mcspi_transfer_run(s, ch);
        return ret;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mcspi_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    int ch = 0;

    switch (addr) {
    case 0x00:	/* MCSPI_REVISION */
    case 0x14:	/* MCSPI_SYSSTATUS */
    case 0x30:	/* MCSPI_CHSTAT0 */
    case 0x3c:	/* MCSPI_RX0 */
    case 0x44:	/* MCSPI_CHSTAT1 */
    case 0x50:	/* MCSPI_RX1 */
    case 0x58:	/* MCSPI_CHSTAT2 */
    case 0x64:	/* MCSPI_RX2 */
    case 0x6c:	/* MCSPI_CHSTAT3 */
    case 0x78:	/* MCSPI_RX3 */
        OMAP_RO_REG(addr);
        return;

    case 0x10:	/* MCSPI_SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_mcspi_reset(s);
        s->sysconfig = value & 0x31d;
        break;

    case 0x18:	/* MCSPI_IRQSTATUS */
        if (!((s->control & (1 << 3)) && (s->systest & (1 << 11)))) {
            s->irqst &= ~value;
            omap_mcspi_interrupt_update(s);
        }
        break;

    case 0x1c:	/* MCSPI_IRQENABLE */
        s->irqen = value & 0x1777f;
        omap_mcspi_interrupt_update(s);
        break;

    case 0x20:	/* MCSPI_WAKEUPENABLE */
        s->wken = value & 1;
        break;

    case 0x24:	/* MCSPI_SYST */
        if (s->control & (1 << 3))			/* SYSTEM_TEST */
            if (value & (1 << 11)) {			/* SSB */
                s->irqst |= 0x1777f;
                omap_mcspi_interrupt_update(s);
            }
        s->systest = value & 0xfff;
        break;

    case 0x28:	/* MCSPI_MODULCTRL */
        if (value & (1 << 3))				/* SYSTEM_TEST */
            if (s->systest & (1 << 11)) {		/* SSB */
                s->irqst |= 0x1777f;
                omap_mcspi_interrupt_update(s);
            }
        s->control = value & 0xf;
        break;

    case 0x68: ch ++;
    case 0x54: ch ++;
    case 0x40: ch ++;
    case 0x2c:	/* MCSPI_CHCONF */
        if ((value ^ s->ch[ch].config) & (3 << 14))	/* DMAR | DMAW */
            omap_mcspi_dmarequest_update(s->ch + ch);
        if (((value >> 12) & 3) == 3)			/* TRM */
            fprintf(stderr, "%s: invalid TRM value (3)\n", __FUNCTION__);
        if (((value >> 7) & 0x1f) < 3)			/* WL */
            fprintf(stderr, "%s: invalid WL value (%i)\n",
                            __FUNCTION__, (value >> 7) & 0x1f);
        s->ch[ch].config = value & 0x7fffff;
        break;

    case 0x70: ch ++;
    case 0x5c: ch ++;
    case 0x48: ch ++;
    case 0x34:	/* MCSPI_CHCTRL */
        if (value & ~s->ch[ch].control & 1) {		/* EN */
            s->ch[ch].control |= 1;
            omap_mcspi_transfer_run(s, ch);
        } else
            s->ch[ch].control = value & 1;
        break;

    case 0x74: ch ++;
    case 0x60: ch ++;
    case 0x4c: ch ++;
    case 0x38:	/* MCSPI_TX */
        s->ch[ch].tx = value;
        s->ch[ch].status &= ~(1 << 1);			/* TXS */
        omap_mcspi_transfer_run(s, ch);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_mcspi_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_mcspi_read,
};

static CPUWriteMemoryFunc *omap_mcspi_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_mcspi_write,
};

struct omap_mcspi_s *omap_mcspi_init(struct omap_target_agent_s *ta, int chnum,
                qemu_irq irq, qemu_irq *drq, omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap_mcspi_s *s = (struct omap_mcspi_s *)
            qemu_mallocz(sizeof(struct omap_mcspi_s));
    struct omap_mcspi_ch_s *ch = s->ch;

    s->irq = irq;
    s->chnum = chnum;
    while (chnum --) {
        ch->txdrq = *drq ++;
        ch->rxdrq = *drq ++;
        ch ++;
    }
    omap_mcspi_reset(s);

    iomemtype = l4_register_io_memory(0, omap_mcspi_readfn,
                    omap_mcspi_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;
}

void omap_mcspi_attach(struct omap_mcspi_s *s,
                uint32_t (*txrx)(void *opaque, uint32_t, int), void *opaque,
                int chipselect)
{
    if (chipselect < 0 || chipselect >= s->chnum)
        hw_error("%s: Bad chipselect %i\n", __FUNCTION__, chipselect);

    s->ch[chipselect].txrx = txrx;
    s->ch[chipselect].opaque = opaque;
}

/* Enhanced Audio Controller (CODEC only) */
struct omap_eac_s {
    qemu_irq irq;

    uint16_t sysconfig;
    uint8_t config[4];
    uint8_t control;
    uint8_t address;
    uint16_t data;
    uint8_t vtol;
    uint8_t vtsl;
    uint16_t mixer;
    uint16_t gain[4];
    uint8_t att;
    uint16_t max[7];

    struct {
        qemu_irq txdrq;
        qemu_irq rxdrq;
        uint32_t (*txrx)(void *opaque, uint32_t, int);
        void *opaque;

#define EAC_BUF_LEN 1024
        uint32_t rxbuf[EAC_BUF_LEN];
        int rxoff;
        int rxlen;
        int rxavail;
        uint32_t txbuf[EAC_BUF_LEN];
        int txlen;
        int txavail;

        int enable;
        int rate;

        uint16_t config[4];

        /* These need to be moved to the actual codec */
        QEMUSoundCard card;
        SWVoiceIn *in_voice;
        SWVoiceOut *out_voice;
        int hw_enable;
    } codec;

    struct {
        uint8_t control;
        uint16_t config;
    } modem, bt;
};

static inline void omap_eac_interrupt_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->irq, (s->codec.config[1] >> 14) & 1);	/* AURDI */
}

static inline void omap_eac_in_dmarequest_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->codec.rxdrq, (s->codec.rxavail || s->codec.rxlen) &&
                    ((s->codec.config[1] >> 12) & 1));		/* DMAREN */
}

static inline void omap_eac_out_dmarequest_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->codec.txdrq, s->codec.txlen < s->codec.txavail &&
                    ((s->codec.config[1] >> 11) & 1));		/* DMAWEN */
}

static inline void omap_eac_in_refill(struct omap_eac_s *s)
{
    int left = MIN(EAC_BUF_LEN - s->codec.rxlen, s->codec.rxavail) << 2;
    int start = ((s->codec.rxoff + s->codec.rxlen) & (EAC_BUF_LEN - 1)) << 2;
    int leftwrap = MIN(left, (EAC_BUF_LEN << 2) - start);
    int recv = 1;
    uint8_t *buf = (uint8_t *) s->codec.rxbuf + start;

    left -= leftwrap;
    start = 0;
    while (leftwrap && (recv = AUD_read(s->codec.in_voice, buf + start,
                                    leftwrap)) > 0) {	/* Be defensive */
        start += recv;
        leftwrap -= recv;
    }
    if (recv <= 0)
        s->codec.rxavail = 0;
    else
        s->codec.rxavail -= start >> 2;
    s->codec.rxlen += start >> 2;

    if (recv > 0 && left > 0) {
        start = 0;
        while (left && (recv = AUD_read(s->codec.in_voice,
                                        (uint8_t *) s->codec.rxbuf + start,
                                        left)) > 0) {	/* Be defensive */
            start += recv;
            left -= recv;
        }
        if (recv <= 0)
            s->codec.rxavail = 0;
        else
            s->codec.rxavail -= start >> 2;
        s->codec.rxlen += start >> 2;
    }
}

static inline void omap_eac_out_empty(struct omap_eac_s *s)
{
    int left = s->codec.txlen << 2;
    int start = 0;
    int sent = 1;

    while (left && (sent = AUD_write(s->codec.out_voice,
                                    (uint8_t *) s->codec.txbuf + start,
                                    left)) > 0) {	/* Be defensive */
        start += sent;
        left -= sent;
    }

    if (!sent) {
        s->codec.txavail = 0;
        omap_eac_out_dmarequest_update(s);
    }

    if (start)
        s->codec.txlen = 0;
}

static void omap_eac_in_cb(void *opaque, int avail_b)
{
    struct omap_eac_s *s = (struct omap_eac_s *) opaque;

    s->codec.rxavail = avail_b >> 2;
    omap_eac_in_refill(s);
    /* TODO: possibly discard current buffer if overrun */
    omap_eac_in_dmarequest_update(s);
}

static void omap_eac_out_cb(void *opaque, int free_b)
{
    struct omap_eac_s *s = (struct omap_eac_s *) opaque;

    s->codec.txavail = free_b >> 2;
    if (s->codec.txlen)
        omap_eac_out_empty(s);
    else
        omap_eac_out_dmarequest_update(s);
}

static void omap_eac_enable_update(struct omap_eac_s *s)
{
    s->codec.enable = !(s->codec.config[1] & 1) &&		/* EACPWD */
            (s->codec.config[1] & 2) &&				/* AUDEN */
            s->codec.hw_enable;
}

static const int omap_eac_fsint[4] = {
    8000,
    11025,
    22050,
    44100,
};

static const int omap_eac_fsint2[8] = {
    8000,
    11025,
    22050,
    44100,
    48000,
    0, 0, 0,
};

static const int omap_eac_fsint3[16] = {
    8000,
    11025,
    16000,
    22050,
    24000,
    32000,
    44100,
    48000,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static void omap_eac_rate_update(struct omap_eac_s *s)
{
    int fsint[3];

    fsint[2] = (s->codec.config[3] >> 9) & 0xf;
    fsint[1] = (s->codec.config[2] >> 0) & 0x7;
    fsint[0] = (s->codec.config[0] >> 6) & 0x3;
    if (fsint[2] < 0xf)
        s->codec.rate = omap_eac_fsint3[fsint[2]];
    else if (fsint[1] < 0x7)
        s->codec.rate = omap_eac_fsint2[fsint[1]];
    else
        s->codec.rate = omap_eac_fsint[fsint[0]];
}

static void omap_eac_volume_update(struct omap_eac_s *s)
{
    /* TODO */
}

static void omap_eac_format_update(struct omap_eac_s *s)
{
    struct audsettings fmt;

    /* The hardware buffers at most one sample */
    if (s->codec.rxlen)
        s->codec.rxlen = 1;

    if (s->codec.in_voice) {
        AUD_set_active_in(s->codec.in_voice, 0);
        AUD_close_in(&s->codec.card, s->codec.in_voice);
        s->codec.in_voice = 0;
    }
    if (s->codec.out_voice) {
        omap_eac_out_empty(s);
        AUD_set_active_out(s->codec.out_voice, 0);
        AUD_close_out(&s->codec.card, s->codec.out_voice);
        s->codec.out_voice = 0;
        s->codec.txavail = 0;
    }
    /* Discard what couldn't be written */
    s->codec.txlen = 0;

    omap_eac_enable_update(s);
    if (!s->codec.enable)
        return;

    omap_eac_rate_update(s);
    fmt.endianness = ((s->codec.config[0] >> 8) & 1);		/* LI_BI */
    fmt.nchannels = ((s->codec.config[0] >> 10) & 1) ? 2 : 1;	/* MN_ST */
    fmt.freq = s->codec.rate;
    /* TODO: signedness possibly depends on the CODEC hardware - or
     * does I2S specify it?  */
    /* All register writes are 16 bits so we we store 16-bit samples
     * in the buffers regardless of AGCFR[B8_16] value.  */
    fmt.fmt = AUD_FMT_U16;

    s->codec.in_voice = AUD_open_in(&s->codec.card, s->codec.in_voice,
                    "eac.codec.in", s, omap_eac_in_cb, &fmt);
    s->codec.out_voice = AUD_open_out(&s->codec.card, s->codec.out_voice,
                    "eac.codec.out", s, omap_eac_out_cb, &fmt);

    omap_eac_volume_update(s);

    AUD_set_active_in(s->codec.in_voice, 1);
    AUD_set_active_out(s->codec.out_voice, 1);
}

static void omap_eac_reset(struct omap_eac_s *s)
{
    s->sysconfig = 0;
    s->config[0] = 0x0c;
    s->config[1] = 0x09;
    s->config[2] = 0xab;
    s->config[3] = 0x03;
    s->control = 0x00;
    s->address = 0x00;
    s->data = 0x0000;
    s->vtol = 0x00;
    s->vtsl = 0x00;
    s->mixer = 0x0000;
    s->gain[0] = 0xe7e7;
    s->gain[1] = 0x6767;
    s->gain[2] = 0x6767;
    s->gain[3] = 0x6767;
    s->att = 0xce;
    s->max[0] = 0;
    s->max[1] = 0;
    s->max[2] = 0;
    s->max[3] = 0;
    s->max[4] = 0;
    s->max[5] = 0;
    s->max[6] = 0;

    s->modem.control = 0x00;
    s->modem.config = 0x0000;
    s->bt.control = 0x00;
    s->bt.config = 0x0000;
    s->codec.config[0] = 0x0649;
    s->codec.config[1] = 0x0000;
    s->codec.config[2] = 0x0007;
    s->codec.config[3] = 0x1ffc;
    s->codec.rxoff = 0;
    s->codec.rxlen = 0;
    s->codec.txlen = 0;
    s->codec.rxavail = 0;
    s->codec.txavail = 0;

    omap_eac_format_update(s);
    omap_eac_interrupt_update(s);
}

static uint32_t omap_eac_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_eac_s *s = (struct omap_eac_s *) opaque;
    uint32_t ret;

    switch (addr) {
    case 0x000:	/* CPCFR1 */
        return s->config[0];
    case 0x004:	/* CPCFR2 */
        return s->config[1];
    case 0x008:	/* CPCFR3 */
        return s->config[2];
    case 0x00c:	/* CPCFR4 */
        return s->config[3];

    case 0x010:	/* CPTCTL */
        return s->control | ((s->codec.rxavail + s->codec.rxlen > 0) << 7) |
                ((s->codec.txlen < s->codec.txavail) << 5);

    case 0x014:	/* CPTTADR */
        return s->address;
    case 0x018:	/* CPTDATL */
        return s->data & 0xff;
    case 0x01c:	/* CPTDATH */
        return s->data >> 8;
    case 0x020:	/* CPTVSLL */
        return s->vtol;
    case 0x024:	/* CPTVSLH */
        return s->vtsl | (3 << 5);	/* CRDY1 | CRDY2 */
    case 0x040:	/* MPCTR */
        return s->modem.control;
    case 0x044:	/* MPMCCFR */
        return s->modem.config;
    case 0x060:	/* BPCTR */
        return s->bt.control;
    case 0x064:	/* BPMCCFR */
        return s->bt.config;
    case 0x080:	/* AMSCFR */
        return s->mixer;
    case 0x084:	/* AMVCTR */
        return s->gain[0];
    case 0x088:	/* AM1VCTR */
        return s->gain[1];
    case 0x08c:	/* AM2VCTR */
        return s->gain[2];
    case 0x090:	/* AM3VCTR */
        return s->gain[3];
    case 0x094:	/* ASTCTR */
        return s->att;
    case 0x098:	/* APD1LCR */
        return s->max[0];
    case 0x09c:	/* APD1RCR */
        return s->max[1];
    case 0x0a0:	/* APD2LCR */
        return s->max[2];
    case 0x0a4:	/* APD2RCR */
        return s->max[3];
    case 0x0a8:	/* APD3LCR */
        return s->max[4];
    case 0x0ac:	/* APD3RCR */
        return s->max[5];
    case 0x0b0:	/* APD4R */
        return s->max[6];
    case 0x0b4:	/* ADWR */
        /* This should be write-only?  Docs list it as read-only.  */
        return 0x0000;
    case 0x0b8:	/* ADRDR */
        if (likely(s->codec.rxlen > 1)) {
            ret = s->codec.rxbuf[s->codec.rxoff ++];
            s->codec.rxlen --;
            s->codec.rxoff &= EAC_BUF_LEN - 1;
            return ret;
        } else if (s->codec.rxlen) {
            ret = s->codec.rxbuf[s->codec.rxoff ++];
            s->codec.rxlen --;
            s->codec.rxoff &= EAC_BUF_LEN - 1;
            if (s->codec.rxavail)
                omap_eac_in_refill(s);
            omap_eac_in_dmarequest_update(s);
            return ret;
        }
        return 0x0000;
    case 0x0bc:	/* AGCFR */
        return s->codec.config[0];
    case 0x0c0:	/* AGCTR */
        return s->codec.config[1] | ((s->codec.config[1] & 2) << 14);
    case 0x0c4:	/* AGCFR2 */
        return s->codec.config[2];
    case 0x0c8:	/* AGCFR3 */
        return s->codec.config[3];
    case 0x0cc:	/* MBPDMACTR */
    case 0x0d0:	/* MPDDMARR */
    case 0x0d8:	/* MPUDMARR */
    case 0x0e4:	/* BPDDMARR */
    case 0x0ec:	/* BPUDMARR */
        return 0x0000;

    case 0x100:	/* VERSION_NUMBER */
        return 0x0010;

    case 0x104:	/* SYSCONFIG */
        return s->sysconfig;

    case 0x108:	/* SYSSTATUS */
        return 1 | 0xe;					/* RESETDONE | stuff */
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_eac_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_eac_s *s = (struct omap_eac_s *) opaque;

    switch (addr) {
    case 0x098:	/* APD1LCR */
    case 0x09c:	/* APD1RCR */
    case 0x0a0:	/* APD2LCR */
    case 0x0a4:	/* APD2RCR */
    case 0x0a8:	/* APD3LCR */
    case 0x0ac:	/* APD3RCR */
    case 0x0b0:	/* APD4R */
    case 0x0b8:	/* ADRDR */
    case 0x0d0:	/* MPDDMARR */
    case 0x0d8:	/* MPUDMARR */
    case 0x0e4:	/* BPDDMARR */
    case 0x0ec:	/* BPUDMARR */
    case 0x100:	/* VERSION_NUMBER */
    case 0x108:	/* SYSSTATUS */
        OMAP_RO_REG(addr);
        return;

    case 0x000:	/* CPCFR1 */
        s->config[0] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x004:	/* CPCFR2 */
        s->config[1] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x008:	/* CPCFR3 */
        s->config[2] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x00c:	/* CPCFR4 */
        s->config[3] = value & 0xff;
        omap_eac_format_update(s);
        break;

    case 0x010:	/* CPTCTL */
        /* Assuming TXF and TXE bits are read-only... */
        s->control = value & 0x5f;
        omap_eac_interrupt_update(s);
        break;

    case 0x014:	/* CPTTADR */
        s->address = value & 0xff;
        break;
    case 0x018:	/* CPTDATL */
        s->data &= 0xff00;
        s->data |= value & 0xff;
        break;
    case 0x01c:	/* CPTDATH */
        s->data &= 0x00ff;
        s->data |= value << 8;
        break;
    case 0x020:	/* CPTVSLL */
        s->vtol = value & 0xf8;
        break;
    case 0x024:	/* CPTVSLH */
        s->vtsl = value & 0x9f;
        break;
    case 0x040:	/* MPCTR */
        s->modem.control = value & 0x8f;
        break;
    case 0x044:	/* MPMCCFR */
        s->modem.config = value & 0x7fff;
        break;
    case 0x060:	/* BPCTR */
        s->bt.control = value & 0x8f;
        break;
    case 0x064:	/* BPMCCFR */
        s->bt.config = value & 0x7fff;
        break;
    case 0x080:	/* AMSCFR */
        s->mixer = value & 0x0fff;
        break;
    case 0x084:	/* AMVCTR */
        s->gain[0] = value & 0xffff;
        break;
    case 0x088:	/* AM1VCTR */
        s->gain[1] = value & 0xff7f;
        break;
    case 0x08c:	/* AM2VCTR */
        s->gain[2] = value & 0xff7f;
        break;
    case 0x090:	/* AM3VCTR */
        s->gain[3] = value & 0xff7f;
        break;
    case 0x094:	/* ASTCTR */
        s->att = value & 0xff;
        break;

    case 0x0b4:	/* ADWR */
        s->codec.txbuf[s->codec.txlen ++] = value;
        if (unlikely(s->codec.txlen == EAC_BUF_LEN ||
                                s->codec.txlen == s->codec.txavail)) {
            if (s->codec.txavail)
                omap_eac_out_empty(s);
            /* Discard what couldn't be written */
            s->codec.txlen = 0;
        }
        break;

    case 0x0bc:	/* AGCFR */
        s->codec.config[0] = value & 0x07ff;
        omap_eac_format_update(s);
        break;
    case 0x0c0:	/* AGCTR */
        s->codec.config[1] = value & 0x780f;
        omap_eac_format_update(s);
        break;
    case 0x0c4:	/* AGCFR2 */
        s->codec.config[2] = value & 0x003f;
        omap_eac_format_update(s);
        break;
    case 0x0c8:	/* AGCFR3 */
        s->codec.config[3] = value & 0xffff;
        omap_eac_format_update(s);
        break;
    case 0x0cc:	/* MBPDMACTR */
    case 0x0d4:	/* MPDDMAWR */
    case 0x0e0:	/* MPUDMAWR */
    case 0x0e8:	/* BPDDMAWR */
    case 0x0f0:	/* BPUDMAWR */
        break;

    case 0x104:	/* SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_eac_reset(s);
        s->sysconfig = value & 0x31d;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_eac_readfn[] = {
    omap_badwidth_read16,
    omap_eac_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_eac_writefn[] = {
    omap_badwidth_write16,
    omap_eac_write,
    omap_badwidth_write16,
};

struct omap_eac_s *omap_eac_init(struct omap_target_agent_s *ta,
                qemu_irq irq, qemu_irq *drq, omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap_eac_s *s = (struct omap_eac_s *)
            qemu_mallocz(sizeof(struct omap_eac_s));

    s->irq = irq;
    s->codec.rxdrq = *drq ++;
    s->codec.txdrq = *drq ++;
    omap_eac_reset(s);

#ifdef HAS_AUDIO
    /* TODO: do AUD_init globally for machine */
    AUD_register_card(AUD_init(), "OMAP EAC", &s->codec.card);

    iomemtype = cpu_register_io_memory(0, omap_eac_readfn,
                    omap_eac_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
#endif

    return s;
}

/* STI/XTI (emulation interface) console - reverse engineered only */
struct omap_sti_s {
    qemu_irq irq;
    CharDriverState *chr;

    uint32_t sysconfig;
    uint32_t systest;
    uint32_t irqst;
    uint32_t irqen;
    uint32_t clkcontrol;
    uint32_t serial_config;
};

#define STI_TRACE_CONSOLE_CHANNEL	239
#define STI_TRACE_CONTROL_CHANNEL	253

static inline void omap_sti_interrupt_update(struct omap_sti_s *s)
{
    qemu_set_irq(s->irq, s->irqst & s->irqen);
}

static void omap_sti_reset(struct omap_sti_s *s)
{
    s->sysconfig = 0;
    s->irqst = 0;
    s->irqen = 0;
    s->clkcontrol = 0;
    s->serial_config = 0;

    omap_sti_interrupt_update(s);
}

static uint32_t omap_sti_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_sti_s *s = (struct omap_sti_s *) opaque;

    switch (addr) {
    case 0x00:	/* STI_REVISION */
        return 0x10;

    case 0x10:	/* STI_SYSCONFIG */
        return s->sysconfig;

    case 0x14:	/* STI_SYSSTATUS / STI_RX_STATUS / XTI_SYSSTATUS */
        return 0x00;

    case 0x18:	/* STI_IRQSTATUS */
        return s->irqst;

    case 0x1c:	/* STI_IRQSETEN / STI_IRQCLREN */
        return s->irqen;

    case 0x24:	/* STI_ER / STI_DR / XTI_TRACESELECT */
    case 0x28:	/* STI_RX_DR / XTI_RXDATA */
        /* TODO */
        return 0;

    case 0x2c:	/* STI_CLK_CTRL / XTI_SCLKCRTL */
        return s->clkcontrol;

    case 0x30:	/* STI_SERIAL_CFG / XTI_SCONFIG */
        return s->serial_config;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sti_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_sti_s *s = (struct omap_sti_s *) opaque;

    switch (addr) {
    case 0x00:	/* STI_REVISION */
    case 0x14:	/* STI_SYSSTATUS / STI_RX_STATUS / XTI_SYSSTATUS */
        OMAP_RO_REG(addr);
        return;

    case 0x10:	/* STI_SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_sti_reset(s);
        s->sysconfig = value & 0xfe;
        break;

    case 0x18:	/* STI_IRQSTATUS */
        s->irqst &= ~value;
        omap_sti_interrupt_update(s);
        break;

    case 0x1c:	/* STI_IRQSETEN / STI_IRQCLREN */
        s->irqen = value & 0xffff;
        omap_sti_interrupt_update(s);
        break;

    case 0x2c:	/* STI_CLK_CTRL / XTI_SCLKCRTL */
        s->clkcontrol = value & 0xff;
        break;

    case 0x30:	/* STI_SERIAL_CFG / XTI_SCONFIG */
        s->serial_config = value & 0xff;
        break;

    case 0x24:	/* STI_ER / STI_DR / XTI_TRACESELECT */
    case 0x28:	/* STI_RX_DR / XTI_RXDATA */
        /* TODO */
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_sti_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_sti_read,
};

static CPUWriteMemoryFunc *omap_sti_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_sti_write,
};

static uint32_t omap_sti_fifo_read(void *opaque, target_phys_addr_t addr)
{
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sti_fifo_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_sti_s *s = (struct omap_sti_s *) opaque;
    int ch = addr >> 6;
    uint8_t byte = value;

    if (ch == STI_TRACE_CONTROL_CHANNEL) {
        /* Flush channel <i>value</i>.  */
        qemu_chr_write(s->chr, (const uint8_t *) "\r", 1);
    } else if (ch == STI_TRACE_CONSOLE_CHANNEL || 1) {
        if (value == 0xc0 || value == 0xc3) {
            /* Open channel <i>ch</i>.  */
        } else if (value == 0x00)
            qemu_chr_write(s->chr, (const uint8_t *) "\n", 1);
        else
            qemu_chr_write(s->chr, &byte, 1);
    }
}

static CPUReadMemoryFunc *omap_sti_fifo_readfn[] = {
    omap_sti_fifo_read,
    omap_badwidth_read8,
    omap_badwidth_read8,
};

static CPUWriteMemoryFunc *omap_sti_fifo_writefn[] = {
    omap_sti_fifo_write,
    omap_badwidth_write8,
    omap_badwidth_write8,
};

static struct omap_sti_s *omap_sti_init(struct omap_target_agent_s *ta,
                target_phys_addr_t channel_base, qemu_irq irq, omap_clk clk,
                CharDriverState *chr)
{
    int iomemtype;
    struct omap_sti_s *s = (struct omap_sti_s *)
            qemu_mallocz(sizeof(struct omap_sti_s));

    s->irq = irq;
    omap_sti_reset(s);

    s->chr = chr ?: qemu_chr_open("null", "null", NULL);

    iomemtype = l4_register_io_memory(0, omap_sti_readfn,
                    omap_sti_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    iomemtype = cpu_register_io_memory(0, omap_sti_fifo_readfn,
                    omap_sti_fifo_writefn, s);
    cpu_register_physical_memory(channel_base, 0x10000, iomemtype);

    return s;
}

/* L4 Interconnect */
struct omap_target_agent_s {
    struct omap_l4_s *bus;
    int regions;
    struct omap_l4_region_s *start;
    target_phys_addr_t base;
    uint32_t component;
    uint32_t control;
    uint32_t status;
};

struct omap_l4_s {
    target_phys_addr_t base;
    int ta_num;
    struct omap_target_agent_s ta[0];
};

#ifdef L4_MUX_HACK
static int omap_l4_io_entries;
static int omap_cpu_io_entry;
static struct omap_l4_entry {
        CPUReadMemoryFunc **mem_read;
        CPUWriteMemoryFunc **mem_write;
        void *opaque;
} *omap_l4_io_entry;
static CPUReadMemoryFunc **omap_l4_io_readb_fn;
static CPUReadMemoryFunc **omap_l4_io_readh_fn;
static CPUReadMemoryFunc **omap_l4_io_readw_fn;
static CPUWriteMemoryFunc **omap_l4_io_writeb_fn;
static CPUWriteMemoryFunc **omap_l4_io_writeh_fn;
static CPUWriteMemoryFunc **omap_l4_io_writew_fn;
static void **omap_l4_io_opaque;

int l4_register_io_memory(int io_index, CPUReadMemoryFunc **mem_read,
                CPUWriteMemoryFunc **mem_write, void *opaque)
{
    omap_l4_io_entry[omap_l4_io_entries].mem_read = mem_read;
    omap_l4_io_entry[omap_l4_io_entries].mem_write = mem_write;
    omap_l4_io_entry[omap_l4_io_entries].opaque = opaque;

    return omap_l4_io_entries ++;
}

static uint32_t omap_l4_io_readb(void *opaque, target_phys_addr_t addr)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_readb_fn[i](omap_l4_io_opaque[i], addr);
}

static uint32_t omap_l4_io_readh(void *opaque, target_phys_addr_t addr)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_readh_fn[i](omap_l4_io_opaque[i], addr);
}

static uint32_t omap_l4_io_readw(void *opaque, target_phys_addr_t addr)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_readw_fn[i](omap_l4_io_opaque[i], addr);
}

static void omap_l4_io_writeb(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_writeb_fn[i](omap_l4_io_opaque[i], addr, value);
}

static void omap_l4_io_writeh(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_writeh_fn[i](omap_l4_io_opaque[i], addr, value);
}

static void omap_l4_io_writew(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    unsigned int i = (addr - OMAP2_L4_BASE) >> TARGET_PAGE_BITS;

    return omap_l4_io_writew_fn[i](omap_l4_io_opaque[i], addr, value);
}

static CPUReadMemoryFunc *omap_l4_io_readfn[] = {
    omap_l4_io_readb,
    omap_l4_io_readh,
    omap_l4_io_readw,
};

static CPUWriteMemoryFunc *omap_l4_io_writefn[] = {
    omap_l4_io_writeb,
    omap_l4_io_writeh,
    omap_l4_io_writew,
};
#endif

struct omap_l4_s *omap_l4_init(target_phys_addr_t base, int ta_num)
{
    struct omap_l4_s *bus = qemu_mallocz(
                    sizeof(*bus) + ta_num * sizeof(*bus->ta));

    bus->ta_num = ta_num;
    bus->base = base;

#ifdef L4_MUX_HACK
    omap_l4_io_entries = 1;
    omap_l4_io_entry = qemu_mallocz(125 * sizeof(*omap_l4_io_entry));

    omap_cpu_io_entry =
            cpu_register_io_memory(0, omap_l4_io_readfn,
                            omap_l4_io_writefn, bus);
# define L4_PAGES	(0xb4000 / TARGET_PAGE_SIZE)
    omap_l4_io_readb_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_readh_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_readw_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_writeb_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_writeh_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_writew_fn = qemu_mallocz(sizeof(void *) * L4_PAGES);
    omap_l4_io_opaque = qemu_mallocz(sizeof(void *) * L4_PAGES);
#endif

    return bus;
}

static uint32_t omap_l4ta_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    switch (addr) {
    case 0x00:	/* COMPONENT */
        return s->component;

    case 0x20:	/* AGENT_CONTROL */
        return s->control;

    case 0x28:	/* AGENT_STATUS */
        return s->status;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_l4ta_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    switch (addr) {
    case 0x00:	/* COMPONENT */
    case 0x28:	/* AGENT_STATUS */
        OMAP_RO_REG(addr);
        break;

    case 0x20:	/* AGENT_CONTROL */
        s->control = value & 0x01000700;
        if (value & 1)					/* OCP_RESET */
            s->status &= ~1;				/* REQ_TIMEOUT */
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc *omap_l4ta_readfn[] = {
    omap_badwidth_read16,
    omap_l4ta_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_l4ta_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_l4ta_write,
};

#define L4TA(n)		(n)
#define L4TAO(n)	((n) + 39)

static struct omap_l4_region_s {
    target_phys_addr_t offset;
    size_t size;
    int access;
} omap_l4_region[125] = {
    [  1] = { 0x40800,  0x800, 32          }, /* Initiator agent */
    [  2] = { 0x41000, 0x1000, 32          }, /* Link agent */
    [  0] = { 0x40000,  0x800, 32          }, /* Address and protection */
    [  3] = { 0x00000, 0x1000, 32 | 16 | 8 }, /* System Control and Pinout */
    [  4] = { 0x01000, 0x1000, 32 | 16 | 8 }, /* L4TAO1 */
    [  5] = { 0x04000, 0x1000, 32 | 16     }, /* 32K Timer */
    [  6] = { 0x05000, 0x1000, 32 | 16 | 8 }, /* L4TAO2 */
    [  7] = { 0x08000,  0x800, 32          }, /* PRCM Region A */
    [  8] = { 0x08800,  0x800, 32          }, /* PRCM Region B */
    [  9] = { 0x09000, 0x1000, 32 | 16 | 8 }, /* L4TAO */
    [ 10] = { 0x12000, 0x1000, 32 | 16 | 8 }, /* Test (BCM) */
    [ 11] = { 0x13000, 0x1000, 32 | 16 | 8 }, /* L4TA1 */
    [ 12] = { 0x14000, 0x1000, 32          }, /* Test/emulation (TAP) */
    [ 13] = { 0x15000, 0x1000, 32 | 16 | 8 }, /* L4TA2 */
    [ 14] = { 0x18000, 0x1000, 32 | 16 | 8 }, /* GPIO1 */
    [ 16] = { 0x1a000, 0x1000, 32 | 16 | 8 }, /* GPIO2 */
    [ 18] = { 0x1c000, 0x1000, 32 | 16 | 8 }, /* GPIO3 */
    [ 19] = { 0x1e000, 0x1000, 32 | 16 | 8 }, /* GPIO4 */
    [ 15] = { 0x19000, 0x1000, 32 | 16 | 8 }, /* Quad GPIO TOP */
    [ 17] = { 0x1b000, 0x1000, 32 | 16 | 8 }, /* L4TA3 */
    [ 20] = { 0x20000, 0x1000, 32 | 16 | 8 }, /* WD Timer 1 (Secure) */
    [ 22] = { 0x22000, 0x1000, 32 | 16 | 8 }, /* WD Timer 2 (OMAP) */
    [ 21] = { 0x21000, 0x1000, 32 | 16 | 8 }, /* Dual WD timer TOP */
    [ 23] = { 0x23000, 0x1000, 32 | 16 | 8 }, /* L4TA4 */
    [ 24] = { 0x28000, 0x1000, 32 | 16 | 8 }, /* GP Timer 1 */
    [ 25] = { 0x29000, 0x1000, 32 | 16 | 8 }, /* L4TA7 */
    [ 26] = { 0x48000, 0x2000, 32 | 16 | 8 }, /* Emulation (ARM11ETB) */
    [ 27] = { 0x4a000, 0x1000, 32 | 16 | 8 }, /* L4TA9 */
    [ 28] = { 0x50000,  0x400, 32 | 16 | 8 }, /* Display top */
    [ 29] = { 0x50400,  0x400, 32 | 16 | 8 }, /* Display control */
    [ 30] = { 0x50800,  0x400, 32 | 16 | 8 }, /* Display RFBI */
    [ 31] = { 0x50c00,  0x400, 32 | 16 | 8 }, /* Display encoder */
    [ 32] = { 0x51000, 0x1000, 32 | 16 | 8 }, /* L4TA10 */
    [ 33] = { 0x52000,  0x400, 32 | 16 | 8 }, /* Camera top */
    [ 34] = { 0x52400,  0x400, 32 | 16 | 8 }, /* Camera core */
    [ 35] = { 0x52800,  0x400, 32 | 16 | 8 }, /* Camera DMA */
    [ 36] = { 0x52c00,  0x400, 32 | 16 | 8 }, /* Camera MMU */
    [ 37] = { 0x53000, 0x1000, 32 | 16 | 8 }, /* L4TA11 */
    [ 38] = { 0x56000, 0x1000, 32 | 16 | 8 }, /* sDMA */
    [ 39] = { 0x57000, 0x1000, 32 | 16 | 8 }, /* L4TA12 */
    [ 40] = { 0x58000, 0x1000, 32 | 16 | 8 }, /* SSI top */
    [ 41] = { 0x59000, 0x1000, 32 | 16 | 8 }, /* SSI GDD */
    [ 42] = { 0x5a000, 0x1000, 32 | 16 | 8 }, /* SSI Port1 */
    [ 43] = { 0x5b000, 0x1000, 32 | 16 | 8 }, /* SSI Port2 */
    [ 44] = { 0x5c000, 0x1000, 32 | 16 | 8 }, /* L4TA13 */
    [ 45] = { 0x5e000, 0x1000, 32 | 16 | 8 }, /* USB OTG */
    [ 46] = { 0x5f000, 0x1000, 32 | 16 | 8 }, /* L4TAO4 */
    [ 47] = { 0x60000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER1SDRC) */
    [ 48] = { 0x61000, 0x1000, 32 | 16 | 8 }, /* L4TA14 */
    [ 49] = { 0x62000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER2GPMC) */
    [ 50] = { 0x63000, 0x1000, 32 | 16 | 8 }, /* L4TA15 */
    [ 51] = { 0x64000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER3OCM) */
    [ 52] = { 0x65000, 0x1000, 32 | 16 | 8 }, /* L4TA16 */
    [ 53] = { 0x66000,  0x300, 32 | 16 | 8 }, /* Emulation (WIN_TRACER4L4) */
    [ 54] = { 0x67000, 0x1000, 32 | 16 | 8 }, /* L4TA17 */
    [ 55] = { 0x68000, 0x1000, 32 | 16 | 8 }, /* Emulation (XTI) */
    [ 56] = { 0x69000, 0x1000, 32 | 16 | 8 }, /* L4TA18 */
    [ 57] = { 0x6a000, 0x1000,      16 | 8 }, /* UART1 */
    [ 58] = { 0x6b000, 0x1000, 32 | 16 | 8 }, /* L4TA19 */
    [ 59] = { 0x6c000, 0x1000,      16 | 8 }, /* UART2 */
    [ 60] = { 0x6d000, 0x1000, 32 | 16 | 8 }, /* L4TA20 */
    [ 61] = { 0x6e000, 0x1000,      16 | 8 }, /* UART3 */
    [ 62] = { 0x6f000, 0x1000, 32 | 16 | 8 }, /* L4TA21 */
    [ 63] = { 0x70000, 0x1000,      16     }, /* I2C1 */
    [ 64] = { 0x71000, 0x1000, 32 | 16 | 8 }, /* L4TAO5 */
    [ 65] = { 0x72000, 0x1000,      16     }, /* I2C2 */
    [ 66] = { 0x73000, 0x1000, 32 | 16 | 8 }, /* L4TAO6 */
    [ 67] = { 0x74000, 0x1000,      16     }, /* McBSP1 */
    [ 68] = { 0x75000, 0x1000, 32 | 16 | 8 }, /* L4TAO7 */
    [ 69] = { 0x76000, 0x1000,      16     }, /* McBSP2 */
    [ 70] = { 0x77000, 0x1000, 32 | 16 | 8 }, /* L4TAO8 */
    [ 71] = { 0x24000, 0x1000, 32 | 16 | 8 }, /* WD Timer 3 (DSP) */
    [ 72] = { 0x25000, 0x1000, 32 | 16 | 8 }, /* L4TA5 */
    [ 73] = { 0x26000, 0x1000, 32 | 16 | 8 }, /* WD Timer 4 (IVA) */
    [ 74] = { 0x27000, 0x1000, 32 | 16 | 8 }, /* L4TA6 */
    [ 75] = { 0x2a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 2 */
    [ 76] = { 0x2b000, 0x1000, 32 | 16 | 8 }, /* L4TA8 */
    [ 77] = { 0x78000, 0x1000, 32 | 16 | 8 }, /* GP Timer 3 */
    [ 78] = { 0x79000, 0x1000, 32 | 16 | 8 }, /* L4TA22 */
    [ 79] = { 0x7a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 4 */
    [ 80] = { 0x7b000, 0x1000, 32 | 16 | 8 }, /* L4TA23 */
    [ 81] = { 0x7c000, 0x1000, 32 | 16 | 8 }, /* GP Timer 5 */
    [ 82] = { 0x7d000, 0x1000, 32 | 16 | 8 }, /* L4TA24 */
    [ 83] = { 0x7e000, 0x1000, 32 | 16 | 8 }, /* GP Timer 6 */
    [ 84] = { 0x7f000, 0x1000, 32 | 16 | 8 }, /* L4TA25 */
    [ 85] = { 0x80000, 0x1000, 32 | 16 | 8 }, /* GP Timer 7 */
    [ 86] = { 0x81000, 0x1000, 32 | 16 | 8 }, /* L4TA26 */
    [ 87] = { 0x82000, 0x1000, 32 | 16 | 8 }, /* GP Timer 8 */
    [ 88] = { 0x83000, 0x1000, 32 | 16 | 8 }, /* L4TA27 */
    [ 89] = { 0x84000, 0x1000, 32 | 16 | 8 }, /* GP Timer 9 */
    [ 90] = { 0x85000, 0x1000, 32 | 16 | 8 }, /* L4TA28 */
    [ 91] = { 0x86000, 0x1000, 32 | 16 | 8 }, /* GP Timer 10 */
    [ 92] = { 0x87000, 0x1000, 32 | 16 | 8 }, /* L4TA29 */
    [ 93] = { 0x88000, 0x1000, 32 | 16 | 8 }, /* GP Timer 11 */
    [ 94] = { 0x89000, 0x1000, 32 | 16 | 8 }, /* L4TA30 */
    [ 95] = { 0x8a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 12 */
    [ 96] = { 0x8b000, 0x1000, 32 | 16 | 8 }, /* L4TA31 */
    [ 97] = { 0x90000, 0x1000,      16     }, /* EAC */
    [ 98] = { 0x91000, 0x1000, 32 | 16 | 8 }, /* L4TA32 */
    [ 99] = { 0x92000, 0x1000,      16     }, /* FAC */
    [100] = { 0x93000, 0x1000, 32 | 16 | 8 }, /* L4TA33 */
    [101] = { 0x94000, 0x1000, 32 | 16 | 8 }, /* IPC (MAILBOX) */
    [102] = { 0x95000, 0x1000, 32 | 16 | 8 }, /* L4TA34 */
    [103] = { 0x98000, 0x1000, 32 | 16 | 8 }, /* SPI1 */
    [104] = { 0x99000, 0x1000, 32 | 16 | 8 }, /* L4TA35 */
    [105] = { 0x9a000, 0x1000, 32 | 16 | 8 }, /* SPI2 */
    [106] = { 0x9b000, 0x1000, 32 | 16 | 8 }, /* L4TA36 */
    [107] = { 0x9c000, 0x1000,      16 | 8 }, /* MMC SDIO */
    [108] = { 0x9d000, 0x1000, 32 | 16 | 8 }, /* L4TAO9 */
    [109] = { 0x9e000, 0x1000, 32 | 16 | 8 }, /* MS_PRO */
    [110] = { 0x9f000, 0x1000, 32 | 16 | 8 }, /* L4TAO10 */
    [111] = { 0xa0000, 0x1000, 32          }, /* RNG */
    [112] = { 0xa1000, 0x1000, 32 | 16 | 8 }, /* L4TAO11 */
    [113] = { 0xa2000, 0x1000, 32          }, /* DES3DES */
    [114] = { 0xa3000, 0x1000, 32 | 16 | 8 }, /* L4TAO12 */
    [115] = { 0xa4000, 0x1000, 32          }, /* SHA1MD5 */
    [116] = { 0xa5000, 0x1000, 32 | 16 | 8 }, /* L4TAO13 */
    [117] = { 0xa6000, 0x1000, 32          }, /* AES */
    [118] = { 0xa7000, 0x1000, 32 | 16 | 8 }, /* L4TA37 */
    [119] = { 0xa8000, 0x2000, 32          }, /* PKA */
    [120] = { 0xaa000, 0x1000, 32 | 16 | 8 }, /* L4TA38 */
    [121] = { 0xb0000, 0x1000, 32          }, /* MG */
    [122] = { 0xb1000, 0x1000, 32 | 16 | 8 },
    [123] = { 0xb2000, 0x1000, 32          }, /* HDQ/1-Wire */
    [124] = { 0xb3000, 0x1000, 32 | 16 | 8 }, /* L4TA39 */
};

static struct omap_l4_agent_info_s {
    int ta;
    int region;
    int regions;
    int ta_region;
} omap_l4_agent_info[54] = {
    { 0,           0, 3, 2 }, /* L4IA initiatior agent */
    { L4TAO(1),    3, 2, 1 }, /* Control and pinout module */
    { L4TAO(2),    5, 2, 1 }, /* 32K timer */
    { L4TAO(3),    7, 3, 2 }, /* PRCM */
    { L4TA(1),    10, 2, 1 }, /* BCM */
    { L4TA(2),    12, 2, 1 }, /* Test JTAG */
    { L4TA(3),    14, 6, 3 }, /* Quad GPIO */
    { L4TA(4),    20, 4, 3 }, /* WD timer 1/2 */
    { L4TA(7),    24, 2, 1 }, /* GP timer 1 */
    { L4TA(9),    26, 2, 1 }, /* ATM11 ETB */
    { L4TA(10),   28, 5, 4 }, /* Display subsystem */
    { L4TA(11),   33, 5, 4 }, /* Camera subsystem */
    { L4TA(12),   38, 2, 1 }, /* sDMA */
    { L4TA(13),   40, 5, 4 }, /* SSI */
    { L4TAO(4),   45, 2, 1 }, /* USB */
    { L4TA(14),   47, 2, 1 }, /* Win Tracer1 */
    { L4TA(15),   49, 2, 1 }, /* Win Tracer2 */
    { L4TA(16),   51, 2, 1 }, /* Win Tracer3 */
    { L4TA(17),   53, 2, 1 }, /* Win Tracer4 */
    { L4TA(18),   55, 2, 1 }, /* XTI */
    { L4TA(19),   57, 2, 1 }, /* UART1 */
    { L4TA(20),   59, 2, 1 }, /* UART2 */
    { L4TA(21),   61, 2, 1 }, /* UART3 */
    { L4TAO(5),   63, 2, 1 }, /* I2C1 */
    { L4TAO(6),   65, 2, 1 }, /* I2C2 */
    { L4TAO(7),   67, 2, 1 }, /* McBSP1 */
    { L4TAO(8),   69, 2, 1 }, /* McBSP2 */
    { L4TA(5),    71, 2, 1 }, /* WD Timer 3 (DSP) */
    { L4TA(6),    73, 2, 1 }, /* WD Timer 4 (IVA) */
    { L4TA(8),    75, 2, 1 }, /* GP Timer 2 */
    { L4TA(22),   77, 2, 1 }, /* GP Timer 3 */
    { L4TA(23),   79, 2, 1 }, /* GP Timer 4 */
    { L4TA(24),   81, 2, 1 }, /* GP Timer 5 */
    { L4TA(25),   83, 2, 1 }, /* GP Timer 6 */
    { L4TA(26),   85, 2, 1 }, /* GP Timer 7 */
    { L4TA(27),   87, 2, 1 }, /* GP Timer 8 */
    { L4TA(28),   89, 2, 1 }, /* GP Timer 9 */
    { L4TA(29),   91, 2, 1 }, /* GP Timer 10 */
    { L4TA(30),   93, 2, 1 }, /* GP Timer 11 */
    { L4TA(31),   95, 2, 1 }, /* GP Timer 12 */
    { L4TA(32),   97, 2, 1 }, /* EAC */
    { L4TA(33),   99, 2, 1 }, /* FAC */
    { L4TA(34),  101, 2, 1 }, /* IPC */
    { L4TA(35),  103, 2, 1 }, /* SPI1 */
    { L4TA(36),  105, 2, 1 }, /* SPI2 */
    { L4TAO(9),  107, 2, 1 }, /* MMC SDIO */
    { L4TAO(10), 109, 2, 1 },
    { L4TAO(11), 111, 2, 1 }, /* RNG */
    { L4TAO(12), 113, 2, 1 }, /* DES3DES */
    { L4TAO(13), 115, 2, 1 }, /* SHA1MD5 */
    { L4TA(37),  117, 2, 1 }, /* AES */
    { L4TA(38),  119, 2, 1 }, /* PKA */
    { -1,        121, 2, 1 },
    { L4TA(39),  123, 2, 1 }, /* HDQ/1-Wire */
};

#define omap_l4ta(bus, cs)	omap_l4ta_get(bus, L4TA(cs))
#define omap_l4tao(bus, cs)	omap_l4ta_get(bus, L4TAO(cs))

struct omap_target_agent_s *omap_l4ta_get(struct omap_l4_s *bus, int cs)
{
    int i, iomemtype;
    struct omap_target_agent_s *ta = 0;
    struct omap_l4_agent_info_s *info = 0;

    for (i = 0; i < bus->ta_num; i ++)
        if (omap_l4_agent_info[i].ta == cs) {
            ta = &bus->ta[i];
            info = &omap_l4_agent_info[i];
            break;
        }
    if (!ta) {
        fprintf(stderr, "%s: bad target agent (%i)\n", __FUNCTION__, cs);
        exit(-1);
    }

    ta->bus = bus;
    ta->start = &omap_l4_region[info->region];
    ta->regions = info->regions;

    ta->component = ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    ta->status = 0x00000000;
    ta->control = 0x00000200;	/* XXX 01000200 for L4TAO */

    iomemtype = l4_register_io_memory(0, omap_l4ta_readfn,
                    omap_l4ta_writefn, ta);
    ta->base = omap_l4_attach(ta, info->ta_region, iomemtype);

    return ta;
}

target_phys_addr_t omap_l4_attach(struct omap_target_agent_s *ta, int region,
                int iotype)
{
    target_phys_addr_t base;
    ssize_t size;
#ifdef L4_MUX_HACK
    int i;
#endif

    if (region < 0 || region >= ta->regions) {
        fprintf(stderr, "%s: bad io region (%i)\n", __FUNCTION__, region);
        exit(-1);
    }

    base = ta->bus->base + ta->start[region].offset;
    size = ta->start[region].size;
    if (iotype) {
#ifndef L4_MUX_HACK
        cpu_register_physical_memory(base, size, iotype);
#else
        cpu_register_physical_memory(base, size, omap_cpu_io_entry);
        i = (base - ta->bus->base) / TARGET_PAGE_SIZE;
        for (; size > 0; size -= TARGET_PAGE_SIZE, i ++) {
            omap_l4_io_readb_fn[i] = omap_l4_io_entry[iotype].mem_read[0];
            omap_l4_io_readh_fn[i] = omap_l4_io_entry[iotype].mem_read[1];
            omap_l4_io_readw_fn[i] = omap_l4_io_entry[iotype].mem_read[2];
            omap_l4_io_writeb_fn[i] = omap_l4_io_entry[iotype].mem_write[0];
            omap_l4_io_writeh_fn[i] = omap_l4_io_entry[iotype].mem_write[1];
            omap_l4_io_writew_fn[i] = omap_l4_io_entry[iotype].mem_write[2];
            omap_l4_io_opaque[i] = omap_l4_io_entry[iotype].opaque;
        }
#endif
    }

    return base;
}

/* TEST-Chip-level TAP */
static uint32_t omap_tap_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    switch (addr) {
    case 0x204:	/* IDCODE_reg */
        switch (s->mpu_model) {
        case omap2420:
        case omap2422:
        case omap2423:
            return 0x5b5d902f;	/* ES 2.2 */
        case omap2430:
            return 0x5b68a02f;	/* ES 2.2 */
        case omap3430:
            return 0x1b7ae02f;	/* ES 2 */
        default:
            hw_error("%s: Bad mpu model\n", __FUNCTION__);
        }

    case 0x208:	/* PRODUCTION_ID_reg for OMAP2 */
    case 0x210:	/* PRODUCTION_ID_reg for OMAP3 */
        switch (s->mpu_model) {
        case omap2420:
            return 0x000254f0;	/* POP ESHS2.1.1 in N91/93/95, ES2 in N800 */
        case omap2422:
            return 0x000400f0;
        case omap2423:
            return 0x000800f0;
        case omap2430:
            return 0x000000f0;
        case omap3430:
            return 0x000000f0;
        default:
            hw_error("%s: Bad mpu model\n", __FUNCTION__);
        }

    case 0x20c:
        switch (s->mpu_model) {
        case omap2420:
        case omap2422:
        case omap2423:
            return 0xcafeb5d9;	/* ES 2.2 */
        case omap2430:
            return 0xcafeb68a;	/* ES 2.2 */
        case omap3430:
            return 0xcafeb7ae;	/* ES 2 */
        default:
            hw_error("%s: Bad mpu model\n", __FUNCTION__);
        }

    case 0x218:	/* DIE_ID_reg */
        return ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    case 0x21c:	/* DIE_ID_reg */
        return 0x54 << 24;
    case 0x220:	/* DIE_ID_reg */
        return ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    case 0x224:	/* DIE_ID_reg */
        return ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_tap_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_BAD_REG(addr);
}

static CPUReadMemoryFunc *omap_tap_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_tap_read,
};

static CPUWriteMemoryFunc *omap_tap_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_tap_write,
};

void omap_tap_init(struct omap_target_agent_s *ta,
                struct omap_mpu_state_s *mpu)
{
    omap_l4_attach(ta, 0, l4_register_io_memory(0,
                            omap_tap_readfn, omap_tap_writefn, mpu));
}

/* Power, Reset, and Clock Management */
struct omap_prcm_s {
    qemu_irq irq[3];
    struct omap_mpu_state_s *mpu;

    uint32_t irqst[3];
    uint32_t irqen[3];

    uint32_t sysconfig;
    uint32_t voltctrl;
    uint32_t scratch[20];

    uint32_t clksrc[1];
    uint32_t clkout[1];
    uint32_t clkemul[1];
    uint32_t clkpol[1];
    uint32_t clksel[8];
    uint32_t clken[12];
    uint32_t clkctrl[4];
    uint32_t clkidle[7];
    uint32_t setuptime[2];

    uint32_t wkup[3];
    uint32_t wken[3];
    uint32_t wkst[3];
    uint32_t rst[4];
    uint32_t rstctrl[1];
    uint32_t power[4];
    uint32_t rsttime_wkup;

    uint32_t ev;
    uint32_t evtime[2];

    int dpll_lock, apll_lock[2];
};

static void omap_prcm_int_update(struct omap_prcm_s *s, int dom)
{
    qemu_set_irq(s->irq[dom], s->irqst[dom] & s->irqen[dom]);
    /* XXX or is the mask applied before PRCM_IRQSTATUS_* ? */
}

static uint32_t omap_prcm_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_prcm_s *s = (struct omap_prcm_s *) opaque;
    uint32_t ret;

    switch (addr) {
    case 0x000:	/* PRCM_REVISION */
        return 0x10;

    case 0x010:	/* PRCM_SYSCONFIG */
        return s->sysconfig;

    case 0x018:	/* PRCM_IRQSTATUS_MPU */
        return s->irqst[0];

    case 0x01c:	/* PRCM_IRQENABLE_MPU */
        return s->irqen[0];

    case 0x050:	/* PRCM_VOLTCTRL */
        return s->voltctrl;
    case 0x054:	/* PRCM_VOLTST */
        return s->voltctrl & 3;

    case 0x060:	/* PRCM_CLKSRC_CTRL */
        return s->clksrc[0];
    case 0x070:	/* PRCM_CLKOUT_CTRL */
        return s->clkout[0];
    case 0x078:	/* PRCM_CLKEMUL_CTRL */
        return s->clkemul[0];
    case 0x080:	/* PRCM_CLKCFG_CTRL */
    case 0x084:	/* PRCM_CLKCFG_STATUS */
        return 0;

    case 0x090:	/* PRCM_VOLTSETUP */
        return s->setuptime[0];

    case 0x094:	/* PRCM_CLKSSETUP */
        return s->setuptime[1];

    case 0x098:	/* PRCM_POLCTRL */
        return s->clkpol[0];

    case 0x0b0:	/* GENERAL_PURPOSE1 */
    case 0x0b4:	/* GENERAL_PURPOSE2 */
    case 0x0b8:	/* GENERAL_PURPOSE3 */
    case 0x0bc:	/* GENERAL_PURPOSE4 */
    case 0x0c0:	/* GENERAL_PURPOSE5 */
    case 0x0c4:	/* GENERAL_PURPOSE6 */
    case 0x0c8:	/* GENERAL_PURPOSE7 */
    case 0x0cc:	/* GENERAL_PURPOSE8 */
    case 0x0d0:	/* GENERAL_PURPOSE9 */
    case 0x0d4:	/* GENERAL_PURPOSE10 */
    case 0x0d8:	/* GENERAL_PURPOSE11 */
    case 0x0dc:	/* GENERAL_PURPOSE12 */
    case 0x0e0:	/* GENERAL_PURPOSE13 */
    case 0x0e4:	/* GENERAL_PURPOSE14 */
    case 0x0e8:	/* GENERAL_PURPOSE15 */
    case 0x0ec:	/* GENERAL_PURPOSE16 */
    case 0x0f0:	/* GENERAL_PURPOSE17 */
    case 0x0f4:	/* GENERAL_PURPOSE18 */
    case 0x0f8:	/* GENERAL_PURPOSE19 */
    case 0x0fc:	/* GENERAL_PURPOSE20 */
        return s->scratch[(addr - 0xb0) >> 2];

    case 0x140:	/* CM_CLKSEL_MPU */
        return s->clksel[0];
    case 0x148:	/* CM_CLKSTCTRL_MPU */
        return s->clkctrl[0];

    case 0x158:	/* RM_RSTST_MPU */
        return s->rst[0];
    case 0x1c8:	/* PM_WKDEP_MPU */
        return s->wkup[0];
    case 0x1d4:	/* PM_EVGENCTRL_MPU */
        return s->ev;
    case 0x1d8:	/* PM_EVEGENONTIM_MPU */
        return s->evtime[0];
    case 0x1dc:	/* PM_EVEGENOFFTIM_MPU */
        return s->evtime[1];
    case 0x1e0:	/* PM_PWSTCTRL_MPU */
        return s->power[0];
    case 0x1e4:	/* PM_PWSTST_MPU */
        return 0;

    case 0x200:	/* CM_FCLKEN1_CORE */
        return s->clken[0];
    case 0x204:	/* CM_FCLKEN2_CORE */
        return s->clken[1];
    case 0x210:	/* CM_ICLKEN1_CORE */
        return s->clken[2];
    case 0x214:	/* CM_ICLKEN2_CORE */
        return s->clken[3];
    case 0x21c:	/* CM_ICLKEN4_CORE */
        return s->clken[4];

    case 0x220:	/* CM_IDLEST1_CORE */
        /* TODO: check the actual iclk status */
        return 0x7ffffff9;
    case 0x224:	/* CM_IDLEST2_CORE */
        /* TODO: check the actual iclk status */
        return 0x00000007;
    case 0x22c:	/* CM_IDLEST4_CORE */
        /* TODO: check the actual iclk status */
        return 0x0000001f;

    case 0x230:	/* CM_AUTOIDLE1_CORE */
        return s->clkidle[0];
    case 0x234:	/* CM_AUTOIDLE2_CORE */
        return s->clkidle[1];
    case 0x238:	/* CM_AUTOIDLE3_CORE */
        return s->clkidle[2];
    case 0x23c:	/* CM_AUTOIDLE4_CORE */
        return s->clkidle[3];

    case 0x240:	/* CM_CLKSEL1_CORE */
        return s->clksel[1];
    case 0x244:	/* CM_CLKSEL2_CORE */
        return s->clksel[2];

    case 0x248:	/* CM_CLKSTCTRL_CORE */
        return s->clkctrl[1];

    case 0x2a0:	/* PM_WKEN1_CORE */
        return s->wken[0];
    case 0x2a4:	/* PM_WKEN2_CORE */
        return s->wken[1];

    case 0x2b0:	/* PM_WKST1_CORE */
        return s->wkst[0];
    case 0x2b4:	/* PM_WKST2_CORE */
        return s->wkst[1];
    case 0x2c8:	/* PM_WKDEP_CORE */
        return 0x1e;

    case 0x2e0:	/* PM_PWSTCTRL_CORE */
        return s->power[1];
    case 0x2e4:	/* PM_PWSTST_CORE */
        return 0x000030 | (s->power[1] & 0xfc00);

    case 0x300:	/* CM_FCLKEN_GFX */
        return s->clken[5];
    case 0x310:	/* CM_ICLKEN_GFX */
        return s->clken[6];
    case 0x320:	/* CM_IDLEST_GFX */
        /* TODO: check the actual iclk status */
        return 0x00000001;
    case 0x340:	/* CM_CLKSEL_GFX */
        return s->clksel[3];
    case 0x348:	/* CM_CLKSTCTRL_GFX */
        return s->clkctrl[2];
    case 0x350:	/* RM_RSTCTRL_GFX */
        return s->rstctrl[0];
    case 0x358:	/* RM_RSTST_GFX */
        return s->rst[1];
    case 0x3c8:	/* PM_WKDEP_GFX */
        return s->wkup[1];

    case 0x3e0:	/* PM_PWSTCTRL_GFX */
        return s->power[2];
    case 0x3e4:	/* PM_PWSTST_GFX */
        return s->power[2] & 3;

    case 0x400:	/* CM_FCLKEN_WKUP */
        return s->clken[7];
    case 0x410:	/* CM_ICLKEN_WKUP */
        return s->clken[8];
    case 0x420:	/* CM_IDLEST_WKUP */
        /* TODO: check the actual iclk status */
        return 0x0000003f;
    case 0x430:	/* CM_AUTOIDLE_WKUP */
        return s->clkidle[4];
    case 0x440:	/* CM_CLKSEL_WKUP */
        return s->clksel[4];
    case 0x450:	/* RM_RSTCTRL_WKUP */
        return 0;
    case 0x454:	/* RM_RSTTIME_WKUP */
        return s->rsttime_wkup;
    case 0x458:	/* RM_RSTST_WKUP */
        return s->rst[2];
    case 0x4a0:	/* PM_WKEN_WKUP */
        return s->wken[2];
    case 0x4b0:	/* PM_WKST_WKUP */
        return s->wkst[2];

    case 0x500:	/* CM_CLKEN_PLL */
        return s->clken[9];
    case 0x520:	/* CM_IDLEST_CKGEN */
        ret = 0x0000070 | (s->apll_lock[0] << 9) | (s->apll_lock[1] << 8);
        if (!(s->clksel[6] & 3))
            /* Core uses 32-kHz clock */
            ret |= 3 << 0;
        else if (!s->dpll_lock)
            /* DPLL not locked, core uses ref_clk */
            ret |= 1 << 0;
        else
            /* Core uses DPLL */
            ret |= 2 << 0;
        return ret;
    case 0x530:	/* CM_AUTOIDLE_PLL */
        return s->clkidle[5];
    case 0x540:	/* CM_CLKSEL1_PLL */
        return s->clksel[5];
    case 0x544:	/* CM_CLKSEL2_PLL */
        return s->clksel[6];

    case 0x800:	/* CM_FCLKEN_DSP */
        return s->clken[10];
    case 0x810:	/* CM_ICLKEN_DSP */
        return s->clken[11];
    case 0x820:	/* CM_IDLEST_DSP */
        /* TODO: check the actual iclk status */
        return 0x00000103;
    case 0x830:	/* CM_AUTOIDLE_DSP */
        return s->clkidle[6];
    case 0x840:	/* CM_CLKSEL_DSP */
        return s->clksel[7];
    case 0x848:	/* CM_CLKSTCTRL_DSP */
        return s->clkctrl[3];
    case 0x850:	/* RM_RSTCTRL_DSP */
        return 0;
    case 0x858:	/* RM_RSTST_DSP */
        return s->rst[3];
    case 0x8c8:	/* PM_WKDEP_DSP */
        return s->wkup[2];
    case 0x8e0:	/* PM_PWSTCTRL_DSP */
        return s->power[3];
    case 0x8e4:	/* PM_PWSTST_DSP */
        return 0x008030 | (s->power[3] & 0x3003);

    case 0x8f0:	/* PRCM_IRQSTATUS_DSP */
        return s->irqst[1];
    case 0x8f4:	/* PRCM_IRQENABLE_DSP */
        return s->irqen[1];

    case 0x8f8:	/* PRCM_IRQSTATUS_IVA */
        return s->irqst[2];
    case 0x8fc:	/* PRCM_IRQENABLE_IVA */
        return s->irqen[2];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_prcm_apll_update(struct omap_prcm_s *s)
{
    int mode[2];

    mode[0] = (s->clken[9] >> 6) & 3;
    s->apll_lock[0] = (mode[0] == 3);
    mode[1] = (s->clken[9] >> 2) & 3;
    s->apll_lock[1] = (mode[1] == 3);
    /* TODO: update clocks */

    if (mode[0] == 1 || mode[0] == 2 || mode[1] == 1 || mode[2] == 2)
        fprintf(stderr, "%s: bad EN_54M_PLL or bad EN_96M_PLL\n",
                        __FUNCTION__);
}

static void omap_prcm_dpll_update(struct omap_prcm_s *s)
{
    omap_clk dpll = omap_findclk(s->mpu, "dpll");
    omap_clk dpll_x2 = omap_findclk(s->mpu, "dpll");
    omap_clk core = omap_findclk(s->mpu, "core_clk");
    int mode = (s->clken[9] >> 0) & 3;
    int mult, div;

    mult = (s->clksel[5] >> 12) & 0x3ff;
    div = (s->clksel[5] >> 8) & 0xf;
    if (mult == 0 || mult == 1)
        mode = 1;	/* Bypass */

    s->dpll_lock = 0;
    switch (mode) {
    case 0:
        fprintf(stderr, "%s: bad EN_DPLL\n", __FUNCTION__);
        break;
    case 1:	/* Low-power bypass mode (Default) */
    case 2:	/* Fast-relock bypass mode */
        omap_clk_setrate(dpll, 1, 1);
        omap_clk_setrate(dpll_x2, 1, 1);
        break;
    case 3:	/* Lock mode */
        s->dpll_lock = 1; /* After 20 FINT cycles (ref_clk / (div + 1)).  */

        omap_clk_setrate(dpll, div + 1, mult);
        omap_clk_setrate(dpll_x2, div + 1, mult * 2);
        break;
    }

    switch ((s->clksel[6] >> 0) & 3) {
    case 0:
        omap_clk_reparent(core, omap_findclk(s->mpu, "clk32-kHz"));
        break;
    case 1:
        omap_clk_reparent(core, dpll);
        break;
    case 2:
        /* Default */
        omap_clk_reparent(core, dpll_x2);
        break;
    case 3:
        fprintf(stderr, "%s: bad CORE_CLK_SRC\n", __FUNCTION__);
        break;
    }
}

static void omap_prcm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_prcm_s *s = (struct omap_prcm_s *) opaque;

    switch (addr) {
    case 0x000:	/* PRCM_REVISION */
    case 0x054:	/* PRCM_VOLTST */
    case 0x084:	/* PRCM_CLKCFG_STATUS */
    case 0x1e4:	/* PM_PWSTST_MPU */
    case 0x220:	/* CM_IDLEST1_CORE */
    case 0x224:	/* CM_IDLEST2_CORE */
    case 0x22c:	/* CM_IDLEST4_CORE */
    case 0x2c8:	/* PM_WKDEP_CORE */
    case 0x2e4:	/* PM_PWSTST_CORE */
    case 0x320:	/* CM_IDLEST_GFX */
    case 0x3e4:	/* PM_PWSTST_GFX */
    case 0x420:	/* CM_IDLEST_WKUP */
    case 0x520:	/* CM_IDLEST_CKGEN */
    case 0x820:	/* CM_IDLEST_DSP */
    case 0x8e4:	/* PM_PWSTST_DSP */
        OMAP_RO_REG(addr);
        return;

    case 0x010:	/* PRCM_SYSCONFIG */
        s->sysconfig = value & 1;
        break;

    case 0x018:	/* PRCM_IRQSTATUS_MPU */
        s->irqst[0] &= ~value;
        omap_prcm_int_update(s, 0);
        break;
    case 0x01c:	/* PRCM_IRQENABLE_MPU */
        s->irqen[0] = value & 0x3f;
        omap_prcm_int_update(s, 0);
        break;

    case 0x050:	/* PRCM_VOLTCTRL */
        s->voltctrl = value & 0xf1c3;
        break;

    case 0x060:	/* PRCM_CLKSRC_CTRL */
        s->clksrc[0] = value & 0xdb;
        /* TODO update clocks */
        break;

    case 0x070:	/* PRCM_CLKOUT_CTRL */
        s->clkout[0] = value & 0xbbbb;
        /* TODO update clocks */
        break;

    case 0x078:	/* PRCM_CLKEMUL_CTRL */
        s->clkemul[0] = value & 1;
        /* TODO update clocks */
        break;

    case 0x080:	/* PRCM_CLKCFG_CTRL */
        break;

    case 0x090:	/* PRCM_VOLTSETUP */
        s->setuptime[0] = value & 0xffff;
        break;
    case 0x094:	/* PRCM_CLKSSETUP */
        s->setuptime[1] = value & 0xffff;
        break;

    case 0x098:	/* PRCM_POLCTRL */
        s->clkpol[0] = value & 0x701;
        break;

    case 0x0b0:	/* GENERAL_PURPOSE1 */
    case 0x0b4:	/* GENERAL_PURPOSE2 */
    case 0x0b8:	/* GENERAL_PURPOSE3 */
    case 0x0bc:	/* GENERAL_PURPOSE4 */
    case 0x0c0:	/* GENERAL_PURPOSE5 */
    case 0x0c4:	/* GENERAL_PURPOSE6 */
    case 0x0c8:	/* GENERAL_PURPOSE7 */
    case 0x0cc:	/* GENERAL_PURPOSE8 */
    case 0x0d0:	/* GENERAL_PURPOSE9 */
    case 0x0d4:	/* GENERAL_PURPOSE10 */
    case 0x0d8:	/* GENERAL_PURPOSE11 */
    case 0x0dc:	/* GENERAL_PURPOSE12 */
    case 0x0e0:	/* GENERAL_PURPOSE13 */
    case 0x0e4:	/* GENERAL_PURPOSE14 */
    case 0x0e8:	/* GENERAL_PURPOSE15 */
    case 0x0ec:	/* GENERAL_PURPOSE16 */
    case 0x0f0:	/* GENERAL_PURPOSE17 */
    case 0x0f4:	/* GENERAL_PURPOSE18 */
    case 0x0f8:	/* GENERAL_PURPOSE19 */
    case 0x0fc:	/* GENERAL_PURPOSE20 */
        s->scratch[(addr - 0xb0) >> 2] = value;
        break;

    case 0x140:	/* CM_CLKSEL_MPU */
        s->clksel[0] = value & 0x1f;
        /* TODO update clocks */
        break;
    case 0x148:	/* CM_CLKSTCTRL_MPU */
        s->clkctrl[0] = value & 0x1f;
        break;

    case 0x158:	/* RM_RSTST_MPU */
        s->rst[0] &= ~value;
        break;
    case 0x1c8:	/* PM_WKDEP_MPU */
        s->wkup[0] = value & 0x15;
        break;

    case 0x1d4:	/* PM_EVGENCTRL_MPU */
        s->ev = value & 0x1f;
        break;
    case 0x1d8:	/* PM_EVEGENONTIM_MPU */
        s->evtime[0] = value;
        break;
    case 0x1dc:	/* PM_EVEGENOFFTIM_MPU */
        s->evtime[1] = value;
        break;

    case 0x1e0:	/* PM_PWSTCTRL_MPU */
        s->power[0] = value & 0xc0f;
        break;

    case 0x200:	/* CM_FCLKEN1_CORE */
        s->clken[0] = value & 0xbfffffff;
        /* TODO update clocks */
        /* The EN_EAC bit only gets/puts func_96m_clk.  */
        break;
    case 0x204:	/* CM_FCLKEN2_CORE */
        s->clken[1] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x210:	/* CM_ICLKEN1_CORE */
        s->clken[2] = value & 0xfffffff9;
        /* TODO update clocks */
        /* The EN_EAC bit only gets/puts core_l4_iclk.  */
        break;
    case 0x214:	/* CM_ICLKEN2_CORE */
        s->clken[3] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x21c:	/* CM_ICLKEN4_CORE */
        s->clken[4] = value & 0x0000001f;
        /* TODO update clocks */
        break;

    case 0x230:	/* CM_AUTOIDLE1_CORE */
        s->clkidle[0] = value & 0xfffffff9;
        /* TODO update clocks */
        break;
    case 0x234:	/* CM_AUTOIDLE2_CORE */
        s->clkidle[1] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x238:	/* CM_AUTOIDLE3_CORE */
        s->clkidle[2] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x23c:	/* CM_AUTOIDLE4_CORE */
        s->clkidle[3] = value & 0x0000001f;
        /* TODO update clocks */
        break;

    case 0x240:	/* CM_CLKSEL1_CORE */
        s->clksel[1] = value & 0x0fffbf7f;
        /* TODO update clocks */
        break;

    case 0x244:	/* CM_CLKSEL2_CORE */
        s->clksel[2] = value & 0x00fffffc;
        /* TODO update clocks */
        break;

    case 0x248:	/* CM_CLKSTCTRL_CORE */
        s->clkctrl[1] = value & 0x7;
        break;

    case 0x2a0:	/* PM_WKEN1_CORE */
        s->wken[0] = value & 0x04667ff8;
        break;
    case 0x2a4:	/* PM_WKEN2_CORE */
        s->wken[1] = value & 0x00000005;
        break;

    case 0x2b0:	/* PM_WKST1_CORE */
        s->wkst[0] &= ~value;
        break;
    case 0x2b4:	/* PM_WKST2_CORE */
        s->wkst[1] &= ~value;
        break;

    case 0x2e0:	/* PM_PWSTCTRL_CORE */
        s->power[1] = (value & 0x00fc3f) | (1 << 2);
        break;

    case 0x300:	/* CM_FCLKEN_GFX */
        s->clken[5] = value & 6;
        /* TODO update clocks */
        break;
    case 0x310:	/* CM_ICLKEN_GFX */
        s->clken[6] = value & 1;
        /* TODO update clocks */
        break;
    case 0x340:	/* CM_CLKSEL_GFX */
        s->clksel[3] = value & 7;
        /* TODO update clocks */
        break;
    case 0x348:	/* CM_CLKSTCTRL_GFX */
        s->clkctrl[2] = value & 1;
        break;
    case 0x350:	/* RM_RSTCTRL_GFX */
        s->rstctrl[0] = value & 1;
        /* TODO: reset */
        break;
    case 0x358:	/* RM_RSTST_GFX */
        s->rst[1] &= ~value;
        break;
    case 0x3c8:	/* PM_WKDEP_GFX */
        s->wkup[1] = value & 0x13;
        break;
    case 0x3e0:	/* PM_PWSTCTRL_GFX */
        s->power[2] = (value & 0x00c0f) | (3 << 2);
        break;

    case 0x400:	/* CM_FCLKEN_WKUP */
        s->clken[7] = value & 0xd;
        /* TODO update clocks */
        break;
    case 0x410:	/* CM_ICLKEN_WKUP */
        s->clken[8] = value & 0x3f;
        /* TODO update clocks */
        break;
    case 0x430:	/* CM_AUTOIDLE_WKUP */
        s->clkidle[4] = value & 0x0000003f;
        /* TODO update clocks */
        break;
    case 0x440:	/* CM_CLKSEL_WKUP */
        s->clksel[4] = value & 3;
        /* TODO update clocks */
        break;
    case 0x450:	/* RM_RSTCTRL_WKUP */
        /* TODO: reset */
        if (value & 2)
            qemu_system_reset_request();
        break;
    case 0x454:	/* RM_RSTTIME_WKUP */
        s->rsttime_wkup = value & 0x1fff;
        break;
    case 0x458:	/* RM_RSTST_WKUP */
        s->rst[2] &= ~value;
        break;
    case 0x4a0:	/* PM_WKEN_WKUP */
        s->wken[2] = value & 0x00000005;
        break;
    case 0x4b0:	/* PM_WKST_WKUP */
        s->wkst[2] &= ~value;
        break;

    case 0x500:	/* CM_CLKEN_PLL */
        if (value & 0xffffff30)
            fprintf(stderr, "%s: write 0s in CM_CLKEN_PLL for "
                            "future compatiblity\n", __FUNCTION__);
        if ((s->clken[9] ^ value) & 0xcc) {
            s->clken[9] &= ~0xcc;
            s->clken[9] |= value & 0xcc;
            omap_prcm_apll_update(s);
        }
        if ((s->clken[9] ^ value) & 3) {
            s->clken[9] &= ~3;
            s->clken[9] |= value & 3;
            omap_prcm_dpll_update(s);
        }
        break;
    case 0x530:	/* CM_AUTOIDLE_PLL */
        s->clkidle[5] = value & 0x000000cf;
        /* TODO update clocks */
        break;
    case 0x540:	/* CM_CLKSEL1_PLL */
        if (value & 0xfc4000d7)
            fprintf(stderr, "%s: write 0s in CM_CLKSEL1_PLL for "
                            "future compatiblity\n", __FUNCTION__);
        if ((s->clksel[5] ^ value) & 0x003fff00) {
            s->clksel[5] = value & 0x03bfff28;
            omap_prcm_dpll_update(s);
        }
        /* TODO update the other clocks */

        s->clksel[5] = value & 0x03bfff28;
        break;
    case 0x544:	/* CM_CLKSEL2_PLL */
        if (value & ~3)
            fprintf(stderr, "%s: write 0s in CM_CLKSEL2_PLL[31:2] for "
                            "future compatiblity\n", __FUNCTION__);
        if (s->clksel[6] != (value & 3)) {
            s->clksel[6] = value & 3;
            omap_prcm_dpll_update(s);
        }
        break;

    case 0x800:	/* CM_FCLKEN_DSP */
        s->clken[10] = value & 0x501;
        /* TODO update clocks */
        break;
    case 0x810:	/* CM_ICLKEN_DSP */
        s->clken[11] = value & 0x2;
        /* TODO update clocks */
        break;
    case 0x830:	/* CM_AUTOIDLE_DSP */
        s->clkidle[6] = value & 0x2;
        /* TODO update clocks */
        break;
    case 0x840:	/* CM_CLKSEL_DSP */
        s->clksel[7] = value & 0x3fff;
        /* TODO update clocks */
        break;
    case 0x848:	/* CM_CLKSTCTRL_DSP */
        s->clkctrl[3] = value & 0x101;
        break;
    case 0x850:	/* RM_RSTCTRL_DSP */
        /* TODO: reset */
        break;
    case 0x858:	/* RM_RSTST_DSP */
        s->rst[3] &= ~value;
        break;
    case 0x8c8:	/* PM_WKDEP_DSP */
        s->wkup[2] = value & 0x13;
        break;
    case 0x8e0:	/* PM_PWSTCTRL_DSP */
        s->power[3] = (value & 0x03017) | (3 << 2);
        break;

    case 0x8f0:	/* PRCM_IRQSTATUS_DSP */
        s->irqst[1] &= ~value;
        omap_prcm_int_update(s, 1);
        break;
    case 0x8f4:	/* PRCM_IRQENABLE_DSP */
        s->irqen[1] = value & 0x7;
        omap_prcm_int_update(s, 1);
        break;

    case 0x8f8:	/* PRCM_IRQSTATUS_IVA */
        s->irqst[2] &= ~value;
        omap_prcm_int_update(s, 2);
        break;
    case 0x8fc:	/* PRCM_IRQENABLE_IVA */
        s->irqen[2] = value & 0x7;
        omap_prcm_int_update(s, 2);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_prcm_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_prcm_read,
};

static CPUWriteMemoryFunc *omap_prcm_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_prcm_write,
};

static void omap_prcm_reset(struct omap_prcm_s *s)
{
    s->sysconfig = 0;
    s->irqst[0] = 0;
    s->irqst[1] = 0;
    s->irqst[2] = 0;
    s->irqen[0] = 0;
    s->irqen[1] = 0;
    s->irqen[2] = 0;
    s->voltctrl = 0x1040;
    s->ev = 0x14;
    s->evtime[0] = 0;
    s->evtime[1] = 0;
    s->clkctrl[0] = 0;
    s->clkctrl[1] = 0;
    s->clkctrl[2] = 0;
    s->clkctrl[3] = 0;
    s->clken[1] = 7;
    s->clken[3] = 7;
    s->clken[4] = 0;
    s->clken[5] = 0;
    s->clken[6] = 0;
    s->clken[7] = 0xc;
    s->clken[8] = 0x3e;
    s->clken[9] = 0x0d;
    s->clken[10] = 0;
    s->clken[11] = 0;
    s->clkidle[0] = 0;
    s->clkidle[2] = 7;
    s->clkidle[3] = 0;
    s->clkidle[4] = 0;
    s->clkidle[5] = 0x0c;
    s->clkidle[6] = 0;
    s->clksel[0] = 0x01;
    s->clksel[1] = 0x02100121;
    s->clksel[2] = 0x00000000;
    s->clksel[3] = 0x01;
    s->clksel[4] = 0;
    s->clksel[7] = 0x0121;
    s->wkup[0] = 0x15;
    s->wkup[1] = 0x13;
    s->wkup[2] = 0x13;
    s->wken[0] = 0x04667ff8;
    s->wken[1] = 0x00000005;
    s->wken[2] = 5;
    s->wkst[0] = 0;
    s->wkst[1] = 0;
    s->wkst[2] = 0;
    s->power[0] = 0x00c;
    s->power[1] = 4;
    s->power[2] = 0x0000c;
    s->power[3] = 0x14;
    s->rstctrl[0] = 1;
    s->rst[3] = 1;
    omap_prcm_apll_update(s);
    omap_prcm_dpll_update(s);
}

static void omap_prcm_coldreset(struct omap_prcm_s *s)
{
    s->setuptime[0] = 0;
    s->setuptime[1] = 0;
    memset(&s->scratch, 0, sizeof(s->scratch));
    s->rst[0] = 0x01;
    s->rst[1] = 0x00;
    s->rst[2] = 0x01;
    s->clken[0] = 0;
    s->clken[2] = 0;
    s->clkidle[1] = 0;
    s->clksel[5] = 0;
    s->clksel[6] = 2;
    s->clksrc[0] = 0x43;
    s->clkout[0] = 0x0303;
    s->clkemul[0] = 0;
    s->clkpol[0] = 0x100;
    s->rsttime_wkup = 0x1002;

    omap_prcm_reset(s);
}

struct omap_prcm_s *omap_prcm_init(struct omap_target_agent_s *ta,
                qemu_irq mpu_int, qemu_irq dsp_int, qemu_irq iva_int,
                struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap_prcm_s *s = (struct omap_prcm_s *)
            qemu_mallocz(sizeof(struct omap_prcm_s));

    s->irq[0] = mpu_int;
    s->irq[1] = dsp_int;
    s->irq[2] = iva_int;
    s->mpu = mpu;
    omap_prcm_coldreset(s);

    iomemtype = l4_register_io_memory(0, omap_prcm_readfn,
                    omap_prcm_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    omap_l4_attach(ta, 1, iomemtype);

    return s;
}

/* System and Pinout control */
struct omap_sysctl_s {
    struct omap_mpu_state_s *mpu;

    uint32_t sysconfig;
    uint32_t devconfig;
    uint32_t psaconfig;
    uint32_t padconf[0x45];
    uint8_t obs;
    uint32_t msuspendmux[5];
};

static uint32_t omap_sysctl_read8(void *opaque, target_phys_addr_t addr)
{

    struct omap_sysctl_s *s = (struct omap_sysctl_s *) opaque;
    int pad_offset, byte_offset;
    int value;

    switch (addr) {
    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        pad_offset = (addr - 0x30) >> 2;
        byte_offset = (addr - 0x30) & (4 - 1);

        value = s->padconf[pad_offset];
        value = (value >> (byte_offset * 8)) & 0xff;

        return value;

    default:
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap_sysctl_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_sysctl_s *s = (struct omap_sysctl_s *) opaque;

    switch (addr) {
    case 0x000:	/* CONTROL_REVISION */
        return 0x20;

    case 0x010:	/* CONTROL_SYSCONFIG */
        return s->sysconfig;

    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        return s->padconf[(addr - 0x30) >> 2];

    case 0x270:	/* CONTROL_DEBOBS */
        return s->obs;

    case 0x274:	/* CONTROL_DEVCONF */
        return s->devconfig;

    case 0x28c:	/* CONTROL_EMU_SUPPORT */
        return 0;

    case 0x290:	/* CONTROL_MSUSPENDMUX_0 */
        return s->msuspendmux[0];
    case 0x294:	/* CONTROL_MSUSPENDMUX_1 */
        return s->msuspendmux[1];
    case 0x298:	/* CONTROL_MSUSPENDMUX_2 */
        return s->msuspendmux[2];
    case 0x29c:	/* CONTROL_MSUSPENDMUX_3 */
        return s->msuspendmux[3];
    case 0x2a0:	/* CONTROL_MSUSPENDMUX_4 */
        return s->msuspendmux[4];
    case 0x2a4:	/* CONTROL_MSUSPENDMUX_5 */
        return 0;

    case 0x2b8:	/* CONTROL_PSA_CTRL */
        return s->psaconfig;
    case 0x2bc:	/* CONTROL_PSA_CMD */
    case 0x2c0:	/* CONTROL_PSA_VALUE */
        return 0;

    case 0x2b0:	/* CONTROL_SEC_CTRL */
        return 0x800000f1;
    case 0x2d0:	/* CONTROL_SEC_EMU */
        return 0x80000015;
    case 0x2d4:	/* CONTROL_SEC_TAP */
        return 0x8000007f;
    case 0x2b4:	/* CONTROL_SEC_TEST */
    case 0x2f0:	/* CONTROL_SEC_STATUS */
    case 0x2f4:	/* CONTROL_SEC_ERR_STATUS */
        /* Secure mode is not present on general-pusrpose device.  Outside
         * secure mode these values cannot be read or written.  */
        return 0;

    case 0x2d8:	/* CONTROL_OCM_RAM_PERM */
        return 0xff;
    case 0x2dc:	/* CONTROL_OCM_PUB_RAM_ADD */
    case 0x2e0:	/* CONTROL_EXT_SEC_RAM_START_ADD */
    case 0x2e4:	/* CONTROL_EXT_SEC_RAM_STOP_ADD */
        /* No secure mode so no Extended Secure RAM present.  */
        return 0;

    case 0x2f8:	/* CONTROL_STATUS */
        /* Device Type => General-purpose */
        return 0x0300;
    case 0x2fc:	/* CONTROL_GENERAL_PURPOSE_STATUS */

    case 0x300:	/* CONTROL_RPUB_KEY_H_0 */
    case 0x304:	/* CONTROL_RPUB_KEY_H_1 */
    case 0x308:	/* CONTROL_RPUB_KEY_H_2 */
    case 0x30c:	/* CONTROL_RPUB_KEY_H_3 */
        return 0xdecafbad;

    case 0x310:	/* CONTROL_RAND_KEY_0 */
    case 0x314:	/* CONTROL_RAND_KEY_1 */
    case 0x318:	/* CONTROL_RAND_KEY_2 */
    case 0x31c:	/* CONTROL_RAND_KEY_3 */
    case 0x320:	/* CONTROL_CUST_KEY_0 */
    case 0x324:	/* CONTROL_CUST_KEY_1 */
    case 0x330:	/* CONTROL_TEST_KEY_0 */
    case 0x334:	/* CONTROL_TEST_KEY_1 */
    case 0x338:	/* CONTROL_TEST_KEY_2 */
    case 0x33c:	/* CONTROL_TEST_KEY_3 */
    case 0x340:	/* CONTROL_TEST_KEY_4 */
    case 0x344:	/* CONTROL_TEST_KEY_5 */
    case 0x348:	/* CONTROL_TEST_KEY_6 */
    case 0x34c:	/* CONTROL_TEST_KEY_7 */
    case 0x350:	/* CONTROL_TEST_KEY_8 */
    case 0x354:	/* CONTROL_TEST_KEY_9 */
        /* Can only be accessed in secure mode and when C_FieldAccEnable
         * bit is set in CONTROL_SEC_CTRL.
         * TODO: otherwise an interconnect access error is generated.  */
        return 0;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sysctl_write8(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_sysctl_s *s = (struct omap_sysctl_s *) opaque;
    int pad_offset, byte_offset;
    int prev_value;

    switch (addr) {
    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        pad_offset = (addr - 0x30) >> 2;
        byte_offset = (addr - 0x30) & (4 - 1);

        prev_value = s->padconf[pad_offset];
        prev_value &= ~(0xff << (byte_offset * 8));
        prev_value |= ((value & 0x1f1f1f1f) << (byte_offset * 8)) & 0x1f1f1f1f;
        s->padconf[pad_offset] = prev_value;
        break;

    default:
        OMAP_BAD_REG(addr);
        break;
    }
}

static void omap_sysctl_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_sysctl_s *s = (struct omap_sysctl_s *) opaque;

    switch (addr) {
    case 0x000:	/* CONTROL_REVISION */
    case 0x2a4:	/* CONTROL_MSUSPENDMUX_5 */
    case 0x2c0:	/* CONTROL_PSA_VALUE */
    case 0x2f8:	/* CONTROL_STATUS */
    case 0x2fc:	/* CONTROL_GENERAL_PURPOSE_STATUS */
    case 0x300:	/* CONTROL_RPUB_KEY_H_0 */
    case 0x304:	/* CONTROL_RPUB_KEY_H_1 */
    case 0x308:	/* CONTROL_RPUB_KEY_H_2 */
    case 0x30c:	/* CONTROL_RPUB_KEY_H_3 */
    case 0x310:	/* CONTROL_RAND_KEY_0 */
    case 0x314:	/* CONTROL_RAND_KEY_1 */
    case 0x318:	/* CONTROL_RAND_KEY_2 */
    case 0x31c:	/* CONTROL_RAND_KEY_3 */
    case 0x320:	/* CONTROL_CUST_KEY_0 */
    case 0x324:	/* CONTROL_CUST_KEY_1 */
    case 0x330:	/* CONTROL_TEST_KEY_0 */
    case 0x334:	/* CONTROL_TEST_KEY_1 */
    case 0x338:	/* CONTROL_TEST_KEY_2 */
    case 0x33c:	/* CONTROL_TEST_KEY_3 */
    case 0x340:	/* CONTROL_TEST_KEY_4 */
    case 0x344:	/* CONTROL_TEST_KEY_5 */
    case 0x348:	/* CONTROL_TEST_KEY_6 */
    case 0x34c:	/* CONTROL_TEST_KEY_7 */
    case 0x350:	/* CONTROL_TEST_KEY_8 */
    case 0x354:	/* CONTROL_TEST_KEY_9 */
        OMAP_RO_REG(addr);
        return;

    case 0x010:	/* CONTROL_SYSCONFIG */
        s->sysconfig = value & 0x1e;
        break;

    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        /* XXX: should check constant bits */
        s->padconf[(addr - 0x30) >> 2] = value & 0x1f1f1f1f;
        break;

    case 0x270:	/* CONTROL_DEBOBS */
        s->obs = value & 0xff;
        break;

    case 0x274:	/* CONTROL_DEVCONF */
        s->devconfig = value & 0xffffc7ff;
        break;

    case 0x28c:	/* CONTROL_EMU_SUPPORT */
        break;

    case 0x290:	/* CONTROL_MSUSPENDMUX_0 */
        s->msuspendmux[0] = value & 0x3fffffff;
        break;
    case 0x294:	/* CONTROL_MSUSPENDMUX_1 */
        s->msuspendmux[1] = value & 0x3fffffff;
        break;
    case 0x298:	/* CONTROL_MSUSPENDMUX_2 */
        s->msuspendmux[2] = value & 0x3fffffff;
        break;
    case 0x29c:	/* CONTROL_MSUSPENDMUX_3 */
        s->msuspendmux[3] = value & 0x3fffffff;
        break;
    case 0x2a0:	/* CONTROL_MSUSPENDMUX_4 */
        s->msuspendmux[4] = value & 0x3fffffff;
        break;

    case 0x2b8:	/* CONTROL_PSA_CTRL */
        s->psaconfig = value & 0x1c;
        s->psaconfig |= (value & 0x20) ? 2 : 1;
        break;
    case 0x2bc:	/* CONTROL_PSA_CMD */
        break;

    case 0x2b0:	/* CONTROL_SEC_CTRL */
    case 0x2b4:	/* CONTROL_SEC_TEST */
    case 0x2d0:	/* CONTROL_SEC_EMU */
    case 0x2d4:	/* CONTROL_SEC_TAP */
    case 0x2d8:	/* CONTROL_OCM_RAM_PERM */
    case 0x2dc:	/* CONTROL_OCM_PUB_RAM_ADD */
    case 0x2e0:	/* CONTROL_EXT_SEC_RAM_START_ADD */
    case 0x2e4:	/* CONTROL_EXT_SEC_RAM_STOP_ADD */
    case 0x2f0:	/* CONTROL_SEC_STATUS */
    case 0x2f4:	/* CONTROL_SEC_ERR_STATUS */
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_sysctl_readfn[] = {
    omap_sysctl_read8,
    omap_badwidth_read32,	/* TODO */
    omap_sysctl_read,
};

static CPUWriteMemoryFunc *omap_sysctl_writefn[] = {
    omap_sysctl_write8,
    omap_badwidth_write32,	/* TODO */
    omap_sysctl_write,
};

static void omap_sysctl_reset(struct omap_sysctl_s *s)
{
    /* (power-on reset) */
    s->sysconfig = 0;
    s->obs = 0;
    s->devconfig = 0x0c000000;
    s->msuspendmux[0] = 0x00000000;
    s->msuspendmux[1] = 0x00000000;
    s->msuspendmux[2] = 0x00000000;
    s->msuspendmux[3] = 0x00000000;
    s->msuspendmux[4] = 0x00000000;
    s->psaconfig = 1;

    s->padconf[0x00] = 0x000f0f0f;
    s->padconf[0x01] = 0x00000000;
    s->padconf[0x02] = 0x00000000;
    s->padconf[0x03] = 0x00000000;
    s->padconf[0x04] = 0x00000000;
    s->padconf[0x05] = 0x00000000;
    s->padconf[0x06] = 0x00000000;
    s->padconf[0x07] = 0x00000000;
    s->padconf[0x08] = 0x08080800;
    s->padconf[0x09] = 0x08080808;
    s->padconf[0x0a] = 0x08080808;
    s->padconf[0x0b] = 0x08080808;
    s->padconf[0x0c] = 0x08080808;
    s->padconf[0x0d] = 0x08080800;
    s->padconf[0x0e] = 0x08080808;
    s->padconf[0x0f] = 0x08080808;
    s->padconf[0x10] = 0x18181808;	/* | 0x07070700 if SBoot3 */
    s->padconf[0x11] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x12] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x13] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x14] = 0x18181818;	/* | 0x00070707 if SBoot3 */
    s->padconf[0x15] = 0x18181818;
    s->padconf[0x16] = 0x18181818;	/* | 0x07000000 if SBoot3 */
    s->padconf[0x17] = 0x1f001f00;
    s->padconf[0x18] = 0x1f1f1f1f;
    s->padconf[0x19] = 0x00000000;
    s->padconf[0x1a] = 0x1f180000;
    s->padconf[0x1b] = 0x00001f1f;
    s->padconf[0x1c] = 0x1f001f00;
    s->padconf[0x1d] = 0x00000000;
    s->padconf[0x1e] = 0x00000000;
    s->padconf[0x1f] = 0x08000000;
    s->padconf[0x20] = 0x08080808;
    s->padconf[0x21] = 0x08080808;
    s->padconf[0x22] = 0x0f080808;
    s->padconf[0x23] = 0x0f0f0f0f;
    s->padconf[0x24] = 0x000f0f0f;
    s->padconf[0x25] = 0x1f1f1f0f;
    s->padconf[0x26] = 0x080f0f1f;
    s->padconf[0x27] = 0x070f1808;
    s->padconf[0x28] = 0x0f070707;
    s->padconf[0x29] = 0x000f0f1f;
    s->padconf[0x2a] = 0x0f0f0f1f;
    s->padconf[0x2b] = 0x08000000;
    s->padconf[0x2c] = 0x0000001f;
    s->padconf[0x2d] = 0x0f0f1f00;
    s->padconf[0x2e] = 0x1f1f0f0f;
    s->padconf[0x2f] = 0x0f1f1f1f;
    s->padconf[0x30] = 0x0f0f0f0f;
    s->padconf[0x31] = 0x0f1f0f1f;
    s->padconf[0x32] = 0x0f0f0f0f;
    s->padconf[0x33] = 0x0f1f0f1f;
    s->padconf[0x34] = 0x1f1f0f0f;
    s->padconf[0x35] = 0x0f0f1f1f;
    s->padconf[0x36] = 0x0f0f1f0f;
    s->padconf[0x37] = 0x0f0f0f0f;
    s->padconf[0x38] = 0x1f18180f;
    s->padconf[0x39] = 0x1f1f1f1f;
    s->padconf[0x3a] = 0x00001f1f;
    s->padconf[0x3b] = 0x00000000;
    s->padconf[0x3c] = 0x00000000;
    s->padconf[0x3d] = 0x0f0f0f0f;
    s->padconf[0x3e] = 0x18000f0f;
    s->padconf[0x3f] = 0x00070000;
    s->padconf[0x40] = 0x00000707;
    s->padconf[0x41] = 0x0f1f0700;
    s->padconf[0x42] = 0x1f1f070f;
    s->padconf[0x43] = 0x0008081f;
    s->padconf[0x44] = 0x00000800;
}

struct omap_sysctl_s *omap_sysctl_init(struct omap_target_agent_s *ta,
                omap_clk iclk, struct omap_mpu_state_s *mpu)
{
    int iomemtype;
    struct omap_sysctl_s *s = (struct omap_sysctl_s *)
            qemu_mallocz(sizeof(struct omap_sysctl_s));

    s->mpu = mpu;
    omap_sysctl_reset(s);

    iomemtype = l4_register_io_memory(0, omap_sysctl_readfn,
                    omap_sysctl_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;
}

/* SDRAM Controller Subsystem */
struct omap_sdrc_s {
    uint8_t config;
};

static void omap_sdrc_reset(struct omap_sdrc_s *s)
{
    s->config = 0x10;
}

static uint32_t omap_sdrc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_sdrc_s *s = (struct omap_sdrc_s *) opaque;

    switch (addr) {
    case 0x00:	/* SDRC_REVISION */
        return 0x20;

    case 0x10:	/* SDRC_SYSCONFIG */
        return s->config;

    case 0x14:	/* SDRC_SYSSTATUS */
        return 1;						/* RESETDONE */

    case 0x40:	/* SDRC_CS_CFG */
    case 0x44:	/* SDRC_SHARING */
    case 0x48:	/* SDRC_ERR_ADDR */
    case 0x4c:	/* SDRC_ERR_TYPE */
    case 0x60:	/* SDRC_DLLA_SCTRL */
    case 0x64:	/* SDRC_DLLA_STATUS */
    case 0x68:	/* SDRC_DLLB_CTRL */
    case 0x6c:	/* SDRC_DLLB_STATUS */
    case 0x70:	/* SDRC_POWER */
    case 0x80:	/* SDRC_MCFG_0 */
    case 0x84:	/* SDRC_MR_0 */
    case 0x88:	/* SDRC_EMR1_0 */
    case 0x8c:	/* SDRC_EMR2_0 */
    case 0x90:	/* SDRC_EMR3_0 */
    case 0x94:	/* SDRC_DCDL1_CTRL */
    case 0x98:	/* SDRC_DCDL2_CTRL */
    case 0x9c:	/* SDRC_ACTIM_CTRLA_0 */
    case 0xa0:	/* SDRC_ACTIM_CTRLB_0 */
    case 0xa4:	/* SDRC_RFR_CTRL_0 */
    case 0xa8:	/* SDRC_MANUAL_0 */
    case 0xb0:	/* SDRC_MCFG_1 */
    case 0xb4:	/* SDRC_MR_1 */
    case 0xb8:	/* SDRC_EMR1_1 */
    case 0xbc:	/* SDRC_EMR2_1 */
    case 0xc0:	/* SDRC_EMR3_1 */
    case 0xc4:	/* SDRC_ACTIM_CTRLA_1 */
    case 0xc8:	/* SDRC_ACTIM_CTRLB_1 */
    case 0xd4:	/* SDRC_RFR_CTRL_1 */
    case 0xd8:	/* SDRC_MANUAL_1 */
        return 0x00;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sdrc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_sdrc_s *s = (struct omap_sdrc_s *) opaque;

    switch (addr) {
    case 0x00:	/* SDRC_REVISION */
    case 0x14:	/* SDRC_SYSSTATUS */
    case 0x48:	/* SDRC_ERR_ADDR */
    case 0x64:	/* SDRC_DLLA_STATUS */
    case 0x6c:	/* SDRC_DLLB_STATUS */
        OMAP_RO_REG(addr);
        return;

    case 0x10:	/* SDRC_SYSCONFIG */
        if ((value >> 3) != 0x2)
            fprintf(stderr, "%s: bad SDRAM idle mode %i\n",
                            __FUNCTION__, value >> 3);
        if (value & 2)
            omap_sdrc_reset(s);
        s->config = value & 0x18;
        break;

    case 0x40:	/* SDRC_CS_CFG */
    case 0x44:	/* SDRC_SHARING */
    case 0x4c:	/* SDRC_ERR_TYPE */
    case 0x60:	/* SDRC_DLLA_SCTRL */
    case 0x68:	/* SDRC_DLLB_CTRL */
    case 0x70:	/* SDRC_POWER */
    case 0x80:	/* SDRC_MCFG_0 */
    case 0x84:	/* SDRC_MR_0 */
    case 0x88:	/* SDRC_EMR1_0 */
    case 0x8c:	/* SDRC_EMR2_0 */
    case 0x90:	/* SDRC_EMR3_0 */
    case 0x94:	/* SDRC_DCDL1_CTRL */
    case 0x98:	/* SDRC_DCDL2_CTRL */
    case 0x9c:	/* SDRC_ACTIM_CTRLA_0 */
    case 0xa0:	/* SDRC_ACTIM_CTRLB_0 */
    case 0xa4:	/* SDRC_RFR_CTRL_0 */
    case 0xa8:	/* SDRC_MANUAL_0 */
    case 0xb0:	/* SDRC_MCFG_1 */
    case 0xb4:	/* SDRC_MR_1 */
    case 0xb8:	/* SDRC_EMR1_1 */
    case 0xbc:	/* SDRC_EMR2_1 */
    case 0xc0:	/* SDRC_EMR3_1 */
    case 0xc4:	/* SDRC_ACTIM_CTRLA_1 */
    case 0xc8:	/* SDRC_ACTIM_CTRLB_1 */
    case 0xd4:	/* SDRC_RFR_CTRL_1 */
    case 0xd8:	/* SDRC_MANUAL_1 */
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_sdrc_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_sdrc_read,
};

static CPUWriteMemoryFunc *omap_sdrc_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_sdrc_write,
};

struct omap_sdrc_s *omap_sdrc_init(target_phys_addr_t base)
{
    int iomemtype;
    struct omap_sdrc_s *s = (struct omap_sdrc_s *)
            qemu_mallocz(sizeof(struct omap_sdrc_s));

    omap_sdrc_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_sdrc_readfn,
                    omap_sdrc_writefn, s);
    cpu_register_physical_memory(base, 0x1000, iomemtype);

    return s;
}

/* General-Purpose Memory Controller */
struct omap_gpmc_s {
    qemu_irq irq;

    uint8_t sysconfig;
    uint16_t irqst;
    uint16_t irqen;
    uint16_t timeout;
    uint16_t config;
    uint32_t prefconfig[2];
    int prefcontrol;
    int preffifo;
    int prefcount;
    struct omap_gpmc_cs_file_s {
        uint32_t config[7];
        target_phys_addr_t base;
        size_t size;
        int iomemtype;
        void (*base_update)(void *opaque, target_phys_addr_t new);
        void (*unmap)(void *opaque);
        void *opaque;
    } cs_file[8];
    int ecc_cs;
    int ecc_ptr;
    uint32_t ecc_cfg;
    struct ecc_state_s ecc[9];
};

static void omap_gpmc_int_update(struct omap_gpmc_s *s)
{
    qemu_set_irq(s->irq, s->irqen & s->irqst);
}

static void omap_gpmc_cs_map(struct omap_gpmc_cs_file_s *f, int base, int mask)
{
    /* TODO: check for overlapping regions and report access errors */
    if ((mask != 0x8 && mask != 0xc && mask != 0xe && mask != 0xf) ||
                    (base < 0 || base >= 0x40) ||
                    (base & 0x0f & ~mask)) {
        fprintf(stderr, "%s: wrong cs address mapping/decoding!\n",
                        __FUNCTION__);
        return;
    }

    if (!f->opaque)
        return;

    f->base = base << 24;
    f->size = (0x0fffffff & ~(mask << 24)) + 1;
    /* TODO: rather than setting the size of the mapping (which should be
     * constant), the mask should cause wrapping of the address space, so
     * that the same memory becomes accessible at every <i>size</i> bytes
     * starting from <i>base</i>.  */
    if (f->iomemtype)
        cpu_register_physical_memory(f->base, f->size, f->iomemtype);

    if (f->base_update)
        f->base_update(f->opaque, f->base);
}

static void omap_gpmc_cs_unmap(struct omap_gpmc_cs_file_s *f)
{
    if (f->size) {
        if (f->unmap)
            f->unmap(f->opaque);
        if (f->iomemtype)
            cpu_register_physical_memory(f->base, f->size, IO_MEM_UNASSIGNED);
        f->base = 0;
        f->size = 0;
    }
}

static void omap_gpmc_reset(struct omap_gpmc_s *s)
{
    int i;

    s->sysconfig = 0;
    s->irqst = 0;
    s->irqen = 0;
    omap_gpmc_int_update(s);
    s->timeout = 0;
    s->config = 0xa00;
    s->prefconfig[0] = 0x00004000;
    s->prefconfig[1] = 0x00000000;
    s->prefcontrol = 0;
    s->preffifo = 0;
    s->prefcount = 0;
    for (i = 0; i < 8; i ++) {
        if (s->cs_file[i].config[6] & (1 << 6))			/* CSVALID */
            omap_gpmc_cs_unmap(s->cs_file + i);
        s->cs_file[i].config[0] = i ? 1 << 12 : 0;
        s->cs_file[i].config[1] = 0x101001;
        s->cs_file[i].config[2] = 0x020201;
        s->cs_file[i].config[3] = 0x10031003;
        s->cs_file[i].config[4] = 0x10f1111;
        s->cs_file[i].config[5] = 0;
        s->cs_file[i].config[6] = 0xf00 | (i ? 0 : 1 << 6);
        if (s->cs_file[i].config[6] & (1 << 6))			/* CSVALID */
            omap_gpmc_cs_map(&s->cs_file[i],
                            s->cs_file[i].config[6] & 0x1f,	/* MASKADDR */
                        (s->cs_file[i].config[6] >> 8 & 0xf));	/* BASEADDR */
    }
    omap_gpmc_cs_map(s->cs_file, 0, 0xf);
    s->ecc_cs = 0;
    s->ecc_ptr = 0;
    s->ecc_cfg = 0x3fcff000;
    for (i = 0; i < 9; i ++)
        ecc_reset(&s->ecc[i]);
}

static uint32_t omap_gpmc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_gpmc_s *s = (struct omap_gpmc_s *) opaque;
    int cs;
    struct omap_gpmc_cs_file_s *f;

    switch (addr) {
    case 0x000:	/* GPMC_REVISION */
        return 0x20;

    case 0x010:	/* GPMC_SYSCONFIG */
        return s->sysconfig;

    case 0x014:	/* GPMC_SYSSTATUS */
        return 1;						/* RESETDONE */

    case 0x018:	/* GPMC_IRQSTATUS */
        return s->irqst;

    case 0x01c:	/* GPMC_IRQENABLE */
        return s->irqen;

    case 0x040:	/* GPMC_TIMEOUT_CONTROL */
        return s->timeout;

    case 0x044:	/* GPMC_ERR_ADDRESS */
    case 0x048:	/* GPMC_ERR_TYPE */
        return 0;

    case 0x050:	/* GPMC_CONFIG */
        return s->config;

    case 0x054:	/* GPMC_STATUS */
        return 0x001;

    case 0x060 ... 0x1d4:
        cs = (addr - 0x060) / 0x30;
        addr -= cs * 0x30;
        f = s->cs_file + cs;
        switch (addr) {
            case 0x60:	/* GPMC_CONFIG1 */
                return f->config[0];
            case 0x64:	/* GPMC_CONFIG2 */
                return f->config[1];
            case 0x68:	/* GPMC_CONFIG3 */
                return f->config[2];
            case 0x6c:	/* GPMC_CONFIG4 */
                return f->config[3];
            case 0x70:	/* GPMC_CONFIG5 */
                return f->config[4];
            case 0x74:	/* GPMC_CONFIG6 */
                return f->config[5];
            case 0x78:	/* GPMC_CONFIG7 */
                return f->config[6];
            case 0x84:	/* GPMC_NAND_DATA */
                return 0;
        }
        break;

    case 0x1e0:	/* GPMC_PREFETCH_CONFIG1 */
        return s->prefconfig[0];
    case 0x1e4:	/* GPMC_PREFETCH_CONFIG2 */
        return s->prefconfig[1];
    case 0x1ec:	/* GPMC_PREFETCH_CONTROL */
        return s->prefcontrol;
    case 0x1f0:	/* GPMC_PREFETCH_STATUS */
        return (s->preffifo << 24) |
                ((s->preffifo >
                  ((s->prefconfig[0] >> 8) & 0x7f) ? 1 : 0) << 16) |
                s->prefcount;

    case 0x1f4:	/* GPMC_ECC_CONFIG */
        return s->ecc_cs;
    case 0x1f8:	/* GPMC_ECC_CONTROL */
        return s->ecc_ptr;
    case 0x1fc:	/* GPMC_ECC_SIZE_CONFIG */
        return s->ecc_cfg;
    case 0x200 ... 0x220:	/* GPMC_ECC_RESULT */
        cs = (addr & 0x1f) >> 2;
        /* TODO: check correctness */
        return
                ((s->ecc[cs].cp    &  0x07) <<  0) |
                ((s->ecc[cs].cp    &  0x38) << 13) |
                ((s->ecc[cs].lp[0] & 0x1ff) <<  3) |
                ((s->ecc[cs].lp[1] & 0x1ff) << 19);

    case 0x230:	/* GPMC_TESTMODE_CTRL */
        return 0;
    case 0x234:	/* GPMC_PSA_LSB */
    case 0x238:	/* GPMC_PSA_MSB */
        return 0x00000000;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpmc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_gpmc_s *s = (struct omap_gpmc_s *) opaque;
    int cs;
    struct omap_gpmc_cs_file_s *f;

    switch (addr) {
    case 0x000:	/* GPMC_REVISION */
    case 0x014:	/* GPMC_SYSSTATUS */
    case 0x054:	/* GPMC_STATUS */
    case 0x1f0:	/* GPMC_PREFETCH_STATUS */
    case 0x200 ... 0x220:	/* GPMC_ECC_RESULT */
    case 0x234:	/* GPMC_PSA_LSB */
    case 0x238:	/* GPMC_PSA_MSB */
        OMAP_RO_REG(addr);
        break;

    case 0x010:	/* GPMC_SYSCONFIG */
        if ((value >> 3) == 0x3)
            fprintf(stderr, "%s: bad SDRAM idle mode %i\n",
                            __FUNCTION__, value >> 3);
        if (value & 2)
            omap_gpmc_reset(s);
        s->sysconfig = value & 0x19;
        break;

    case 0x018:	/* GPMC_IRQSTATUS */
        s->irqen = ~value;
        omap_gpmc_int_update(s);
        break;

    case 0x01c:	/* GPMC_IRQENABLE */
        s->irqen = value & 0xf03;
        omap_gpmc_int_update(s);
        break;

    case 0x040:	/* GPMC_TIMEOUT_CONTROL */
        s->timeout = value & 0x1ff1;
        break;

    case 0x044:	/* GPMC_ERR_ADDRESS */
    case 0x048:	/* GPMC_ERR_TYPE */
        break;

    case 0x050:	/* GPMC_CONFIG */
        s->config = value & 0xf13;
        break;

    case 0x060 ... 0x1d4:
        cs = (addr - 0x060) / 0x30;
        addr -= cs * 0x30;
        f = s->cs_file + cs;
        switch (addr) {
            case 0x60:	/* GPMC_CONFIG1 */
                f->config[0] = value & 0xffef3e13;
                break;
            case 0x64:	/* GPMC_CONFIG2 */
                f->config[1] = value & 0x001f1f8f;
                break;
            case 0x68:	/* GPMC_CONFIG3 */
                f->config[2] = value & 0x001f1f8f;
                break;
            case 0x6c:	/* GPMC_CONFIG4 */
                f->config[3] = value & 0x1f8f1f8f;
                break;
            case 0x70:	/* GPMC_CONFIG5 */
                f->config[4] = value & 0x0f1f1f1f;
                break;
            case 0x74:	/* GPMC_CONFIG6 */
                f->config[5] = value & 0x00000fcf;
                break;
            case 0x78:	/* GPMC_CONFIG7 */
                if ((f->config[6] ^ value) & 0xf7f) {
                    if (f->config[6] & (1 << 6))		/* CSVALID */
                        omap_gpmc_cs_unmap(f);
                    if (value & (1 << 6))			/* CSVALID */
                        omap_gpmc_cs_map(f, value & 0x1f,	/* MASKADDR */
                                        (value >> 8 & 0xf));	/* BASEADDR */
                }
                f->config[6] = value & 0x00000f7f;
                break;
            case 0x7c:	/* GPMC_NAND_COMMAND */
            case 0x80:	/* GPMC_NAND_ADDRESS */
            case 0x84:	/* GPMC_NAND_DATA */
                break;

            default:
                goto bad_reg;
        }
        break;

    case 0x1e0:	/* GPMC_PREFETCH_CONFIG1 */
        s->prefconfig[0] = value & 0x7f8f7fbf;
        /* TODO: update interrupts, fifos, dmas */
        break;

    case 0x1e4:	/* GPMC_PREFETCH_CONFIG2 */
        s->prefconfig[1] = value & 0x3fff;
        break;

    case 0x1ec:	/* GPMC_PREFETCH_CONTROL */
        s->prefcontrol = value & 1;
        if (s->prefcontrol) {
            if (s->prefconfig[0] & 1)
                s->preffifo = 0x40;
            else
                s->preffifo = 0x00;
        }
        /* TODO: start */
        break;

    case 0x1f4:	/* GPMC_ECC_CONFIG */
        s->ecc_cs = 0x8f;
        break;
    case 0x1f8:	/* GPMC_ECC_CONTROL */
        if (value & (1 << 8))
            for (cs = 0; cs < 9; cs ++)
                ecc_reset(&s->ecc[cs]);
        s->ecc_ptr = value & 0xf;
        if (s->ecc_ptr == 0 || s->ecc_ptr > 9) {
            s->ecc_ptr = 0;
            s->ecc_cs &= ~1;
        }
        break;
    case 0x1fc:	/* GPMC_ECC_SIZE_CONFIG */
        s->ecc_cfg = value & 0x3fcff1ff;
        break;
    case 0x230:	/* GPMC_TESTMODE_CTRL */
        if (value & 7)
            fprintf(stderr, "%s: test mode enable attempt\n", __FUNCTION__);
        break;

    default:
    bad_reg:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_gpmc_readfn[] = {
    omap_badwidth_read32,	/* TODO */
    omap_badwidth_read32,	/* TODO */
    omap_gpmc_read,
};

static CPUWriteMemoryFunc *omap_gpmc_writefn[] = {
    omap_badwidth_write32,	/* TODO */
    omap_badwidth_write32,	/* TODO */
    omap_gpmc_write,
};

struct omap_gpmc_s *omap_gpmc_init(target_phys_addr_t base, qemu_irq irq)
{
    int iomemtype;
    struct omap_gpmc_s *s = (struct omap_gpmc_s *)
            qemu_mallocz(sizeof(struct omap_gpmc_s));

    omap_gpmc_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_gpmc_readfn,
                    omap_gpmc_writefn, s);
    cpu_register_physical_memory(base, 0x1000, iomemtype);

    return s;
}

void omap_gpmc_attach(struct omap_gpmc_s *s, int cs, int iomemtype,
                void (*base_upd)(void *opaque, target_phys_addr_t new),
                void (*unmap)(void *opaque), void *opaque)
{
    struct omap_gpmc_cs_file_s *f;

    if (cs < 0 || cs >= 8) {
        fprintf(stderr, "%s: bad chip-select %i\n", __FUNCTION__, cs);
        exit(-1);
    }
    f = &s->cs_file[cs];

    f->iomemtype = iomemtype;
    f->base_update = base_upd;
    f->unmap = unmap;
    f->opaque = opaque;

    if (f->config[6] & (1 << 6))				/* CSVALID */
        omap_gpmc_cs_map(f, f->config[6] & 0x1f,		/* MASKADDR */
                        (f->config[6] >> 8 & 0xf));		/* BASEADDR */
}

/* General chip reset */
static void omap2_mpu_reset(void *opaque)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;

    omap_inth_reset(mpu->ih[0]);
    omap_dma_reset(mpu->dma);
    omap_prcm_reset(mpu->prcm);
    omap_sysctl_reset(mpu->sysc);
    omap_gp_timer_reset(mpu->gptimer[0]);
    omap_gp_timer_reset(mpu->gptimer[1]);
    omap_gp_timer_reset(mpu->gptimer[2]);
    omap_gp_timer_reset(mpu->gptimer[3]);
    omap_gp_timer_reset(mpu->gptimer[4]);
    omap_gp_timer_reset(mpu->gptimer[5]);
    omap_gp_timer_reset(mpu->gptimer[6]);
    omap_gp_timer_reset(mpu->gptimer[7]);
    omap_gp_timer_reset(mpu->gptimer[8]);
    omap_gp_timer_reset(mpu->gptimer[9]);
    omap_gp_timer_reset(mpu->gptimer[10]);
    omap_gp_timer_reset(mpu->gptimer[11]);
    omap_synctimer_reset(&mpu->synctimer);
    omap_sdrc_reset(mpu->sdrc);
    omap_gpmc_reset(mpu->gpmc);
    omap_dss_reset(mpu->dss);
    omap_uart_reset(mpu->uart[0]);
    omap_uart_reset(mpu->uart[1]);
    omap_uart_reset(mpu->uart[2]);
    omap_mmc_reset(mpu->mmc);
    omap_gpif_reset(mpu->gpif);
    omap_mcspi_reset(mpu->mcspi[0]);
    omap_mcspi_reset(mpu->mcspi[1]);
    omap_i2c_reset(mpu->i2c[0]);
    omap_i2c_reset(mpu->i2c[1]);
    cpu_reset(mpu->env);
}

static int omap2_validate_addr(struct omap_mpu_state_s *s,
                target_phys_addr_t addr)
{
    return 1;
}

static const struct dma_irq_map omap2_dma_irq_map[] = {
    { 0, OMAP_INT_24XX_SDMA_IRQ0 },
    { 0, OMAP_INT_24XX_SDMA_IRQ1 },
    { 0, OMAP_INT_24XX_SDMA_IRQ2 },
    { 0, OMAP_INT_24XX_SDMA_IRQ3 },
};

struct omap_mpu_state_s *omap2420_mpu_init(unsigned long sdram_size,
                const char *core)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *)
            qemu_mallocz(sizeof(struct omap_mpu_state_s));
    ram_addr_t sram_base, q2_base;
    qemu_irq *cpu_irq;
    qemu_irq dma_irqs[4];
    omap_clk gpio_clks[4];
    int sdindex;
    int i;

    /* Core */
    s->mpu_model = omap2420;
    s->env = cpu_init(core ?: "arm1136-r2");
    if (!s->env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    s->sdram_size = sdram_size;
    s->sram_size = OMAP242X_SRAM_SIZE;

    s->wakeup = qemu_allocate_irqs(omap_mpu_wakeup, s, 1)[0];

    /* Clocks */
    omap_clk_init(s);

    /* Memory-mapped stuff */
    cpu_register_physical_memory(OMAP2_Q2_BASE, s->sdram_size,
                    (q2_base = qemu_ram_alloc(s->sdram_size)) | IO_MEM_RAM);
    cpu_register_physical_memory(OMAP2_SRAM_BASE, s->sram_size,
                    (sram_base = qemu_ram_alloc(s->sram_size)) | IO_MEM_RAM);

    s->l4 = omap_l4_init(OMAP2_L4_BASE, 54);

    /* Actually mapped at any 2K boundary in the ARM11 private-peripheral if */
    cpu_irq = arm_pic_init_cpu(s->env);
    s->ih[0] = omap2_inth_init(0x480fe000, 0x1000, 3, &s->irq[0],
                    cpu_irq[ARM_PIC_CPU_IRQ], cpu_irq[ARM_PIC_CPU_FIQ],
                    omap_findclk(s, "mpu_intc_fclk"),
                    omap_findclk(s, "mpu_intc_iclk"));

    s->prcm = omap_prcm_init(omap_l4tao(s->l4, 3),
                    s->irq[0][OMAP_INT_24XX_PRCM_MPU_IRQ], NULL, NULL, s);

    s->sysc = omap_sysctl_init(omap_l4tao(s->l4, 1),
                    omap_findclk(s, "omapctrl_iclk"), s);

    for (i = 0; i < 4; i ++)
        dma_irqs[i] =
                s->irq[omap2_dma_irq_map[i].ih][omap2_dma_irq_map[i].intr];
    s->dma = omap_dma4_init(0x48056000, dma_irqs, s, 256, 32,
                    omap_findclk(s, "sdma_iclk"),
                    omap_findclk(s, "sdma_fclk"));
    s->port->addr_valid = omap2_validate_addr;

    /* Register SDRAM and SRAM ports for fast DMA transfers.  */
    soc_dma_port_add_mem_ram(s->dma, q2_base, OMAP2_Q2_BASE, s->sdram_size);
    soc_dma_port_add_mem_ram(s->dma, sram_base, OMAP2_SRAM_BASE, s->sram_size);

    s->uart[0] = omap2_uart_init(omap_l4ta(s->l4, 19),
                    s->irq[0][OMAP_INT_24XX_UART1_IRQ],
                    omap_findclk(s, "uart1_fclk"),
                    omap_findclk(s, "uart1_iclk"),
                    s->drq[OMAP24XX_DMA_UART1_TX],
                    s->drq[OMAP24XX_DMA_UART1_RX], serial_hds[0]);
    s->uart[1] = omap2_uart_init(omap_l4ta(s->l4, 20),
                    s->irq[0][OMAP_INT_24XX_UART2_IRQ],
                    omap_findclk(s, "uart2_fclk"),
                    omap_findclk(s, "uart2_iclk"),
                    s->drq[OMAP24XX_DMA_UART2_TX],
                    s->drq[OMAP24XX_DMA_UART2_RX],
                    serial_hds[0] ? serial_hds[1] : 0);
    s->uart[2] = omap2_uart_init(omap_l4ta(s->l4, 21),
                    s->irq[0][OMAP_INT_24XX_UART3_IRQ],
                    omap_findclk(s, "uart3_fclk"),
                    omap_findclk(s, "uart3_iclk"),
                    s->drq[OMAP24XX_DMA_UART3_TX],
                    s->drq[OMAP24XX_DMA_UART3_RX],
                    serial_hds[0] && serial_hds[1] ? serial_hds[2] : 0);

    s->gptimer[0] = omap_gp_timer_init(omap_l4ta(s->l4, 7),
                    s->irq[0][OMAP_INT_24XX_GPTIMER1],
                    omap_findclk(s, "wu_gpt1_clk"),
                    omap_findclk(s, "wu_l4_iclk"));
    s->gptimer[1] = omap_gp_timer_init(omap_l4ta(s->l4, 8),
                    s->irq[0][OMAP_INT_24XX_GPTIMER2],
                    omap_findclk(s, "core_gpt2_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[2] = omap_gp_timer_init(omap_l4ta(s->l4, 22),
                    s->irq[0][OMAP_INT_24XX_GPTIMER3],
                    omap_findclk(s, "core_gpt3_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[3] = omap_gp_timer_init(omap_l4ta(s->l4, 23),
                    s->irq[0][OMAP_INT_24XX_GPTIMER4],
                    omap_findclk(s, "core_gpt4_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[4] = omap_gp_timer_init(omap_l4ta(s->l4, 24),
                    s->irq[0][OMAP_INT_24XX_GPTIMER5],
                    omap_findclk(s, "core_gpt5_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[5] = omap_gp_timer_init(omap_l4ta(s->l4, 25),
                    s->irq[0][OMAP_INT_24XX_GPTIMER6],
                    omap_findclk(s, "core_gpt6_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[6] = omap_gp_timer_init(omap_l4ta(s->l4, 26),
                    s->irq[0][OMAP_INT_24XX_GPTIMER7],
                    omap_findclk(s, "core_gpt7_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[7] = omap_gp_timer_init(omap_l4ta(s->l4, 27),
                    s->irq[0][OMAP_INT_24XX_GPTIMER8],
                    omap_findclk(s, "core_gpt8_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[8] = omap_gp_timer_init(omap_l4ta(s->l4, 28),
                    s->irq[0][OMAP_INT_24XX_GPTIMER9],
                    omap_findclk(s, "core_gpt9_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[9] = omap_gp_timer_init(omap_l4ta(s->l4, 29),
                    s->irq[0][OMAP_INT_24XX_GPTIMER10],
                    omap_findclk(s, "core_gpt10_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[10] = omap_gp_timer_init(omap_l4ta(s->l4, 30),
                    s->irq[0][OMAP_INT_24XX_GPTIMER11],
                    omap_findclk(s, "core_gpt11_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[11] = omap_gp_timer_init(omap_l4ta(s->l4, 31),
                    s->irq[0][OMAP_INT_24XX_GPTIMER12],
                    omap_findclk(s, "core_gpt12_clk"),
                    omap_findclk(s, "core_l4_iclk"));

    omap_tap_init(omap_l4ta(s->l4, 2), s);

    omap_synctimer_init(omap_l4tao(s->l4, 2), s,
                    omap_findclk(s, "clk32-kHz"),
                    omap_findclk(s, "core_l4_iclk"));

    s->i2c[0] = omap2_i2c_init(omap_l4tao(s->l4, 5),
                    s->irq[0][OMAP_INT_24XX_I2C1_IRQ],
                    &s->drq[OMAP24XX_DMA_I2C1_TX],
                    omap_findclk(s, "i2c1.fclk"),
                    omap_findclk(s, "i2c1.iclk"));
    s->i2c[1] = omap2_i2c_init(omap_l4tao(s->l4, 6),
                    s->irq[0][OMAP_INT_24XX_I2C2_IRQ],
                    &s->drq[OMAP24XX_DMA_I2C2_TX],
                    omap_findclk(s, "i2c2.fclk"),
                    omap_findclk(s, "i2c2.iclk"));

    gpio_clks[0] = omap_findclk(s, "gpio1_dbclk");
    gpio_clks[1] = omap_findclk(s, "gpio2_dbclk");
    gpio_clks[2] = omap_findclk(s, "gpio3_dbclk");
    gpio_clks[3] = omap_findclk(s, "gpio4_dbclk");
    s->gpif = omap2_gpio_init(omap_l4ta(s->l4, 3),
                    &s->irq[0][OMAP_INT_24XX_GPIO_BANK1],
                    gpio_clks, omap_findclk(s, "gpio_iclk"), 4);

    s->sdrc = omap_sdrc_init(0x68009000);
    s->gpmc = omap_gpmc_init(0x6800a000, s->irq[0][OMAP_INT_24XX_GPMC_IRQ]);

    sdindex = drive_get_index(IF_SD, 0, 0);
    if (sdindex == -1) {
        fprintf(stderr, "qemu: missing SecureDigital device\n");
        exit(1);
    }
    s->mmc = omap2_mmc_init(omap_l4tao(s->l4, 9), drives_table[sdindex].bdrv,
                    s->irq[0][OMAP_INT_24XX_MMC_IRQ],
                    &s->drq[OMAP24XX_DMA_MMC1_TX],
                    omap_findclk(s, "mmc_fclk"), omap_findclk(s, "mmc_iclk"));

    s->mcspi[0] = omap_mcspi_init(omap_l4ta(s->l4, 35), 4,
                    s->irq[0][OMAP_INT_24XX_MCSPI1_IRQ],
                    &s->drq[OMAP24XX_DMA_SPI1_TX0],
                    omap_findclk(s, "spi1_fclk"),
                    omap_findclk(s, "spi1_iclk"));
    s->mcspi[1] = omap_mcspi_init(omap_l4ta(s->l4, 36), 2,
                    s->irq[0][OMAP_INT_24XX_MCSPI2_IRQ],
                    &s->drq[OMAP24XX_DMA_SPI2_TX0],
                    omap_findclk(s, "spi2_fclk"),
                    omap_findclk(s, "spi2_iclk"));

    s->dss = omap_dss_init(omap_l4ta(s->l4, 10), 0x68000800,
                    /* XXX wire M_IRQ_25, D_L2_IRQ_30 and I_IRQ_13 together */
                    s->irq[0][OMAP_INT_24XX_DSS_IRQ], s->drq[OMAP24XX_DMA_DSS],
                    omap_findclk(s, "dss_clk1"), omap_findclk(s, "dss_clk2"),
                    omap_findclk(s, "dss_54m_clk"),
                    omap_findclk(s, "dss_l3_iclk"),
                    omap_findclk(s, "dss_l4_iclk"));

    omap_sti_init(omap_l4ta(s->l4, 18), 0x54000000,
                    s->irq[0][OMAP_INT_24XX_STI], omap_findclk(s, "emul_ck"),
                    serial_hds[0] && serial_hds[1] && serial_hds[2] ?
                    serial_hds[3] : 0);

    s->eac = omap_eac_init(omap_l4ta(s->l4, 32),
                    s->irq[0][OMAP_INT_24XX_EAC_IRQ],
                    /* Ten consecutive lines */
                    &s->drq[OMAP24XX_DMA_EAC_AC_RD],
                    omap_findclk(s, "func_96m_clk"),
                    omap_findclk(s, "core_l4_iclk"));

    /* All register mappings (includin those not currenlty implemented):
     * SystemControlMod	48000000 - 48000fff
     * SystemControlL4	48001000 - 48001fff
     * 32kHz Timer Mod	48004000 - 48004fff
     * 32kHz Timer L4	48005000 - 48005fff
     * PRCM ModA	48008000 - 480087ff
     * PRCM ModB	48008800 - 48008fff
     * PRCM L4		48009000 - 48009fff
     * TEST-BCM Mod	48012000 - 48012fff
     * TEST-BCM L4	48013000 - 48013fff
     * TEST-TAP Mod	48014000 - 48014fff
     * TEST-TAP L4	48015000 - 48015fff
     * GPIO1 Mod	48018000 - 48018fff
     * GPIO Top		48019000 - 48019fff
     * GPIO2 Mod	4801a000 - 4801afff
     * GPIO L4		4801b000 - 4801bfff
     * GPIO3 Mod	4801c000 - 4801cfff
     * GPIO4 Mod	4801e000 - 4801efff
     * WDTIMER1 Mod	48020000 - 48010fff
     * WDTIMER Top	48021000 - 48011fff
     * WDTIMER2 Mod	48022000 - 48012fff
     * WDTIMER L4	48023000 - 48013fff
     * WDTIMER3 Mod	48024000 - 48014fff
     * WDTIMER3 L4	48025000 - 48015fff
     * WDTIMER4 Mod	48026000 - 48016fff
     * WDTIMER4 L4	48027000 - 48017fff
     * GPTIMER1 Mod	48028000 - 48018fff
     * GPTIMER1 L4	48029000 - 48019fff
     * GPTIMER2 Mod	4802a000 - 4801afff
     * GPTIMER2 L4	4802b000 - 4801bfff
     * L4-Config AP	48040000 - 480407ff
     * L4-Config IP	48040800 - 48040fff
     * L4-Config LA	48041000 - 48041fff
     * ARM11ETB Mod	48048000 - 48049fff
     * ARM11ETB L4	4804a000 - 4804afff
     * DISPLAY Top	48050000 - 480503ff
     * DISPLAY DISPC	48050400 - 480507ff
     * DISPLAY RFBI	48050800 - 48050bff
     * DISPLAY VENC	48050c00 - 48050fff
     * DISPLAY L4	48051000 - 48051fff
     * CAMERA Top	48052000 - 480523ff
     * CAMERA core	48052400 - 480527ff
     * CAMERA DMA	48052800 - 48052bff
     * CAMERA MMU	48052c00 - 48052fff
     * CAMERA L4	48053000 - 48053fff
     * SDMA Mod		48056000 - 48056fff
     * SDMA L4		48057000 - 48057fff
     * SSI Top		48058000 - 48058fff
     * SSI GDD		48059000 - 48059fff
     * SSI Port1	4805a000 - 4805afff
     * SSI Port2	4805b000 - 4805bfff
     * SSI L4		4805c000 - 4805cfff
     * USB Mod		4805e000 - 480fefff
     * USB L4		4805f000 - 480fffff
     * WIN_TRACER1 Mod	48060000 - 48060fff
     * WIN_TRACER1 L4	48061000 - 48061fff
     * WIN_TRACER2 Mod	48062000 - 48062fff
     * WIN_TRACER2 L4	48063000 - 48063fff
     * WIN_TRACER3 Mod	48064000 - 48064fff
     * WIN_TRACER3 L4	48065000 - 48065fff
     * WIN_TRACER4 Top	48066000 - 480660ff
     * WIN_TRACER4 ETT	48066100 - 480661ff
     * WIN_TRACER4 WT	48066200 - 480662ff
     * WIN_TRACER4 L4	48067000 - 48067fff
     * XTI Mod		48068000 - 48068fff
     * XTI L4		48069000 - 48069fff
     * UART1 Mod	4806a000 - 4806afff
     * UART1 L4		4806b000 - 4806bfff
     * UART2 Mod	4806c000 - 4806cfff
     * UART2 L4		4806d000 - 4806dfff
     * UART3 Mod	4806e000 - 4806efff
     * UART3 L4		4806f000 - 4806ffff
     * I2C1 Mod		48070000 - 48070fff
     * I2C1 L4		48071000 - 48071fff
     * I2C2 Mod		48072000 - 48072fff
     * I2C2 L4		48073000 - 48073fff
     * McBSP1 Mod	48074000 - 48074fff
     * McBSP1 L4	48075000 - 48075fff
     * McBSP2 Mod	48076000 - 48076fff
     * McBSP2 L4	48077000 - 48077fff
     * GPTIMER3 Mod	48078000 - 48078fff
     * GPTIMER3 L4	48079000 - 48079fff
     * GPTIMER4 Mod	4807a000 - 4807afff
     * GPTIMER4 L4	4807b000 - 4807bfff
     * GPTIMER5 Mod	4807c000 - 4807cfff
     * GPTIMER5 L4	4807d000 - 4807dfff
     * GPTIMER6 Mod	4807e000 - 4807efff
     * GPTIMER6 L4	4807f000 - 4807ffff
     * GPTIMER7 Mod	48080000 - 48080fff
     * GPTIMER7 L4	48081000 - 48081fff
     * GPTIMER8 Mod	48082000 - 48082fff
     * GPTIMER8 L4	48083000 - 48083fff
     * GPTIMER9 Mod	48084000 - 48084fff
     * GPTIMER9 L4	48085000 - 48085fff
     * GPTIMER10 Mod	48086000 - 48086fff
     * GPTIMER10 L4	48087000 - 48087fff
     * GPTIMER11 Mod	48088000 - 48088fff
     * GPTIMER11 L4	48089000 - 48089fff
     * GPTIMER12 Mod	4808a000 - 4808afff
     * GPTIMER12 L4	4808b000 - 4808bfff
     * EAC Mod		48090000 - 48090fff
     * EAC L4		48091000 - 48091fff
     * FAC Mod		48092000 - 48092fff
     * FAC L4		48093000 - 48093fff
     * MAILBOX Mod	48094000 - 48094fff
     * MAILBOX L4	48095000 - 48095fff
     * SPI1 Mod		48098000 - 48098fff
     * SPI1 L4		48099000 - 48099fff
     * SPI2 Mod		4809a000 - 4809afff
     * SPI2 L4		4809b000 - 4809bfff
     * MMC/SDIO Mod	4809c000 - 4809cfff
     * MMC/SDIO L4	4809d000 - 4809dfff
     * MS_PRO Mod	4809e000 - 4809efff
     * MS_PRO L4	4809f000 - 4809ffff
     * RNG Mod		480a0000 - 480a0fff
     * RNG L4		480a1000 - 480a1fff
     * DES3DES Mod	480a2000 - 480a2fff
     * DES3DES L4	480a3000 - 480a3fff
     * SHA1MD5 Mod	480a4000 - 480a4fff
     * SHA1MD5 L4	480a5000 - 480a5fff
     * AES Mod		480a6000 - 480a6fff
     * AES L4		480a7000 - 480a7fff
     * PKA Mod		480a8000 - 480a9fff
     * PKA L4		480aa000 - 480aafff
     * MG Mod		480b0000 - 480b0fff
     * MG L4		480b1000 - 480b1fff
     * HDQ/1-wire Mod	480b2000 - 480b2fff
     * HDQ/1-wire L4	480b3000 - 480b3fff
     * MPU interrupt	480fe000 - 480fefff
     * STI channel base	54000000 - 5400ffff
     * IVA RAM		5c000000 - 5c01ffff
     * IVA ROM		5c020000 - 5c027fff
     * IMG_BUF_A	5c040000 - 5c040fff
     * IMG_BUF_B	5c042000 - 5c042fff
     * VLCDS		5c048000 - 5c0487ff
     * IMX_COEF		5c049000 - 5c04afff
     * IMX_CMD		5c051000 - 5c051fff
     * VLCDQ		5c053000 - 5c0533ff
     * VLCDH		5c054000 - 5c054fff
     * SEQ_CMD		5c055000 - 5c055fff
     * IMX_REG		5c056000 - 5c0560ff
     * VLCD_REG		5c056100 - 5c0561ff
     * SEQ_REG		5c056200 - 5c0562ff
     * IMG_BUF_REG	5c056300 - 5c0563ff
     * SEQIRQ_REG	5c056400 - 5c0564ff
     * OCP_REG		5c060000 - 5c060fff
     * SYSC_REG		5c070000 - 5c070fff
     * MMU_REG		5d000000 - 5d000fff
     * sDMA R		68000400 - 680005ff
     * sDMA W		68000600 - 680007ff
     * Display Control	68000800 - 680009ff
     * DSP subsystem	68000a00 - 68000bff
     * MPU subsystem	68000c00 - 68000dff
     * IVA subsystem	68001000 - 680011ff
     * USB		68001200 - 680013ff
     * Camera		68001400 - 680015ff
     * VLYNQ (firewall)	68001800 - 68001bff
     * VLYNQ		68001e00 - 68001fff
     * SSI		68002000 - 680021ff
     * L4		68002400 - 680025ff
     * DSP (firewall)	68002800 - 68002bff
     * DSP subsystem	68002e00 - 68002fff
     * IVA (firewall)	68003000 - 680033ff
     * IVA		68003600 - 680037ff
     * GFX		68003a00 - 68003bff
     * CMDWR emulation	68003c00 - 68003dff
     * SMS		68004000 - 680041ff
     * OCM		68004200 - 680043ff
     * GPMC		68004400 - 680045ff
     * RAM (firewall)	68005000 - 680053ff
     * RAM (err login)	68005400 - 680057ff
     * ROM (firewall)	68005800 - 68005bff
     * ROM (err login)	68005c00 - 68005fff
     * GPMC (firewall)	68006000 - 680063ff
     * GPMC (err login)	68006400 - 680067ff
     * SMS (err login)	68006c00 - 68006fff
     * SMS registers	68008000 - 68008fff
     * SDRC registers	68009000 - 68009fff
     * GPMC registers	6800a000   6800afff
     */

    qemu_register_reset(omap2_mpu_reset, s);

    return s;
}
