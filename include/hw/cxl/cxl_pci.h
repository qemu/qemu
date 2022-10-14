/*
 * QEMU CXL PCI interfaces
 *
 * Copyright (c) 2020 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_PCI_H
#define CXL_PCI_H

#include "qemu/compiler.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/cxl/cxl_cdat.h"

#define CXL_VENDOR_ID 0x1e98

#define PCIE_DVSEC_HEADER1_OFFSET 0x4 /* Offset from start of extend cap */
#define PCIE_DVSEC_ID_OFFSET 0x8

#define PCIE_CXL_DEVICE_DVSEC_LENGTH 0x38
#define PCIE_CXL1_DEVICE_DVSEC_REVID 0
#define PCIE_CXL2_DEVICE_DVSEC_REVID 1

#define EXTENSIONS_PORT_DVSEC_LENGTH 0x28
#define EXTENSIONS_PORT_DVSEC_REVID 0

#define GPF_PORT_DVSEC_LENGTH 0x10
#define GPF_PORT_DVSEC_REVID  0

#define GPF_DEVICE_DVSEC_LENGTH 0x10
#define GPF_DEVICE_DVSEC_REVID 0

#define PCIE_FLEXBUS_PORT_DVSEC_LENGTH_2_0 0x14
#define PCIE_FLEXBUS_PORT_DVSEC_REVID_2_0  1

#define REG_LOC_DVSEC_LENGTH 0x24
#define REG_LOC_DVSEC_REVID  0

enum {
    PCIE_CXL_DEVICE_DVSEC      = 0,
    NON_CXL_FUNCTION_MAP_DVSEC = 2,
    EXTENSIONS_PORT_DVSEC      = 3,
    GPF_PORT_DVSEC             = 4,
    GPF_DEVICE_DVSEC           = 5,
    PCIE_FLEXBUS_PORT_DVSEC    = 7,
    REG_LOC_DVSEC              = 8,
    MLD_DVSEC                  = 9,
    CXL20_MAX_DVSEC
};

typedef struct DVSECHeader {
    uint32_t cap_hdr;
    uint32_t dv_hdr1;
    uint16_t dv_hdr2;
} QEMU_PACKED DVSECHeader;
QEMU_BUILD_BUG_ON(sizeof(DVSECHeader) != 10);

/*
 * CXL 2.0 devices must implement certain DVSEC IDs, and can [optionally]
 * implement others.
 *
 * CXL 2.0 Device: 0, [2], 5, 8
 * CXL 2.0 RP: 3, 4, 7, 8
 * CXL 2.0 Upstream Port: [2], 7, 8
 * CXL 2.0 Downstream Port: 3, 4, 7, 8
 */

/* CXL 2.0 - 8.1.3 (ID 0001) */
typedef struct CXLDVSECDevice {
    DVSECHeader hdr;
    uint16_t cap;
    uint16_t ctrl;
    uint16_t status;
    uint16_t ctrl2;
    uint16_t status2;
    uint16_t lock;
    uint16_t cap2;
    uint32_t range1_size_hi;
    uint32_t range1_size_lo;
    uint32_t range1_base_hi;
    uint32_t range1_base_lo;
    uint32_t range2_size_hi;
    uint32_t range2_size_lo;
    uint32_t range2_base_hi;
    uint32_t range2_base_lo;
} CXLDVSECDevice;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECDevice) != 0x38);

/* CXL 2.0 - 8.1.5 (ID 0003) */
typedef struct CXLDVSECPortExtensions {
    DVSECHeader hdr;
    uint16_t status;
    uint16_t control;
    uint8_t alt_bus_base;
    uint8_t alt_bus_limit;
    uint16_t alt_memory_base;
    uint16_t alt_memory_limit;
    uint16_t alt_prefetch_base;
    uint16_t alt_prefetch_limit;
    uint32_t alt_prefetch_base_high;
    uint32_t alt_prefetch_limit_high;
    uint32_t rcrb_base;
    uint32_t rcrb_base_high;
} CXLDVSECPortExtensions;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECPortExtensions) != 0x28);

#define PORT_CONTROL_OFFSET          0xc
#define PORT_CONTROL_UNMASK_SBR      1
#define PORT_CONTROL_ALT_MEMID_EN    4

/* CXL 2.0 - 8.1.6 GPF DVSEC (ID 0004) */
typedef struct CXLDVSECPortGPF {
    DVSECHeader hdr;
    uint16_t rsvd;
    uint16_t phase1_ctrl;
    uint16_t phase2_ctrl;
} CXLDVSECPortGPF;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECPortGPF) != 0x10);

/* CXL 2.0 - 8.1.7 GPF DVSEC for CXL Device */
typedef struct CXLDVSECDeviceGPF {
    DVSECHeader hdr;
    uint16_t phase2_duration;
    uint32_t phase2_power;
} CXLDVSECDeviceGPF;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECDeviceGPF) != 0x10);

/* CXL 2.0 - 8.1.8/8.2.1.3 Flex Bus DVSEC (ID 0007) */
typedef struct CXLDVSECPortFlexBus {
    DVSECHeader hdr;
    uint16_t cap;
    uint16_t ctrl;
    uint16_t status;
    uint32_t rcvd_mod_ts_data_phase1;
} CXLDVSECPortFlexBus;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECPortFlexBus) != 0x14);

/* CXL 2.0 - 8.1.9 Register Locator DVSEC (ID 0008) */
typedef struct CXLDVSECRegisterLocator {
    DVSECHeader hdr;
    uint16_t rsvd;
    uint32_t reg0_base_lo;
    uint32_t reg0_base_hi;
    uint32_t reg1_base_lo;
    uint32_t reg1_base_hi;
    uint32_t reg2_base_lo;
    uint32_t reg2_base_hi;
} CXLDVSECRegisterLocator;
QEMU_BUILD_BUG_ON(sizeof(CXLDVSECRegisterLocator) != 0x24);

/* BAR Equivalence Indicator */
#define BEI_BAR_10H 0
#define BEI_BAR_14H 1
#define BEI_BAR_18H 2
#define BEI_BAR_1cH 3
#define BEI_BAR_20H 4
#define BEI_BAR_24H 5

/* Register Block Identifier */
#define RBI_EMPTY          0
#define RBI_COMPONENT_REG  (1 << 8)
#define RBI_BAR_VIRT_ACL   (2 << 8)
#define RBI_CXL_DEVICE_REG (3 << 8)

#endif
