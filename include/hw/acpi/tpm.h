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

#include "hw/registerfields.h"

#define TPM_TIS_ADDR_BASE           0xFED40000
#define TPM_TIS_ADDR_SIZE           0x5000

#define TPM_TIS_IRQ                 5

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

#define TPM_LOG_AREA_MINIMUM_SIZE   (64 * 1024)

#define TPM_TCPA_ACPI_CLASS_CLIENT  0
#define TPM_TCPA_ACPI_CLASS_SERVER  1

#define TPM2_ACPI_CLASS_CLIENT      0
#define TPM2_ACPI_CLASS_SERVER      1

#define TPM2_START_METHOD_MMIO      6
#define TPM2_START_METHOD_CRB       7

#endif /* HW_ACPI_TPM_H */
