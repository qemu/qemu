/*
 * Xtensa cpu parameters for qemu.
 *
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef XTENSA_CPU_PARAM_H
#define XTENSA_CPU_PARAM_H

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#ifdef CONFIG_USER_ONLY
#define TARGET_VIRT_ADDR_SPACE_BITS 30
#else
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#endif
