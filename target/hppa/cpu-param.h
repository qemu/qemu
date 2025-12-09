/*
 * PA-RISC cpu parameters for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HPPA_CPU_PARAM_H
#define HPPA_CPU_PARAM_H

#if defined(CONFIG_USER_ONLY) && defined(TARGET_ABI32)
# define TARGET_VIRT_ADDR_SPACE_BITS  32
#else
# define TARGET_VIRT_ADDR_SPACE_BITS  64
#endif

#define TARGET_PAGE_BITS 12

#endif
