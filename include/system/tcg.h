/*
 * QEMU TCG support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* header to be included in non-TCG-specific code */

#ifndef SYSTEM_TCG_H
#define SYSTEM_TCG_H

#ifdef CONFIG_TCG
extern bool tcg_allowed;
#define tcg_enabled() (tcg_allowed)
#else
#define tcg_enabled() 0
#endif

#endif
