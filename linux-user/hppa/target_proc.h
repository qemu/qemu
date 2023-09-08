/*
 * HPPA specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HPPA_TARGET_PROC_H
#define HPPA_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int i, num_cpus;

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "processor\t: %d\n", i);
        dprintf(fd, "cpu family\t: PA-RISC 1.1e\n");
        dprintf(fd, "cpu\t\t: PA7300LC (PCX-L2)\n");
        dprintf(fd, "capabilities\t: os32\n");
        dprintf(fd, "model\t\t: 9000/778/B160L - "
                    "Merlin L2 160 QEMU (9000/778/B160L)\n\n");
    }
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* HPPA_TARGET_PROC_H */
