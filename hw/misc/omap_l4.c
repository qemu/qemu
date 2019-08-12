/*
 * TI OMAP L4 interconnect emulation.
 *
 * Copyright (C) 2007-2009 Nokia Corporation
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
#include "hw/arm/omap.h"

struct omap_l4_s {
    MemoryRegion *address_space;
    hwaddr base;
    int ta_num;
    struct omap_target_agent_s ta[0];
};

struct omap_l4_s *omap_l4_init(MemoryRegion *address_space,
                               hwaddr base, int ta_num)
{
    struct omap_l4_s *bus = g_malloc0(
                    sizeof(*bus) + ta_num * sizeof(*bus->ta));

    bus->address_space = address_space;
    bus->ta_num = ta_num;
    bus->base = base;

    return bus;
}

hwaddr omap_l4_region_base(struct omap_target_agent_s *ta,
                                       int region)
{
    return ta->bus->base + ta->start[region].offset;
}

hwaddr omap_l4_region_size(struct omap_target_agent_s *ta,
                                       int region)
{
    return ta->start[region].size;
}

static uint64_t omap_l4ta_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

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

static void omap_l4ta_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_target_agent_s *s = (struct omap_target_agent_s *) opaque;

    if (size != 4) {
        omap_badwidth_write32(opaque, addr, value);
        return;
    }

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

static const MemoryRegionOps omap_l4ta_ops = {
    .read = omap_l4ta_read,
    .write = omap_l4ta_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_target_agent_s *omap_l4ta_get(struct omap_l4_s *bus,
        const struct omap_l4_region_s *regions,
        const struct omap_l4_agent_info_s *agents,
        int cs)
{
    int i;
    struct omap_target_agent_s *ta = NULL;
    const struct omap_l4_agent_info_s *info = NULL;

    for (i = 0; i < bus->ta_num; i ++)
        if (agents[i].ta == cs) {
            ta = &bus->ta[i];
            info = &agents[i];
            break;
        }
    if (!ta) {
        fprintf(stderr, "%s: bad target agent (%i)\n", __func__, cs);
        exit(-1);
    }

    ta->bus = bus;
    ta->start = &regions[info->region];
    ta->regions = info->regions;

    ta->component = ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    ta->status = 0x00000000;
    ta->control = 0x00000200;	/* XXX 01000200 for L4TAO */

    memory_region_init_io(&ta->iomem, NULL, &omap_l4ta_ops, ta, "omap.l4ta",
                          omap_l4_region_size(ta, info->ta_region));
    omap_l4_attach(ta, info->ta_region, &ta->iomem);

    return ta;
}

hwaddr omap_l4_attach(struct omap_target_agent_s *ta,
                                         int region, MemoryRegion *mr)
{
    hwaddr base;

    if (region < 0 || region >= ta->regions) {
        fprintf(stderr, "%s: bad io region (%i)\n", __func__, region);
        exit(-1);
    }

    base = ta->bus->base + ta->start[region].offset;
    if (mr) {
        memory_region_add_subregion(ta->bus->address_space, base, mr);
    }

    return base;
}
