/*
 * tpm.h - TPM ACPI definitions
 *
 * Copyright (C) 2014 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org
 *
 */
#ifndef HW_ACPI_TPM_H
#define HW_ACPI_TPM_H

#include "qemu/units.h"
#include "hw/registerfields.h"
#include "hw/acpi/aml-build.h"
#include "sysemu/tpm.h"

#define TPM_TIS_ADDR_BASE           0xFED40000
#define TPM_TIS_ADDR_SIZE           0x5000

#define TPM_TIS_IRQ                 5

#define TPM_TIS_NUM_LOCALITIES      5     /* per spec */
#define TPM_TIS_LOCALITY_SHIFT      12

/* tis registers */
#define TPM_TIS_REG_ACCESS                0x00
#define TPM_TIS_REG_INT_ENABLE            0x08
#define TPM_TIS_REG_INT_VECTOR            0x0c
#define TPM_TIS_REG_INT_STATUS            0x10
#define TPM_TIS_REG_INTF_CAPABILITY       0x14
#define TPM_TIS_REG_STS                   0x18
#define TPM_TIS_REG_DATA_FIFO             0x24
#define TPM_TIS_REG_INTERFACE_ID          0x30
#define TPM_TIS_REG_DATA_XFIFO            0x80
#define TPM_TIS_REG_DATA_XFIFO_END        0xbc
#define TPM_TIS_REG_DID_VID               0xf00
#define TPM_TIS_REG_RID                   0xf04

/* vendor-specific registers */
#define TPM_TIS_REG_DEBUG                 0xf90

#define TPM_TIS_STS_TPM_FAMILY_MASK         (0x3 << 26)/* TPM 2.0 */
#define TPM_TIS_STS_TPM_FAMILY1_2           (0 << 26)  /* TPM 2.0 */
#define TPM_TIS_STS_TPM_FAMILY2_0           (1 << 26)  /* TPM 2.0 */
#define TPM_TIS_STS_RESET_ESTABLISHMENT_BIT (1 << 25)  /* TPM 2.0 */
#define TPM_TIS_STS_COMMAND_CANCEL          (1 << 24)  /* TPM 2.0 */

#define TPM_TIS_STS_VALID                 (1 << 7)
#define TPM_TIS_STS_COMMAND_READY         (1 << 6)
#define TPM_TIS_STS_TPM_GO                (1 << 5)
#define TPM_TIS_STS_DATA_AVAILABLE        (1 << 4)
#define TPM_TIS_STS_EXPECT                (1 << 3)
#define TPM_TIS_STS_SELFTEST_DONE         (1 << 2)
#define TPM_TIS_STS_RESPONSE_RETRY        (1 << 1)

#define TPM_TIS_BURST_COUNT_SHIFT         8
#define TPM_TIS_BURST_COUNT(X) \
    ((X) << TPM_TIS_BURST_COUNT_SHIFT)

#define TPM_TIS_ACCESS_TPM_REG_VALID_STS  (1 << 7)
#define TPM_TIS_ACCESS_ACTIVE_LOCALITY    (1 << 5)
#define TPM_TIS_ACCESS_BEEN_SEIZED        (1 << 4)
#define TPM_TIS_ACCESS_SEIZE              (1 << 3)
#define TPM_TIS_ACCESS_PENDING_REQUEST    (1 << 2)
#define TPM_TIS_ACCESS_REQUEST_USE        (1 << 1)
#define TPM_TIS_ACCESS_TPM_ESTABLISHMENT  (1 << 0)

#define TPM_TIS_INT_ENABLED               (1 << 31)
#define TPM_TIS_INT_DATA_AVAILABLE        (1 << 0)
#define TPM_TIS_INT_STS_VALID             (1 << 1)
#define TPM_TIS_INT_LOCALITY_CHANGED      (1 << 2)
#define TPM_TIS_INT_COMMAND_READY         (1 << 7)

#define TPM_TIS_INT_POLARITY_MASK         (3 << 3)
#define TPM_TIS_INT_POLARITY_LOW_LEVEL    (1 << 3)

#define TPM_TIS_INTERRUPTS_SUPPORTED (TPM_TIS_INT_LOCALITY_CHANGED | \
                                      TPM_TIS_INT_DATA_AVAILABLE   | \
                                      TPM_TIS_INT_STS_VALID | \
                                      TPM_TIS_INT_COMMAND_READY)

#define TPM_TIS_CAP_INTERFACE_VERSION1_3 (2 << 28)
#define TPM_TIS_CAP_INTERFACE_VERSION1_3_FOR_TPM2_0 (3 << 28)
#define TPM_TIS_CAP_DATA_TRANSFER_64B    (3 << 9)
#define TPM_TIS_CAP_DATA_TRANSFER_LEGACY (0 << 9)
#define TPM_TIS_CAP_BURST_COUNT_DYNAMIC  (0 << 8)
#define TPM_TIS_CAP_INTERRUPT_LOW_LEVEL  (1 << 4) /* support is mandatory */
#define TPM_TIS_CAPABILITIES_SUPPORTED1_3 \
    (TPM_TIS_CAP_INTERRUPT_LOW_LEVEL | \
     TPM_TIS_CAP_BURST_COUNT_DYNAMIC | \
     TPM_TIS_CAP_DATA_TRANSFER_64B | \
     TPM_TIS_CAP_INTERFACE_VERSION1_3 | \
     TPM_TIS_INTERRUPTS_SUPPORTED)

#define TPM_TIS_CAPABILITIES_SUPPORTED2_0 \
    (TPM_TIS_CAP_INTERRUPT_LOW_LEVEL | \
     TPM_TIS_CAP_BURST_COUNT_DYNAMIC | \
     TPM_TIS_CAP_DATA_TRANSFER_64B | \
     TPM_TIS_CAP_INTERFACE_VERSION1_3_FOR_TPM2_0 | \
     TPM_TIS_INTERRUPTS_SUPPORTED)

#define TPM_TIS_IFACE_ID_INTERFACE_TIS1_3   (0xf)     /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INTERFACE_FIFO     (0x0)     /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INTERFACE_VER_FIFO (0 << 4)  /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_CAP_5_LOCALITIES   (1 << 8)  /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_CAP_TIS_SUPPORTED  (1 << 13) /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INT_SEL_LOCK       (1 << 19) /* TPM 2.0 */

#define TPM_TIS_IFACE_ID_SUPPORTED_FLAGS1_3 \
    (TPM_TIS_IFACE_ID_INTERFACE_TIS1_3 | \
     (~0u << 4)/* all of it is don't care */)

/* if backend was a TPM 2.0: */
#define TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0 \
    (TPM_TIS_IFACE_ID_INTERFACE_FIFO | \
     TPM_TIS_IFACE_ID_INTERFACE_VER_FIFO | \
     TPM_TIS_IFACE_ID_CAP_5_LOCALITIES | \
     TPM_TIS_IFACE_ID_CAP_TIS_SUPPORTED)

#define TPM_TIS_TPM_DID       0x0001
#define TPM_TIS_TPM_VID       PCI_VENDOR_ID_IBM
#define TPM_TIS_TPM_RID       0x0001

#define TPM_TIS_NO_DATA_BYTE  0xff


REG32(CRB_LOC_STATE, 0x00)
  FIELD(CRB_LOC_STATE, tpmEstablished, 0, 1)
  FIELD(CRB_LOC_STATE, locAssigned, 1, 1)
  FIELD(CRB_LOC_STATE, activeLocality, 2, 3)
  FIELD(CRB_LOC_STATE, reserved, 5, 2)
  FIELD(CRB_LOC_STATE, tpmRegValidSts, 7, 1)
REG32(CRB_LOC_CTRL, 0x08)
REG32(CRB_LOC_STS, 0x0C)
  FIELD(CRB_LOC_STS, Granted, 0, 1)
  FIELD(CRB_LOC_STS, beenSeized, 1, 1)
REG32(CRB_INTF_ID, 0x30)
  FIELD(CRB_INTF_ID, InterfaceType, 0, 4)
  FIELD(CRB_INTF_ID, InterfaceVersion, 4, 4)
  FIELD(CRB_INTF_ID, CapLocality, 8, 1)
  FIELD(CRB_INTF_ID, CapCRBIdleBypass, 9, 1)
  FIELD(CRB_INTF_ID, Reserved1, 10, 1)
  FIELD(CRB_INTF_ID, CapDataXferSizeSupport, 11, 2)
  FIELD(CRB_INTF_ID, CapFIFO, 13, 1)
  FIELD(CRB_INTF_ID, CapCRB, 14, 1)
  FIELD(CRB_INTF_ID, CapIFRes, 15, 2)
  FIELD(CRB_INTF_ID, InterfaceSelector, 17, 2)
  FIELD(CRB_INTF_ID, IntfSelLock, 19, 1)
  FIELD(CRB_INTF_ID, Reserved2, 20, 4)
  FIELD(CRB_INTF_ID, RID, 24, 8)
REG32(CRB_INTF_ID2, 0x34)
  FIELD(CRB_INTF_ID2, VID, 0, 16)
  FIELD(CRB_INTF_ID2, DID, 16, 16)
REG32(CRB_CTRL_EXT, 0x38)
REG32(CRB_CTRL_REQ, 0x40)
REG32(CRB_CTRL_STS, 0x44)
  FIELD(CRB_CTRL_STS, tpmSts, 0, 1)
  FIELD(CRB_CTRL_STS, tpmIdle, 1, 1)
REG32(CRB_CTRL_CANCEL, 0x48)
REG32(CRB_CTRL_START, 0x4C)
REG32(CRB_INT_ENABLED, 0x50)
REG32(CRB_INT_STS, 0x54)
REG32(CRB_CTRL_CMD_SIZE, 0x58)
REG32(CRB_CTRL_CMD_LADDR, 0x5C)
REG32(CRB_CTRL_CMD_HADDR, 0x60)
REG32(CRB_CTRL_RSP_SIZE, 0x64)
REG32(CRB_CTRL_RSP_ADDR, 0x68)
REG32(CRB_DATA_BUFFER, 0x80)

#define TPM_CRB_ADDR_BASE           0xFED40000
#define TPM_CRB_ADDR_SIZE           0x1000
#define TPM_CRB_ADDR_CTRL           (TPM_CRB_ADDR_BASE + A_CRB_CTRL_REQ)
#define TPM_CRB_R_MAX               R_CRB_DATA_BUFFER

#define TPM_LOG_AREA_MINIMUM_SIZE   (64 * KiB)

#define TPM_TCPA_ACPI_CLASS_CLIENT  0
#define TPM_TCPA_ACPI_CLASS_SERVER  1

#define TPM2_ACPI_CLASS_CLIENT      0
#define TPM2_ACPI_CLASS_SERVER      1

#define TPM2_START_METHOD_MMIO      6
#define TPM2_START_METHOD_CRB       7

/*
 * Physical Presence Interface
 */
#define TPM_PPI_ADDR_SIZE           0x400
#define TPM_PPI_ADDR_BASE           0xFED45000

#define TPM_PPI_VERSION_NONE        0
#define TPM_PPI_VERSION_1_30        1

/* whether function is blocked by BIOS settings; bits 0, 1, 2 */
#define TPM_PPI_FUNC_NOT_IMPLEMENTED     (0 << 0)
#define TPM_PPI_FUNC_BIOS_ONLY           (1 << 0)
#define TPM_PPI_FUNC_BLOCKED             (2 << 0)
#define TPM_PPI_FUNC_ALLOWED_USR_REQ     (3 << 0)
#define TPM_PPI_FUNC_ALLOWED_USR_NOT_REQ (4 << 0)
#define TPM_PPI_FUNC_MASK                (7 << 0)

void tpm_build_ppi_acpi(TPMIf *tpm, Aml *dev);

#endif /* HW_ACPI_TPM_H */
