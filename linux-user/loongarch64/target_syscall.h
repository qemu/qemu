/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_SYSCALL_H
#define LOONGARCH_TARGET_SYSCALL_H

#include "qemu/units.h"

#define UNAME_MACHINE "loongarch64"
#define UNAME_MINIMUM_RELEASE "5.19.0"

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif
