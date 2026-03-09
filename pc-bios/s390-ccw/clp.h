/*
 * Call Logical Processor (CLP) architecture definitions
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CLP_H
#define CLP_H

#ifndef QEMU_PACKED
#define QEMU_PACKED __attribute__((packed))
#endif

#include <stdint.h>
#include <s390-pci-clp.h>

int clp_pci(void *data);
int find_pci_function(uint32_t fid, ClpFhListEntry *entry);
int enable_pci_function(uint32_t *fhandle);

#endif
