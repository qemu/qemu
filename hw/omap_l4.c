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
#include "hw.h"
#include "omap.h"

int l4_register_io_memory(CPUReadMemoryFunc * const *mem_read,
                          CPUWriteMemoryFunc * const *mem_write,
                          void *opaque)
{
    return cpu_register_io_memory(mem_read, mem_write, opaque,
                                  DEVICE_NATIVE_ENDIAN);
}

struct omap_l4_s {
    target_phys_addr_t base;
    int ta_num;
    struct omap_target_agent_s ta[0];
};

struct omap_l4_s *omap_l4_init(target_phys_addr_t base, int ta_num)
{
    struct omap_l4_s *bus = g_malloc0(
                    sizeof(*bus) + ta_num * sizeof(*bus->ta));

    bus->ta_num = ta_num;
    bus->base = base;

    return bus;
}

target_phys_addr_t omap_l4_region_base(struct omap_target_agent_s *ta,
                                       int region)
{
    return ta->bus->base + ta->start[region].offset;
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

static CPUReadMemoryFunc * const omap_l4ta_readfn[] = {
    omap_badwidth_read16,
    omap_l4ta_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc * const omap_l4ta_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_l4ta_write,
};

struct omap_target_agent_s *omap_l4ta_get(struct omap_l4_s *bus,
        const struct omap_l4_region_s *regions,
	const struct omap_l4_agent_info_s *agents,
	int cs)
{
    int i, iomemtype;
    struct omap_target_agent_s *ta = NULL;
    const struct omap_l4_agent_info_s *info = NULL;

    for (i = 0; i < bus->ta_num; i ++)
        if (agents[i].ta == cs) {
            ta = &bus->ta[i];
            info = &agents[i];
            break;
        }
    if (!ta) {
        fprintf(stderr, "%s: bad target agent (%i)\n", __FUNCTION__, cs);
        exit(-1);
    }

    ta->bus = bus;
    ta->start = &regions[info->region];
    ta->regions = info->regions;

    ta->component = ('Q' << 24) | ('E' << 16) | ('M' << 8) | ('U' << 0);
    ta->status = 0x00000000;
    ta->control = 0x00000200;	/* XXX 01000200 for L4TAO */

    iomemtype = l4_register_io_memory(omap_l4ta_readfn,
                    omap_l4ta_writefn, ta);
    ta->base = omap_l4_attach(ta, info->ta_region, iomemtype);

    return ta;
}

target_phys_addr_t omap_l4_attach(struct omap_target_agent_s *ta, int region,
                int iotype)
{
    target_phys_addr_t base;
    ssize_t size;

    if (region < 0 || region >= ta->regions) {
        fprintf(stderr, "%s: bad io region (%i)\n", __FUNCTION__, region);
        exit(-1);
    }

    base = ta->bus->base + ta->start[region].offset;
    size = ta->start[region].size;
    if (iotype) {
        cpu_register_physical_memory(base, size, iotype);
    }

    return base;
}
