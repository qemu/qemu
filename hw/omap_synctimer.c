/*
 * TI OMAP2 32kHz sync timer emulation.
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
#include "hw.h"
#include "qemu-timer.h"
#include "omap.h"
struct omap_synctimer_s {
    uint32_t val;
    uint16_t readh;
};

/* 32-kHz Sync Timer of the OMAP2 */
static uint32_t omap_synctimer_read(struct omap_synctimer_s *s) {
    return muldiv64(qemu_get_clock_ns(vm_clock), 0x8000, get_ticks_per_sec());
}

void omap_synctimer_reset(struct omap_synctimer_s *s)
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

static CPUReadMemoryFunc * const omap_synctimer_readfn[] = {
    omap_badwidth_read32,
    omap_synctimer_readh,
    omap_synctimer_readw,
};

static void omap_synctimer_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    OMAP_BAD_REG(addr);
}

static CPUWriteMemoryFunc * const omap_synctimer_writefn[] = {
    omap_badwidth_write32,
    omap_synctimer_write,
    omap_synctimer_write,
};

struct omap_synctimer_s *omap_synctimer_init(struct omap_target_agent_s *ta,
                struct omap_mpu_state_s *mpu, omap_clk fclk, omap_clk iclk)
{
    struct omap_synctimer_s *s = g_malloc0(sizeof(*s));

    omap_synctimer_reset(s);
    omap_l4_attach(ta, 0, l4_register_io_memory(
                      omap_synctimer_readfn, omap_synctimer_writefn, s));

    return s;
}
