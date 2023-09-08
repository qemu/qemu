/*
 * Sparc specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SPARC_TARGET_PROC_H
#define SPARC_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    dprintf(fd, "type\t\t: sun4u\n");
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* SPARC_TARGET_PROC_H */
