/*
 * PA-RISC cpu parameters for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HPPA_CPU_PARAM_H
#define HPPA_CPU_PARAM_H

#define TARGET_LONG_BITS              64

#if defined(CONFIG_USER_ONLY) && defined(TARGET_ABI32)
# define TARGET_PHYS_ADDR_SPACE_BITS  32
# define TARGET_VIRT_ADDR_SPACE_BITS  32
#else
/* ??? PA-8000 through 8600 have 40 bits; PA-8700 and 8900 have 44 bits. */
# define TARGET_PHYS_ADDR_SPACE_BITS  40
# define TARGET_VIRT_ADDR_SPACE_BITS  64
#endif

#define TARGET_PAGE_BITS 12

/* PA-RISC 1.x processors have a strong memory model.  */
/*
 * ??? While we do not yet implement PA-RISC 2.0, those processors have
 * a weak memory model, but with TLB bits that force ordering on a per-page
 * basis.  It's probably easier to fall back to a strong memory model.
 */
#define TCG_GUEST_DEFAULT_MO        TCG_MO_ALL

#endif
