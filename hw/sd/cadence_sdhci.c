/*
 * Cadence SDHCI emulation
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
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/sd/cadence_sdhci.h"
#include "sdhci-internal.h"

/* HRS - Host Register Set (specific to Cadence) */

#define CADENCE_SDHCI_HRS00             0x00    /* general information */
#define CADENCE_SDHCI_HRS00_SWR             BIT(0)
#define CADENCE_SDHCI_HRS00_POR_VAL         0x00010000

#define CADENCE_SDHCI_HRS04             0x10    /* PHY access port */
#define CADENCE_SDHCI_HRS04_WR              BIT(24)
#define CADENCE_SDHCI_HRS04_RD              BIT(25)
#define CADENCE_SDHCI_HRS04_ACK             BIT(26)

#define CADENCE_SDHCI_HRS06             0x18    /* eMMC control */
#define CADENCE_SDHCI_HRS06_TUNE_UP         BIT(15)

/* SRS - Slot Register Set (SDHCI-compatible) */

#define CADENCE_SDHCI_SRS_BASE          0x200

#define TO_REG(addr)    ((addr) / sizeof(uint32_t))

static void cadence_sdhci_instance_init(Object *obj)
{
    CadenceSDHCIState *s = CADENCE_SDHCI(obj);

    object_initialize_child(OBJECT(s), "generic-sdhci",
                            &s->sdhci, TYPE_SYSBUS_SDHCI);
}

static void cadence_sdhci_reset(DeviceState *dev)
{
    CadenceSDHCIState *s = CADENCE_SDHCI(dev);

    memset(s->regs, 0, CADENCE_SDHCI_REG_SIZE);
    s->regs[TO_REG(CADENCE_SDHCI_HRS00)] = CADENCE_SDHCI_HRS00_POR_VAL;

    device_cold_reset(DEVICE(&s->sdhci));
}

static uint64_t cadence_sdhci_read(void *opaque, hwaddr addr, unsigned int size)
{
    CadenceSDHCIState *s = opaque;
    uint32_t val;

    val = s->regs[TO_REG(addr)];

    return (uint64_t)val;
}

static void cadence_sdhci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned int size)
{
    CadenceSDHCIState *s = opaque;
    uint32_t val32 = (uint32_t)val;

    switch (addr) {
    case CADENCE_SDHCI_HRS00:
        /*
         * The only writable bit is SWR (software reset) and it automatically
         * clears to zero, so essentially this register remains unchanged.
         */
        if (val32 & CADENCE_SDHCI_HRS00_SWR) {
            cadence_sdhci_reset(DEVICE(s));
        }

        break;
    case CADENCE_SDHCI_HRS04:
        /*
         * Only emulate the ACK bit behavior when read or write transaction
         * are requested.
         */
        if (val32 & (CADENCE_SDHCI_HRS04_WR | CADENCE_SDHCI_HRS04_RD)) {
            val32 |= CADENCE_SDHCI_HRS04_ACK;
        } else {
            val32 &= ~CADENCE_SDHCI_HRS04_ACK;
        }

        s->regs[TO_REG(addr)] = val32;
        break;
    case CADENCE_SDHCI_HRS06:
        if (val32 & CADENCE_SDHCI_HRS06_TUNE_UP) {
            val32 &= ~CADENCE_SDHCI_HRS06_TUNE_UP;
        }

        s->regs[TO_REG(addr)] = val32;
        break;
    default:
        s->regs[TO_REG(addr)] = val32;
        break;
    }
}

static const MemoryRegionOps cadence_sdhci_ops = {
    .read = cadence_sdhci_read,
    .write = cadence_sdhci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void cadence_sdhci_realize(DeviceState *dev, Error **errp)
{
    CadenceSDHCIState *s = CADENCE_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sbd_sdhci = SYS_BUS_DEVICE(&s->sdhci);

    memory_region_init(&s->container, OBJECT(s),
                       "cadence.sdhci-container", 0x1000);
    sysbus_init_mmio(sbd, &s->container);

    memory_region_init_io(&s->iomem, OBJECT(s), &cadence_sdhci_ops,
                          s, TYPE_CADENCE_SDHCI, CADENCE_SDHCI_REG_SIZE);
    memory_region_add_subregion(&s->container, 0, &s->iomem);

    sysbus_realize(sbd_sdhci, errp);
    memory_region_add_subregion(&s->container, CADENCE_SDHCI_SRS_BASE,
                                sysbus_mmio_get_region(sbd_sdhci, 0));

    /* propagate irq and "sd-bus" from generic-sdhci */
    sysbus_pass_irq(sbd, sbd_sdhci);
    s->bus = qdev_get_child_bus(DEVICE(sbd_sdhci), "sd-bus");
}

static const VMStateDescription vmstate_cadence_sdhci = {
    .name = TYPE_CADENCE_SDHCI,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CadenceSDHCIState, CADENCE_SDHCI_NUM_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void cadence_sdhci_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->desc = "Cadence SD/SDIO/eMMC Host Controller (SD4HC)";
    dc->realize = cadence_sdhci_realize;
    dc->reset = cadence_sdhci_reset;
    dc->vmsd = &vmstate_cadence_sdhci;
}

static TypeInfo cadence_sdhci_info = {
    .name          = TYPE_CADENCE_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceSDHCIState),
    .instance_init = cadence_sdhci_instance_init,
    .class_init    = cadence_sdhci_class_init,
};

static void cadence_sdhci_register_types(void)
{
    type_register_static(&cadence_sdhci_info);
}

type_init(cadence_sdhci_register_types)
