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

#ifdef NEED_CPU_H

#ifdef CONFIG_WHPX

int whpx_enabled(void);
bool whpx_apic_in_platform(void);

#else /* CONFIG_WHPX */

#define whpx_enabled() (0)
#define whpx_apic_in_platform() (0)

#endif /* CONFIG_WHPX */

#endif /* NEED_CPU_H */

#endif /* QEMU_WHPX_H */
