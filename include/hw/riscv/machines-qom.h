/*
 * QOM type definitions for riscv32 / riscv64 machines
 *
 *  Copyright (c) rev.ng Labs Srl.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_MACHINES_QOM_H
#define HW_RISCV_MACHINES_QOM_H

#include "hw/core/boards.h"

#define TYPE_TARGET_RISCV32_MACHINE \
        "target-info-riscv32-machine"

#define TYPE_TARGET_RISCV64_MACHINE \
        "target-info-riscv64-machine"

/*
 * Interfaces specifying whether a given QOM object is available in
 * qemu-system-riscv32, qemu-system-riscv64, or both.
 */

extern const InterfaceInfo riscv32_machine_interfaces[];
extern const InterfaceInfo riscv64_machine_interfaces[];
extern const InterfaceInfo riscv32_64_machine_interfaces[];

/*
 * Helper macros for defining machines available in qemu-system-riscv32,
 * qemu-system-riscv64, or both.
 */

#define DEFINE_MACHINE_RISCV32(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            riscv32_machine_interfaces)

#define DEFINE_MACHINE_RISCV64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            riscv64_machine_interfaces)

#define DEFINE_MACHINE_RISCV32_64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            riscv32_64_machine_interfaces)

#endif
