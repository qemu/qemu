/*
 * i.MX8 PCIe PHY emulation
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci-host/fsl_imx8m_phy.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"

#define CMN_REG075 0x1d4
#define ANA_PLL_LOCK_DONE BIT(1)
#define ANA_PLL_AFC_DONE BIT(0)

static uint64_t fsl_imx8m_pcie_phy_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    FslImx8mPciePhyState *s = opaque;

    if (offset == CMN_REG075) {
        return s->data[offset] | ANA_PLL_LOCK_DONE | ANA_PLL_AFC_DONE;
    }

    return s->data[offset];
}

static void fsl_imx8m_pcie_phy_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    FslImx8mPciePhyState *s = opaque;

    s->data[offset] = value;
}

static const MemoryRegionOps fsl_imx8m_pcie_phy_ops = {
    .read = fsl_imx8m_pcie_phy_read,
    .write = fsl_imx8m_pcie_phy_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fsl_imx8m_pcie_phy_realize(DeviceState *dev, Error **errp)
{
    FslImx8mPciePhyState *s = FSL_IMX8M_PCIE_PHY(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &fsl_imx8m_pcie_phy_ops, s,
                          TYPE_FSL_IMX8M_PCIE_PHY, ARRAY_SIZE(s->data));
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void fsl_imx8m_pcie_phy_reset_hold(Object *obj, ResetType type)
{
    FslImx8mPciePhyState *s = FSL_IMX8M_PCIE_PHY(obj);

    memset(s->data, 0, sizeof(s->data));
}

static const VMStateDescription fsl_imx8m_pcie_phy_vmstate = {
    .name = "fsl-imx8m-pcie-phy",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, FslImx8mPciePhyState,
                            FSL_IMX8M_PCIE_PHY_DATA_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void fsl_imx8m_pcie_phy_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = fsl_imx8m_pcie_phy_realize;
    dc->vmsd = &fsl_imx8m_pcie_phy_vmstate;
    rc->phases.hold = fsl_imx8m_pcie_phy_reset_hold;
}

static const TypeInfo fsl_imx8m_pcie_phy_types[] = {
    {
        .name = TYPE_FSL_IMX8M_PCIE_PHY,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslImx8mPciePhyState),
        .class_init = fsl_imx8m_pcie_phy_class_init,
    }
};

DEFINE_TYPES(fsl_imx8m_pcie_phy_types)
