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
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/arm/omap.h"
struct omap_synctimer_s {
    MemoryRegion iomem;
    uint32_t val;
    uint16_t readh;
};

/* 32-kHz Sync Timer of the OMAP2 */
static uint32_t omap_synctimer_read(struct omap_synctimer_s *s) {
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), 0x8000,
                    NANOSECONDS_PER_SECOND);
}

void omap_synctimer_reset(struct omap_synctimer_s *s)
{
    s->val = omap_synctimer_read(s);
}

static uint32_t omap_synctimer_readw(void *opaque, hwaddr addr)
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

static uint32_t omap_synctimer_readh(void *opaque, hwaddr addr)
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

static uint64_t omap_synctimer_readfn(void *opaque, hwaddr addr,
                                      unsigned size)
{
    switch (size) {
    case 1:
        return omap_badwidth_read32(opaque, addr);
    case 2:
        return omap_synctimer_readh(opaque, addr);
    case 4:
        return omap_synctimer_readw(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void omap_synctimer_writefn(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    OMAP_BAD_REG(addr);
}

static const MemoryRegionOps omap_synctimer_ops = {
    .read = omap_synctimer_readfn,
    .write = omap_synctimer_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_synctimer_s *omap_synctimer_init(struct omap_target_agent_s *ta,
                struct omap_mpu_state_s *mpu, omap_clk fclk, omap_clk iclk)
{
    struct omap_synctimer_s *s = g_malloc0(sizeof(*s));

    omap_synctimer_reset(s);
    memory_region_init_io(&s->iomem, NULL, &omap_synctimer_ops, s, "omap.synctimer",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    return s;
}
