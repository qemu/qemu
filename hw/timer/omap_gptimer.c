/*
 * TI OMAP2 general purpose timers emulation.
 *
 * Copyright (C) 2007-2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/arm/omap.h"

/* GP timers */
struct omap_gp_timer_s {
    MemoryRegion iomem;
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
        distance = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->time;
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
        timer->time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
}

static inline void omap_gp_timer_update(struct omap_gp_timer_s *timer)
{
    int64_t expires, matches;

    if (timer->st && timer->rate) {
        expires = muldiv64(0x100000000ll - timer->val,
                        timer->ticks_per_sec, timer->rate);
        timer_mod(timer->timer, timer->time + expires);

        if (timer->ce && timer->match_val >= timer->val) {
            matches = muldiv64(timer->ticks_per_sec,
                               timer->match_val - timer->val, timer->rate);
            timer_mod(timer->match, timer->time + matches);
        } else
            timer_del(timer->match);
    } else {
        timer_del(timer->timer);
        timer_del(timer->match);
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
        timer->time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
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
                     qemu_allocate_irq(omap_gp_timer_clk_update, timer, 0));
    timer->rate = omap_clk_getrate(timer->clk);
}

void omap_gp_timer_reset(struct omap_gp_timer_s *s)
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

static uint32_t omap_gp_timer_readw(void *opaque, hwaddr addr)
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

static uint32_t omap_gp_timer_readh(void *opaque, hwaddr addr)
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

static void omap_gp_timer_write(void *opaque, hwaddr addr,
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
                            __func__);
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
                            "for this trigger mode\n", __func__);
        if (!s->inout && s->capture != gpt_capture_none)
            fprintf(stderr, "%s: GP timer pin must be an input "
                            "for this capture mode\n", __func__);
        if (s->trigger == gpt_trigger_none)
            omap_gp_timer_out(s, s->scpwm);
        /* TODO: make sure this doesn't overflow 32-bits */
        s->ticks_per_sec = NANOSECONDS_PER_SECOND << (s->pre ? s->ptv + 1 : 0);
        omap_gp_timer_update(s);
        break;

    case 0x28:	/* TCRR */
        s->time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->val = value;
        omap_gp_timer_update(s);
        break;

    case 0x2c:	/* TLDR */
        s->load_val = value;
        break;

    case 0x30:	/* TTGR */
        s->time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
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

static void omap_gp_timer_writeh(void *opaque, hwaddr addr,
                uint32_t value)
{
    struct omap_gp_timer_s *s = (struct omap_gp_timer_s *) opaque;

    if (addr & 2)
        omap_gp_timer_write(opaque, addr, (value << 16) | s->writeh);
    else
        s->writeh = (uint16_t) value;
}

static uint64_t omap_gp_timer_readfn(void *opaque, hwaddr addr,
                                     unsigned size)
{
    switch (size) {
    case 1:
        return omap_badwidth_read32(opaque, addr);
    case 2:
        return omap_gp_timer_readh(opaque, addr);
    case 4:
        return omap_gp_timer_readw(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void omap_gp_timer_writefn(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        omap_badwidth_write32(opaque, addr, value);
        break;
    case 2:
        omap_gp_timer_writeh(opaque, addr, value);
        break;
    case 4:
        omap_gp_timer_write(opaque, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps omap_gp_timer_ops = {
    .read = omap_gp_timer_readfn,
    .write = omap_gp_timer_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_gp_timer_s *omap_gp_timer_init(struct omap_target_agent_s *ta,
                qemu_irq irq, omap_clk fclk, omap_clk iclk)
{
    struct omap_gp_timer_s *s = g_new0(struct omap_gp_timer_s, 1);

    s->ta = ta;
    s->irq = irq;
    s->clk = fclk;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_gp_timer_tick, s);
    s->match = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_gp_timer_match, s);
    s->in = qemu_allocate_irq(omap_gp_timer_input, s, 0);
    omap_gp_timer_reset(s);
    omap_gp_timer_clk_setup(s);

    memory_region_init_io(&s->iomem, NULL, &omap_gp_timer_ops, s, "omap.gptimer",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    return s;
}
