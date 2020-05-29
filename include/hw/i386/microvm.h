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

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "qemu/notify.h"

#include "hw/boards.h"
#include "hw/i386/x86.h"

/* Platform virtio definitions */
#define VIRTIO_MMIO_BASE      0xfeb00000
#define VIRTIO_IRQ_BASE       5
#define VIRTIO_NUM_TRANSPORTS 8
#define VIRTIO_CMDLINE_MAXLEN 64

/* Machine type options */
#define MICROVM_MACHINE_PIT                 "pit"
#define MICROVM_MACHINE_PIC                 "pic"
#define MICROVM_MACHINE_RTC                 "rtc"
#define MICROVM_MACHINE_ISA_SERIAL          "isa-serial"
#define MICROVM_MACHINE_OPTION_ROMS         "x-option-roms"
#define MICROVM_MACHINE_AUTO_KERNEL_CMDLINE "auto-kernel-cmdline"

typedef struct {
    X86MachineClass parent;
    HotplugHandler *(*orig_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
} MicrovmMachineClass;

typedef struct {
    X86MachineState parent;

    /* Machine type options */
    OnOffAuto pic;
    OnOffAuto pit;
    OnOffAuto rtc;
    bool isa_serial;
    bool option_roms;
    bool auto_kernel_cmdline;

    /* Machine state */
    bool kernel_cmdline_fixed;
} MicrovmMachineState;

#define TYPE_MICROVM_MACHINE   MACHINE_TYPE_NAME("microvm")
#define MICROVM_MACHINE(obj) \
    OBJECT_CHECK(MicrovmMachineState, (obj), TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MicrovmMachineClass, obj, TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_CLASS(class) \
    OBJECT_CLASS_CHECK(MicrovmMachineClass, class, TYPE_MICROVM_MACHINE)

#endif
