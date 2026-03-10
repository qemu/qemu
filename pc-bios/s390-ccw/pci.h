/*
 * s390x PCI definitions
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>
#include "clp.h"

#define ZPCI_CREATE_REQ(handle, space, len)                    \
    ((uint64_t) handle << 32 | space << 16 | len)

union register_pair {
    unsigned __int128 pair;
    struct {
        unsigned long even;
        unsigned long odd;
    };
};

#define PCI_CFGBAR             0xF  /* Base Address Register for config space */
#define PCI_CMD_REG            0x4  /* Offset of command register */
#define PCI_CAPABILITY_LIST    0x34 /* Offset of first capability list entry */

#define PCI_BUS_MASTER_MASK    0x0020 /* LE bit 3 of 16 bit register */

int pci_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint64_t data,
              uint8_t len);
int pci_read(uint32_t fhandle, uint64_t offset, uint8_t pcias, void *buf,
             uint8_t len);

#endif
