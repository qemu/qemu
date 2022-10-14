/*
 * PCIe Data Object Exchange
 *
 * Copyright (C) 2021 Avery Design Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef PCIE_DOE_H
#define PCIE_DOE_H

#include "qemu/range.h"
#include "qemu/typedefs.h"
#include "hw/register.h"

/*
 * Reference:
 * PCIe r6.0 - 7.9.24 Data Object Exchange Extended Capability
 */
/* Capabilities Register - r6.0 7.9.24.2 */
#define PCI_EXP_DOE_CAP             0x04
REG32(PCI_DOE_CAP_REG, 0)
    FIELD(PCI_DOE_CAP_REG, INTR_SUPP, 0, 1)
    FIELD(PCI_DOE_CAP_REG, DOE_INTR_MSG_NUM, 1, 11)

/* Control Register - r6.0 7.9.24.3 */
#define PCI_EXP_DOE_CTRL            0x08
REG32(PCI_DOE_CAP_CONTROL, 0)
    FIELD(PCI_DOE_CAP_CONTROL, DOE_ABORT, 0, 1)
    FIELD(PCI_DOE_CAP_CONTROL, DOE_INTR_EN, 1, 1)
    FIELD(PCI_DOE_CAP_CONTROL, DOE_GO, 31, 1)

/* Status Register - r6.0 7.9.24.4 */
#define PCI_EXP_DOE_STATUS          0x0c
REG32(PCI_DOE_CAP_STATUS, 0)
    FIELD(PCI_DOE_CAP_STATUS, DOE_BUSY, 0, 1)
    FIELD(PCI_DOE_CAP_STATUS, DOE_INTR_STATUS, 1, 1)
    FIELD(PCI_DOE_CAP_STATUS, DOE_ERROR, 2, 1)
    FIELD(PCI_DOE_CAP_STATUS, DATA_OBJ_RDY, 31, 1)

/* Write Data Mailbox Register - r6.0 7.9.24.5 */
#define PCI_EXP_DOE_WR_DATA_MBOX    0x10

/* Read Data Mailbox Register - 7.9.xx.6 */
#define PCI_EXP_DOE_RD_DATA_MBOX    0x14

/* PCI-SIG defined Data Object Types - r6.0 Table 6-32 */
#define PCI_SIG_DOE_DISCOVERY       0x00

#define PCI_DOE_DW_SIZE_MAX         (1 << 18)
#define PCI_DOE_PROTOCOL_NUM_MAX    256

#define DATA_OBJ_BUILD_HEADER1(v, p)    (((p) << 16) | (v))
#define DATA_OBJ_LEN_MASK(len)          ((len) & (PCI_DOE_DW_SIZE_MAX - 1))

typedef struct DOEHeader DOEHeader;
typedef struct DOEProtocol DOEProtocol;
typedef struct DOECap DOECap;

struct DOEHeader {
    uint16_t vendor_id;
    uint8_t data_obj_type;
    uint8_t reserved;
    uint32_t length;
} QEMU_PACKED;

/* Protocol infos and rsp function callback */
struct DOEProtocol {
    uint16_t vendor_id;
    uint8_t data_obj_type;
    bool (*handle_request)(DOECap *);
};

struct DOECap {
    /* Owner */
    PCIDevice *pdev;

    uint16_t offset;

    struct {
        bool intr;
        uint16_t vec;
    } cap;

    struct {
        bool abort;
        bool intr;
        bool go;
    } ctrl;

    struct {
        bool busy;
        bool intr;
        bool error;
        bool ready;
    } status;

    uint32_t *write_mbox;
    uint32_t *read_mbox;

    /* Mailbox position indicator */
    uint32_t read_mbox_idx;
    uint32_t read_mbox_len;
    uint32_t write_mbox_len;

    /* Protocols and its callback response */
    DOEProtocol *protocols;
    uint16_t protocol_num;
};

void pcie_doe_init(PCIDevice *pdev, DOECap *doe_cap, uint16_t offset,
                   DOEProtocol *protocols, bool intr, uint16_t vec);
void pcie_doe_fini(DOECap *doe_cap);
bool pcie_doe_read_config(DOECap *doe_cap, uint32_t addr, int size,
                          uint32_t *buf);
void pcie_doe_write_config(DOECap *doe_cap, uint32_t addr,
                           uint32_t val, int size);
uint32_t pcie_doe_build_protocol(DOEProtocol *p);
void *pcie_doe_get_write_mbox_ptr(DOECap *doe_cap);
void pcie_doe_set_rsp(DOECap *doe_cap, void *rsp);
uint32_t pcie_doe_get_obj_len(void *obj);
#endif /* PCIE_DOE_H */
