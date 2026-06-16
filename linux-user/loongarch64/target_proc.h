/*
 * Loongson specific proc functions for linux-user
 *
 * Copyright (c) 2026 Helge Deller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LOONGARCH_TARGET_PROC_H
#define LOONGARCH_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    uint32_t elf_hwcap = get_elf_hwcap(env_cpu(cpu_env));
    uint32_t prid, ser_id, pabits, vabits, i, n, num_cpus;
    bool is_64bit = is_la64(cpu_env);
    const char *hwcap_str;
    struct timespec res;
    double freq_mhz;

#if TARGET_LONG_BITS == 32
    pabits = 31;
    vabits = 31;
#else
    pabits = FIELD_EX32(cpu_env->cpucfg[1], CPUCFG1, PALEN);
    vabits = FIELD_EX32(cpu_env->cpucfg[1], CPUCFG1, VALEN);
#endif

    if (clock_getres(CLOCK_REALTIME, &res) == -1) {
        res.tv_nsec = 1;
    }
    freq_mhz = 1000.0 / res.tv_nsec;

    ser_id = FIELD_EX32(cpu_env->cpucfg[0], CPUCFG0, SERID);
    prid   = FIELD_EX32(cpu_env->cpucfg[0], CPUCFG0, PRID);

    dprintf(fd, "system type\t\t: generic-loongson-machine\n");

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (n = 0; n < num_cpus; n++) {
        dprintf(fd, "\nprocessor\t\t: %d\n", n);
        dprintf(fd, "package\t\t\t: 0\n");
        dprintf(fd, "core\t\t\t: %d\n", n);
        dprintf(fd, "global_id\t\t: %d\n", n);
        dprintf(fd, "CPU Family\t\t: Loongson-%dbit\n", is_64bit ? 64 : 32);
        dprintf(fd, "Model Name\t\t: QEMU_user-v" QEMU_VERSION "\n");
        dprintf(fd, "PRID\t\t\t: %s (%08x)\n",
                    ser_id == PRID_SERIES_LA464 ? "LA464" :
                    ser_id == PRID_SERIES_LA132 ? "LA132" : "Unknown",
                    cpu_env->cpucfg[0]);
        dprintf(fd, "CPU Revision\t\t: 0x%02x\n", prid);
        dprintf(fd, "FPU Revision\t\t: 0x%02x\n",
                    FIELD_EX32(cpu_env->cpucfg[2], CPUCFG2, FP_VER));
        dprintf(fd, "CPU MHz\t\t\t: %.2f\n", freq_mhz);
        dprintf(fd, "Address Sizes\t\t: %d bits physical, %d bits virtual\n",
                    pabits + 1, vabits + 1);

        dprintf(fd, "ISA\t\t\t:%s", " loongarch32r loongarch32s");
        if (is_64bit) {
            dprintf(fd, " loongarch64");
        }

        dprintf(fd, "\nFeatures\t\t:");
        for (i = 0; i < sizeof(elf_hwcap) * 8; i++) {
            if (!(elf_hwcap & (1 << i))) {
                continue;
            }
            hwcap_str = elf_hwcap_str(i);
            if (hwcap_str) {
                dprintf(fd, " %s", hwcap_str);
            }
        }
        dprintf(fd, "\nHardware Watchpoint\t: no\n");
    }
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* LOONGARCH_TARGET_PROC_H */
