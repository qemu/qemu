/*
 * QEMU TCG support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_TCG_H
#define SYSEMU_TCG_H

extern bool tcg_allowed;
void tcg_exec_init(unsigned long tb_size);
#ifdef CONFIG_TCG
#define tcg_enabled() (tcg_allowed)
#else
#define tcg_enabled() 0
#endif

#endif
