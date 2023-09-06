/*
 * M68K specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef M68K_TARGET_PROC_H
#define M68K_TARGET_PROC_H

static int open_hardware(CPUArchState *cpu_env, int fd)
{
    dprintf(fd, "Model:\t\tqemu-m68k\n");
    return 0;
}
#define HAVE_ARCH_PROC_HARDWARE

#endif /* M68K_TARGET_PROC_H */
