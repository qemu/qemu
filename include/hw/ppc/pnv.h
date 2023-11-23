/*
 * QEMU PowerPC PowerNV various definitions
 *
 * Copyright (c) 2014-2016 BenH, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_H
#define PPC_PNV_H

#include "cpu.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/ipmi/ipmi.h"
#include "hw/ppc/pnv_pnor.h"

#define TYPE_PNV_CHIP "pnv-chip"

typedef struct PnvCore PnvCore;
typedef struct PnvChip PnvChip;
typedef struct Pnv8Chip Pnv8Chip;
typedef struct Pnv9Chip Pnv9Chip;
typedef struct Pnv10Chip Pnv10Chip;

#define PNV_CHIP_TYPE_SUFFIX "-" TYPE_PNV_CHIP
#define PNV_CHIP_TYPE_NAME(cpu_model) cpu_model PNV_CHIP_TYPE_SUFFIX

#define TYPE_PNV_CHIP_POWER8E PNV_CHIP_TYPE_NAME("power8e_v2.1")
DECLARE_INSTANCE_CHECKER(PnvChip, PNV_CHIP_POWER8E,
                         TYPE_PNV_CHIP_POWER8E)

#define TYPE_PNV_CHIP_POWER8 PNV_CHIP_TYPE_NAME("power8_v2.0")
DECLARE_INSTANCE_CHECKER(PnvChip, PNV_CHIP_POWER8,
                         TYPE_PNV_CHIP_POWER8)

#define TYPE_PNV_CHIP_POWER8NVL PNV_CHIP_TYPE_NAME("power8nvl_v1.0")
DECLARE_INSTANCE_CHECKER(PnvChip, PNV_CHIP_POWER8NVL,
                         TYPE_PNV_CHIP_POWER8NVL)

#define TYPE_PNV_CHIP_POWER9 PNV_CHIP_TYPE_NAME("power9_v2.2")
DECLARE_INSTANCE_CHECKER(PnvChip, PNV_CHIP_POWER9,
                         TYPE_PNV_CHIP_POWER9)

#define TYPE_PNV_CHIP_POWER10 PNV_CHIP_TYPE_NAME("power10_v2.0")
DECLARE_INSTANCE_CHECKER(PnvChip, PNV_CHIP_POWER10,
                         TYPE_PNV_CHIP_POWER10)

PnvCore *pnv_chip_find_core(PnvChip *chip, uint32_t core_id);
PowerPCCPU *pnv_chip_find_cpu(PnvChip *chip, uint32_t pir);

typedef struct PnvPHB PnvPHB;

#define TYPE_PNV_MACHINE       MACHINE_TYPE_NAME("powernv")
typedef struct PnvMachineClass PnvMachineClass;
typedef struct PnvMachineState PnvMachineState;
DECLARE_OBJ_CHECKERS(PnvMachineState, PnvMachineClass,
                     PNV_MACHINE, TYPE_PNV_MACHINE)


struct PnvMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    const char *compat;
    int compat_size;

    void (*dt_power_mgt)(PnvMachineState *pnv, void *fdt);
    void (*i2c_init)(PnvMachineState *pnv);
};

struct PnvMachineState {
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

    PnvPnor      *pnor;

    hwaddr       fw_load_addr;
};

PnvChip *pnv_get_chip(PnvMachineState *pnv, uint32_t chip_id);
PnvChip *pnv_chip_add_phb(PnvChip *chip, PnvPHB *phb);

#define PNV_FDT_ADDR          0x01000000
#define PNV_TIMEBASE_FREQ     512000000ULL

/*
 * BMC helpers
 */
void pnv_dt_bmc_sensors(IPMIBmc *bmc, void *fdt);
void pnv_bmc_powerdown(IPMIBmc *bmc);
IPMIBmc *pnv_bmc_create(PnvPnor *pnor);
IPMIBmc *pnv_bmc_find(Error **errp);
void pnv_bmc_set_pnor(IPMIBmc *bmc, PnvPnor *pnor);

/*
 * POWER8 MMIO base addresses
 */
#define PNV_XSCOM_SIZE        0x800000000ull
#define PNV_XSCOM_BASE(chip)                                            \
    (0x0003fc0000000000ull + ((uint64_t)(chip)->chip_id) * PNV_XSCOM_SIZE)

#define PNV_OCC_COMMON_AREA_SIZE    0x0000000000800000ull
#define PNV_OCC_COMMON_AREA_BASE    0x7fff800000ull
#define PNV_OCC_SENSOR_BASE(chip)   (PNV_OCC_COMMON_AREA_BASE + \
    PNV_OCC_SENSOR_DATA_BLOCK_BASE((chip)->chip_id))

#define PNV_HOMER_SIZE              0x0000000000400000ull
#define PNV_HOMER_BASE(chip)                                            \
    (0x7ffd800000ull + ((uint64_t)(chip)->chip_id) * PNV_HOMER_SIZE)


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
    (0x0003ffff80000000ull + (uint64_t) (chip)->chip_id * PNV_ICP_SIZE)


#define PNV_PSIHB_SIZE       0x0000000000100000ull
#define PNV_PSIHB_BASE(chip) \
    (0x0003fffe80000000ull + (uint64_t)(chip)->chip_id * PNV_PSIHB_SIZE)

#define PNV_PSIHB_FSP_SIZE   0x0000000100000000ull
#define PNV_PSIHB_FSP_BASE(chip) \
    (0x0003ffe000000000ull + (uint64_t)(chip)->chip_id * \
     PNV_PSIHB_FSP_SIZE)

/*
 * POWER9 MMIO base addresses
 */
#define PNV9_CHIP_BASE(chip, base)   \
    ((base) + ((uint64_t) (chip)->chip_id << 42))

#define PNV9_XIVE_VC_SIZE            0x0000008000000000ull
#define PNV9_XIVE_VC_BASE(chip)      PNV9_CHIP_BASE(chip, 0x0006010000000000ull)

#define PNV9_XIVE_PC_SIZE            0x0000001000000000ull
#define PNV9_XIVE_PC_BASE(chip)      PNV9_CHIP_BASE(chip, 0x0006018000000000ull)

#define PNV9_LPCM_SIZE               0x0000000100000000ull
#define PNV9_LPCM_BASE(chip)         PNV9_CHIP_BASE(chip, 0x0006030000000000ull)

#define PNV9_PSIHB_SIZE              0x0000000000100000ull
#define PNV9_PSIHB_BASE(chip)        PNV9_CHIP_BASE(chip, 0x0006030203000000ull)

#define PNV9_XIVE_IC_SIZE            0x0000000000080000ull
#define PNV9_XIVE_IC_BASE(chip)      PNV9_CHIP_BASE(chip, 0x0006030203100000ull)

#define PNV9_XIVE_TM_SIZE            0x0000000000040000ull
#define PNV9_XIVE_TM_BASE(chip)      PNV9_CHIP_BASE(chip, 0x0006030203180000ull)

#define PNV9_PSIHB_ESB_SIZE          0x0000000000010000ull
#define PNV9_PSIHB_ESB_BASE(chip)    PNV9_CHIP_BASE(chip, 0x00060302031c0000ull)

#define PNV9_XSCOM_SIZE              0x0000000400000000ull
#define PNV9_XSCOM_BASE(chip)        PNV9_CHIP_BASE(chip, 0x00603fc00000000ull)

#define PNV9_OCC_COMMON_AREA_SIZE    0x0000000000800000ull
#define PNV9_OCC_COMMON_AREA_BASE    0x203fff800000ull
#define PNV9_OCC_SENSOR_BASE(chip)   (PNV9_OCC_COMMON_AREA_BASE +       \
    PNV_OCC_SENSOR_DATA_BLOCK_BASE((chip)->chip_id))

#define PNV9_HOMER_SIZE              0x0000000000400000ull
#define PNV9_HOMER_BASE(chip)                                           \
    (0x203ffd800000ull + ((uint64_t)(chip)->chip_id) * PNV9_HOMER_SIZE)

/*
 * POWER10 MMIO base addresses - 16TB stride per chip
 */
#define PNV10_CHIP_BASE(chip, base)   \
    ((base) + ((uint64_t) (chip)->chip_id << 44))

#define PNV10_XSCOM_SIZE             0x0000000400000000ull
#define PNV10_XSCOM_BASE(chip)       PNV10_CHIP_BASE(chip, 0x00603fc00000000ull)

#define PNV10_LPCM_SIZE             0x0000000100000000ull
#define PNV10_LPCM_BASE(chip)       PNV10_CHIP_BASE(chip, 0x0006030000000000ull)

#define PNV10_XIVE2_IC_SIZE         0x0000000002000000ull
#define PNV10_XIVE2_IC_BASE(chip)   PNV10_CHIP_BASE(chip, 0x0006030200000000ull)

#define PNV10_PSIHB_ESB_SIZE        0x0000000000100000ull
#define PNV10_PSIHB_ESB_BASE(chip)  PNV10_CHIP_BASE(chip, 0x0006030202000000ull)

#define PNV10_PSIHB_SIZE            0x0000000000100000ull
#define PNV10_PSIHB_BASE(chip)      PNV10_CHIP_BASE(chip, 0x0006030203000000ull)

#define PNV10_XIVE2_TM_SIZE         0x0000000000040000ull
#define PNV10_XIVE2_TM_BASE(chip)   PNV10_CHIP_BASE(chip, 0x0006030203180000ull)

#define PNV10_XIVE2_NVC_SIZE        0x0000000008000000ull
#define PNV10_XIVE2_NVC_BASE(chip)  PNV10_CHIP_BASE(chip, 0x0006030208000000ull)

#define PNV10_XIVE2_NVPG_SIZE       0x0000010000000000ull
#define PNV10_XIVE2_NVPG_BASE(chip) PNV10_CHIP_BASE(chip, 0x0006040000000000ull)

#define PNV10_XIVE2_ESB_SIZE        0x0000010000000000ull
#define PNV10_XIVE2_ESB_BASE(chip)  PNV10_CHIP_BASE(chip, 0x0006050000000000ull)

#define PNV10_XIVE2_END_SIZE        0x0000020000000000ull
#define PNV10_XIVE2_END_BASE(chip)  PNV10_CHIP_BASE(chip, 0x0006060000000000ull)

#define PNV10_OCC_COMMON_AREA_SIZE  0x0000000000800000ull
#define PNV10_OCC_COMMON_AREA_BASE  0x300fff800000ull
#define PNV10_OCC_SENSOR_BASE(chip) (PNV10_OCC_COMMON_AREA_BASE +       \
    PNV_OCC_SENSOR_DATA_BLOCK_BASE((chip)->chip_id))

#define PNV10_HOMER_SIZE              0x0000000000400000ull
#define PNV10_HOMER_BASE(chip)                                           \
    (0x300ffd800000ll + ((uint64_t)(chip)->chip_id) * PNV10_HOMER_SIZE)

#endif /* PPC_PNV_H */
