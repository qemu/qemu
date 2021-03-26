/*
 * Microchip PolarFire SoC IOSCB module emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
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

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/misc/mchp_pfsoc_ioscb.h"

/*
 * The whole IOSCB module registers map into the system address at 0x3000_0000,
 * named as "System Port 0 (AXI-D0)".
 */
#define IOSCB_WHOLE_REG_SIZE        0x10000000
#define IOSCB_SUBMOD_REG_SIZE       0x1000

/*
 * There are many sub-modules in the IOSCB module.
 * See Microchip PolarFire SoC documentation (Register_Map.zip),
 * Register Map/PF_SoC_RegMap_V1_1/MPFS250T/mpfs250t_ioscb_memmap_dri.htm
 *
 * The following are sub-modules offsets that are of concern.
 */
#define IOSCB_LANE01_BASE           0x06500000
#define IOSCB_LANE23_BASE           0x06510000
#define IOSCB_CTRL_BASE             0x07020000
#define IOSCB_CFG_BASE              0x07080000
#define IOSCB_PLL_MSS_BASE          0x0E001000
#define IOSCB_CFM_MSS_BASE          0x0E002000
#define IOSCB_PLL_DDR_BASE          0x0E010000
#define IOSCB_BC_DDR_BASE           0x0E020000
#define IOSCB_IO_CALIB_DDR_BASE     0x0E040000
#define IOSCB_PLL_SGMII_BASE        0x0E080000
#define IOSCB_DLL_SGMII_BASE        0x0E100000
#define IOSCB_CFM_SGMII_BASE        0x0E200000
#define IOSCB_BC_SGMII_BASE         0x0E400000
#define IOSCB_IO_CALIB_SGMII_BASE   0x0E800000

static uint64_t mchp_pfsoc_dummy_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                  "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, offset);

    return 0;
}

static void mchp_pfsoc_dummy_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_dummy_ops = {
    .read = mchp_pfsoc_dummy_read,
    .write = mchp_pfsoc_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* All PLL modules in IOSCB have the same register layout */

#define PLL_CTRL    0x04

static uint64_t mchp_pfsoc_pll_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case PLL_CTRL:
        /* PLL is locked */
        val = BIT(25);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static const MemoryRegionOps mchp_pfsoc_pll_ops = {
    .read = mchp_pfsoc_pll_read,
    .write = mchp_pfsoc_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* IO_CALIB_DDR submodule */

#define IO_CALIB_DDR_IOC_REG1   0x08

static uint64_t mchp_pfsoc_io_calib_ddr_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case IO_CALIB_DDR_IOC_REG1:
        /* calibration completed */
        val = BIT(2);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static const MemoryRegionOps mchp_pfsoc_io_calib_ddr_ops = {
    .read = mchp_pfsoc_io_calib_ddr_read,
    .write = mchp_pfsoc_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_ioscb_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCIoscbState *s = MCHP_PFSOC_IOSCB(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->container, OBJECT(s),
                       "mchp.pfsoc.ioscb", IOSCB_WHOLE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    /* add subregions for all sub-modules in IOSCB */

    memory_region_init_io(&s->lane01, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.lane01", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_LANE01_BASE, &s->lane01);

    memory_region_init_io(&s->lane23, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.lane23", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_LANE23_BASE, &s->lane23);

    memory_region_init_io(&s->ctrl, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.ctrl", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CTRL_BASE, &s->ctrl);

    memory_region_init_io(&s->cfg, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfg", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFG_BASE, &s->cfg);

    memory_region_init_io(&s->pll_mss, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_mss", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_MSS_BASE, &s->pll_mss);

    memory_region_init_io(&s->cfm_mss, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfm_mss", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFM_MSS_BASE, &s->cfm_mss);

    memory_region_init_io(&s->pll_ddr, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_ddr", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_DDR_BASE, &s->pll_ddr);

    memory_region_init_io(&s->bc_ddr, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.bc_ddr", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_BC_DDR_BASE, &s->bc_ddr);

    memory_region_init_io(&s->io_calib_ddr, OBJECT(s),
                          &mchp_pfsoc_io_calib_ddr_ops, s,
                          "mchp.pfsoc.ioscb.io_calib_ddr",
                          IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_IO_CALIB_DDR_BASE,
                                &s->io_calib_ddr);

    memory_region_init_io(&s->pll_sgmii, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_SGMII_BASE,
                                &s->pll_sgmii);

    memory_region_init_io(&s->dll_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.dll_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_DLL_SGMII_BASE,
                                &s->dll_sgmii);

    memory_region_init_io(&s->cfm_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfm_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFM_SGMII_BASE,
                                &s->cfm_sgmii);

    memory_region_init_io(&s->bc_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.bc_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_BC_SGMII_BASE,
                                &s->bc_sgmii);

    memory_region_init_io(&s->io_calib_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops,
                          s, "mchp.pfsoc.ioscb.io_calib_sgmii",
                          IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_IO_CALIB_SGMII_BASE,
                                &s->io_calib_sgmii);
}

static void mchp_pfsoc_ioscb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC IOSCB modules";
    dc->realize = mchp_pfsoc_ioscb_realize;
}

static const TypeInfo mchp_pfsoc_ioscb_info = {
    .name          = TYPE_MCHP_PFSOC_IOSCB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCIoscbState),
    .class_init    = mchp_pfsoc_ioscb_class_init,
};

static void mchp_pfsoc_ioscb_register_types(void)
{
    type_register_static(&mchp_pfsoc_ioscb_info);
}

type_init(mchp_pfsoc_ioscb_register_types)
