/*
 * M68K specific proc functions for linux-user
 *
 * Copyright (c) 2026 Helge Deller
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


static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    const char *cpu, *fpu;
    struct timespec res;
    double freq_mhz;

    if (clock_getres(CLOCK_REALTIME, &res) == -1) {
        res.tv_nsec = 1;
    }
    freq_mhz = 1000.0 / res.tv_nsec;

    if (m68k_feature(cpu_env, M68K_FEATURE_M68010)) {
        cpu = "68010";
    } else if (m68k_feature(cpu_env, M68K_FEATURE_M68020)) {
        cpu = "68020";
    } else if (m68k_feature(cpu_env, M68K_FEATURE_M68030)) {
        cpu = "68030";
    } else if (m68k_feature(cpu_env, M68K_FEATURE_M68040)) {
        cpu = "68040";
    } else if (m68k_feature(cpu_env, M68K_FEATURE_M68060)) {
        cpu = "68060";
    } else {
        cpu = "680x0";
    }

    if (m68k_feature(cpu_env, M68K_FEATURE_FPU)) {
        fpu = cpu;
    } else {
	fpu = "none(soft float)";
    }

    dprintf(fd, "CPU:\t\t%s\n"
                "MMU:\t\t%s\n"
                "FPU:\t\t%s\n"
                "Clocking:\t%.1fMHz\n"
                "Model:\t\tQEMU user v" QEMU_VERSION "\n",
                cpu, cpu, fpu, freq_mhz);
    /* dropped BogoMips and Calibration for now */

    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* M68K_TARGET_PROC_H */
