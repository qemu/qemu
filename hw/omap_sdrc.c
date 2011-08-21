/*
 * TI OMAP SDRAM controller emulation.
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
#include "omap.h"

/* SDRAM Controller Subsystem */
struct omap_sdrc_s {
    uint8_t config;
};

void omap_sdrc_reset(struct omap_sdrc_s *s)
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

static CPUReadMemoryFunc * const omap_sdrc_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap_sdrc_read,
};

static CPUWriteMemoryFunc * const omap_sdrc_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap_sdrc_write,
};

struct omap_sdrc_s *omap_sdrc_init(target_phys_addr_t base)
{
    int iomemtype;
    struct omap_sdrc_s *s = (struct omap_sdrc_s *)
            g_malloc0(sizeof(struct omap_sdrc_s));

    omap_sdrc_reset(s);

    iomemtype = cpu_register_io_memory(omap_sdrc_readfn,
                    omap_sdrc_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x1000, iomemtype);

    return s;
}
