/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "exec/cpu-common.h"

/*
 * user-mode CPUs never create address spaces with
 * cpu_address_space_init(), so the cleanup function doesn't
 * need to do anything. We need this stub because cpu-common.c
 * is built-once so it can't #ifndef CONFIG_USER around the
 * call; the real function is in physmem.c which is system-only.
 */
void cpu_destroy_address_spaces(CPUState *cpu)
{
}
