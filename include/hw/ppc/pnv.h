/*
 * QEMU PowerPC PowerNV various definitions
 *
 * Copyright (c) 2014-2016 BenH, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _PPC_PNV_H
#define _PPC_PNV_H

#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/ppc/pnv_lpc.h"

#define TYPE_PNV_CHIP "powernv-chip"
#define PNV_CHIP(obj) OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP)
#define PNV_CHIP_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvChipClass, (klass), TYPE_PNV_CHIP)
#define PNV_CHIP_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvChipClass, (obj), TYPE_PNV_CHIP)

typedef enum PnvChipType {
    PNV_CHIP_POWER8E,     /* AKA Murano (default) */
    PNV_CHIP_POWER8,      /* AKA Venice */
    PNV_CHIP_POWER8NVL,   /* AKA Naples */
    PNV_CHIP_POWER9,      /* AKA Nimbus */
} PnvChipType;

typedef struct PnvChip {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint32_t     chip_id;
    uint64_t     ram_start;
    uint64_t     ram_size;

    uint32_t     nr_cores;
    uint64_t     cores_mask;
    void         *cores;

    hwaddr       xscom_base;
    MemoryRegion xscom_mmio;
    MemoryRegion xscom;
    AddressSpace xscom_as;

    PnvLpcController lpc;
} PnvChip;

typedef struct PnvChipClass {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    const char *cpu_model;
    PnvChipType  chip_type;
    uint64_t     chip_cfam_id;
    uint64_t     cores_mask;

    hwaddr       xscom_base;
    hwaddr       xscom_core_base;

    uint32_t (*core_pir)(PnvChip *chip, uint32_t core_id);
} PnvChipClass;

#define TYPE_PNV_CHIP_POWER8E TYPE_PNV_CHIP "-POWER8E"
#define PNV_CHIP_POWER8E(obj) \
    OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP_POWER8E)

#define TYPE_PNV_CHIP_POWER8 TYPE_PNV_CHIP "-POWER8"
#define PNV_CHIP_POWER8(obj) \
    OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP_POWER8)

#define TYPE_PNV_CHIP_POWER8NVL TYPE_PNV_CHIP "-POWER8NVL"
#define PNV_CHIP_POWER8NVL(obj) \
    OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP_POWER8NVL)

#define TYPE_PNV_CHIP_POWER9 TYPE_PNV_CHIP "-POWER9"
#define PNV_CHIP_POWER9(obj) \
    OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP_POWER9)

/*
 * This generates a HW chip id depending on an index:
 *
 *    0x0, 0x1, 0x10, 0x11
 *
 * 4 chips should be the maximum
 */
#define PNV_CHIP_HWID(i) ((((i) & 0x3e) << 3) | ((i) & 0x1))

#define TYPE_POWERNV_MACHINE       MACHINE_TYPE_NAME("powernv")
#define POWERNV_MACHINE(obj) \
    OBJECT_CHECK(PnvMachineState, (obj), TYPE_POWERNV_MACHINE)

typedef struct PnvMachineState {
    /*< private >*/
    MachineState parent_obj;

    uint32_t     initrd_base;
    long         initrd_size;

    uint32_t     num_chips;
    PnvChip      **chips;

    ISABus       *isa_bus;
} PnvMachineState;

#define PNV_FDT_ADDR          0x01000000
#define PNV_TIMEBASE_FREQ     512000000ULL

/*
 * POWER8 MMIO base addresses
 */
#define PNV_XSCOM_SIZE        0x800000000ull
#define PNV_XSCOM_BASE(chip)                                            \
    (chip->xscom_base + ((uint64_t)(chip)->chip_id) * PNV_XSCOM_SIZE)

#endif /* _PPC_PNV_H */
