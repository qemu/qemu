/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LS7A_H
#define HW_LS7A_H

#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci-host/pam.h"
#include "qemu/units.h"
#include "qemu/range.h"
#include "qom/object.h"

#define LS7A_PCH_REG_BASE       0x10000000UL
#define LS7A_IOAPIC_REG_BASE    (LS7A_PCH_REG_BASE)
#define LS7A_PCH_MSI_ADDR_LOW   0x2FF00000UL

/*
 * According to the kernel pch irq start from 64 offset
 * 0 ~ 16 irqs used for non-pci device while 16 ~ 64 irqs
 * used for pci device.
 */
#define PCH_PIC_IRQ_OFFSET      64
#define LS7A_DEVICE_IRQS        16
#define LS7A_PCI_IRQS           48
#endif
