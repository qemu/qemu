/*
 * Microchip PolarFire SoC DDR Memory Controller module emulation
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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/mchp_pfsoc_dmc.h"

/* DDR SGMII PHY module */

#define SGMII_PHY_IOC_REG1              0x208
#define SGMII_PHY_TRAINING_STATUS       0x814
#define SGMII_PHY_DQ_DQS_ERR_DONE       0x834
#define SGMII_PHY_DQDQS_STATUS1         0x84c
#define SGMII_PHY_PVT_STAT              0xc20

static uint64_t mchp_pfsoc_ddr_sgmii_phy_read(void *opaque, hwaddr offset,
                                              unsigned size)
{
    uint32_t val = 0;
    static int training_status_bit;

    switch (offset) {
    case SGMII_PHY_IOC_REG1:
        /* See ddr_pvt_calibration() in HSS */
        val = BIT(4) | BIT(2);
        break;
    case SGMII_PHY_TRAINING_STATUS:
        /*
         * The codes logic emulates the training status change from
         * DDR_TRAINING_IP_SM_BCLKSCLK to DDR_TRAINING_IP_SM_DQ_DQS.
         *
         * See ddr_setup() in mss_ddr.c in the HSS source codes.
         */
        val = 1 << training_status_bit;
        training_status_bit = (training_status_bit + 1) % 5;
        break;
    case SGMII_PHY_DQ_DQS_ERR_DONE:
        /*
         * DDR_TRAINING_IP_SM_VERIFY state in ddr_setup(),
         * check that DQ/DQS training passed without error.
         */
        val = 8;
        break;
    case SGMII_PHY_DQDQS_STATUS1:
        /*
         * DDR_TRAINING_IP_SM_VERIFY state in ddr_setup(),
         * check that DQ/DQS calculated window is above 5 taps.
         */
        val = 0xff;
        break;
    case SGMII_PHY_PVT_STAT:
        /* See sgmii_channel_setup() in HSS */
        val = BIT(14) | BIT(6);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_ddr_sgmii_phy_write(void *opaque, hwaddr offset,
                                           uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_ddr_sgmii_phy_ops = {
    .read = mchp_pfsoc_ddr_sgmii_phy_read,
    .write = mchp_pfsoc_ddr_sgmii_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_ddr_sgmii_phy_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCDdrSgmiiPhyState *s = MCHP_PFSOC_DDR_SGMII_PHY(dev);

    memory_region_init_io(&s->sgmii_phy, OBJECT(dev),
                          &mchp_pfsoc_ddr_sgmii_phy_ops, s,
                          "mchp.pfsoc.ddr_sgmii_phy",
                          MCHP_PFSOC_DDR_SGMII_PHY_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sgmii_phy);
}

static void mchp_pfsoc_ddr_sgmii_phy_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC DDR SGMII PHY module";
    dc->realize = mchp_pfsoc_ddr_sgmii_phy_realize;
}

static const TypeInfo mchp_pfsoc_ddr_sgmii_phy_info = {
    .name          = TYPE_MCHP_PFSOC_DDR_SGMII_PHY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCDdrSgmiiPhyState),
    .class_init    = mchp_pfsoc_ddr_sgmii_phy_class_init,
};

static void mchp_pfsoc_ddr_sgmii_phy_register_types(void)
{
    type_register_static(&mchp_pfsoc_ddr_sgmii_phy_info);
}

type_init(mchp_pfsoc_ddr_sgmii_phy_register_types)

/* DDR CFG module */

#define CFG_MT_DONE_ACK                 0x4428
#define CFG_STAT_DFI_INIT_COMPLETE      0x10034
#define CFG_STAT_DFI_TRAINING_COMPLETE  0x10038

static uint64_t mchp_pfsoc_ddr_cfg_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case CFG_MT_DONE_ACK:
        /* memory test in MTC_test() */
        val = BIT(0);
        break;
    case CFG_STAT_DFI_INIT_COMPLETE:
        /* DDR_TRAINING_IP_SM_START_CHECK state in ddr_setup() */
        val = BIT(0);
        break;
    case CFG_STAT_DFI_TRAINING_COMPLETE:
        /* DDR_TRAINING_IP_SM_VERIFY state in ddr_setup() */
        val = BIT(0);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_ddr_cfg_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_ddr_cfg_ops = {
    .read = mchp_pfsoc_ddr_cfg_read,
    .write = mchp_pfsoc_ddr_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_ddr_cfg_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCDdrCfgState *s = MCHP_PFSOC_DDR_CFG(dev);

    memory_region_init_io(&s->cfg, OBJECT(dev),
                          &mchp_pfsoc_ddr_cfg_ops, s,
                          "mchp.pfsoc.ddr_cfg",
                          MCHP_PFSOC_DDR_CFG_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->cfg);
}

static void mchp_pfsoc_ddr_cfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC DDR CFG module";
    dc->realize = mchp_pfsoc_ddr_cfg_realize;
}

static const TypeInfo mchp_pfsoc_ddr_cfg_info = {
    .name          = TYPE_MCHP_PFSOC_DDR_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCDdrCfgState),
    .class_init    = mchp_pfsoc_ddr_cfg_class_init,
};

static void mchp_pfsoc_ddr_cfg_register_types(void)
{
    type_register_static(&mchp_pfsoc_ddr_cfg_info);
}

type_init(mchp_pfsoc_ddr_cfg_register_types)
