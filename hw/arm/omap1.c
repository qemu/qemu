/*
 * TI OMAP processors emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "hw/arm/omap.h"
#include "sysemu/sysemu.h"
#include "hw/arm/soc_dma.h"
#include "sysemu/blockdev.h"
#include "qemu/range.h"
#include "hw/sysbus.h"

/* Should signal the TCMI/GPMC */
uint32_t omap_badwidth_read8(void *opaque, hwaddr addr)
{
    uint8_t ret;

    OMAP_8B_REG(addr);
    cpu_physical_memory_read(addr, &ret, 1);
    return ret;
}

void omap_badwidth_write8(void *opaque, hwaddr addr,
                uint32_t value)
{
    uint8_t val8 = value;

    OMAP_8B_REG(addr);
    cpu_physical_memory_write(addr, &val8, 1);
}

uint32_t omap_badwidth_read16(void *opaque, hwaddr addr)
{
    uint16_t ret;

    OMAP_16B_REG(addr);
    cpu_physical_memory_read(addr, &ret, 2);
    return ret;
}

void omap_badwidth_write16(void *opaque, hwaddr addr,
                uint32_t value)
{
    uint16_t val16 = value;

    OMAP_16B_REG(addr);
    cpu_physical_memory_write(addr, &val16, 2);
}

uint32_t omap_badwidth_read32(void *opaque, hwaddr addr)
{
    uint32_t ret;

    OMAP_32B_REG(addr);
    cpu_physical_memory_read(addr, &ret, 4);
    return ret;
}

void omap_badwidth_write32(void *opaque, hwaddr addr,
                uint32_t value)
{
    OMAP_32B_REG(addr);
    cpu_physical_memory_write(addr, &value, 4);
}

/* MPU OS timers */
struct omap_mpu_timer_s {
    MemoryRegion iomem;
    qemu_irq irq;
    omap_clk clk;
    uint32_t val;
    int64_t time;
    QEMUTimer *timer;
    QEMUBH *tick;
    int64_t rate;
    int it_ena;

    int enable;
    int ptv;
    int ar;
    int st;
    uint32_t reset_val;
};

static inline uint32_t omap_timer_read(struct omap_mpu_timer_s *timer)
{
    uint64_t distance = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->time;

    if (timer->st && timer->enable && timer->rate)
        return timer->val - muldiv64(distance >> (timer->ptv + 1),
                                     timer->rate, get_ticks_per_sec());
    else
        return timer->val;
}

static inline void omap_timer_sync(struct omap_mpu_timer_s *timer)
{
    timer->val = omap_timer_read(timer);
    timer->time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static inline void omap_timer_update(struct omap_mpu_timer_s *timer)
{
    int64_t expires;

    if (timer->enable && timer->st && timer->rate) {
        timer->val = timer->reset_val;	/* Should skip this on clk enable */
        expires = muldiv64((uint64_t) timer->val << (timer->ptv + 1),
                           get_ticks_per_sec(), timer->rate);

        /* If timer expiry would be sooner than in about 1 ms and
         * auto-reload isn't set, then fire immediately.  This is a hack
         * to make systems like PalmOS run in acceptable time.  PalmOS
         * sets the interval to a very low value and polls the status bit
         * in a busy loop when it wants to sleep just a couple of CPU
         * ticks.  */
        if (expires > (get_ticks_per_sec() >> 10) || timer->ar)
            timer_mod(timer->timer, timer->time + expires);
        else
            qemu_bh_schedule(timer->tick);
    } else
        timer_del(timer->timer);
}

static void omap_timer_fire(void *opaque)
{
    struct omap_mpu_timer_s *timer = opaque;

    if (!timer->ar) {
        timer->val = 0;
        timer->st = 0;
    }

    if (timer->it_ena)
        /* Edge-triggered irq */
        qemu_irq_pulse(timer->irq);
}

static void omap_timer_tick(void *opaque)
{
    struct omap_mpu_timer_s *timer = (struct omap_mpu_timer_s *) opaque;

    omap_timer_sync(timer);
    omap_timer_fire(timer);
    omap_timer_update(timer);
}

static void omap_timer_clk_update(void *opaque, int line, int on)
{
    struct omap_mpu_timer_s *timer = (struct omap_mpu_timer_s *) opaque;

    omap_timer_sync(timer);
    timer->rate = on ? omap_clk_getrate(timer->clk) : 0;
    omap_timer_update(timer);
}

static void omap_timer_clk_setup(struct omap_mpu_timer_s *timer)
{
    omap_clk_adduser(timer->clk,
                    qemu_allocate_irq(omap_timer_clk_update, timer, 0));
    timer->rate = omap_clk_getrate(timer->clk);
}

static uint64_t omap_mpu_timer_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *) opaque;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* CNTL_TIMER */
        return (s->enable << 5) | (s->ptv << 2) | (s->ar << 1) | s->st;

    case 0x04:	/* LOAD_TIM */
        break;

    case 0x08:	/* READ_TIM */
        return omap_timer_read(s);
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpu_timer_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *) opaque;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* CNTL_TIMER */
        omap_timer_sync(s);
        s->enable = (value >> 5) & 1;
        s->ptv = (value >> 2) & 7;
        s->ar = (value >> 1) & 1;
        s->st = value & 1;
        omap_timer_update(s);
        return;

    case 0x04:	/* LOAD_TIM */
        s->reset_val = value;
        return;

    case 0x08:	/* READ_TIM */
        OMAP_RO_REG(addr);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_mpu_timer_ops = {
    .read = omap_mpu_timer_read,
    .write = omap_mpu_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void omap_mpu_timer_reset(struct omap_mpu_timer_s *s)
{
    timer_del(s->timer);
    s->enable = 0;
    s->reset_val = 31337;
    s->val = 0;
    s->ptv = 0;
    s->ar = 0;
    s->st = 0;
    s->it_ena = 1;
}

static struct omap_mpu_timer_s *omap_mpu_timer_init(MemoryRegion *system_memory,
                hwaddr base,
                qemu_irq irq, omap_clk clk)
{
    struct omap_mpu_timer_s *s = (struct omap_mpu_timer_s *)
            g_malloc0(sizeof(struct omap_mpu_timer_s));

    s->irq = irq;
    s->clk = clk;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_timer_tick, s);
    s->tick = qemu_bh_new(omap_timer_fire, s);
    omap_mpu_timer_reset(s);
    omap_timer_clk_setup(s);

    memory_region_init_io(&s->iomem, NULL, &omap_mpu_timer_ops, s,
                          "omap-mpu-timer", 0x100);

    memory_region_add_subregion(system_memory, base, &s->iomem);

    return s;
}

/* Watchdog timer */
struct omap_watchdog_timer_s {
    struct omap_mpu_timer_s timer;
    MemoryRegion iomem;
    uint8_t last_wr;
    int mode;
    int free;
    int reset;
};

static uint64_t omap_wd_timer_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *) opaque;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* CNTL_TIMER */
        return (s->timer.ptv << 9) | (s->timer.ar << 8) |
                (s->timer.st << 7) | (s->free << 1);

    case 0x04:	/* READ_TIMER */
        return omap_timer_read(&s->timer);

    case 0x08:	/* TIMER_MODE */
        return s->mode << 15;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_wd_timer_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *) opaque;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* CNTL_TIMER */
        omap_timer_sync(&s->timer);
        s->timer.ptv = (value >> 9) & 7;
        s->timer.ar = (value >> 8) & 1;
        s->timer.st = (value >> 7) & 1;
        s->free = (value >> 1) & 1;
        omap_timer_update(&s->timer);
        break;

    case 0x04:	/* LOAD_TIMER */
        s->timer.reset_val = value & 0xffff;
        break;

    case 0x08:	/* TIMER_MODE */
        if (!s->mode && ((value >> 15) & 1))
            omap_clk_get(s->timer.clk);
        s->mode |= (value >> 15) & 1;
        if (s->last_wr == 0xf5) {
            if ((value & 0xff) == 0xa0) {
                if (s->mode) {
                    s->mode = 0;
                    omap_clk_put(s->timer.clk);
                }
            } else {
                /* XXX: on T|E hardware somehow this has no effect,
                 * on Zire 71 it works as specified.  */
                s->reset = 1;
                qemu_system_reset_request();
            }
        }
        s->last_wr = value & 0xff;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_wd_timer_ops = {
    .read = omap_wd_timer_read,
    .write = omap_wd_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_wd_timer_reset(struct omap_watchdog_timer_s *s)
{
    timer_del(s->timer.timer);
    if (!s->mode)
        omap_clk_get(s->timer.clk);
    s->mode = 1;
    s->free = 1;
    s->reset = 0;
    s->timer.enable = 1;
    s->timer.it_ena = 1;
    s->timer.reset_val = 0xffff;
    s->timer.val = 0;
    s->timer.st = 0;
    s->timer.ptv = 0;
    s->timer.ar = 0;
    omap_timer_update(&s->timer);
}

static struct omap_watchdog_timer_s *omap_wd_timer_init(MemoryRegion *memory,
                hwaddr base,
                qemu_irq irq, omap_clk clk)
{
    struct omap_watchdog_timer_s *s = (struct omap_watchdog_timer_s *)
            g_malloc0(sizeof(struct omap_watchdog_timer_s));

    s->timer.irq = irq;
    s->timer.clk = clk;
    s->timer.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_timer_tick, &s->timer);
    omap_wd_timer_reset(s);
    omap_timer_clk_setup(&s->timer);

    memory_region_init_io(&s->iomem, NULL, &omap_wd_timer_ops, s,
                          "omap-wd-timer", 0x100);
    memory_region_add_subregion(memory, base, &s->iomem);

    return s;
}

/* 32-kHz timer */
struct omap_32khz_timer_s {
    struct omap_mpu_timer_s timer;
    MemoryRegion iomem;
};

static uint64_t omap_os_timer_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* TVR */
        return s->timer.reset_val;

    case 0x04:	/* TCR */
        return omap_timer_read(&s->timer);

    case 0x08:	/* CR */
        return (s->timer.ar << 3) | (s->timer.it_ena << 2) | s->timer.st;

    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_os_timer_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* TVR */
        s->timer.reset_val = value & 0x00ffffff;
        break;

    case 0x04:	/* TCR */
        OMAP_RO_REG(addr);
        break;

    case 0x08:	/* CR */
        s->timer.ar = (value >> 3) & 1;
        s->timer.it_ena = (value >> 2) & 1;
        if (s->timer.st != (value & 1) || (value & 2)) {
            omap_timer_sync(&s->timer);
            s->timer.enable = value & 1;
            s->timer.st = value & 1;
            omap_timer_update(&s->timer);
        }
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_os_timer_ops = {
    .read = omap_os_timer_read,
    .write = omap_os_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_os_timer_reset(struct omap_32khz_timer_s *s)
{
    timer_del(s->timer.timer);
    s->timer.enable = 0;
    s->timer.it_ena = 0;
    s->timer.reset_val = 0x00ffffff;
    s->timer.val = 0;
    s->timer.st = 0;
    s->timer.ptv = 0;
    s->timer.ar = 1;
}

static struct omap_32khz_timer_s *omap_os_timer_init(MemoryRegion *memory,
                hwaddr base,
                qemu_irq irq, omap_clk clk)
{
    struct omap_32khz_timer_s *s = (struct omap_32khz_timer_s *)
            g_malloc0(sizeof(struct omap_32khz_timer_s));

    s->timer.irq = irq;
    s->timer.clk = clk;
    s->timer.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_timer_tick, &s->timer);
    omap_os_timer_reset(s);
    omap_timer_clk_setup(&s->timer);

    memory_region_init_io(&s->iomem, NULL, &omap_os_timer_ops, s,
                          "omap-os-timer", 0x800);
    memory_region_add_subregion(memory, base, &s->iomem);

    return s;
}

/* Ultra Low-Power Device Module */
static uint64_t omap_ulpd_pm_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    uint16_t ret;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x14:	/* IT_STATUS */
        ret = s->ulpd_pm_regs[addr >> 2];
        s->ulpd_pm_regs[addr >> 2] = 0;
        qemu_irq_lower(qdev_get_gpio_in(s->ih[1], OMAP_INT_GAUGE_32K));
        return ret;

    case 0x18:	/* Reserved */
    case 0x1c:	/* Reserved */
    case 0x20:	/* Reserved */
    case 0x28:	/* Reserved */
    case 0x2c:	/* Reserved */
        OMAP_BAD_REG(addr);
        /* fall through */
    case 0x00:	/* COUNTER_32_LSB */
    case 0x04:	/* COUNTER_32_MSB */
    case 0x08:	/* COUNTER_HIGH_FREQ_LSB */
    case 0x0c:	/* COUNTER_HIGH_FREQ_MSB */
    case 0x10:	/* GAUGING_CTRL */
    case 0x24:	/* SETUP_ANALOG_CELL3_ULPD1 */
    case 0x30:	/* CLOCK_CTRL */
    case 0x34:	/* SOFT_REQ */
    case 0x38:	/* COUNTER_32_FIQ */
    case 0x3c:	/* DPLL_CTRL */
    case 0x40:	/* STATUS_REQ */
        /* XXX: check clk::usecount state for every clock */
    case 0x48:	/* LOCL_TIME */
    case 0x4c:	/* APLL_CTRL */
    case 0x50:	/* POWER_CTRL */
        return s->ulpd_pm_regs[addr >> 2];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_ulpd_clk_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    if (diff & (1 << 4))				/* USB_MCLK_EN */
        omap_clk_onoff(omap_findclk(s, "usb_clk0"), (value >> 4) & 1);
    if (diff & (1 << 5))				/* DIS_USB_PVCI_CLK */
        omap_clk_onoff(omap_findclk(s, "usb_w2fc_ck"), (~value >> 5) & 1);
}

static inline void omap_ulpd_req_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    if (diff & (1 << 0))				/* SOFT_DPLL_REQ */
        omap_clk_canidle(omap_findclk(s, "dpll4"), (~value >> 0) & 1);
    if (diff & (1 << 1))				/* SOFT_COM_REQ */
        omap_clk_canidle(omap_findclk(s, "com_mclk_out"), (~value >> 1) & 1);
    if (diff & (1 << 2))				/* SOFT_SDW_REQ */
        omap_clk_canidle(omap_findclk(s, "bt_mclk_out"), (~value >> 2) & 1);
    if (diff & (1 << 3))				/* SOFT_USB_REQ */
        omap_clk_canidle(omap_findclk(s, "usb_clk0"), (~value >> 3) & 1);
}

static void omap_ulpd_pm_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    int64_t now, ticks;
    int div, mult;
    static const int bypass_div[4] = { 1, 2, 4, 4 };
    uint16_t diff;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* COUNTER_32_LSB */
    case 0x04:	/* COUNTER_32_MSB */
    case 0x08:	/* COUNTER_HIGH_FREQ_LSB */
    case 0x0c:	/* COUNTER_HIGH_FREQ_MSB */
    case 0x14:	/* IT_STATUS */
    case 0x40:	/* STATUS_REQ */
        OMAP_RO_REG(addr);
        break;

    case 0x10:	/* GAUGING_CTRL */
        /* Bits 0 and 1 seem to be confused in the OMAP 310 TRM */
        if ((s->ulpd_pm_regs[addr >> 2] ^ value) & 1) {
            now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            if (value & 1)
                s->ulpd_gauge_start = now;
            else {
                now -= s->ulpd_gauge_start;

                /* 32-kHz ticks */
                ticks = muldiv64(now, 32768, get_ticks_per_sec());
                s->ulpd_pm_regs[0x00 >> 2] = (ticks >>  0) & 0xffff;
                s->ulpd_pm_regs[0x04 >> 2] = (ticks >> 16) & 0xffff;
                if (ticks >> 32)	/* OVERFLOW_32K */
                    s->ulpd_pm_regs[0x14 >> 2] |= 1 << 2;

                /* High frequency ticks */
                ticks = muldiv64(now, 12000000, get_ticks_per_sec());
                s->ulpd_pm_regs[0x08 >> 2] = (ticks >>  0) & 0xffff;
                s->ulpd_pm_regs[0x0c >> 2] = (ticks >> 16) & 0xffff;
                if (ticks >> 32)	/* OVERFLOW_HI_FREQ */
                    s->ulpd_pm_regs[0x14 >> 2] |= 1 << 1;

                s->ulpd_pm_regs[0x14 >> 2] |= 1 << 0;	/* IT_GAUGING */
                qemu_irq_raise(qdev_get_gpio_in(s->ih[1], OMAP_INT_GAUGE_32K));
            }
        }
        s->ulpd_pm_regs[addr >> 2] = value;
        break;

    case 0x18:	/* Reserved */
    case 0x1c:	/* Reserved */
    case 0x20:	/* Reserved */
    case 0x28:	/* Reserved */
    case 0x2c:	/* Reserved */
        OMAP_BAD_REG(addr);
        /* fall through */
    case 0x24:	/* SETUP_ANALOG_CELL3_ULPD1 */
    case 0x38:	/* COUNTER_32_FIQ */
    case 0x48:	/* LOCL_TIME */
    case 0x50:	/* POWER_CTRL */
        s->ulpd_pm_regs[addr >> 2] = value;
        break;

    case 0x30:	/* CLOCK_CTRL */
        diff = s->ulpd_pm_regs[addr >> 2] ^ value;
        s->ulpd_pm_regs[addr >> 2] = value & 0x3f;
        omap_ulpd_clk_update(s, diff, value);
        break;

    case 0x34:	/* SOFT_REQ */
        diff = s->ulpd_pm_regs[addr >> 2] ^ value;
        s->ulpd_pm_regs[addr >> 2] = value & 0x1f;
        omap_ulpd_req_update(s, diff, value);
        break;

    case 0x3c:	/* DPLL_CTRL */
        /* XXX: OMAP310 TRM claims bit 3 is PLL_ENABLE, and bit 4 is
         * omitted altogether, probably a typo.  */
        /* This register has identical semantics with DPLL(1:3) control
         * registers, see omap_dpll_write() */
        diff = s->ulpd_pm_regs[addr >> 2] & value;
        s->ulpd_pm_regs[addr >> 2] = value & 0x2fff;
        if (diff & (0x3ff << 2)) {
            if (value & (1 << 4)) {			/* PLL_ENABLE */
                div = ((value >> 5) & 3) + 1;		/* PLL_DIV */
                mult = MIN((value >> 7) & 0x1f, 1);	/* PLL_MULT */
            } else {
                div = bypass_div[((value >> 2) & 3)];	/* BYPASS_DIV */
                mult = 1;
            }
            omap_clk_setrate(omap_findclk(s, "dpll4"), div, mult);
        }

        /* Enter the desired mode.  */
        s->ulpd_pm_regs[addr >> 2] =
                (s->ulpd_pm_regs[addr >> 2] & 0xfffe) |
                ((s->ulpd_pm_regs[addr >> 2] >> 4) & 1);

        /* Act as if the lock is restored.  */
        s->ulpd_pm_regs[addr >> 2] |= 2;
        break;

    case 0x4c:	/* APLL_CTRL */
        diff = s->ulpd_pm_regs[addr >> 2] & value;
        s->ulpd_pm_regs[addr >> 2] = value & 0xf;
        if (diff & (1 << 0))				/* APLL_NDPLL_SWITCH */
            omap_clk_reparent(omap_findclk(s, "ck_48m"), omap_findclk(s,
                                    (value & (1 << 0)) ? "apll" : "dpll4"));
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_ulpd_pm_ops = {
    .read = omap_ulpd_pm_read,
    .write = omap_ulpd_pm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_ulpd_pm_reset(struct omap_mpu_state_s *mpu)
{
    mpu->ulpd_pm_regs[0x00 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x04 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x08 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x0c >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x10 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x18 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x1c >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x20 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x24 >> 2] = 0x03ff;
    mpu->ulpd_pm_regs[0x28 >> 2] = 0x01;
    mpu->ulpd_pm_regs[0x2c >> 2] = 0x01;
    omap_ulpd_clk_update(mpu, mpu->ulpd_pm_regs[0x30 >> 2], 0x0000);
    mpu->ulpd_pm_regs[0x30 >> 2] = 0x0000;
    omap_ulpd_req_update(mpu, mpu->ulpd_pm_regs[0x34 >> 2], 0x0000);
    mpu->ulpd_pm_regs[0x34 >> 2] = 0x0000;
    mpu->ulpd_pm_regs[0x38 >> 2] = 0x0001;
    mpu->ulpd_pm_regs[0x3c >> 2] = 0x2211;
    mpu->ulpd_pm_regs[0x40 >> 2] = 0x0000; /* FIXME: dump a real STATUS_REQ */
    mpu->ulpd_pm_regs[0x48 >> 2] = 0x960;
    mpu->ulpd_pm_regs[0x4c >> 2] = 0x08;
    mpu->ulpd_pm_regs[0x50 >> 2] = 0x08;
    omap_clk_setrate(omap_findclk(mpu, "dpll4"), 1, 4);
    omap_clk_reparent(omap_findclk(mpu, "ck_48m"), omap_findclk(mpu, "dpll4"));
}

static void omap_ulpd_pm_init(MemoryRegion *system_memory,
                hwaddr base,
                struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->ulpd_pm_iomem, NULL, &omap_ulpd_pm_ops, mpu,
                          "omap-ulpd-pm", 0x800);
    memory_region_add_subregion(system_memory, base, &mpu->ulpd_pm_iomem);
    omap_ulpd_pm_reset(mpu);
}

/* OMAP Pin Configuration */
static uint64_t omap_pin_cfg_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* FUNC_MUX_CTRL_0 */
    case 0x04:	/* FUNC_MUX_CTRL_1 */
    case 0x08:	/* FUNC_MUX_CTRL_2 */
        return s->func_mux_ctrl[addr >> 2];

    case 0x0c:	/* COMP_MODE_CTRL_0 */
        return s->comp_mode_ctrl[0];

    case 0x10:	/* FUNC_MUX_CTRL_3 */
    case 0x14:	/* FUNC_MUX_CTRL_4 */
    case 0x18:	/* FUNC_MUX_CTRL_5 */
    case 0x1c:	/* FUNC_MUX_CTRL_6 */
    case 0x20:	/* FUNC_MUX_CTRL_7 */
    case 0x24:	/* FUNC_MUX_CTRL_8 */
    case 0x28:	/* FUNC_MUX_CTRL_9 */
    case 0x2c:	/* FUNC_MUX_CTRL_A */
    case 0x30:	/* FUNC_MUX_CTRL_B */
    case 0x34:	/* FUNC_MUX_CTRL_C */
    case 0x38:	/* FUNC_MUX_CTRL_D */
        return s->func_mux_ctrl[(addr >> 2) - 1];

    case 0x40:	/* PULL_DWN_CTRL_0 */
    case 0x44:	/* PULL_DWN_CTRL_1 */
    case 0x48:	/* PULL_DWN_CTRL_2 */
    case 0x4c:	/* PULL_DWN_CTRL_3 */
        return s->pull_dwn_ctrl[(addr & 0xf) >> 2];

    case 0x50:	/* GATE_INH_CTRL_0 */
        return s->gate_inh_ctrl[0];

    case 0x60:	/* VOLTAGE_CTRL_0 */
        return s->voltage_ctrl[0];

    case 0x70:	/* TEST_DBG_CTRL_0 */
        return s->test_dbg_ctrl[0];

    case 0x80:	/* MOD_CONF_CTRL_0 */
        return s->mod_conf_ctrl[0];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_pin_funcmux0_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (s->compat1509) {
        if (diff & (1 << 9))			/* BLUETOOTH */
            omap_clk_onoff(omap_findclk(s, "bt_mclk_out"),
                            (~value >> 9) & 1);
        if (diff & (1 << 7))			/* USB.CLKO */
            omap_clk_onoff(omap_findclk(s, "usb.clko"),
                            (value >> 7) & 1);
    }
}

static inline void omap_pin_funcmux1_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (s->compat1509) {
        if (diff & (1U << 31)) {
            /* MCBSP3_CLK_HIZ_DI */
            omap_clk_onoff(omap_findclk(s, "mcbsp3.clkx"), (value >> 31) & 1);
        }
        if (diff & (1 << 1)) {
            /* CLK32K */
            omap_clk_onoff(omap_findclk(s, "clk32k_out"), (~value >> 1) & 1);
        }
    }
}

static inline void omap_pin_modconf1_update(struct omap_mpu_state_s *s,
                uint32_t diff, uint32_t value)
{
    if (diff & (1U << 31)) {
        /* CONF_MOD_UART3_CLK_MODE_R */
        omap_clk_reparent(omap_findclk(s, "uart3_ck"),
                          omap_findclk(s, ((value >> 31) & 1) ?
                                       "ck_48m" : "armper_ck"));
    }
    if (diff & (1 << 30))			/* CONF_MOD_UART2_CLK_MODE_R */
         omap_clk_reparent(omap_findclk(s, "uart2_ck"),
                         omap_findclk(s, ((value >> 30) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 29))			/* CONF_MOD_UART1_CLK_MODE_R */
         omap_clk_reparent(omap_findclk(s, "uart1_ck"),
                         omap_findclk(s, ((value >> 29) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 23))			/* CONF_MOD_MMC_SD_CLK_REQ_R */
         omap_clk_reparent(omap_findclk(s, "mmc_ck"),
                         omap_findclk(s, ((value >> 23) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 12))			/* CONF_MOD_COM_MCLK_12_48_S */
         omap_clk_reparent(omap_findclk(s, "com_mclk_out"),
                         omap_findclk(s, ((value >> 12) & 1) ?
                                 "ck_48m" : "armper_ck"));
    if (diff & (1 << 9))			/* CONF_MOD_USB_HOST_HHC_UHO */
         omap_clk_onoff(omap_findclk(s, "usb_hhc_ck"), (value >> 9) & 1);
}

static void omap_pin_cfg_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    uint32_t diff;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* FUNC_MUX_CTRL_0 */
        diff = s->func_mux_ctrl[addr >> 2] ^ value;
        s->func_mux_ctrl[addr >> 2] = value;
        omap_pin_funcmux0_update(s, diff, value);
        return;

    case 0x04:	/* FUNC_MUX_CTRL_1 */
        diff = s->func_mux_ctrl[addr >> 2] ^ value;
        s->func_mux_ctrl[addr >> 2] = value;
        omap_pin_funcmux1_update(s, diff, value);
        return;

    case 0x08:	/* FUNC_MUX_CTRL_2 */
        s->func_mux_ctrl[addr >> 2] = value;
        return;

    case 0x0c:	/* COMP_MODE_CTRL_0 */
        s->comp_mode_ctrl[0] = value;
        s->compat1509 = (value != 0x0000eaef);
        omap_pin_funcmux0_update(s, ~0, s->func_mux_ctrl[0]);
        omap_pin_funcmux1_update(s, ~0, s->func_mux_ctrl[1]);
        return;

    case 0x10:	/* FUNC_MUX_CTRL_3 */
    case 0x14:	/* FUNC_MUX_CTRL_4 */
    case 0x18:	/* FUNC_MUX_CTRL_5 */
    case 0x1c:	/* FUNC_MUX_CTRL_6 */
    case 0x20:	/* FUNC_MUX_CTRL_7 */
    case 0x24:	/* FUNC_MUX_CTRL_8 */
    case 0x28:	/* FUNC_MUX_CTRL_9 */
    case 0x2c:	/* FUNC_MUX_CTRL_A */
    case 0x30:	/* FUNC_MUX_CTRL_B */
    case 0x34:	/* FUNC_MUX_CTRL_C */
    case 0x38:	/* FUNC_MUX_CTRL_D */
        s->func_mux_ctrl[(addr >> 2) - 1] = value;
        return;

    case 0x40:	/* PULL_DWN_CTRL_0 */
    case 0x44:	/* PULL_DWN_CTRL_1 */
    case 0x48:	/* PULL_DWN_CTRL_2 */
    case 0x4c:	/* PULL_DWN_CTRL_3 */
        s->pull_dwn_ctrl[(addr & 0xf) >> 2] = value;
        return;

    case 0x50:	/* GATE_INH_CTRL_0 */
        s->gate_inh_ctrl[0] = value;
        return;

    case 0x60:	/* VOLTAGE_CTRL_0 */
        s->voltage_ctrl[0] = value;
        return;

    case 0x70:	/* TEST_DBG_CTRL_0 */
        s->test_dbg_ctrl[0] = value;
        return;

    case 0x80:	/* MOD_CONF_CTRL_0 */
        diff = s->mod_conf_ctrl[0] ^ value;
        s->mod_conf_ctrl[0] = value;
        omap_pin_modconf1_update(s, diff, value);
        return;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_pin_cfg_ops = {
    .read = omap_pin_cfg_read,
    .write = omap_pin_cfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_pin_cfg_reset(struct omap_mpu_state_s *mpu)
{
    /* Start in Compatibility Mode.  */
    mpu->compat1509 = 1;
    omap_pin_funcmux0_update(mpu, mpu->func_mux_ctrl[0], 0);
    omap_pin_funcmux1_update(mpu, mpu->func_mux_ctrl[1], 0);
    omap_pin_modconf1_update(mpu, mpu->mod_conf_ctrl[0], 0);
    memset(mpu->func_mux_ctrl, 0, sizeof(mpu->func_mux_ctrl));
    memset(mpu->comp_mode_ctrl, 0, sizeof(mpu->comp_mode_ctrl));
    memset(mpu->pull_dwn_ctrl, 0, sizeof(mpu->pull_dwn_ctrl));
    memset(mpu->gate_inh_ctrl, 0, sizeof(mpu->gate_inh_ctrl));
    memset(mpu->voltage_ctrl, 0, sizeof(mpu->voltage_ctrl));
    memset(mpu->test_dbg_ctrl, 0, sizeof(mpu->test_dbg_ctrl));
    memset(mpu->mod_conf_ctrl, 0, sizeof(mpu->mod_conf_ctrl));
}

static void omap_pin_cfg_init(MemoryRegion *system_memory,
                hwaddr base,
                struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->pin_cfg_iomem, NULL, &omap_pin_cfg_ops, mpu,
                          "omap-pin-cfg", 0x800);
    memory_region_add_subregion(system_memory, base, &mpu->pin_cfg_iomem);
    omap_pin_cfg_reset(mpu);
}

/* Device Identification, Die Identification */
static uint64_t omap_id_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0xfffe1800:	/* DIE_ID_LSB */
        return 0xc9581f0e;
    case 0xfffe1804:	/* DIE_ID_MSB */
        return 0xa8858bfa;

    case 0xfffe2000:	/* PRODUCT_ID_LSB */
        return 0x00aaaafc;
    case 0xfffe2004:	/* PRODUCT_ID_MSB */
        return 0xcafeb574;

    case 0xfffed400:	/* JTAG_ID_LSB */
        switch (s->mpu_model) {
        case omap310:
            return 0x03310315;
        case omap1510:
            return 0x03310115;
        default:
            hw_error("%s: bad mpu model\n", __FUNCTION__);
        }
        break;

    case 0xfffed404:	/* JTAG_ID_MSB */
        switch (s->mpu_model) {
        case omap310:
            return 0xfb57402f;
        case omap1510:
            return 0xfb47002f;
        default:
            hw_error("%s: bad mpu model\n", __FUNCTION__);
        }
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_id_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
{
    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap_id_ops = {
    .read = omap_id_read,
    .write = omap_id_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_id_init(MemoryRegion *memory, struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->id_iomem, NULL, &omap_id_ops, mpu,
                          "omap-id", 0x100000000ULL);
    memory_region_init_alias(&mpu->id_iomem_e18, NULL, "omap-id-e18", &mpu->id_iomem,
                             0xfffe1800, 0x800);
    memory_region_add_subregion(memory, 0xfffe1800, &mpu->id_iomem_e18);
    memory_region_init_alias(&mpu->id_iomem_ed4, NULL, "omap-id-ed4", &mpu->id_iomem,
                             0xfffed400, 0x100);
    memory_region_add_subregion(memory, 0xfffed400, &mpu->id_iomem_ed4);
    if (!cpu_is_omap15xx(mpu)) {
        memory_region_init_alias(&mpu->id_iomem_ed4, NULL, "omap-id-e20",
                                 &mpu->id_iomem, 0xfffe2000, 0x800);
        memory_region_add_subregion(memory, 0xfffe2000, &mpu->id_iomem_e20);
    }
}

/* MPUI Control (Dummy) */
static uint64_t omap_mpui_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* CTRL */
        return s->mpui_ctrl;
    case 0x04:	/* DEBUG_ADDR */
        return 0x01ffffff;
    case 0x08:	/* DEBUG_DATA */
        return 0xffffffff;
    case 0x0c:	/* DEBUG_FLAG */
        return 0x00000800;
    case 0x10:	/* STATUS */
        return 0x00000000;

    /* Not in OMAP310 */
    case 0x14:	/* DSP_STATUS */
    case 0x18:	/* DSP_BOOT_CONFIG */
        return 0x00000000;
    case 0x1c:	/* DSP_MPUI_CONFIG */
        return 0x0000ffff;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpui_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* CTRL */
        s->mpui_ctrl = value & 0x007fffff;
        break;

    case 0x04:	/* DEBUG_ADDR */
    case 0x08:	/* DEBUG_DATA */
    case 0x0c:	/* DEBUG_FLAG */
    case 0x10:	/* STATUS */
    /* Not in OMAP310 */
    case 0x14:	/* DSP_STATUS */
        OMAP_RO_REG(addr);
        break;
    case 0x18:	/* DSP_BOOT_CONFIG */
    case 0x1c:	/* DSP_MPUI_CONFIG */
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_mpui_ops = {
    .read = omap_mpui_read,
    .write = omap_mpui_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_mpui_reset(struct omap_mpu_state_s *s)
{
    s->mpui_ctrl = 0x0003ff1b;
}

static void omap_mpui_init(MemoryRegion *memory, hwaddr base,
                struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->mpui_iomem, NULL, &omap_mpui_ops, mpu,
                          "omap-mpui", 0x100);
    memory_region_add_subregion(memory, base, &mpu->mpui_iomem);

    omap_mpui_reset(mpu);
}

/* TIPB Bridges */
struct omap_tipb_bridge_s {
    qemu_irq abort;
    MemoryRegion iomem;

    int width_intr;
    uint16_t control;
    uint16_t alloc;
    uint16_t buffer;
    uint16_t enh_control;
};

static uint64_t omap_tipb_bridge_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *) opaque;

    if (size < 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* TIPB_CNTL */
        return s->control;
    case 0x04:	/* TIPB_BUS_ALLOC */
        return s->alloc;
    case 0x08:	/* MPU_TIPB_CNTL */
        return s->buffer;
    case 0x0c:	/* ENHANCED_TIPB_CNTL */
        return s->enh_control;
    case 0x10:	/* ADDRESS_DBG */
    case 0x14:	/* DATA_DEBUG_LOW */
    case 0x18:	/* DATA_DEBUG_HIGH */
        return 0xffff;
    case 0x1c:	/* DEBUG_CNTR_SIG */
        return 0x00f8;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_tipb_bridge_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *) opaque;

    if (size < 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* TIPB_CNTL */
        s->control = value & 0xffff;
        break;

    case 0x04:	/* TIPB_BUS_ALLOC */
        s->alloc = value & 0x003f;
        break;

    case 0x08:	/* MPU_TIPB_CNTL */
        s->buffer = value & 0x0003;
        break;

    case 0x0c:	/* ENHANCED_TIPB_CNTL */
        s->width_intr = !(value & 2);
        s->enh_control = value & 0x000f;
        break;

    case 0x10:	/* ADDRESS_DBG */
    case 0x14:	/* DATA_DEBUG_LOW */
    case 0x18:	/* DATA_DEBUG_HIGH */
    case 0x1c:	/* DEBUG_CNTR_SIG */
        OMAP_RO_REG(addr);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_tipb_bridge_ops = {
    .read = omap_tipb_bridge_read,
    .write = omap_tipb_bridge_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_tipb_bridge_reset(struct omap_tipb_bridge_s *s)
{
    s->control = 0xffff;
    s->alloc = 0x0009;
    s->buffer = 0x0000;
    s->enh_control = 0x000f;
}

static struct omap_tipb_bridge_s *omap_tipb_bridge_init(
    MemoryRegion *memory, hwaddr base,
    qemu_irq abort_irq, omap_clk clk)
{
    struct omap_tipb_bridge_s *s = (struct omap_tipb_bridge_s *)
            g_malloc0(sizeof(struct omap_tipb_bridge_s));

    s->abort = abort_irq;
    omap_tipb_bridge_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_tipb_bridge_ops, s,
                          "omap-tipb-bridge", 0x100);
    memory_region_add_subregion(memory, base, &s->iomem);

    return s;
}

/* Dummy Traffic Controller's Memory Interface */
static uint64_t omap_tcmi_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    uint32_t ret;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* IMIF_PRIO */
    case 0x04:	/* EMIFS_PRIO */
    case 0x08:	/* EMIFF_PRIO */
    case 0x0c:	/* EMIFS_CONFIG */
    case 0x10:	/* EMIFS_CS0_CONFIG */
    case 0x14:	/* EMIFS_CS1_CONFIG */
    case 0x18:	/* EMIFS_CS2_CONFIG */
    case 0x1c:	/* EMIFS_CS3_CONFIG */
    case 0x24:	/* EMIFF_MRS */
    case 0x28:	/* TIMEOUT1 */
    case 0x2c:	/* TIMEOUT2 */
    case 0x30:	/* TIMEOUT3 */
    case 0x3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0x40:	/* EMIFS_CFG_DYN_WAIT */
        return s->tcmi_regs[addr >> 2];

    case 0x20:	/* EMIFF_SDRAM_CONFIG */
        ret = s->tcmi_regs[addr >> 2];
        s->tcmi_regs[addr >> 2] &= ~1; /* XXX: Clear SLRF on SDRAM access */
        /* XXX: We can try using the VGA_DIRTY flag for this */
        return ret;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_tcmi_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* IMIF_PRIO */
    case 0x04:	/* EMIFS_PRIO */
    case 0x08:	/* EMIFF_PRIO */
    case 0x10:	/* EMIFS_CS0_CONFIG */
    case 0x14:	/* EMIFS_CS1_CONFIG */
    case 0x18:	/* EMIFS_CS2_CONFIG */
    case 0x1c:	/* EMIFS_CS3_CONFIG */
    case 0x20:	/* EMIFF_SDRAM_CONFIG */
    case 0x24:	/* EMIFF_MRS */
    case 0x28:	/* TIMEOUT1 */
    case 0x2c:	/* TIMEOUT2 */
    case 0x30:	/* TIMEOUT3 */
    case 0x3c:	/* EMIFF_SDRAM_CONFIG_2 */
    case 0x40:	/* EMIFS_CFG_DYN_WAIT */
        s->tcmi_regs[addr >> 2] = value;
        break;
    case 0x0c:	/* EMIFS_CONFIG */
        s->tcmi_regs[addr >> 2] = (value & 0xf) | (1 << 4);
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_tcmi_ops = {
    .read = omap_tcmi_read,
    .write = omap_tcmi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_tcmi_reset(struct omap_mpu_state_s *mpu)
{
    mpu->tcmi_regs[0x00 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x04 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x08 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x0c >> 2] = 0x00000010;
    mpu->tcmi_regs[0x10 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x14 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x18 >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x1c >> 2] = 0x0010fffb;
    mpu->tcmi_regs[0x20 >> 2] = 0x00618800;
    mpu->tcmi_regs[0x24 >> 2] = 0x00000037;
    mpu->tcmi_regs[0x28 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x2c >> 2] = 0x00000000;
    mpu->tcmi_regs[0x30 >> 2] = 0x00000000;
    mpu->tcmi_regs[0x3c >> 2] = 0x00000003;
    mpu->tcmi_regs[0x40 >> 2] = 0x00000000;
}

static void omap_tcmi_init(MemoryRegion *memory, hwaddr base,
                struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->tcmi_iomem, NULL, &omap_tcmi_ops, mpu,
                          "omap-tcmi", 0x100);
    memory_region_add_subregion(memory, base, &mpu->tcmi_iomem);
    omap_tcmi_reset(mpu);
}

/* Digital phase-locked loops control */
struct dpll_ctl_s {
    MemoryRegion iomem;
    uint16_t mode;
    omap_clk dpll;
};

static uint64_t omap_dpll_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct dpll_ctl_s *s = (struct dpll_ctl_s *) opaque;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    if (addr == 0x00)	/* CTL_REG */
        return s->mode;

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_dpll_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct dpll_ctl_s *s = (struct dpll_ctl_s *) opaque;
    uint16_t diff;
    static const int bypass_div[4] = { 1, 2, 4, 4 };
    int div, mult;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    if (addr == 0x00) {	/* CTL_REG */
        /* See omap_ulpd_pm_write() too */
        diff = s->mode & value;
        s->mode = value & 0x2fff;
        if (diff & (0x3ff << 2)) {
            if (value & (1 << 4)) {			/* PLL_ENABLE */
                div = ((value >> 5) & 3) + 1;		/* PLL_DIV */
                mult = MIN((value >> 7) & 0x1f, 1);	/* PLL_MULT */
            } else {
                div = bypass_div[((value >> 2) & 3)];	/* BYPASS_DIV */
                mult = 1;
            }
            omap_clk_setrate(s->dpll, div, mult);
        }

        /* Enter the desired mode.  */
        s->mode = (s->mode & 0xfffe) | ((s->mode >> 4) & 1);

        /* Act as if the lock is restored.  */
        s->mode |= 2;
    } else {
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_dpll_ops = {
    .read = omap_dpll_read,
    .write = omap_dpll_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_dpll_reset(struct dpll_ctl_s *s)
{
    s->mode = 0x2002;
    omap_clk_setrate(s->dpll, 1, 1);
}

static struct dpll_ctl_s  *omap_dpll_init(MemoryRegion *memory,
                           hwaddr base, omap_clk clk)
{
    struct dpll_ctl_s *s = g_malloc0(sizeof(*s));
    memory_region_init_io(&s->iomem, NULL, &omap_dpll_ops, s, "omap-dpll", 0x100);

    s->dpll = clk;
    omap_dpll_reset(s);

    memory_region_add_subregion(memory, base, &s->iomem);
    return s;
}

/* MPU Clock/Reset/Power Mode Control */
static uint64_t omap_clkm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* ARM_CKCTL */
        return s->clkm.arm_ckctl;

    case 0x04:	/* ARM_IDLECT1 */
        return s->clkm.arm_idlect1;

    case 0x08:	/* ARM_IDLECT2 */
        return s->clkm.arm_idlect2;

    case 0x0c:	/* ARM_EWUPCT */
        return s->clkm.arm_ewupct;

    case 0x10:	/* ARM_RSTCT1 */
        return s->clkm.arm_rstct1;

    case 0x14:	/* ARM_RSTCT2 */
        return s->clkm.arm_rstct2;

    case 0x18:	/* ARM_SYSST */
        return (s->clkm.clocking_scheme << 11) | s->clkm.cold_start;

    case 0x1c:	/* ARM_CKOUT1 */
        return s->clkm.arm_ckout1;

    case 0x20:	/* ARM_CKOUT2 */
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_clkm_ckctl_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (diff & (1 << 14)) {				/* ARM_INTHCK_SEL */
        if (value & (1 << 14))
            /* Reserved */;
        else {
            clk = omap_findclk(s, "arminth_ck");
            omap_clk_reparent(clk, omap_findclk(s, "tc_ck"));
        }
    }
    if (diff & (1 << 12)) {				/* ARM_TIMXO */
        clk = omap_findclk(s, "armtim_ck");
        if (value & (1 << 12))
            omap_clk_reparent(clk, omap_findclk(s, "clkin"));
        else
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen1"));
    }
    /* XXX: en_dspck */
    if (diff & (3 << 10)) {				/* DSPMMUDIV */
        clk = omap_findclk(s, "dspmmu_ck");
        omap_clk_setrate(clk, 1 << ((value >> 10) & 3), 1);
    }
    if (diff & (3 << 8)) {				/* TCDIV */
        clk = omap_findclk(s, "tc_ck");
        omap_clk_setrate(clk, 1 << ((value >> 8) & 3), 1);
    }
    if (diff & (3 << 6)) {				/* DSPDIV */
        clk = omap_findclk(s, "dsp_ck");
        omap_clk_setrate(clk, 1 << ((value >> 6) & 3), 1);
    }
    if (diff & (3 << 4)) {				/* ARMDIV */
        clk = omap_findclk(s, "arm_ck");
        omap_clk_setrate(clk, 1 << ((value >> 4) & 3), 1);
    }
    if (diff & (3 << 2)) {				/* LCDDIV */
        clk = omap_findclk(s, "lcd_ck");
        omap_clk_setrate(clk, 1 << ((value >> 2) & 3), 1);
    }
    if (diff & (3 << 0)) {				/* PERDIV */
        clk = omap_findclk(s, "armper_ck");
        omap_clk_setrate(clk, 1 << ((value >> 0) & 3), 1);
    }
}

static inline void omap_clkm_idlect1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (value & (1 << 11)) {                            /* SETARM_IDLE */
        cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HALT);
    }
    if (!(value & (1 << 10)))				/* WKUP_MODE */
        qemu_system_shutdown_request();	/* XXX: disable wakeup from IRQ */

#define SET_CANIDLE(clock, bit)				\
    if (diff & (1 << bit)) {				\
        clk = omap_findclk(s, clock);			\
        omap_clk_canidle(clk, (value >> bit) & 1);	\
    }
    SET_CANIDLE("mpuwd_ck", 0)				/* IDLWDT_ARM */
    SET_CANIDLE("armxor_ck", 1)				/* IDLXORP_ARM */
    SET_CANIDLE("mpuper_ck", 2)				/* IDLPER_ARM */
    SET_CANIDLE("lcd_ck", 3)				/* IDLLCD_ARM */
    SET_CANIDLE("lb_ck", 4)				/* IDLLB_ARM */
    SET_CANIDLE("hsab_ck", 5)				/* IDLHSAB_ARM */
    SET_CANIDLE("tipb_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("dma_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("tc_ck", 6)				/* IDLIF_ARM */
    SET_CANIDLE("dpll1", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("dpll2", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("dpll3", 7)				/* IDLDPLL_ARM */
    SET_CANIDLE("mpui_ck", 8)				/* IDLAPI_ARM */
    SET_CANIDLE("armtim_ck", 9)				/* IDLTIM_ARM */
}

static inline void omap_clkm_idlect2_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

#define SET_ONOFF(clock, bit)				\
    if (diff & (1 << bit)) {				\
        clk = omap_findclk(s, clock);			\
        omap_clk_onoff(clk, (value >> bit) & 1);	\
    }
    SET_ONOFF("mpuwd_ck", 0)				/* EN_WDTCK */
    SET_ONOFF("armxor_ck", 1)				/* EN_XORPCK */
    SET_ONOFF("mpuper_ck", 2)				/* EN_PERCK */
    SET_ONOFF("lcd_ck", 3)				/* EN_LCDCK */
    SET_ONOFF("lb_ck", 4)				/* EN_LBCK */
    SET_ONOFF("hsab_ck", 5)				/* EN_HSABCK */
    SET_ONOFF("mpui_ck", 6)				/* EN_APICK */
    SET_ONOFF("armtim_ck", 7)				/* EN_TIMCK */
    SET_CANIDLE("dma_ck", 8)				/* DMACK_REQ */
    SET_ONOFF("arm_gpio_ck", 9)				/* EN_GPIOCK */
    SET_ONOFF("lbfree_ck", 10)				/* EN_LBFREECK */
}

static inline void omap_clkm_ckout1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    if (diff & (3 << 4)) {				/* TCLKOUT */
        clk = omap_findclk(s, "tclk_out");
        switch ((value >> 4) & 3) {
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen3"));
            omap_clk_onoff(clk, 1);
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "tc_ck"));
            omap_clk_onoff(clk, 1);
            break;
        default:
            omap_clk_onoff(clk, 0);
        }
    }
    if (diff & (3 << 2)) {				/* DCLKOUT */
        clk = omap_findclk(s, "dclk_out");
        switch ((value >> 2) & 3) {
        case 0:
            omap_clk_reparent(clk, omap_findclk(s, "dspmmu_ck"));
            break;
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen2"));
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "dsp_ck"));
            break;
        case 3:
            omap_clk_reparent(clk, omap_findclk(s, "ck_ref14"));
            break;
        }
    }
    if (diff & (3 << 0)) {				/* ACLKOUT */
        clk = omap_findclk(s, "aclk_out");
        switch ((value >> 0) & 3) {
        case 1:
            omap_clk_reparent(clk, omap_findclk(s, "ck_gen1"));
            omap_clk_onoff(clk, 1);
            break;
        case 2:
            omap_clk_reparent(clk, omap_findclk(s, "arm_ck"));
            omap_clk_onoff(clk, 1);
            break;
        case 3:
            omap_clk_reparent(clk, omap_findclk(s, "ck_ref14"));
            omap_clk_onoff(clk, 1);
            break;
        default:
            omap_clk_onoff(clk, 0);
        }
    }
}

static void omap_clkm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    uint16_t diff;
    omap_clk clk;
    static const char *clkschemename[8] = {
        "fully synchronous", "fully asynchronous", "synchronous scalable",
        "mix mode 1", "mix mode 2", "bypass mode", "mix mode 3", "mix mode 4",
    };

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (addr) {
    case 0x00:	/* ARM_CKCTL */
        diff = s->clkm.arm_ckctl ^ value;
        s->clkm.arm_ckctl = value & 0x7fff;
        omap_clkm_ckctl_update(s, diff, value);
        return;

    case 0x04:	/* ARM_IDLECT1 */
        diff = s->clkm.arm_idlect1 ^ value;
        s->clkm.arm_idlect1 = value & 0x0fff;
        omap_clkm_idlect1_update(s, diff, value);
        return;

    case 0x08:	/* ARM_IDLECT2 */
        diff = s->clkm.arm_idlect2 ^ value;
        s->clkm.arm_idlect2 = value & 0x07ff;
        omap_clkm_idlect2_update(s, diff, value);
        return;

    case 0x0c:	/* ARM_EWUPCT */
        s->clkm.arm_ewupct = value & 0x003f;
        return;

    case 0x10:	/* ARM_RSTCT1 */
        diff = s->clkm.arm_rstct1 ^ value;
        s->clkm.arm_rstct1 = value & 0x0007;
        if (value & 9) {
            qemu_system_reset_request();
            s->clkm.cold_start = 0xa;
        }
        if (diff & ~value & 4) {				/* DSP_RST */
            omap_mpui_reset(s);
            omap_tipb_bridge_reset(s->private_tipb);
            omap_tipb_bridge_reset(s->public_tipb);
        }
        if (diff & 2) {						/* DSP_EN */
            clk = omap_findclk(s, "dsp_ck");
            omap_clk_canidle(clk, (~value >> 1) & 1);
        }
        return;

    case 0x14:	/* ARM_RSTCT2 */
        s->clkm.arm_rstct2 = value & 0x0001;
        return;

    case 0x18:	/* ARM_SYSST */
        if ((s->clkm.clocking_scheme ^ (value >> 11)) & 7) {
            s->clkm.clocking_scheme = (value >> 11) & 7;
            printf("%s: clocking scheme set to %s\n", __FUNCTION__,
                            clkschemename[s->clkm.clocking_scheme]);
        }
        s->clkm.cold_start &= value & 0x3f;
        return;

    case 0x1c:	/* ARM_CKOUT1 */
        diff = s->clkm.arm_ckout1 ^ value;
        s->clkm.arm_ckout1 = value & 0x003f;
        omap_clkm_ckout1_update(s, diff, value);
        return;

    case 0x20:	/* ARM_CKOUT2 */
    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_clkm_ops = {
    .read = omap_clkm_read,
    .write = omap_clkm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t omap_clkdsp_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    CPUState *cpu = CPU(s->cpu);

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x04:	/* DSP_IDLECT1 */
        return s->clkm.dsp_idlect1;

    case 0x08:	/* DSP_IDLECT2 */
        return s->clkm.dsp_idlect2;

    case 0x14:	/* DSP_RSTCT2 */
        return s->clkm.dsp_rstct2;

    case 0x18:	/* DSP_SYSST */
        cpu = CPU(s->cpu);
        return (s->clkm.clocking_scheme << 11) | s->clkm.cold_start |
                (cpu->halted << 6);      /* Quite useless... */
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static inline void omap_clkdsp_idlect1_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    SET_CANIDLE("dspxor_ck", 1);			/* IDLXORP_DSP */
}

static inline void omap_clkdsp_idlect2_update(struct omap_mpu_state_s *s,
                uint16_t diff, uint16_t value)
{
    omap_clk clk;

    SET_ONOFF("dspxor_ck", 1);				/* EN_XORPCK */
}

static void omap_clkdsp_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *) opaque;
    uint16_t diff;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (addr) {
    case 0x04:	/* DSP_IDLECT1 */
        diff = s->clkm.dsp_idlect1 ^ value;
        s->clkm.dsp_idlect1 = value & 0x01f7;
        omap_clkdsp_idlect1_update(s, diff, value);
        break;

    case 0x08:	/* DSP_IDLECT2 */
        s->clkm.dsp_idlect2 = value & 0x0037;
        diff = s->clkm.dsp_idlect1 ^ value;
        omap_clkdsp_idlect2_update(s, diff, value);
        break;

    case 0x14:	/* DSP_RSTCT2 */
        s->clkm.dsp_rstct2 = value & 0x0001;
        break;

    case 0x18:	/* DSP_SYSST */
        s->clkm.cold_start &= value & 0x3f;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_clkdsp_ops = {
    .read = omap_clkdsp_read,
    .write = omap_clkdsp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_clkm_reset(struct omap_mpu_state_s *s)
{
    if (s->wdt && s->wdt->reset)
        s->clkm.cold_start = 0x6;
    s->clkm.clocking_scheme = 0;
    omap_clkm_ckctl_update(s, ~0, 0x3000);
    s->clkm.arm_ckctl = 0x3000;
    omap_clkm_idlect1_update(s, s->clkm.arm_idlect1 ^ 0x0400, 0x0400);
    s->clkm.arm_idlect1 = 0x0400;
    omap_clkm_idlect2_update(s, s->clkm.arm_idlect2 ^ 0x0100, 0x0100);
    s->clkm.arm_idlect2 = 0x0100;
    s->clkm.arm_ewupct = 0x003f;
    s->clkm.arm_rstct1 = 0x0000;
    s->clkm.arm_rstct2 = 0x0000;
    s->clkm.arm_ckout1 = 0x0015;
    s->clkm.dpll1_mode = 0x2002;
    omap_clkdsp_idlect1_update(s, s->clkm.dsp_idlect1 ^ 0x0040, 0x0040);
    s->clkm.dsp_idlect1 = 0x0040;
    omap_clkdsp_idlect2_update(s, ~0, 0x0000);
    s->clkm.dsp_idlect2 = 0x0000;
    s->clkm.dsp_rstct2 = 0x0000;
}

static void omap_clkm_init(MemoryRegion *memory, hwaddr mpu_base,
                hwaddr dsp_base, struct omap_mpu_state_s *s)
{
    memory_region_init_io(&s->clkm_iomem, NULL, &omap_clkm_ops, s,
                          "omap-clkm", 0x100);
    memory_region_init_io(&s->clkdsp_iomem, NULL, &omap_clkdsp_ops, s,
                          "omap-clkdsp", 0x1000);

    s->clkm.arm_idlect1 = 0x03ff;
    s->clkm.arm_idlect2 = 0x0100;
    s->clkm.dsp_idlect1 = 0x0002;
    omap_clkm_reset(s);
    s->clkm.cold_start = 0x3a;

    memory_region_add_subregion(memory, mpu_base, &s->clkm_iomem);
    memory_region_add_subregion(memory, dsp_base, &s->clkdsp_iomem);
}

/* MPU I/O */
struct omap_mpuio_s {
    qemu_irq irq;
    qemu_irq kbd_irq;
    qemu_irq *in;
    qemu_irq handler[16];
    qemu_irq wakeup;
    MemoryRegion iomem;

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;

    uint16_t debounce;
    uint16_t latch;
    uint8_t event;

    uint8_t buttons[5];
    uint8_t row_latch;
    uint8_t cols;
    int kbd_mask;
    int clk;
};

static void omap_mpuio_set(void *opaque, int line, int level)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((1 << line) & s->dir & ~s->mask) && s->clk) {
        if ((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) {
            s->ints |= 1 << line;
            qemu_irq_raise(s->irq);
            /* TODO: wakeup */
        }
        if ((s->event & (1 << 0)) &&		/* SET_GPIO_EVENT_MODE */
                (s->event >> 1) == line)	/* PIN_SELECT */
            s->latch = s->inputs;
    }
}

static void omap_mpuio_kbd_update(struct omap_mpuio_s *s)
{
    int i;
    uint8_t *row, rows = 0, cols = ~s->cols;

    for (row = s->buttons + 4, i = 1 << 4; i; row --, i >>= 1)
        if (*row & cols)
            rows |= i;

    qemu_set_irq(s->kbd_irq, rows && !s->kbd_mask && s->clk);
    s->row_latch = ~rows;
}

static uint64_t omap_mpuio_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* INPUT_LATCH */
        return s->inputs;

    case 0x04:	/* OUTPUT_REG */
        return s->outputs;

    case 0x08:	/* IO_CNTL */
        return s->dir;

    case 0x10:	/* KBR_LATCH */
        return s->row_latch;

    case 0x14:	/* KBC_REG */
        return s->cols;

    case 0x18:	/* GPIO_EVENT_MODE_REG */
        return s->event;

    case 0x1c:	/* GPIO_INT_EDGE_REG */
        return s->edge;

    case 0x20:	/* KBD_INT */
        return (~s->row_latch & 0x1f) && !s->kbd_mask;

    case 0x24:	/* GPIO_INT */
        ret = s->ints;
        s->ints &= s->mask;
        if (ret)
            qemu_irq_lower(s->irq);
        return ret;

    case 0x28:	/* KBD_MASKIT */
        return s->kbd_mask;

    case 0x2c:	/* GPIO_MASKIT */
        return s->mask;

    case 0x30:	/* GPIO_DEBOUNCING_REG */
        return s->debounce;

    case 0x34:	/* GPIO_LATCH_REG */
        return s->latch;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpuio_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (offset) {
    case 0x04:	/* OUTPUT_REG */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* IO_CNTL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ffs(diff))) {
            ln --;
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x14:	/* KBC_REG */
        s->cols = value;
        omap_mpuio_kbd_update(s);
        break;

    case 0x18:	/* GPIO_EVENT_MODE_REG */
        s->event = value & 0x1f;
        break;

    case 0x1c:	/* GPIO_INT_EDGE_REG */
        s->edge = value;
        break;

    case 0x28:	/* KBD_MASKIT */
        s->kbd_mask = value & 1;
        omap_mpuio_kbd_update(s);
        break;

    case 0x2c:	/* GPIO_MASKIT */
        s->mask = value;
        break;

    case 0x30:	/* GPIO_DEBOUNCING_REG */
        s->debounce = value & 0x1ff;
        break;

    case 0x00:	/* INPUT_LATCH */
    case 0x10:	/* KBR_LATCH */
    case 0x20:	/* KBD_INT */
    case 0x24:	/* GPIO_INT */
    case 0x34:	/* GPIO_LATCH_REG */
        OMAP_RO_REG(addr);
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_mpuio_ops  = {
    .read = omap_mpuio_read,
    .write = omap_mpuio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_mpuio_reset(struct omap_mpuio_s *s)
{
    s->inputs = 0;
    s->outputs = 0;
    s->dir = ~0;
    s->event = 0;
    s->edge = 0;
    s->kbd_mask = 0;
    s->mask = 0;
    s->debounce = 0;
    s->latch = 0;
    s->ints = 0;
    s->row_latch = 0x1f;
    s->clk = 1;
}

static void omap_mpuio_onoff(void *opaque, int line, int on)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *) opaque;

    s->clk = on;
    if (on)
        omap_mpuio_kbd_update(s);
}

static struct omap_mpuio_s *omap_mpuio_init(MemoryRegion *memory,
                hwaddr base,
                qemu_irq kbd_int, qemu_irq gpio_int, qemu_irq wakeup,
                omap_clk clk)
{
    struct omap_mpuio_s *s = (struct omap_mpuio_s *)
            g_malloc0(sizeof(struct omap_mpuio_s));

    s->irq = gpio_int;
    s->kbd_irq = kbd_int;
    s->wakeup = wakeup;
    s->in = qemu_allocate_irqs(omap_mpuio_set, s, 16);
    omap_mpuio_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_mpuio_ops, s,
                          "omap-mpuio", 0x800);
    memory_region_add_subregion(memory, base, &s->iomem);

    omap_clk_adduser(clk, qemu_allocate_irq(omap_mpuio_onoff, s, 0));

    return s;
}

qemu_irq *omap_mpuio_in_get(struct omap_mpuio_s *s)
{
    return s->in;
}

void omap_mpuio_out_set(struct omap_mpuio_s *s, int line, qemu_irq handler)
{
    if (line >= 16 || line < 0)
        hw_error("%s: No GPIO line %i\n", __FUNCTION__, line);
    s->handler[line] = handler;
}

void omap_mpuio_key(struct omap_mpuio_s *s, int row, int col, int down)
{
    if (row >= 5 || row < 0)
        hw_error("%s: No key %i-%i\n", __FUNCTION__, col, row);

    if (down)
        s->buttons[row] |= 1 << col;
    else
        s->buttons[row] &= ~(1 << col);

    omap_mpuio_kbd_update(s);
}

/* MicroWire Interface */
struct omap_uwire_s {
    MemoryRegion iomem;
    qemu_irq txirq;
    qemu_irq rxirq;
    qemu_irq txdrq;

    uint16_t txbuf;
    uint16_t rxbuf;
    uint16_t control;
    uint16_t setup[5];

    uWireSlave *chip[4];
};

static void omap_uwire_transfer_start(struct omap_uwire_s *s)
{
    int chipselect = (s->control >> 10) & 3;		/* INDEX */
    uWireSlave *slave = s->chip[chipselect];

    if ((s->control >> 5) & 0x1f) {			/* NB_BITS_WR */
        if (s->control & (1 << 12))			/* CS_CMD */
            if (slave && slave->send)
                slave->send(slave->opaque,
                                s->txbuf >> (16 - ((s->control >> 5) & 0x1f)));
        s->control &= ~(1 << 14);			/* CSRB */
        /* TODO: depending on s->setup[4] bits [1:0] assert an IRQ or
         * a DRQ.  When is the level IRQ supposed to be reset?  */
    }

    if ((s->control >> 0) & 0x1f) {			/* NB_BITS_RD */
        if (s->control & (1 << 12))			/* CS_CMD */
            if (slave && slave->receive)
                s->rxbuf = slave->receive(slave->opaque);
        s->control |= 1 << 15;				/* RDRB */
        /* TODO: depending on s->setup[4] bits [1:0] assert an IRQ or
         * a DRQ.  When is the level IRQ supposed to be reset?  */
    }
}

static uint64_t omap_uwire_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    struct omap_uwire_s *s = (struct omap_uwire_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* RDR */
        s->control &= ~(1 << 15);			/* RDRB */
        return s->rxbuf;

    case 0x04:	/* CSR */
        return s->control;

    case 0x08:	/* SR1 */
        return s->setup[0];
    case 0x0c:	/* SR2 */
        return s->setup[1];
    case 0x10:	/* SR3 */
        return s->setup[2];
    case 0x14:	/* SR4 */
        return s->setup[3];
    case 0x18:	/* SR5 */
        return s->setup[4];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_uwire_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    struct omap_uwire_s *s = (struct omap_uwire_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 2) {
        return omap_badwidth_write16(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* TDR */
        s->txbuf = value;				/* TD */
        if ((s->setup[4] & (1 << 2)) &&			/* AUTO_TX_EN */
                        ((s->setup[4] & (1 << 3)) ||	/* CS_TOGGLE_TX_EN */
                         (s->control & (1 << 12)))) {	/* CS_CMD */
            s->control |= 1 << 14;			/* CSRB */
            omap_uwire_transfer_start(s);
        }
        break;

    case 0x04:	/* CSR */
        s->control = value & 0x1fff;
        if (value & (1 << 13))				/* START */
            omap_uwire_transfer_start(s);
        break;

    case 0x08:	/* SR1 */
        s->setup[0] = value & 0x003f;
        break;

    case 0x0c:	/* SR2 */
        s->setup[1] = value & 0x0fc0;
        break;

    case 0x10:	/* SR3 */
        s->setup[2] = value & 0x0003;
        break;

    case 0x14:	/* SR4 */
        s->setup[3] = value & 0x0001;
        break;

    case 0x18:	/* SR5 */
        s->setup[4] = value & 0x000f;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_uwire_ops = {
    .read = omap_uwire_read,
    .write = omap_uwire_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_uwire_reset(struct omap_uwire_s *s)
{
    s->control = 0;
    s->setup[0] = 0;
    s->setup[1] = 0;
    s->setup[2] = 0;
    s->setup[3] = 0;
    s->setup[4] = 0;
}

static struct omap_uwire_s *omap_uwire_init(MemoryRegion *system_memory,
                                            hwaddr base,
                                            qemu_irq txirq, qemu_irq rxirq,
                                            qemu_irq dma,
                                            omap_clk clk)
{
    struct omap_uwire_s *s = (struct omap_uwire_s *)
            g_malloc0(sizeof(struct omap_uwire_s));

    s->txirq = txirq;
    s->rxirq = rxirq;
    s->txdrq = dma;
    omap_uwire_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_uwire_ops, s, "omap-uwire", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);

    return s;
}

void omap_uwire_attach(struct omap_uwire_s *s,
                uWireSlave *slave, int chipselect)
{
    if (chipselect < 0 || chipselect > 3) {
        fprintf(stderr, "%s: Bad chipselect %i\n", __FUNCTION__, chipselect);
        exit(-1);
    }

    s->chip[chipselect] = slave;
}

/* Pseudonoise Pulse-Width Light Modulator */
struct omap_pwl_s {
    MemoryRegion iomem;
    uint8_t output;
    uint8_t level;
    uint8_t enable;
    int clk;
};

static void omap_pwl_update(struct omap_pwl_s *s)
{
    int output = (s->clk && s->enable) ? s->level : 0;

    if (output != s->output) {
        s->output = output;
        printf("%s: Backlight now at %i/256\n", __FUNCTION__, output);
    }
}

static uint64_t omap_pwl_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    struct omap_pwl_s *s = (struct omap_pwl_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_read8(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* PWL_LEVEL */
        return s->level;
    case 0x04:	/* PWL_CTRL */
        return s->enable;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_pwl_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_pwl_s *s = (struct omap_pwl_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_write8(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* PWL_LEVEL */
        s->level = value;
        omap_pwl_update(s);
        break;
    case 0x04:	/* PWL_CTRL */
        s->enable = value & 1;
        omap_pwl_update(s);
        break;
    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_pwl_ops = {
    .read = omap_pwl_read,
    .write = omap_pwl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_pwl_reset(struct omap_pwl_s *s)
{
    s->output = 0;
    s->level = 0;
    s->enable = 0;
    s->clk = 1;
    omap_pwl_update(s);
}

static void omap_pwl_clk_update(void *opaque, int line, int on)
{
    struct omap_pwl_s *s = (struct omap_pwl_s *) opaque;

    s->clk = on;
    omap_pwl_update(s);
}

static struct omap_pwl_s *omap_pwl_init(MemoryRegion *system_memory,
                                        hwaddr base,
                                        omap_clk clk)
{
    struct omap_pwl_s *s = g_malloc0(sizeof(*s));

    omap_pwl_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_pwl_ops, s,
                          "omap-pwl", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);

    omap_clk_adduser(clk, qemu_allocate_irq(omap_pwl_clk_update, s, 0));
    return s;
}

/* Pulse-Width Tone module */
struct omap_pwt_s {
    MemoryRegion iomem;
    uint8_t frc;
    uint8_t vrc;
    uint8_t gcr;
    omap_clk clk;
};

static uint64_t omap_pwt_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    struct omap_pwt_s *s = (struct omap_pwt_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_read8(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* FRC */
        return s->frc;
    case 0x04:	/* VCR */
        return s->vrc;
    case 0x08:	/* GCR */
        return s->gcr;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_pwt_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_pwt_s *s = (struct omap_pwt_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_write8(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* FRC */
        s->frc = value & 0x3f;
        break;
    case 0x04:	/* VRC */
        if ((value ^ s->vrc) & 1) {
            if (value & 1)
                printf("%s: %iHz buzz on\n", __FUNCTION__, (int)
                                /* 1.5 MHz from a 12-MHz or 13-MHz PWT_CLK */
                                ((omap_clk_getrate(s->clk) >> 3) /
                                 /* Pre-multiplexer divider */
                                 ((s->gcr & 2) ? 1 : 154) /
                                 /* Octave multiplexer */
                                 (2 << (value & 3)) *
                                 /* 101/107 divider */
                                 ((value & (1 << 2)) ? 101 : 107) *
                                 /*  49/55 divider */
                                 ((value & (1 << 3)) ?  49 : 55) *
                                 /*  50/63 divider */
                                 ((value & (1 << 4)) ?  50 : 63) *
                                 /*  80/127 divider */
                                 ((value & (1 << 5)) ?  80 : 127) /
                                 (107 * 55 * 63 * 127)));
            else
                printf("%s: silence!\n", __FUNCTION__);
        }
        s->vrc = value & 0x7f;
        break;
    case 0x08:	/* GCR */
        s->gcr = value & 3;
        break;
    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_pwt_ops = {
    .read =omap_pwt_read,
    .write = omap_pwt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_pwt_reset(struct omap_pwt_s *s)
{
    s->frc = 0;
    s->vrc = 0;
    s->gcr = 0;
}

static struct omap_pwt_s *omap_pwt_init(MemoryRegion *system_memory,
                                        hwaddr base,
                                        omap_clk clk)
{
    struct omap_pwt_s *s = g_malloc0(sizeof(*s));
    s->clk = clk;
    omap_pwt_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_pwt_ops, s,
                          "omap-pwt", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);
    return s;
}

/* Real-time Clock module */
struct omap_rtc_s {
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq alarm;
    QEMUTimer *clk;

    uint8_t interrupts;
    uint8_t status;
    int16_t comp_reg;
    int running;
    int pm_am;
    int auto_comp;
    int round;
    struct tm alarm_tm;
    time_t alarm_ti;

    struct tm current_tm;
    time_t ti;
    uint64_t tick;
};

static void omap_rtc_interrupts_update(struct omap_rtc_s *s)
{
    /* s->alarm is level-triggered */
    qemu_set_irq(s->alarm, (s->status >> 6) & 1);
}

static void omap_rtc_alarm_update(struct omap_rtc_s *s)
{
    s->alarm_ti = mktimegm(&s->alarm_tm);
    if (s->alarm_ti == -1)
        printf("%s: conversion failed\n", __FUNCTION__);
}

static uint64_t omap_rtc_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    struct omap_rtc_s *s = (struct omap_rtc_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint8_t i;

    if (size != 1) {
        return omap_badwidth_read8(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* SECONDS_REG */
        return to_bcd(s->current_tm.tm_sec);

    case 0x04:	/* MINUTES_REG */
        return to_bcd(s->current_tm.tm_min);

    case 0x08:	/* HOURS_REG */
        if (s->pm_am)
            return ((s->current_tm.tm_hour > 11) << 7) |
                    to_bcd(((s->current_tm.tm_hour - 1) % 12) + 1);
        else
            return to_bcd(s->current_tm.tm_hour);

    case 0x0c:	/* DAYS_REG */
        return to_bcd(s->current_tm.tm_mday);

    case 0x10:	/* MONTHS_REG */
        return to_bcd(s->current_tm.tm_mon + 1);

    case 0x14:	/* YEARS_REG */
        return to_bcd(s->current_tm.tm_year % 100);

    case 0x18:	/* WEEK_REG */
        return s->current_tm.tm_wday;

    case 0x20:	/* ALARM_SECONDS_REG */
        return to_bcd(s->alarm_tm.tm_sec);

    case 0x24:	/* ALARM_MINUTES_REG */
        return to_bcd(s->alarm_tm.tm_min);

    case 0x28:	/* ALARM_HOURS_REG */
        if (s->pm_am)
            return ((s->alarm_tm.tm_hour > 11) << 7) |
                    to_bcd(((s->alarm_tm.tm_hour - 1) % 12) + 1);
        else
            return to_bcd(s->alarm_tm.tm_hour);

    case 0x2c:	/* ALARM_DAYS_REG */
        return to_bcd(s->alarm_tm.tm_mday);

    case 0x30:	/* ALARM_MONTHS_REG */
        return to_bcd(s->alarm_tm.tm_mon + 1);

    case 0x34:	/* ALARM_YEARS_REG */
        return to_bcd(s->alarm_tm.tm_year % 100);

    case 0x40:	/* RTC_CTRL_REG */
        return (s->pm_am << 3) | (s->auto_comp << 2) |
                (s->round << 1) | s->running;

    case 0x44:	/* RTC_STATUS_REG */
        i = s->status;
        s->status &= ~0x3d;
        return i;

    case 0x48:	/* RTC_INTERRUPTS_REG */
        return s->interrupts;

    case 0x4c:	/* RTC_COMP_LSB_REG */
        return ((uint16_t) s->comp_reg) & 0xff;

    case 0x50:	/* RTC_COMP_MSB_REG */
        return ((uint16_t) s->comp_reg) >> 8;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_rtc_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_rtc_s *s = (struct omap_rtc_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    struct tm new_tm;
    time_t ti[2];

    if (size != 1) {
        return omap_badwidth_write8(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* SECONDS_REG */
#ifdef ALMDEBUG
        printf("RTC SEC_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_sec;
        s->ti += from_bcd(value);
        return;

    case 0x04:	/* MINUTES_REG */
#ifdef ALMDEBUG
        printf("RTC MIN_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_min * 60;
        s->ti += from_bcd(value) * 60;
        return;

    case 0x08:	/* HOURS_REG */
#ifdef ALMDEBUG
        printf("RTC HRS_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_hour * 3600;
        if (s->pm_am) {
            s->ti += (from_bcd(value & 0x3f) & 12) * 3600;
            s->ti += ((value >> 7) & 1) * 43200;
        } else
            s->ti += from_bcd(value & 0x3f) * 3600;
        return;

    case 0x0c:	/* DAYS_REG */
#ifdef ALMDEBUG
        printf("RTC DAY_REG <-- %02x\n", value);
#endif
        s->ti -= s->current_tm.tm_mday * 86400;
        s->ti += from_bcd(value) * 86400;
        return;

    case 0x10:	/* MONTHS_REG */
#ifdef ALMDEBUG
        printf("RTC MTH_REG <-- %02x\n", value);
#endif
        memcpy(&new_tm, &s->current_tm, sizeof(new_tm));
        new_tm.tm_mon = from_bcd(value);
        ti[0] = mktimegm(&s->current_tm);
        ti[1] = mktimegm(&new_tm);

        if (ti[0] != -1 && ti[1] != -1) {
            s->ti -= ti[0];
            s->ti += ti[1];
        } else {
            /* A less accurate version */
            s->ti -= s->current_tm.tm_mon * 2592000;
            s->ti += from_bcd(value) * 2592000;
        }
        return;

    case 0x14:	/* YEARS_REG */
#ifdef ALMDEBUG
        printf("RTC YRS_REG <-- %02x\n", value);
#endif
        memcpy(&new_tm, &s->current_tm, sizeof(new_tm));
        new_tm.tm_year += from_bcd(value) - (new_tm.tm_year % 100);
        ti[0] = mktimegm(&s->current_tm);
        ti[1] = mktimegm(&new_tm);

        if (ti[0] != -1 && ti[1] != -1) {
            s->ti -= ti[0];
            s->ti += ti[1];
        } else {
            /* A less accurate version */
            s->ti -= (s->current_tm.tm_year % 100) * 31536000;
            s->ti += from_bcd(value) * 31536000;
        }
        return;

    case 0x18:	/* WEEK_REG */
        return;	/* Ignored */

    case 0x20:	/* ALARM_SECONDS_REG */
#ifdef ALMDEBUG
        printf("ALM SEC_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_sec = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x24:	/* ALARM_MINUTES_REG */
#ifdef ALMDEBUG
        printf("ALM MIN_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_min = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x28:	/* ALARM_HOURS_REG */
#ifdef ALMDEBUG
        printf("ALM HRS_REG <-- %02x\n", value);
#endif
        if (s->pm_am)
            s->alarm_tm.tm_hour =
                    ((from_bcd(value & 0x3f)) % 12) +
                    ((value >> 7) & 1) * 12;
        else
            s->alarm_tm.tm_hour = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x2c:	/* ALARM_DAYS_REG */
#ifdef ALMDEBUG
        printf("ALM DAY_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_mday = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x30:	/* ALARM_MONTHS_REG */
#ifdef ALMDEBUG
        printf("ALM MON_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_mon = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x34:	/* ALARM_YEARS_REG */
#ifdef ALMDEBUG
        printf("ALM YRS_REG <-- %02x\n", value);
#endif
        s->alarm_tm.tm_year = from_bcd(value);
        omap_rtc_alarm_update(s);
        return;

    case 0x40:	/* RTC_CTRL_REG */
#ifdef ALMDEBUG
        printf("RTC CONTROL <-- %02x\n", value);
#endif
        s->pm_am = (value >> 3) & 1;
        s->auto_comp = (value >> 2) & 1;
        s->round = (value >> 1) & 1;
        s->running = value & 1;
        s->status &= 0xfd;
        s->status |= s->running << 1;
        return;

    case 0x44:	/* RTC_STATUS_REG */
#ifdef ALMDEBUG
        printf("RTC STATUSL <-- %02x\n", value);
#endif
        s->status &= ~((value & 0xc0) ^ 0x80);
        omap_rtc_interrupts_update(s);
        return;

    case 0x48:	/* RTC_INTERRUPTS_REG */
#ifdef ALMDEBUG
        printf("RTC INTRS <-- %02x\n", value);
#endif
        s->interrupts = value;
        return;

    case 0x4c:	/* RTC_COMP_LSB_REG */
#ifdef ALMDEBUG
        printf("RTC COMPLSB <-- %02x\n", value);
#endif
        s->comp_reg &= 0xff00;
        s->comp_reg |= 0x00ff & value;
        return;

    case 0x50:	/* RTC_COMP_MSB_REG */
#ifdef ALMDEBUG
        printf("RTC COMPMSB <-- %02x\n", value);
#endif
        s->comp_reg &= 0x00ff;
        s->comp_reg |= 0xff00 & (value << 8);
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_rtc_ops = {
    .read = omap_rtc_read,
    .write = omap_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_rtc_tick(void *opaque)
{
    struct omap_rtc_s *s = opaque;

    if (s->round) {
        /* Round to nearest full minute.  */
        if (s->current_tm.tm_sec < 30)
            s->ti -= s->current_tm.tm_sec;
        else
            s->ti += 60 - s->current_tm.tm_sec;

        s->round = 0;
    }

    localtime_r(&s->ti, &s->current_tm);

    if ((s->interrupts & 0x08) && s->ti == s->alarm_ti) {
        s->status |= 0x40;
        omap_rtc_interrupts_update(s);
    }

    if (s->interrupts & 0x04)
        switch (s->interrupts & 3) {
        case 0:
            s->status |= 0x04;
            qemu_irq_pulse(s->irq);
            break;
        case 1:
            if (s->current_tm.tm_sec)
                break;
            s->status |= 0x08;
            qemu_irq_pulse(s->irq);
            break;
        case 2:
            if (s->current_tm.tm_sec || s->current_tm.tm_min)
                break;
            s->status |= 0x10;
            qemu_irq_pulse(s->irq);
            break;
        case 3:
            if (s->current_tm.tm_sec ||
                            s->current_tm.tm_min || s->current_tm.tm_hour)
                break;
            s->status |= 0x20;
            qemu_irq_pulse(s->irq);
            break;
        }

    /* Move on */
    if (s->running)
        s->ti ++;
    s->tick += 1000;

    /*
     * Every full hour add a rough approximation of the compensation
     * register to the 32kHz Timer (which drives the RTC) value. 
     */
    if (s->auto_comp && !s->current_tm.tm_sec && !s->current_tm.tm_min)
        s->tick += s->comp_reg * 1000 / 32768;

    timer_mod(s->clk, s->tick);
}

static void omap_rtc_reset(struct omap_rtc_s *s)
{
    struct tm tm;

    s->interrupts = 0;
    s->comp_reg = 0;
    s->running = 0;
    s->pm_am = 0;
    s->auto_comp = 0;
    s->round = 0;
    s->tick = qemu_clock_get_ms(rtc_clock);
    memset(&s->alarm_tm, 0, sizeof(s->alarm_tm));
    s->alarm_tm.tm_mday = 0x01;
    s->status = 1 << 7;
    qemu_get_timedate(&tm, 0);
    s->ti = mktimegm(&tm);

    omap_rtc_alarm_update(s);
    omap_rtc_tick(s);
}

static struct omap_rtc_s *omap_rtc_init(MemoryRegion *system_memory,
                                        hwaddr base,
                                        qemu_irq timerirq, qemu_irq alarmirq,
                                        omap_clk clk)
{
    struct omap_rtc_s *s = (struct omap_rtc_s *)
            g_malloc0(sizeof(struct omap_rtc_s));

    s->irq = timerirq;
    s->alarm = alarmirq;
    s->clk = timer_new_ms(rtc_clock, omap_rtc_tick, s);

    omap_rtc_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_rtc_ops, s,
                          "omap-rtc", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);

    return s;
}

/* Multi-channel Buffered Serial Port interfaces */
struct omap_mcbsp_s {
    MemoryRegion iomem;
    qemu_irq txirq;
    qemu_irq rxirq;
    qemu_irq txdrq;
    qemu_irq rxdrq;

    uint16_t spcr[2];
    uint16_t rcr[2];
    uint16_t xcr[2];
    uint16_t srgr[2];
    uint16_t mcr[2];
    uint16_t pcr;
    uint16_t rcer[8];
    uint16_t xcer[8];
    int tx_rate;
    int rx_rate;
    int tx_req;
    int rx_req;

    I2SCodec *codec;
    QEMUTimer *source_timer;
    QEMUTimer *sink_timer;
};

static void omap_mcbsp_intr_update(struct omap_mcbsp_s *s)
{
    int irq;

    switch ((s->spcr[0] >> 4) & 3) {			/* RINTM */
    case 0:
        irq = (s->spcr[0] >> 1) & 1;			/* RRDY */
        break;
    case 3:
        irq = (s->spcr[0] >> 3) & 1;			/* RSYNCERR */
        break;
    default:
        irq = 0;
        break;
    }

    if (irq)
        qemu_irq_pulse(s->rxirq);

    switch ((s->spcr[1] >> 4) & 3) {			/* XINTM */
    case 0:
        irq = (s->spcr[1] >> 1) & 1;			/* XRDY */
        break;
    case 3:
        irq = (s->spcr[1] >> 3) & 1;			/* XSYNCERR */
        break;
    default:
        irq = 0;
        break;
    }

    if (irq)
        qemu_irq_pulse(s->txirq);
}

static void omap_mcbsp_rx_newdata(struct omap_mcbsp_s *s)
{
    if ((s->spcr[0] >> 1) & 1)				/* RRDY */
        s->spcr[0] |= 1 << 2;				/* RFULL */
    s->spcr[0] |= 1 << 1;				/* RRDY */
    qemu_irq_raise(s->rxdrq);
    omap_mcbsp_intr_update(s);
}

static void omap_mcbsp_source_tick(void *opaque)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    static const int bps[8] = { 0, 1, 1, 2, 2, 2, -255, -255 };

    if (!s->rx_rate)
        return;
    if (s->rx_req)
        printf("%s: Rx FIFO overrun\n", __FUNCTION__);

    s->rx_req = s->rx_rate << bps[(s->rcr[0] >> 5) & 7];

    omap_mcbsp_rx_newdata(s);
    timer_mod(s->source_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   get_ticks_per_sec());
}

static void omap_mcbsp_rx_start(struct omap_mcbsp_s *s)
{
    if (!s->codec || !s->codec->rts)
        omap_mcbsp_source_tick(s);
    else if (s->codec->in.len) {
        s->rx_req = s->codec->in.len;
        omap_mcbsp_rx_newdata(s);
    }
}

static void omap_mcbsp_rx_stop(struct omap_mcbsp_s *s)
{
    timer_del(s->source_timer);
}

static void omap_mcbsp_rx_done(struct omap_mcbsp_s *s)
{
    s->spcr[0] &= ~(1 << 1);				/* RRDY */
    qemu_irq_lower(s->rxdrq);
    omap_mcbsp_intr_update(s);
}

static void omap_mcbsp_tx_newdata(struct omap_mcbsp_s *s)
{
    s->spcr[1] |= 1 << 1;				/* XRDY */
    qemu_irq_raise(s->txdrq);
    omap_mcbsp_intr_update(s);
}

static void omap_mcbsp_sink_tick(void *opaque)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    static const int bps[8] = { 0, 1, 1, 2, 2, 2, -255, -255 };

    if (!s->tx_rate)
        return;
    if (s->tx_req)
        printf("%s: Tx FIFO underrun\n", __FUNCTION__);

    s->tx_req = s->tx_rate << bps[(s->xcr[0] >> 5) & 7];

    omap_mcbsp_tx_newdata(s);
    timer_mod(s->sink_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   get_ticks_per_sec());
}

static void omap_mcbsp_tx_start(struct omap_mcbsp_s *s)
{
    if (!s->codec || !s->codec->cts)
        omap_mcbsp_sink_tick(s);
    else if (s->codec->out.size) {
        s->tx_req = s->codec->out.size;
        omap_mcbsp_tx_newdata(s);
    }
}

static void omap_mcbsp_tx_done(struct omap_mcbsp_s *s)
{
    s->spcr[1] &= ~(1 << 1);				/* XRDY */
    qemu_irq_lower(s->txdrq);
    omap_mcbsp_intr_update(s);
    if (s->codec && s->codec->cts)
        s->codec->tx_swallow(s->codec->opaque);
}

static void omap_mcbsp_tx_stop(struct omap_mcbsp_s *s)
{
    s->tx_req = 0;
    omap_mcbsp_tx_done(s);
    timer_del(s->sink_timer);
}

static void omap_mcbsp_req_update(struct omap_mcbsp_s *s)
{
    int prev_rx_rate, prev_tx_rate;
    int rx_rate = 0, tx_rate = 0;
    int cpu_rate = 1500000;	/* XXX */

    /* TODO: check CLKSTP bit */
    if (s->spcr[1] & (1 << 6)) {			/* GRST */
        if (s->spcr[0] & (1 << 0)) {			/* RRST */
            if ((s->srgr[1] & (1 << 13)) &&		/* CLKSM */
                            (s->pcr & (1 << 8))) {	/* CLKRM */
                if (~s->pcr & (1 << 7))			/* SCLKME */
                    rx_rate = cpu_rate /
                            ((s->srgr[0] & 0xff) + 1);	/* CLKGDV */
            } else
                if (s->codec)
                    rx_rate = s->codec->rx_rate;
        }

        if (s->spcr[1] & (1 << 0)) {			/* XRST */
            if ((s->srgr[1] & (1 << 13)) &&		/* CLKSM */
                            (s->pcr & (1 << 9))) {	/* CLKXM */
                if (~s->pcr & (1 << 7))			/* SCLKME */
                    tx_rate = cpu_rate /
                            ((s->srgr[0] & 0xff) + 1);	/* CLKGDV */
            } else
                if (s->codec)
                    tx_rate = s->codec->tx_rate;
        }
    }
    prev_tx_rate = s->tx_rate;
    prev_rx_rate = s->rx_rate;
    s->tx_rate = tx_rate;
    s->rx_rate = rx_rate;

    if (s->codec)
        s->codec->set_rate(s->codec->opaque, rx_rate, tx_rate);

    if (!prev_tx_rate && tx_rate)
        omap_mcbsp_tx_start(s);
    else if (s->tx_rate && !tx_rate)
        omap_mcbsp_tx_stop(s);

    if (!prev_rx_rate && rx_rate)
        omap_mcbsp_rx_start(s);
    else if (prev_tx_rate && !tx_rate)
        omap_mcbsp_rx_stop(s);
}

static uint64_t omap_mcbsp_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* DRR2 */
        if (((s->rcr[0] >> 5) & 7) < 3)			/* RWDLEN1 */
            return 0x0000;
        /* Fall through.  */
    case 0x02:	/* DRR1 */
        if (s->rx_req < 2) {
            printf("%s: Rx FIFO underrun\n", __FUNCTION__);
            omap_mcbsp_rx_done(s);
        } else {
            s->tx_req -= 2;
            if (s->codec && s->codec->in.len >= 2) {
                ret = s->codec->in.fifo[s->codec->in.start ++] << 8;
                ret |= s->codec->in.fifo[s->codec->in.start ++];
                s->codec->in.len -= 2;
            } else
                ret = 0x0000;
            if (!s->tx_req)
                omap_mcbsp_rx_done(s);
            return ret;
        }
        return 0x0000;

    case 0x04:	/* DXR2 */
    case 0x06:	/* DXR1 */
        return 0x0000;

    case 0x08:	/* SPCR2 */
        return s->spcr[1];
    case 0x0a:	/* SPCR1 */
        return s->spcr[0];
    case 0x0c:	/* RCR2 */
        return s->rcr[1];
    case 0x0e:	/* RCR1 */
        return s->rcr[0];
    case 0x10:	/* XCR2 */
        return s->xcr[1];
    case 0x12:	/* XCR1 */
        return s->xcr[0];
    case 0x14:	/* SRGR2 */
        return s->srgr[1];
    case 0x16:	/* SRGR1 */
        return s->srgr[0];
    case 0x18:	/* MCR2 */
        return s->mcr[1];
    case 0x1a:	/* MCR1 */
        return s->mcr[0];
    case 0x1c:	/* RCERA */
        return s->rcer[0];
    case 0x1e:	/* RCERB */
        return s->rcer[1];
    case 0x20:	/* XCERA */
        return s->xcer[0];
    case 0x22:	/* XCERB */
        return s->xcer[1];
    case 0x24:	/* PCR0 */
        return s->pcr;
    case 0x26:	/* RCERC */
        return s->rcer[2];
    case 0x28:	/* RCERD */
        return s->rcer[3];
    case 0x2a:	/* XCERC */
        return s->xcer[2];
    case 0x2c:	/* XCERD */
        return s->xcer[3];
    case 0x2e:	/* RCERE */
        return s->rcer[4];
    case 0x30:	/* RCERF */
        return s->rcer[5];
    case 0x32:	/* XCERE */
        return s->xcer[4];
    case 0x34:	/* XCERF */
        return s->xcer[5];
    case 0x36:	/* RCERG */
        return s->rcer[6];
    case 0x38:	/* RCERH */
        return s->rcer[7];
    case 0x3a:	/* XCERG */
        return s->xcer[6];
    case 0x3c:	/* XCERH */
        return s->xcer[7];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mcbsp_writeh(void *opaque, hwaddr addr,
                uint32_t value)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x00:	/* DRR2 */
    case 0x02:	/* DRR1 */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* DXR2 */
        if (((s->xcr[0] >> 5) & 7) < 3)			/* XWDLEN1 */
            return;
        /* Fall through.  */
    case 0x06:	/* DXR1 */
        if (s->tx_req > 1) {
            s->tx_req -= 2;
            if (s->codec && s->codec->cts) {
                s->codec->out.fifo[s->codec->out.len ++] = (value >> 8) & 0xff;
                s->codec->out.fifo[s->codec->out.len ++] = (value >> 0) & 0xff;
            }
            if (s->tx_req < 2)
                omap_mcbsp_tx_done(s);
        } else
            printf("%s: Tx FIFO overrun\n", __FUNCTION__);
        return;

    case 0x08:	/* SPCR2 */
        s->spcr[1] &= 0x0002;
        s->spcr[1] |= 0x03f9 & value;
        s->spcr[1] |= 0x0004 & (value << 2);		/* XEMPTY := XRST */
        if (~value & 1)					/* XRST */
            s->spcr[1] &= ~6;
        omap_mcbsp_req_update(s);
        return;
    case 0x0a:	/* SPCR1 */
        s->spcr[0] &= 0x0006;
        s->spcr[0] |= 0xf8f9 & value;
        if (value & (1 << 15))				/* DLB */
            printf("%s: Digital Loopback mode enable attempt\n", __FUNCTION__);
        if (~value & 1) {				/* RRST */
            s->spcr[0] &= ~6;
            s->rx_req = 0;
            omap_mcbsp_rx_done(s);
        }
        omap_mcbsp_req_update(s);
        return;

    case 0x0c:	/* RCR2 */
        s->rcr[1] = value & 0xffff;
        return;
    case 0x0e:	/* RCR1 */
        s->rcr[0] = value & 0x7fe0;
        return;
    case 0x10:	/* XCR2 */
        s->xcr[1] = value & 0xffff;
        return;
    case 0x12:	/* XCR1 */
        s->xcr[0] = value & 0x7fe0;
        return;
    case 0x14:	/* SRGR2 */
        s->srgr[1] = value & 0xffff;
        omap_mcbsp_req_update(s);
        return;
    case 0x16:	/* SRGR1 */
        s->srgr[0] = value & 0xffff;
        omap_mcbsp_req_update(s);
        return;
    case 0x18:	/* MCR2 */
        s->mcr[1] = value & 0x03e3;
        if (value & 3)					/* XMCM */
            printf("%s: Tx channel selection mode enable attempt\n",
                            __FUNCTION__);
        return;
    case 0x1a:	/* MCR1 */
        s->mcr[0] = value & 0x03e1;
        if (value & 1)					/* RMCM */
            printf("%s: Rx channel selection mode enable attempt\n",
                            __FUNCTION__);
        return;
    case 0x1c:	/* RCERA */
        s->rcer[0] = value & 0xffff;
        return;
    case 0x1e:	/* RCERB */
        s->rcer[1] = value & 0xffff;
        return;
    case 0x20:	/* XCERA */
        s->xcer[0] = value & 0xffff;
        return;
    case 0x22:	/* XCERB */
        s->xcer[1] = value & 0xffff;
        return;
    case 0x24:	/* PCR0 */
        s->pcr = value & 0x7faf;
        return;
    case 0x26:	/* RCERC */
        s->rcer[2] = value & 0xffff;
        return;
    case 0x28:	/* RCERD */
        s->rcer[3] = value & 0xffff;
        return;
    case 0x2a:	/* XCERC */
        s->xcer[2] = value & 0xffff;
        return;
    case 0x2c:	/* XCERD */
        s->xcer[3] = value & 0xffff;
        return;
    case 0x2e:	/* RCERE */
        s->rcer[4] = value & 0xffff;
        return;
    case 0x30:	/* RCERF */
        s->rcer[5] = value & 0xffff;
        return;
    case 0x32:	/* XCERE */
        s->xcer[4] = value & 0xffff;
        return;
    case 0x34:	/* XCERF */
        s->xcer[5] = value & 0xffff;
        return;
    case 0x36:	/* RCERG */
        s->rcer[6] = value & 0xffff;
        return;
    case 0x38:	/* RCERH */
        s->rcer[7] = value & 0xffff;
        return;
    case 0x3a:	/* XCERG */
        s->xcer[6] = value & 0xffff;
        return;
    case 0x3c:	/* XCERH */
        s->xcer[7] = value & 0xffff;
        return;
    }

    OMAP_BAD_REG(addr);
}

static void omap_mcbsp_writew(void *opaque, hwaddr addr,
                uint32_t value)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (offset == 0x04) {				/* DXR */
        if (((s->xcr[0] >> 5) & 7) < 3)			/* XWDLEN1 */
            return;
        if (s->tx_req > 3) {
            s->tx_req -= 4;
            if (s->codec && s->codec->cts) {
                s->codec->out.fifo[s->codec->out.len ++] =
                        (value >> 24) & 0xff;
                s->codec->out.fifo[s->codec->out.len ++] =
                        (value >> 16) & 0xff;
                s->codec->out.fifo[s->codec->out.len ++] =
                        (value >> 8) & 0xff;
                s->codec->out.fifo[s->codec->out.len ++] =
                        (value >> 0) & 0xff;
            }
            if (s->tx_req < 4)
                omap_mcbsp_tx_done(s);
        } else
            printf("%s: Tx FIFO overrun\n", __FUNCTION__);
        return;
    }

    omap_badwidth_write16(opaque, addr, value);
}

static void omap_mcbsp_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    switch (size) {
    case 2: return omap_mcbsp_writeh(opaque, addr, value);
    case 4: return omap_mcbsp_writew(opaque, addr, value);
    default: return omap_badwidth_write16(opaque, addr, value);
    }
}

static const MemoryRegionOps omap_mcbsp_ops = {
    .read = omap_mcbsp_read,
    .write = omap_mcbsp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_mcbsp_reset(struct omap_mcbsp_s *s)
{
    memset(&s->spcr, 0, sizeof(s->spcr));
    memset(&s->rcr, 0, sizeof(s->rcr));
    memset(&s->xcr, 0, sizeof(s->xcr));
    s->srgr[0] = 0x0001;
    s->srgr[1] = 0x2000;
    memset(&s->mcr, 0, sizeof(s->mcr));
    memset(&s->pcr, 0, sizeof(s->pcr));
    memset(&s->rcer, 0, sizeof(s->rcer));
    memset(&s->xcer, 0, sizeof(s->xcer));
    s->tx_req = 0;
    s->rx_req = 0;
    s->tx_rate = 0;
    s->rx_rate = 0;
    timer_del(s->source_timer);
    timer_del(s->sink_timer);
}

static struct omap_mcbsp_s *omap_mcbsp_init(MemoryRegion *system_memory,
                                            hwaddr base,
                                            qemu_irq txirq, qemu_irq rxirq,
                                            qemu_irq *dma, omap_clk clk)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *)
            g_malloc0(sizeof(struct omap_mcbsp_s));

    s->txirq = txirq;
    s->rxirq = rxirq;
    s->txdrq = dma[0];
    s->rxdrq = dma[1];
    s->sink_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_mcbsp_sink_tick, s);
    s->source_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, omap_mcbsp_source_tick, s);
    omap_mcbsp_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_mcbsp_ops, s, "omap-mcbsp", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);

    return s;
}

static void omap_mcbsp_i2s_swallow(void *opaque, int line, int level)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;

    if (s->rx_rate) {
        s->rx_req = s->codec->in.len;
        omap_mcbsp_rx_newdata(s);
    }
}

static void omap_mcbsp_i2s_start(void *opaque, int line, int level)
{
    struct omap_mcbsp_s *s = (struct omap_mcbsp_s *) opaque;

    if (s->tx_rate) {
        s->tx_req = s->codec->out.size;
        omap_mcbsp_tx_newdata(s);
    }
}

void omap_mcbsp_i2s_attach(struct omap_mcbsp_s *s, I2SCodec *slave)
{
    s->codec = slave;
    slave->rx_swallow = qemu_allocate_irq(omap_mcbsp_i2s_swallow, s, 0);
    slave->tx_start = qemu_allocate_irq(omap_mcbsp_i2s_start, s, 0);
}

/* LED Pulse Generators */
struct omap_lpg_s {
    MemoryRegion iomem;
    QEMUTimer *tm;

    uint8_t control;
    uint8_t power;
    int64_t on;
    int64_t period;
    int clk;
    int cycle;
};

static void omap_lpg_tick(void *opaque)
{
    struct omap_lpg_s *s = opaque;

    if (s->cycle)
        timer_mod(s->tm, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->period - s->on);
    else
        timer_mod(s->tm, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->on);

    s->cycle = !s->cycle;
    printf("%s: LED is %s\n", __FUNCTION__, s->cycle ? "on" : "off");
}

static void omap_lpg_update(struct omap_lpg_s *s)
{
    int64_t on, period = 1, ticks = 1000;
    static const int per[8] = { 1, 2, 4, 8, 12, 16, 20, 24 };

    if (~s->control & (1 << 6))					/* LPGRES */
        on = 0;
    else if (s->control & (1 << 7))				/* PERM_ON */
        on = period;
    else {
        period = muldiv64(ticks, per[s->control & 7],		/* PERCTRL */
                        256 / 32);
        on = (s->clk && s->power) ? muldiv64(ticks,
                        per[(s->control >> 3) & 7], 256) : 0;	/* ONCTRL */
    }

    timer_del(s->tm);
    if (on == period && s->on < s->period)
        printf("%s: LED is on\n", __FUNCTION__);
    else if (on == 0 && s->on)
        printf("%s: LED is off\n", __FUNCTION__);
    else if (on && (on != s->on || period != s->period)) {
        s->cycle = 0;
        s->on = on;
        s->period = period;
        omap_lpg_tick(s);
        return;
    }

    s->on = on;
    s->period = period;
}

static void omap_lpg_reset(struct omap_lpg_s *s)
{
    s->control = 0x00;
    s->power = 0x00;
    s->clk = 1;
    omap_lpg_update(s);
}

static uint64_t omap_lpg_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    struct omap_lpg_s *s = (struct omap_lpg_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_read8(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* LCR */
        return s->control;

    case 0x04:	/* PMR */
        return s->power;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_lpg_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_lpg_s *s = (struct omap_lpg_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 1) {
        return omap_badwidth_write8(opaque, addr, value);
    }

    switch (offset) {
    case 0x00:	/* LCR */
        if (~value & (1 << 6))					/* LPGRES */
            omap_lpg_reset(s);
        s->control = value & 0xff;
        omap_lpg_update(s);
        return;

    case 0x04:	/* PMR */
        s->power = value & 0x01;
        omap_lpg_update(s);
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_lpg_ops = {
    .read = omap_lpg_read,
    .write = omap_lpg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_lpg_clk_update(void *opaque, int line, int on)
{
    struct omap_lpg_s *s = (struct omap_lpg_s *) opaque;

    s->clk = on;
    omap_lpg_update(s);
}

static struct omap_lpg_s *omap_lpg_init(MemoryRegion *system_memory,
                                        hwaddr base, omap_clk clk)
{
    struct omap_lpg_s *s = (struct omap_lpg_s *)
            g_malloc0(sizeof(struct omap_lpg_s));

    s->tm = timer_new_ms(QEMU_CLOCK_VIRTUAL, omap_lpg_tick, s);

    omap_lpg_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_lpg_ops, s, "omap-lpg", 0x800);
    memory_region_add_subregion(system_memory, base, &s->iomem);

    omap_clk_adduser(clk, qemu_allocate_irq(omap_lpg_clk_update, s, 0));

    return s;
}

/* MPUI Peripheral Bridge configuration */
static uint64_t omap_mpui_io_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    if (addr == OMAP_MPUI_BASE)	/* CMR */
        return 0xfe4d;

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mpui_io_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    /* FIXME: infinite loop */
    omap_badwidth_write16(opaque, addr, value);
}

static const MemoryRegionOps omap_mpui_io_ops = {
    .read = omap_mpui_io_read,
    .write = omap_mpui_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_setup_mpui_io(MemoryRegion *system_memory,
                               struct omap_mpu_state_s *mpu)
{
    memory_region_init_io(&mpu->mpui_io_iomem, NULL, &omap_mpui_io_ops, mpu,
                          "omap-mpui-io", 0x7fff);
    memory_region_add_subregion(system_memory, OMAP_MPUI_BASE,
                                &mpu->mpui_io_iomem);
}

/* General chip reset */
static void omap1_mpu_reset(void *opaque)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;

    omap_dma_reset(mpu->dma);
    omap_mpu_timer_reset(mpu->timer[0]);
    omap_mpu_timer_reset(mpu->timer[1]);
    omap_mpu_timer_reset(mpu->timer[2]);
    omap_wd_timer_reset(mpu->wdt);
    omap_os_timer_reset(mpu->os_timer);
    omap_lcdc_reset(mpu->lcd);
    omap_ulpd_pm_reset(mpu);
    omap_pin_cfg_reset(mpu);
    omap_mpui_reset(mpu);
    omap_tipb_bridge_reset(mpu->private_tipb);
    omap_tipb_bridge_reset(mpu->public_tipb);
    omap_dpll_reset(mpu->dpll[0]);
    omap_dpll_reset(mpu->dpll[1]);
    omap_dpll_reset(mpu->dpll[2]);
    omap_uart_reset(mpu->uart[0]);
    omap_uart_reset(mpu->uart[1]);
    omap_uart_reset(mpu->uart[2]);
    omap_mmc_reset(mpu->mmc);
    omap_mpuio_reset(mpu->mpuio);
    omap_uwire_reset(mpu->microwire);
    omap_pwl_reset(mpu->pwl);
    omap_pwt_reset(mpu->pwt);
    omap_rtc_reset(mpu->rtc);
    omap_mcbsp_reset(mpu->mcbsp1);
    omap_mcbsp_reset(mpu->mcbsp2);
    omap_mcbsp_reset(mpu->mcbsp3);
    omap_lpg_reset(mpu->led[0]);
    omap_lpg_reset(mpu->led[1]);
    omap_clkm_reset(mpu);
    cpu_reset(CPU(mpu->cpu));
}

static const struct omap_map_s {
    hwaddr phys_dsp;
    hwaddr phys_mpu;
    uint32_t size;
    const char *name;
} omap15xx_dsp_mm[] = {
    /* Strobe 0 */
    { 0xe1010000, 0xfffb0000, 0x800, "UART1 BT" },		/* CS0 */
    { 0xe1010800, 0xfffb0800, 0x800, "UART2 COM" },		/* CS1 */
    { 0xe1011800, 0xfffb1800, 0x800, "McBSP1 audio" },		/* CS3 */
    { 0xe1012000, 0xfffb2000, 0x800, "MCSI2 communication" },	/* CS4 */
    { 0xe1012800, 0xfffb2800, 0x800, "MCSI1 BT u-Law" },	/* CS5 */
    { 0xe1013000, 0xfffb3000, 0x800, "uWire" },			/* CS6 */
    { 0xe1013800, 0xfffb3800, 0x800, "I^2C" },			/* CS7 */
    { 0xe1014000, 0xfffb4000, 0x800, "USB W2FC" },		/* CS8 */
    { 0xe1014800, 0xfffb4800, 0x800, "RTC" },			/* CS9 */
    { 0xe1015000, 0xfffb5000, 0x800, "MPUIO" },			/* CS10 */
    { 0xe1015800, 0xfffb5800, 0x800, "PWL" },			/* CS11 */
    { 0xe1016000, 0xfffb6000, 0x800, "PWT" },			/* CS12 */
    { 0xe1017000, 0xfffb7000, 0x800, "McBSP3" },		/* CS14 */
    { 0xe1017800, 0xfffb7800, 0x800, "MMC" },			/* CS15 */
    { 0xe1019000, 0xfffb9000, 0x800, "32-kHz timer" },		/* CS18 */
    { 0xe1019800, 0xfffb9800, 0x800, "UART3" },			/* CS19 */
    { 0xe101c800, 0xfffbc800, 0x800, "TIPB switches" },		/* CS25 */
    /* Strobe 1 */
    { 0xe101e000, 0xfffce000, 0x800, "GPIOs" },			/* CS28 */

    { 0 }
};

static void omap_setup_dsp_mapping(MemoryRegion *system_memory,
                                   const struct omap_map_s *map)
{
    MemoryRegion *io;

    for (; map->phys_dsp; map ++) {
        io = g_new(MemoryRegion, 1);
        memory_region_init_alias(io, NULL, map->name,
                                 system_memory, map->phys_mpu, map->size);
        memory_region_add_subregion(system_memory, map->phys_dsp, io);
    }
}

void omap_mpu_wakeup(void *opaque, int irq, int req)
{
    struct omap_mpu_state_s *mpu = (struct omap_mpu_state_s *) opaque;
    CPUState *cpu = CPU(mpu->cpu);

    if (cpu->halted) {
        cpu_interrupt(cpu, CPU_INTERRUPT_EXITTB);
    }
}

static const struct dma_irq_map omap1_dma_irq_map[] = {
    { 0, OMAP_INT_DMA_CH0_6 },
    { 0, OMAP_INT_DMA_CH1_7 },
    { 0, OMAP_INT_DMA_CH2_8 },
    { 0, OMAP_INT_DMA_CH3 },
    { 0, OMAP_INT_DMA_CH4 },
    { 0, OMAP_INT_DMA_CH5 },
    { 1, OMAP_INT_1610_DMA_CH6 },
    { 1, OMAP_INT_1610_DMA_CH7 },
    { 1, OMAP_INT_1610_DMA_CH8 },
    { 1, OMAP_INT_1610_DMA_CH9 },
    { 1, OMAP_INT_1610_DMA_CH10 },
    { 1, OMAP_INT_1610_DMA_CH11 },
    { 1, OMAP_INT_1610_DMA_CH12 },
    { 1, OMAP_INT_1610_DMA_CH13 },
    { 1, OMAP_INT_1610_DMA_CH14 },
    { 1, OMAP_INT_1610_DMA_CH15 }
};

/* DMA ports for OMAP1 */
static int omap_validate_emiff_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(OMAP_EMIFF_BASE, s->sdram_size, addr);
}

static int omap_validate_emifs_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(OMAP_EMIFS_BASE, OMAP_EMIFF_BASE - OMAP_EMIFS_BASE,
                             addr);
}

static int omap_validate_imif_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(OMAP_IMIF_BASE, s->sram_size, addr);
}

static int omap_validate_tipb_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(0xfffb0000, 0xffff0000 - 0xfffb0000, addr);
}

static int omap_validate_local_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(OMAP_LOCALBUS_BASE, 0x1000000, addr);
}

static int omap_validate_tipb_mpui_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return range_covers_byte(0xe1010000, 0xe1020004 - 0xe1010000, addr);
}

struct omap_mpu_state_s *omap310_mpu_init(MemoryRegion *system_memory,
                unsigned long sdram_size,
                const char *core)
{
    int i;
    struct omap_mpu_state_s *s = (struct omap_mpu_state_s *)
            g_malloc0(sizeof(struct omap_mpu_state_s));
    qemu_irq dma_irqs[6];
    DriveInfo *dinfo;
    SysBusDevice *busdev;

    if (!core)
        core = "ti925t";

    /* Core */
    s->mpu_model = omap310;
    s->cpu = cpu_arm_init(core);
    if (s->cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    s->sdram_size = sdram_size;
    s->sram_size = OMAP15XX_SRAM_SIZE;

    s->wakeup = qemu_allocate_irq(omap_mpu_wakeup, s, 0);

    /* Clocks */
    omap_clk_init(s);

    /* Memory-mapped stuff */
    memory_region_init_ram(&s->emiff_ram, NULL, "omap1.dram", s->sdram_size);
    vmstate_register_ram_global(&s->emiff_ram);
    memory_region_add_subregion(system_memory, OMAP_EMIFF_BASE, &s->emiff_ram);
    memory_region_init_ram(&s->imif_ram, NULL, "omap1.sram", s->sram_size);
    vmstate_register_ram_global(&s->imif_ram);
    memory_region_add_subregion(system_memory, OMAP_IMIF_BASE, &s->imif_ram);

    omap_clkm_init(system_memory, 0xfffece00, 0xe1008000, s);

    s->ih[0] = qdev_create(NULL, "omap-intc");
    qdev_prop_set_uint32(s->ih[0], "size", 0x100);
    qdev_prop_set_ptr(s->ih[0], "clk", omap_findclk(s, "arminth_ck"));
    qdev_init_nofail(s->ih[0]);
    busdev = SYS_BUS_DEVICE(s->ih[0]);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(busdev, 1,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
    sysbus_mmio_map(busdev, 0, 0xfffecb00);
    s->ih[1] = qdev_create(NULL, "omap-intc");
    qdev_prop_set_uint32(s->ih[1], "size", 0x800);
    qdev_prop_set_ptr(s->ih[1], "clk", omap_findclk(s, "arminth_ck"));
    qdev_init_nofail(s->ih[1]);
    busdev = SYS_BUS_DEVICE(s->ih[1]);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_15XX_IH2_IRQ));
    /* The second interrupt controller's FIQ output is not wired up */
    sysbus_mmio_map(busdev, 0, 0xfffe0000);

    for (i = 0; i < 6; i++) {
        dma_irqs[i] = qdev_get_gpio_in(s->ih[omap1_dma_irq_map[i].ih],
                                       omap1_dma_irq_map[i].intr);
    }
    s->dma = omap_dma_init(0xfffed800, dma_irqs, system_memory,
                           qdev_get_gpio_in(s->ih[0], OMAP_INT_DMA_LCD),
                           s, omap_findclk(s, "dma_ck"), omap_dma_3_1);

    s->port[emiff    ].addr_valid = omap_validate_emiff_addr;
    s->port[emifs    ].addr_valid = omap_validate_emifs_addr;
    s->port[imif     ].addr_valid = omap_validate_imif_addr;
    s->port[tipb     ].addr_valid = omap_validate_tipb_addr;
    s->port[local    ].addr_valid = omap_validate_local_addr;
    s->port[tipb_mpui].addr_valid = omap_validate_tipb_mpui_addr;

    /* Register SDRAM and SRAM DMA ports for fast transfers.  */
    soc_dma_port_add_mem(s->dma, memory_region_get_ram_ptr(&s->emiff_ram),
                         OMAP_EMIFF_BASE, s->sdram_size);
    soc_dma_port_add_mem(s->dma, memory_region_get_ram_ptr(&s->imif_ram),
                         OMAP_IMIF_BASE, s->sram_size);

    s->timer[0] = omap_mpu_timer_init(system_memory, 0xfffec500,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_TIMER1),
                    omap_findclk(s, "mputim_ck"));
    s->timer[1] = omap_mpu_timer_init(system_memory, 0xfffec600,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_TIMER2),
                    omap_findclk(s, "mputim_ck"));
    s->timer[2] = omap_mpu_timer_init(system_memory, 0xfffec700,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_TIMER3),
                    omap_findclk(s, "mputim_ck"));

    s->wdt = omap_wd_timer_init(system_memory, 0xfffec800,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_WD_TIMER),
                    omap_findclk(s, "armwdt_ck"));

    s->os_timer = omap_os_timer_init(system_memory, 0xfffb9000,
                    qdev_get_gpio_in(s->ih[1], OMAP_INT_OS_TIMER),
                    omap_findclk(s, "clk32-kHz"));

    s->lcd = omap_lcdc_init(system_memory, 0xfffec000,
                            qdev_get_gpio_in(s->ih[0], OMAP_INT_LCD_CTRL),
                            omap_dma_get_lcdch(s->dma),
                            omap_findclk(s, "lcd_ck"));

    omap_ulpd_pm_init(system_memory, 0xfffe0800, s);
    omap_pin_cfg_init(system_memory, 0xfffe1000, s);
    omap_id_init(system_memory, s);

    omap_mpui_init(system_memory, 0xfffec900, s);

    s->private_tipb = omap_tipb_bridge_init(system_memory, 0xfffeca00,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_BRIDGE_PRIV),
                    omap_findclk(s, "tipb_ck"));
    s->public_tipb = omap_tipb_bridge_init(system_memory, 0xfffed300,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_BRIDGE_PUB),
                    omap_findclk(s, "tipb_ck"));

    omap_tcmi_init(system_memory, 0xfffecc00, s);

    s->uart[0] = omap_uart_init(0xfffb0000,
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_UART1),
                    omap_findclk(s, "uart1_ck"),
                    omap_findclk(s, "uart1_ck"),
                    s->drq[OMAP_DMA_UART1_TX], s->drq[OMAP_DMA_UART1_RX],
                    "uart1",
                    serial_hds[0]);
    s->uart[1] = omap_uart_init(0xfffb0800,
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_UART2),
                    omap_findclk(s, "uart2_ck"),
                    omap_findclk(s, "uart2_ck"),
                    s->drq[OMAP_DMA_UART2_TX], s->drq[OMAP_DMA_UART2_RX],
                    "uart2",
                    serial_hds[0] ? serial_hds[1] : NULL);
    s->uart[2] = omap_uart_init(0xfffb9800,
                                qdev_get_gpio_in(s->ih[0], OMAP_INT_UART3),
                    omap_findclk(s, "uart3_ck"),
                    omap_findclk(s, "uart3_ck"),
                    s->drq[OMAP_DMA_UART3_TX], s->drq[OMAP_DMA_UART3_RX],
                    "uart3",
                    serial_hds[0] && serial_hds[1] ? serial_hds[2] : NULL);

    s->dpll[0] = omap_dpll_init(system_memory, 0xfffecf00,
                                omap_findclk(s, "dpll1"));
    s->dpll[1] = omap_dpll_init(system_memory, 0xfffed000,
                                omap_findclk(s, "dpll2"));
    s->dpll[2] = omap_dpll_init(system_memory, 0xfffed100,
                                omap_findclk(s, "dpll3"));

    dinfo = drive_get(IF_SD, 0, 0);
    if (!dinfo) {
        fprintf(stderr, "qemu: missing SecureDigital device\n");
        exit(1);
    }
    s->mmc = omap_mmc_init(0xfffb7800, system_memory, dinfo->bdrv,
                           qdev_get_gpio_in(s->ih[1], OMAP_INT_OQN),
                           &s->drq[OMAP_DMA_MMC_TX],
                    omap_findclk(s, "mmc_ck"));

    s->mpuio = omap_mpuio_init(system_memory, 0xfffb5000,
                               qdev_get_gpio_in(s->ih[1], OMAP_INT_KEYBOARD),
                               qdev_get_gpio_in(s->ih[1], OMAP_INT_MPUIO),
                               s->wakeup, omap_findclk(s, "clk32-kHz"));

    s->gpio = qdev_create(NULL, "omap-gpio");
    qdev_prop_set_int32(s->gpio, "mpu_model", s->mpu_model);
    qdev_prop_set_ptr(s->gpio, "clk", omap_findclk(s, "arm_gpio_ck"));
    qdev_init_nofail(s->gpio);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->gpio), 0,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_GPIO_BANK1));
    sysbus_mmio_map(SYS_BUS_DEVICE(s->gpio), 0, 0xfffce000);

    s->microwire = omap_uwire_init(system_memory, 0xfffb3000,
                                   qdev_get_gpio_in(s->ih[1], OMAP_INT_uWireTX),
                                   qdev_get_gpio_in(s->ih[1], OMAP_INT_uWireRX),
                    s->drq[OMAP_DMA_UWIRE_TX], omap_findclk(s, "mpuper_ck"));

    s->pwl = omap_pwl_init(system_memory, 0xfffb5800,
                           omap_findclk(s, "armxor_ck"));
    s->pwt = omap_pwt_init(system_memory, 0xfffb6000,
                           omap_findclk(s, "armxor_ck"));

    s->i2c[0] = qdev_create(NULL, "omap_i2c");
    qdev_prop_set_uint8(s->i2c[0], "revision", 0x11);
    qdev_prop_set_ptr(s->i2c[0], "fclk", omap_findclk(s, "mpuper_ck"));
    qdev_init_nofail(s->i2c[0]);
    busdev = SYS_BUS_DEVICE(s->i2c[0]);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(s->ih[1], OMAP_INT_I2C));
    sysbus_connect_irq(busdev, 1, s->drq[OMAP_DMA_I2C_TX]);
    sysbus_connect_irq(busdev, 2, s->drq[OMAP_DMA_I2C_RX]);
    sysbus_mmio_map(busdev, 0, 0xfffb3800);

    s->rtc = omap_rtc_init(system_memory, 0xfffb4800,
                           qdev_get_gpio_in(s->ih[1], OMAP_INT_RTC_TIMER),
                           qdev_get_gpio_in(s->ih[1], OMAP_INT_RTC_ALARM),
                    omap_findclk(s, "clk32-kHz"));

    s->mcbsp1 = omap_mcbsp_init(system_memory, 0xfffb1800,
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_McBSP1TX),
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_McBSP1RX),
                    &s->drq[OMAP_DMA_MCBSP1_TX], omap_findclk(s, "dspxor_ck"));
    s->mcbsp2 = omap_mcbsp_init(system_memory, 0xfffb1000,
                                qdev_get_gpio_in(s->ih[0],
                                                 OMAP_INT_310_McBSP2_TX),
                                qdev_get_gpio_in(s->ih[0],
                                                 OMAP_INT_310_McBSP2_RX),
                    &s->drq[OMAP_DMA_MCBSP2_TX], omap_findclk(s, "mpuper_ck"));
    s->mcbsp3 = omap_mcbsp_init(system_memory, 0xfffb7000,
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_McBSP3TX),
                                qdev_get_gpio_in(s->ih[1], OMAP_INT_McBSP3RX),
                    &s->drq[OMAP_DMA_MCBSP3_TX], omap_findclk(s, "dspxor_ck"));

    s->led[0] = omap_lpg_init(system_memory,
                              0xfffbd000, omap_findclk(s, "clk32-kHz"));
    s->led[1] = omap_lpg_init(system_memory,
                              0xfffbd800, omap_findclk(s, "clk32-kHz"));

    /* Register mappings not currenlty implemented:
     * MCSI2 Comm	fffb2000 - fffb27ff (not mapped on OMAP310)
     * MCSI1 Bluetooth	fffb2800 - fffb2fff (not mapped on OMAP310)
     * USB W2FC		fffb4000 - fffb47ff
     * Camera Interface	fffb6800 - fffb6fff
     * USB Host		fffba000 - fffba7ff
     * FAC		fffba800 - fffbafff
     * HDQ/1-Wire	fffbc000 - fffbc7ff
     * TIPB switches	fffbc800 - fffbcfff
     * Mailbox		fffcf000 - fffcf7ff
     * Local bus IF	fffec100 - fffec1ff
     * Local bus MMU	fffec200 - fffec2ff
     * DSP MMU		fffed200 - fffed2ff
     */

    omap_setup_dsp_mapping(system_memory, omap15xx_dsp_mm);
    omap_setup_mpui_io(system_memory, s);

    qemu_register_reset(omap1_mpu_reset, s);

    return s;
}
