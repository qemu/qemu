/*
 * QEMU PowerPC PowerNV Emulation of a few HOMER related registers
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"
#include "sysemu/cpus.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_homer.h"
#include "hw/ppc/pnv_xscom.h"


static bool core_max_array(PnvHomer *homer, hwaddr addr)
{
    int i;
    PnvHomerClass *hmrc = PNV_HOMER_GET_CLASS(homer);

    for (i = 0; i <= homer->chip->nr_cores; i++) {
        if (addr == (hmrc->core_max_base + i)) {
            return true;
       }
    }
    return false;
}

/* P8 Pstate table */

#define PNV8_OCC_PSTATE_VERSION          0x1f8001
#define PNV8_OCC_PSTATE_MIN              0x1f8003
#define PNV8_OCC_PSTATE_VALID            0x1f8000
#define PNV8_OCC_PSTATE_THROTTLE         0x1f8002
#define PNV8_OCC_PSTATE_NOM              0x1f8004
#define PNV8_OCC_PSTATE_TURBO            0x1f8005
#define PNV8_OCC_PSTATE_ULTRA_TURBO      0x1f8006
#define PNV8_OCC_PSTATE_DATA             0x1f8008
#define PNV8_OCC_PSTATE_ID_ZERO          0x1f8010
#define PNV8_OCC_PSTATE_ID_ONE           0x1f8018
#define PNV8_OCC_PSTATE_ID_TWO           0x1f8020
#define PNV8_OCC_VDD_VOLTAGE_IDENTIFIER  0x1f8012
#define PNV8_OCC_VCS_VOLTAGE_IDENTIFIER  0x1f8013
#define PNV8_OCC_PSTATE_ZERO_FREQUENCY   0x1f8014
#define PNV8_OCC_PSTATE_ONE_FREQUENCY    0x1f801c
#define PNV8_OCC_PSTATE_TWO_FREQUENCY    0x1f8024
#define PNV8_CORE_MAX_BASE               0x1f8810


static uint64_t pnv_power8_homer_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    PnvHomer *homer = PNV_HOMER(opaque);

    switch (addr) {
    case PNV8_OCC_PSTATE_VERSION:
    case PNV8_OCC_PSTATE_MIN:
    case PNV8_OCC_PSTATE_ID_ZERO:
        return 0;
    case PNV8_OCC_PSTATE_VALID:
    case PNV8_OCC_PSTATE_THROTTLE:
    case PNV8_OCC_PSTATE_NOM:
    case PNV8_OCC_PSTATE_TURBO:
    case PNV8_OCC_PSTATE_ID_ONE:
    case PNV8_OCC_VDD_VOLTAGE_IDENTIFIER:
    case PNV8_OCC_VCS_VOLTAGE_IDENTIFIER:
        return 1;
    case PNV8_OCC_PSTATE_ULTRA_TURBO:
    case PNV8_OCC_PSTATE_ID_TWO:
        return 2;
    case PNV8_OCC_PSTATE_DATA:
        return 0x1000000000000000;
    /* P8 frequency for 0, 1, and 2 pstates */
    case PNV8_OCC_PSTATE_ZERO_FREQUENCY:
    case PNV8_OCC_PSTATE_ONE_FREQUENCY:
    case PNV8_OCC_PSTATE_TWO_FREQUENCY:
        return 3000;
    }
    /* pstate table core max array */
    if (core_max_array(homer, addr)) {
        return 1;
    }
    return 0;
}

static void pnv_power8_homer_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    /* callback function defined to homer write */
    return;
}

static const MemoryRegionOps pnv_power8_homer_ops = {
    .read = pnv_power8_homer_read,
    .write = pnv_power8_homer_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

/* P8 PBA BARs */
#define PBA_BAR0                     0x00
#define PBA_BAR1                     0x01
#define PBA_BAR2                     0x02
#define PBA_BAR3                     0x03
#define PBA_BARMASK0                 0x04
#define PBA_BARMASK1                 0x05
#define PBA_BARMASK2                 0x06
#define PBA_BARMASK3                 0x07

static uint64_t pnv_homer_power8_pba_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvHomer *homer = PNV_HOMER(opaque);
    PnvChip *chip = homer->chip;
    uint32_t reg = addr >> 3;
    uint64_t val = 0;

    switch (reg) {
    case PBA_BAR0:
        val = PNV_HOMER_BASE(chip);
        break;
    case PBA_BARMASK0: /* P8 homer region mask */
        val = (PNV_HOMER_SIZE - 1) & 0x300000;
        break;
    case PBA_BAR3: /* P8 occ common area */
        val = PNV_OCC_COMMON_AREA_BASE;
        break;
    case PBA_BARMASK3: /* P8 occ common area mask */
        val = (PNV_OCC_COMMON_AREA_SIZE - 1) & 0x700000;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PBA: read to unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_homer_power8_pba_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "PBA: write to unimplemented register: Ox%"
                  HWADDR_PRIx "\n", addr >> 3);
}

static const MemoryRegionOps pnv_homer_power8_pba_ops = {
    .read = pnv_homer_power8_pba_read,
    .write = pnv_homer_power8_pba_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_homer_power8_class_init(ObjectClass *klass, void *data)
{
    PnvHomerClass *homer = PNV_HOMER_CLASS(klass);

    homer->pba_size = PNV_XSCOM_PBA_SIZE;
    homer->pba_ops = &pnv_homer_power8_pba_ops;
    homer->homer_size = PNV_HOMER_SIZE;
    homer->homer_ops = &pnv_power8_homer_ops;
    homer->core_max_base = PNV8_CORE_MAX_BASE;
}

static const TypeInfo pnv_homer_power8_type_info = {
    .name          = TYPE_PNV8_HOMER,
    .parent        = TYPE_PNV_HOMER,
    .instance_size = sizeof(PnvHomer),
    .class_init    = pnv_homer_power8_class_init,
};

/* P9 Pstate table */

#define PNV9_OCC_PSTATE_ID_ZERO          0xe2018
#define PNV9_OCC_PSTATE_ID_ONE           0xe2020
#define PNV9_OCC_PSTATE_ID_TWO           0xe2028
#define PNV9_OCC_PSTATE_DATA             0xe2000
#define PNV9_OCC_PSTATE_DATA_AREA        0xe2008
#define PNV9_OCC_PSTATE_MIN              0xe2003
#define PNV9_OCC_PSTATE_NOM              0xe2004
#define PNV9_OCC_PSTATE_TURBO            0xe2005
#define PNV9_OCC_PSTATE_ULTRA_TURBO      0xe2818
#define PNV9_OCC_MAX_PSTATE_ULTRA_TURBO  0xe2006
#define PNV9_OCC_PSTATE_MAJOR_VERSION    0xe2001
#define PNV9_OCC_OPAL_RUNTIME_DATA       0xe2b85
#define PNV9_CHIP_HOMER_IMAGE_POINTER    0x200008
#define PNV9_CHIP_HOMER_BASE             0x0
#define PNV9_OCC_PSTATE_ZERO_FREQUENCY   0xe201c
#define PNV9_OCC_PSTATE_ONE_FREQUENCY    0xe2024
#define PNV9_OCC_PSTATE_TWO_FREQUENCY    0xe202c
#define PNV9_OCC_ROLE_MASTER_OR_SLAVE    0xe2002
#define PNV9_CORE_MAX_BASE               0xe2819


static uint64_t pnv_power9_homer_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    PnvHomer *homer = PNV_HOMER(opaque);

    switch (addr) {
    case PNV9_OCC_MAX_PSTATE_ULTRA_TURBO:
    case PNV9_OCC_PSTATE_ID_ZERO:
        return 0;
    case PNV9_OCC_PSTATE_DATA:
    case PNV9_OCC_ROLE_MASTER_OR_SLAVE:
    case PNV9_OCC_PSTATE_NOM:
    case PNV9_OCC_PSTATE_TURBO:
    case PNV9_OCC_PSTATE_ID_ONE:
    case PNV9_OCC_PSTATE_ULTRA_TURBO:
    case PNV9_OCC_OPAL_RUNTIME_DATA:
        return 1;
    case PNV9_OCC_PSTATE_MIN:
    case PNV9_OCC_PSTATE_ID_TWO:
        return 2;

    /* 3000 khz frequency for 0, 1, and 2 pstates */
    case PNV9_OCC_PSTATE_ZERO_FREQUENCY:
    case PNV9_OCC_PSTATE_ONE_FREQUENCY:
    case PNV9_OCC_PSTATE_TWO_FREQUENCY:
        return 3000;
    case PNV9_OCC_PSTATE_MAJOR_VERSION:
        return 0x90;
    case PNV9_CHIP_HOMER_BASE:
    case PNV9_OCC_PSTATE_DATA_AREA:
    case PNV9_CHIP_HOMER_IMAGE_POINTER:
        return 0x1000000000000000;
    }
    /* pstate table core max array */
    if (core_max_array(homer, addr)) {
        return 1;
    }
    return 0;
}

static void pnv_power9_homer_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    /* callback function defined to homer write */
    return;
}

static const MemoryRegionOps pnv_power9_homer_ops = {
    .read = pnv_power9_homer_read,
    .write = pnv_power9_homer_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_homer_power9_pba_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvHomer *homer = PNV_HOMER(opaque);
    PnvChip *chip = homer->chip;
    uint32_t reg = addr >> 3;
    uint64_t val = 0;

    switch (reg) {
    case PBA_BAR0:
        val = PNV9_HOMER_BASE(chip);
        break;
    case PBA_BARMASK0: /* P9 homer region mask */
        val = (PNV9_HOMER_SIZE - 1) & 0x300000;
        break;
    case PBA_BAR2: /* P9 occ common area */
        val = PNV9_OCC_COMMON_AREA_BASE;
        break;
    case PBA_BARMASK2: /* P9 occ common area size */
        val = (PNV9_OCC_COMMON_AREA_SIZE - 1) & 0x700000;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PBA: read to unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_homer_power9_pba_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "PBA: write to unimplemented register: Ox%"
                  HWADDR_PRIx "\n", addr >> 3);
}

static const MemoryRegionOps pnv_homer_power9_pba_ops = {
    .read = pnv_homer_power9_pba_read,
    .write = pnv_homer_power9_pba_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_homer_power9_class_init(ObjectClass *klass, void *data)
{
    PnvHomerClass *homer = PNV_HOMER_CLASS(klass);

    homer->pba_size = PNV9_XSCOM_PBA_SIZE;
    homer->pba_ops = &pnv_homer_power9_pba_ops;
    homer->homer_size = PNV9_HOMER_SIZE;
    homer->homer_ops = &pnv_power9_homer_ops;
    homer->core_max_base = PNV9_CORE_MAX_BASE;
}

static const TypeInfo pnv_homer_power9_type_info = {
    .name          = TYPE_PNV9_HOMER,
    .parent        = TYPE_PNV_HOMER,
    .instance_size = sizeof(PnvHomer),
    .class_init    = pnv_homer_power9_class_init,
};

static uint64_t pnv_homer_power10_pba_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvHomer *homer = PNV_HOMER(opaque);
    PnvChip *chip = homer->chip;
    uint32_t reg = addr >> 3;
    uint64_t val = 0;

    switch (reg) {
    case PBA_BAR0:
        val = PNV10_HOMER_BASE(chip);
        break;
    case PBA_BARMASK0: /* P10 homer region mask */
        val = (PNV10_HOMER_SIZE - 1) & 0x300000;
        break;
    case PBA_BAR2: /* P10 occ common area */
        val = PNV10_OCC_COMMON_AREA_BASE;
        break;
    case PBA_BARMASK2: /* P10 occ common area size */
        val = (PNV10_OCC_COMMON_AREA_SIZE - 1) & 0x700000;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PBA: read to unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_homer_power10_pba_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "PBA: write to unimplemented register: Ox%"
                  HWADDR_PRIx "\n", addr >> 3);
}

static const MemoryRegionOps pnv_homer_power10_pba_ops = {
    .read = pnv_homer_power10_pba_read,
    .write = pnv_homer_power10_pba_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_homer_power10_class_init(ObjectClass *klass, void *data)
{
    PnvHomerClass *homer = PNV_HOMER_CLASS(klass);

    homer->pba_size = PNV10_XSCOM_PBA_SIZE;
    homer->pba_ops = &pnv_homer_power10_pba_ops;
    homer->homer_size = PNV10_HOMER_SIZE;
    homer->homer_ops = &pnv_power9_homer_ops; /* TODO */
    homer->core_max_base = PNV9_CORE_MAX_BASE;
}

static const TypeInfo pnv_homer_power10_type_info = {
    .name          = TYPE_PNV10_HOMER,
    .parent        = TYPE_PNV_HOMER,
    .instance_size = sizeof(PnvHomer),
    .class_init    = pnv_homer_power10_class_init,
};

static void pnv_homer_realize(DeviceState *dev, Error **errp)
{
    PnvHomer *homer = PNV_HOMER(dev);
    PnvHomerClass *hmrc = PNV_HOMER_GET_CLASS(homer);

    assert(homer->chip);

    pnv_xscom_region_init(&homer->pba_regs, OBJECT(dev), hmrc->pba_ops,
                          homer, "xscom-pba", hmrc->pba_size);

    /* homer region */
    memory_region_init_io(&homer->regs, OBJECT(dev),
                          hmrc->homer_ops, homer, "homer-main-memory",
                          hmrc->homer_size);
}

static Property pnv_homer_properties[] = {
    DEFINE_PROP_LINK("chip", PnvHomer, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_homer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_homer_realize;
    dc->desc = "PowerNV HOMER Memory";
    device_class_set_props(dc, pnv_homer_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_homer_type_info = {
    .name          = TYPE_PNV_HOMER,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvHomer),
    .class_init    = pnv_homer_class_init,
    .class_size    = sizeof(PnvHomerClass),
    .abstract      = true,
};

static void pnv_homer_register_types(void)
{
    type_register_static(&pnv_homer_type_info);
    type_register_static(&pnv_homer_power8_type_info);
    type_register_static(&pnv_homer_power9_type_info);
    type_register_static(&pnv_homer_power10_type_info);
}

type_init(pnv_homer_register_types);
