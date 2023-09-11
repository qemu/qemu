/*
 * Xilinx CFI interface
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Written by Francisco Iglesias <francisco.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XLNX_CFI_IF_H
#define XLNX_CFI_IF_H 1

#include "qemu/help-texts.h"
#include "hw/hw.h"
#include "qom/object.h"

#define TYPE_XLNX_CFI_IF "xlnx-cfi-if"
typedef struct XlnxCfiIfClass XlnxCfiIfClass;
DECLARE_CLASS_CHECKERS(XlnxCfiIfClass, XLNX_CFI_IF, TYPE_XLNX_CFI_IF)

#define XLNX_CFI_IF(obj) \
     INTERFACE_CHECK(XlnxCfiIf, (obj), TYPE_XLNX_CFI_IF)

typedef enum {
    PACKET_TYPE_CFU = 0x52,
    PACKET_TYPE_CFRAME = 0xA1,
} xlnx_cfi_packet_type;

typedef enum {
    CFRAME_FAR = 1,
    CFRAME_SFR = 2,
    CFRAME_FDRI = 4,
    CFRAME_CMD = 6,
} xlnx_cfi_reg_addr;

typedef struct XlnxCfiPacket {
    uint8_t reg_addr;
    uint32_t data[4];
} XlnxCfiPacket;

typedef struct XlnxCfiIf {
    Object Parent;
} XlnxCfiIf;

typedef struct XlnxCfiIfClass {
    InterfaceClass parent;

    void (*cfi_transfer_packet)(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt);
} XlnxCfiIfClass;

/**
 * Transfer a XlnxCfiPacket.
 *
 * @cfi_if: the object implementing this interface
 * @XlnxCfiPacket: a pointer to the XlnxCfiPacket to transfer
 */
void xlnx_cfi_transfer_packet(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt);

#endif /* XLNX_CFI_IF_H */
