/*
 * Axiado SD Host Controller with embedded PHY
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sd/axiado_sdhci.h"
#include "sdhci-internal.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "qemu/log.h"

#define EMMC_PHY_ID     0x00
#define EMMC_PHY_STATUS 0x50

#define DLL_RDY  (1u << 0)
#define CAL_DONE (1u << 6)

static uint64_t emmc_phy_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t val = 0x00;

    switch (offset) {
    case EMMC_PHY_ID:
        val = 0x3dff6870;
        break;
    case EMMC_PHY_STATUS:
        val = DLL_RDY | CAL_DONE;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "Register 0x%" HWADDR_PRIx " not implemented\n", offset);
        break;
    }

    return val;
}

static void emmc_phy_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "Register 0x%" HWADDR_PRIx " not implemented\n", offset);
}

static const MemoryRegionOps emmc_phy_ops = {
    .read = emmc_phy_read,
    .write = emmc_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void axiado_sdhci_realize(DeviceState *dev, Error **errp)
{
    AxiadoSDHCIState *s = AXIADO_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sdhci_sbd;

    qdev_prop_set_uint64(DEVICE(&s->sdhci), "capareg", 0x216737eed0b0);
    qdev_prop_set_uint64(DEVICE(&s->sdhci), "sd-spec-version", 3);

    sdhci_sbd = SYS_BUS_DEVICE(&s->sdhci);
    if (!sysbus_realize(sdhci_sbd, errp)) {
        return;
    }

    sysbus_init_mmio(sbd, sysbus_mmio_get_region(sdhci_sbd, 0));

    /* Propagate IRQ from SDHCI and SD bus  */
    sysbus_pass_irq(sbd, sdhci_sbd);
    s->sd_bus = qdev_get_child_bus(DEVICE(sdhci_sbd), "sd-bus");

    /* Initialize eMMC PHY MMIO */
    memory_region_init_io(&s->emmc_phy, OBJECT(s), &emmc_phy_ops, s,
                          "axiado.emmc-phy", 0x1000);

    sysbus_init_mmio(sbd, &s->emmc_phy);
}

static void axiado_sdhci_instance_init(Object *obj)
{
    AxiadoSDHCIState *s = AXIADO_SDHCI(obj);

    object_initialize_child(OBJECT(s), "sdhci", &s->sdhci,
                            TYPE_SYSBUS_SDHCI);
}

static void axiado_sdhci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = axiado_sdhci_realize;
    dc->desc = "Axiado SD Host Controller with eMMC PHY";
}

static const TypeInfo axiado_sdhci_info = {
    .name          = TYPE_AXIADO_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AxiadoSDHCIState),
    .instance_init = axiado_sdhci_instance_init,
    .class_init    = axiado_sdhci_class_init,
};

static void axiado_sdhci_register_types(void)
{
    type_register_static(&axiado_sdhci_info);
}

type_init(axiado_sdhci_register_types);
