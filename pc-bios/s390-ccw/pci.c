/*
 * s390x PCI functionality
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include "pci.h"
#include "bswap.h"
#include <stdio.h>
#include <stdbool.h>

/* PCI load */
static inline int pcilg(uint64_t *data, uint64_t req, uint64_t offset,
                        uint8_t *status)
{
    union register_pair req_off = {.even = req, .odd = offset};
    int cc = -1;
    uint64_t __data;

    asm volatile (
        "     .insn   rre,0xb9d20000,%[data],%[req_off]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), [data] "=d" (__data),
          [req_off] "+d" (req_off.pair) :: "cc");
    *status = req_off.even >> 24 & 0xff;
    *data = __data;
    return cc;
}

/* PCI store */
static inline int pcistg(uint64_t data, uint64_t req, uint64_t offset,
                         uint8_t *status)
{
    union register_pair req_off = {.even = req, .odd = offset};
    int cc = -1;

    asm volatile (
        "     .insn   rre,0xb9d00000,%[data],%[req_off]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), [req_off] "+d" (req_off.pair)
        : [data] "d" (data)
        : "cc");
    *status = req_off.even >> 24 & 0xff;
    return cc;
}

int pci_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint64_t data,
              uint8_t len)
{

    uint64_t req = ZPCI_CREATE_REQ(fhandle, pcias, len);
    uint8_t status;
    int rc;

    /* len must be non-zero power of 2 with a maximum of 8 bytes per write */
    switch (len) {
    case 1:
    case 2:
    case 4:
    case 8:
        rc = pcistg(data, req, offset, &status);
        break;
    default:
        return -1;
    }

    /* Error condition detected */
    if (rc != 0) {
        printf("PCI store failed with status condition %d, return code %d\n",
               status, rc);
        return -1;
    }

    return 0;
}

int pci_read(uint32_t fhandle, uint64_t offset, uint8_t pcias, void *buf,
             uint8_t len)
{
    uint64_t req, data;
    uint8_t status;
    int rc;

    req = ZPCI_CREATE_REQ(fhandle, pcias, len);
    rc = pcilg(&data, req, offset, &status);

    /* Error condition detected */
    if (rc != 0) {
        printf("PCI load failed with status condition %d, return code %d\n",
               status, rc);
        return -1;
    }

    switch (len) {
    case 1:
        *(uint8_t *)buf = data;
        break;
    case 2:
        *(uint16_t *)buf = data;
        break;
    case 4:
        *(uint32_t *)buf = data;
        break;
    case 8:
        *(uint64_t *)buf = data;
        break;
    default:
        return -1;
    }

    return 0;
}
