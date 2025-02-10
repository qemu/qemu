/*
 * Sparc specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SPARC_TARGET_PROC_H
#define SPARC_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int i, num_cpus;
    const char *cpu_type;

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_env->def.features & CPU_FEATURE_HYPV) {
        cpu_type = "sun4v";
    } else {
        cpu_type = "sun4u";
    }

    dprintf(fd, "cpu\t\t: %s (QEMU)\n", cpu_env->def.name);
    dprintf(fd, "type\t\t: %s\n", cpu_type);
    dprintf(fd, "ncpus probed\t: %d\n", num_cpus);
    dprintf(fd, "ncpus active\t: %d\n", num_cpus);
    dprintf(fd, "State:\n");
    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "CPU%d:\t\t: online\n", i);
    }

    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* SPARC_TARGET_PROC_H */
