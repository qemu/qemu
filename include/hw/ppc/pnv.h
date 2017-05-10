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
#include "hw/ipmi/ipmi.h"
#include "hw/ppc/pnv_lpc.h"
#include "hw/ppc/pnv_psi.h"
#include "hw/ppc/pnv_occ.h"

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
    MemoryRegion icp_mmio;

    PnvLpcController lpc;
    PnvPsi       psi;
    PnvOCC       occ;
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
 * This generates a HW chip id depending on an index, as found on a
 * two socket system with dual chip modules :
 *
 *    0x0, 0x1, 0x10, 0x11
 *
 * 4 chips should be the maximum
 *
 * TODO: use a machine property to define the chip ids
 */
#define PNV_CHIP_HWID(i) ((((i) & 0x3e) << 3) | ((i) & 0x1))

/*
 * Converts back a HW chip id to an index. This is useful to calculate
 * the MMIO addresses of some controllers which depend on the chip id.
 */
#define PNV_CHIP_INDEX(chip)                                    \
    (((chip)->chip_id >> 2) * 2 + ((chip)->chip_id & 0x3))

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
    uint32_t     cpld_irqstate;

    IPMIBmc      *bmc;
    Notifier     powerdown_notifier;
} PnvMachineState;

#define PNV_FDT_ADDR          0x01000000
#define PNV_TIMEBASE_FREQ     512000000ULL

/*
 * BMC helpers
 */
void pnv_bmc_populate_sensors(IPMIBmc *bmc, void *fdt);
void pnv_bmc_powerdown(IPMIBmc *bmc);

/*
 * POWER8 MMIO base addresses
 */
#define PNV_XSCOM_SIZE        0x800000000ull
#define PNV_XSCOM_BASE(chip)                                            \
    (chip->xscom_base + ((uint64_t)(chip)->chip_id) * PNV_XSCOM_SIZE)

/*
 * XSCOM 0x20109CA defines the ICP BAR:
 *
 * 0:29   : bits 14 to 43 of address to define 1 MB region.
 * 30     : 1 to enable ICP to receive loads/stores against its BAR region
 * 31:63  : Constant 0
 *
 * Usually defined as :
 *
 *      0xffffe00200000000 -> 0x0003ffff80000000
 *      0xffffe00600000000 -> 0x0003ffff80100000
 *      0xffffe02200000000 -> 0x0003ffff80800000
 *      0xffffe02600000000 -> 0x0003ffff80900000
 */
#define PNV_ICP_SIZE         0x0000000000100000ull
#define PNV_ICP_BASE(chip)                                              \
    (0x0003ffff80000000ull + (uint64_t) PNV_CHIP_INDEX(chip) * PNV_ICP_SIZE)


#define PNV_PSIHB_SIZE       0x0000000000100000ull
#define PNV_PSIHB_BASE(chip) \
    (0x0003fffe80000000ull + (uint64_t)PNV_CHIP_INDEX(chip) * PNV_PSIHB_SIZE)

#define PNV_PSIHB_FSP_SIZE   0x0000000100000000ull
#define PNV_PSIHB_FSP_BASE(chip) \
    (0x0003ffe000000000ull + (uint64_t)PNV_CHIP_INDEX(chip) * \
     PNV_PSIHB_FSP_SIZE)

#endif /* _PPC_PNV_H */
