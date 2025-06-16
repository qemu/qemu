/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX) support
 *
 * Copyright Microsoft, Corp. 2017
 *
 * Authors:
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-WHPX-specific code */

#ifndef QEMU_WHPX_H
#define QEMU_WHPX_H

#ifdef COMPILING_PER_TARGET
# ifdef CONFIG_WHPX
#  define CONFIG_WHPX_IS_POSSIBLE
# endif /* !CONFIG_WHPX */
#else
# define CONFIG_WHPX_IS_POSSIBLE
#endif /* COMPILING_PER_TARGET */

#ifdef CONFIG_WHPX_IS_POSSIBLE
extern bool whpx_allowed;
#define whpx_enabled() (whpx_allowed)
bool whpx_apic_in_platform(void);
#else /* !CONFIG_WHPX_IS_POSSIBLE */
#define whpx_enabled() 0
#define whpx_apic_in_platform() (0)
#endif /* !CONFIG_WHPX_IS_POSSIBLE */

#endif /* QEMU_WHPX_H */
