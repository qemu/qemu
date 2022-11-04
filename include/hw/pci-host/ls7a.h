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

#define VIRT_PCI_MEM_BASE        0x40000000UL
#define VIRT_PCI_MEM_SIZE        0x40000000UL
#define VIRT_PCI_IO_OFFSET       0x4000
#define VIRT_PCI_CFG_BASE        0x20000000
#define VIRT_PCI_CFG_SIZE        0x08000000
#define VIRT_PCI_IO_BASE         0x18004000UL
#define VIRT_PCI_IO_SIZE         0xC000

#define VIRT_PCH_REG_BASE        0x10000000UL
#define VIRT_IOAPIC_REG_BASE     (VIRT_PCH_REG_BASE)
#define VIRT_PCH_MSI_ADDR_LOW    0x2FF00000UL

/*
 * According to the kernel pch irq start from 64 offset
 * 0 ~ 16 irqs used for non-pci device while 16 ~ 64 irqs
 * used for pci device.
 */
#define PCH_PIC_IRQ_OFFSET       64
#define VIRT_DEVICE_IRQS         16
#define VIRT_PCI_IRQS            48
#define VIRT_UART_IRQ            (PCH_PIC_IRQ_OFFSET + 2)
#define VIRT_UART_BASE           0x1fe001e0
#define VIRT_UART_SIZE           0X100
#define VIRT_RTC_IRQ             (PCH_PIC_IRQ_OFFSET + 3)
#define VIRT_MISC_REG_BASE       (VIRT_PCH_REG_BASE + 0x00080000)
#define VIRT_RTC_REG_BASE        (VIRT_MISC_REG_BASE + 0x00050100)
#define VIRT_RTC_LEN             0x100
#define VIRT_SCI_IRQ             (PCH_PIC_IRQ_OFFSET + 4)

#define VIRT_PLATFORM_BUS_BASEADDRESS   0x16000000
#define VIRT_PLATFORM_BUS_SIZE          0x2000000
#define VIRT_PLATFORM_BUS_NUM_IRQS      2
#define VIRT_PLATFORM_BUS_IRQ           69
#endif
