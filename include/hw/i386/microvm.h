/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_I386_MICROVM_H
#define HW_I386_MICROVM_H

#include "exec/hwaddr.h"
#include "qemu/notify.h"

#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/pci-host/gpex.h"
#include "qom/object.h"

/*
 *  IRQ    |  pc        | microvm (acpi=on)
 * --------+------------+------------------
 *   0     |  pit       |
 *   1     |  kbd       |
 *   2     |  cascade   |
 *   3     |  serial 1  |
 *   4     |  serial 0  | serial
 *   5     |  -         |
 *   6     |  floppy    |
 *   7     |  parallel  |
 *   8     |  rtc       | rtc (rtc=on)
 *   9     |  acpi      | acpi (ged)
 *  10     |  pci lnk   | xhci (usb=on)
 *  11     |  pci lnk   |
 *  12     |  ps2       | pcie
 *  13     |  fpu       | pcie
 *  14     |  ide 0     | pcie
 *  15     |  ide 1     | pcie
 *  16-23  |  pci gsi   | virtio
 */

/* Platform virtio definitions */
#define VIRTIO_MMIO_BASE      0xfeb00000
#define VIRTIO_CMDLINE_MAXLEN 64

#define GED_MMIO_BASE         0xfea00000
#define GED_MMIO_BASE_MEMHP   (GED_MMIO_BASE + 0x100)
#define GED_MMIO_BASE_REGS    (GED_MMIO_BASE + 0x200)
#define GED_MMIO_IRQ          9

#define MICROVM_XHCI_BASE     0xfe900000
#define MICROVM_XHCI_IRQ      10

#define PCIE_MMIO_BASE        0xc0000000
#define PCIE_MMIO_SIZE        0x20000000
#define PCIE_ECAM_BASE        0xe0000000
#define PCIE_ECAM_SIZE        0x10000000

/* Machine type options */
#define MICROVM_MACHINE_PIT                 "pit"
#define MICROVM_MACHINE_PIC                 "pic"
#define MICROVM_MACHINE_RTC                 "rtc"
#define MICROVM_MACHINE_PCIE                "pcie"
#define MICROVM_MACHINE_IOAPIC2             "ioapic2"
#define MICROVM_MACHINE_ISA_SERIAL          "isa-serial"
#define MICROVM_MACHINE_OPTION_ROMS         "x-option-roms"
#define MICROVM_MACHINE_AUTO_KERNEL_CMDLINE "auto-kernel-cmdline"

struct MicrovmMachineClass {
    X86MachineClass parent;
    HotplugHandler *(*orig_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
};

struct MicrovmMachineState {
    X86MachineState parent;

    /* Machine type options */
    OnOffAuto pic;
    OnOffAuto pit;
    OnOffAuto rtc;
    OnOffAuto pcie;
    OnOffAuto ioapic2;
    bool isa_serial;
    bool option_roms;
    bool auto_kernel_cmdline;

    /* Machine state */
    uint32_t pcie_irq_base;
    uint32_t virtio_irq_base;
    uint32_t virtio_num_transports;
    bool kernel_cmdline_fixed;
    Notifier machine_done;
    Notifier powerdown_req;
    struct GPEXConfig gpex;

    /* device tree */
    void *fdt;
    uint32_t ioapic_phandle[2];
};

#define TYPE_MICROVM_MACHINE   MACHINE_TYPE_NAME("microvm")
OBJECT_DECLARE_TYPE(MicrovmMachineState, MicrovmMachineClass, MICROVM_MACHINE)

#endif
