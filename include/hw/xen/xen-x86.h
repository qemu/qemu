/*
 * Xen X86-specific
 *
 * Copyright 2020 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_HW_XEN_X86_H
#define QEMU_HW_XEN_X86_H

#include "hw/i386/pc.h"

void xen_hvm_init_pc(PCMachineState *pcms, MemoryRegion **ram_memory);

#endif /* QEMU_HW_XEN_X86_H */
