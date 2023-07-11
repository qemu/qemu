/*
 * q35.h
 *
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_Q35_H
#define HW_Q35_H

#include "hw/pci/pci_device.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci-host/pam.h"
#include "qemu/units.h"
#include "qemu/range.h"
#include "qom/object.h"

#define TYPE_Q35_HOST_DEVICE "q35-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(Q35PCIHost, Q35_HOST_DEVICE)

#define TYPE_MCH_PCI_DEVICE "mch"
OBJECT_DECLARE_SIMPLE_TYPE(MCHPCIState, MCH_PCI_DEVICE)

struct MCHPCIState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion *ram_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *system_memory;
    MemoryRegion *address_space_io;
    PAMMemoryRegion pam_regions[PAM_REGIONS_COUNT];
    MemoryRegion smram_region, open_high_smram;
    MemoryRegion smram, low_smram, high_smram;
    MemoryRegion tseg_blackhole, tseg_window;
    MemoryRegion smbase_blackhole, smbase_window;
    bool has_smram_at_smbase;
    Range pci_hole;
    uint64_t below_4g_mem_size;
    uint64_t above_4g_mem_size;
    uint64_t pci_hole64_size;
    uint32_t short_root_bus;
    uint16_t ext_tseg_mbytes;
};

struct Q35PCIHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/

    bool pci_hole64_fix;
    MCHPCIState mch;
};

#define Q35_MASK(bit, ms_bit, ls_bit) \
((uint##bit##_t)(((1ULL << ((ms_bit) + 1)) - 1) & ~((1ULL << ls_bit) - 1)))

/*
 * gmch part
 */

/* PCI configuration */
#define MCH_HOST_BRIDGE                        "MCH"

#define MCH_HOST_BRIDGE_CONFIG_ADDR            0xcf8
#define MCH_HOST_BRIDGE_CONFIG_DATA            0xcfc

/* D0:F0 configuration space */
#define MCH_HOST_BRIDGE_REVISION_DEFAULT       0x0

#define MCH_HOST_BRIDGE_EXT_TSEG_MBYTES        0x50
#define MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_SIZE   2
#define MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_QUERY  0xffff
#define MCH_HOST_BRIDGE_EXT_TSEG_MBYTES_MAX    0xfff

#define MCH_HOST_BRIDGE_SMBASE_SIZE            (128 * KiB)
#define MCH_HOST_BRIDGE_SMBASE_ADDR            0x30000
#define MCH_HOST_BRIDGE_F_SMBASE               0x9c
#define MCH_HOST_BRIDGE_F_SMBASE_QUERY         0xff
#define MCH_HOST_BRIDGE_F_SMBASE_IN_RAM        0x01
#define MCH_HOST_BRIDGE_F_SMBASE_LCK           0x02

#define MCH_HOST_BRIDGE_PCIEXBAR               0x60    /* 64bit register */
#define MCH_HOST_BRIDGE_PCIEXBAR_SIZE          8       /* 64bit register */
#define MCH_HOST_BRIDGE_PCIEXBAR_DEFAULT       0xb0000000
#define MCH_HOST_BRIDGE_PCIEXBAR_MAX           (0x10000000) /* 256M */
#define MCH_HOST_BRIDGE_PCIEXBAR_ADMSK         Q35_MASK(64, 35, 28)
#define MCH_HOST_BRIDGE_PCIEXBAR_128ADMSK      ((uint64_t)(1 << 26))
#define MCH_HOST_BRIDGE_PCIEXBAR_64ADMSK       ((uint64_t)(1 << 25))
#define MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_MASK   ((uint64_t)(0x3 << 1))
#define MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_256M   ((uint64_t)(0x0 << 1))
#define MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_128M   ((uint64_t)(0x1 << 1))
#define MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_64M    ((uint64_t)(0x2 << 1))
#define MCH_HOST_BRIDGE_PCIEXBAR_LENGTH_RVD    ((uint64_t)(0x3 << 1))
#define MCH_HOST_BRIDGE_PCIEXBAREN             ((uint64_t)1)

#define MCH_HOST_BRIDGE_PAM_NB                 7
#define MCH_HOST_BRIDGE_PAM_SIZE               7
#define MCH_HOST_BRIDGE_PAM0                   0x90
#define MCH_HOST_BRIDGE_PAM_BIOS_AREA          0xf0000
#define MCH_HOST_BRIDGE_PAM_AREA_SIZE          0x10000 /* 16KB */
#define MCH_HOST_BRIDGE_PAM1                   0x91
#define MCH_HOST_BRIDGE_PAM_EXPAN_AREA         0xc0000
#define MCH_HOST_BRIDGE_PAM_EXPAN_SIZE         0x04000
#define MCH_HOST_BRIDGE_PAM2                   0x92
#define MCH_HOST_BRIDGE_PAM3                   0x93
#define MCH_HOST_BRIDGE_PAM4                   0x94
#define MCH_HOST_BRIDGE_PAM_EXBIOS_AREA        0xe0000
#define MCH_HOST_BRIDGE_PAM_EXBIOS_SIZE        0x04000
#define MCH_HOST_BRIDGE_PAM5                   0x95
#define MCH_HOST_BRIDGE_PAM6                   0x96
#define MCH_HOST_BRIDGE_PAM_WE_HI              ((uint8_t)(0x2 << 4))
#define MCH_HOST_BRIDGE_PAM_RE_HI              ((uint8_t)(0x1 << 4))
#define MCH_HOST_BRIDGE_PAM_HI_MASK            ((uint8_t)(0x3 << 4))
#define MCH_HOST_BRIDGE_PAM_WE_LO              ((uint8_t)0x2)
#define MCH_HOST_BRIDGE_PAM_RE_LO              ((uint8_t)0x1)
#define MCH_HOST_BRIDGE_PAM_LO_MASK            ((uint8_t)0x3)
#define MCH_HOST_BRIDGE_PAM_WE                 ((uint8_t)0x2)
#define MCH_HOST_BRIDGE_PAM_RE                 ((uint8_t)0x1)
#define MCH_HOST_BRIDGE_PAM_MASK               ((uint8_t)0x3)

#define MCH_HOST_BRIDGE_SMRAM                  0x9d
#define MCH_HOST_BRIDGE_SMRAM_SIZE             2
#define MCH_HOST_BRIDGE_SMRAM_D_OPEN           ((uint8_t)(1 << 6))
#define MCH_HOST_BRIDGE_SMRAM_D_CLS            ((uint8_t)(1 << 5))
#define MCH_HOST_BRIDGE_SMRAM_D_LCK            ((uint8_t)(1 << 4))
#define MCH_HOST_BRIDGE_SMRAM_G_SMRAME         ((uint8_t)(1 << 3))
#define MCH_HOST_BRIDGE_SMRAM_C_BASE_SEG_MASK  ((uint8_t)0x7)
#define MCH_HOST_BRIDGE_SMRAM_C_BASE_SEG       ((uint8_t)0x2)  /* hardwired to b010 */
#define MCH_HOST_BRIDGE_SMRAM_C_BASE           0xa0000
#define MCH_HOST_BRIDGE_SMRAM_C_END            0xc0000
#define MCH_HOST_BRIDGE_SMRAM_C_SIZE           0x20000
#define MCH_HOST_BRIDGE_UPPER_SYSTEM_BIOS_END  0x100000
#define MCH_HOST_BRIDGE_SMRAM_DEFAULT           \
    MCH_HOST_BRIDGE_SMRAM_C_BASE_SEG
#define MCH_HOST_BRIDGE_SMRAM_WMASK             \
    (MCH_HOST_BRIDGE_SMRAM_D_OPEN |             \
     MCH_HOST_BRIDGE_SMRAM_D_CLS |              \
     MCH_HOST_BRIDGE_SMRAM_D_LCK |              \
     MCH_HOST_BRIDGE_SMRAM_G_SMRAME)
#define MCH_HOST_BRIDGE_SMRAM_WMASK_LCK         \
    MCH_HOST_BRIDGE_SMRAM_D_CLS

#define MCH_HOST_BRIDGE_ESMRAMC                0x9e
#define MCH_HOST_BRIDGE_ESMRAMC_H_SMRAME       ((uint8_t)(1 << 7))
#define MCH_HOST_BRIDGE_ESMRAMC_E_SMERR        ((uint8_t)(1 << 6))
#define MCH_HOST_BRIDGE_ESMRAMC_SM_CACHE       ((uint8_t)(1 << 5))
#define MCH_HOST_BRIDGE_ESMRAMC_SM_L1          ((uint8_t)(1 << 4))
#define MCH_HOST_BRIDGE_ESMRAMC_SM_L2          ((uint8_t)(1 << 3))
#define MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK   ((uint8_t)(0x3 << 1))
#define MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_1MB    ((uint8_t)(0x0 << 1))
#define MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_2MB    ((uint8_t)(0x1 << 1))
#define MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_8MB    ((uint8_t)(0x2 << 1))
#define MCH_HOST_BRIDGE_ESMRAMC_T_EN           ((uint8_t)1)
#define MCH_HOST_BRIDGE_ESMRAMC_DEFAULT \
    (MCH_HOST_BRIDGE_ESMRAMC_SM_CACHE | \
     MCH_HOST_BRIDGE_ESMRAMC_SM_L1 |    \
     MCH_HOST_BRIDGE_ESMRAMC_SM_L2)
#define MCH_HOST_BRIDGE_ESMRAMC_WMASK               \
    (MCH_HOST_BRIDGE_ESMRAMC_H_SMRAME |             \
     MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK |         \
     MCH_HOST_BRIDGE_ESMRAMC_T_EN)
#define MCH_HOST_BRIDGE_ESMRAMC_WMASK_LCK     0

/* D1:F0 PCIE* port*/
#define MCH_PCIE_DEV                           1
#define MCH_PCIE_FUNC                          0

uint64_t mch_mcfg_base(void);

/*
 * Arbitrary but unique BNF number for IOAPIC device.
 *
 * TODO: make sure there would have no conflict with real PCI bus
 */
#define Q35_PSEUDO_BUS_PLATFORM         (0xff)
#define Q35_PSEUDO_DEVFN_IOAPIC         (0x00)

#endif /* HW_Q35_H */
