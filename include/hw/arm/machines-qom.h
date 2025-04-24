/*
 * QOM type definitions for ARM / Aarch64 machines
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_MACHINES_QOM_H
#define HW_ARM_MACHINES_QOM_H

#include "hw/boards.h"

#define TYPE_TARGET_ARM_MACHINE \
        "target-info-arm-machine"

#define TYPE_TARGET_AARCH64_MACHINE \
        "target-info-aarch64-machine"

/*
 * A machine filtered with arm_machine_interfaces[] or
 * arm_aarch64_machine_interfaces[] will be available
 * in both qemu-system-arm and qemu-system-aarch64 binaries.
 *
 * One filtered with aarch64_machine_interfaces[] will only
 * be available in the qemu-system-aarch64 binary.
 */
extern const InterfaceInfo arm_machine_interfaces[];
extern const InterfaceInfo arm_aarch64_machine_interfaces[];
extern const InterfaceInfo aarch64_machine_interfaces[];

/*
 * A machine defined with the DEFINE_MACHINE_ARM() macro will be
 * available in both qemu-system-arm and qemu-system-aarch64 binaries.
 *
 * One defined with DEFINE_MACHINE_AARCH64() will only be available in
 * the qemu-system-aarch64 binary.
 */
#define DEFINE_MACHINE_ARM(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            arm_machine_interfaces)
#define DEFINE_MACHINE_AARCH64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            aarch64_machine_interfaces)

#endif
