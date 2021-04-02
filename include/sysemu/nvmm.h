/*
 * Copyright (c) 2018-2019 Maxime Villard, All rights reserved.
 *
 * NetBSD Virtual Machine Monitor (NVMM) accelerator support.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_NVMM_H
#define QEMU_NVMM_H

#include "config-host.h"
#include "qemu-common.h"

#ifdef CONFIG_NVMM

int nvmm_enabled(void);

#else /* CONFIG_NVMM */

#define nvmm_enabled() (0)

#endif /* CONFIG_NVMM */

#endif /* CONFIG_NVMM */
