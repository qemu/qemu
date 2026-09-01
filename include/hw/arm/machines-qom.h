/*
 * QOM type definitions for ARM / Aarch64 machines
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_MACHINES_QOM_H
#define HW_ARM_MACHINES_QOM_H

#include "hw/core/boards.h"

/*
 * A machine defined with the DEFINE_MACHINE_ARM() macro will be
 * available in both qemu-system-arm and qemu-system-aarch64 binaries.
 *
 * One defined with DEFINE_MACHINE_AARCH64() will only be available in
 * the qemu-system-aarch64 binary.
 */
#define DEFINE_MACHINE_ARM(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            NULL)
#define DEFINE_MACHINE_AARCH64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            NULL)

#endif
