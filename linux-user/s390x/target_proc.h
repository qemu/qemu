/*
 * S390X specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef S390X_TARGET_PROC_H
#define S390X_TARGET_PROC_H

/*
 * Emulate what a Linux kernel running in qemu-system-s390x -M accel=tcg would
 * show in /proc/cpuinfo.
 *
 * Skip the following in order to match the missing support in op_ecag():
 * - show_cacheinfo().
 * - show_cpu_topology().
 * - show_cpu_mhz().
 *
 * Use fixed values for certain fields:
 * - bogomips per cpu - from a qemu-system-s390x run.
 * - max thread id = 0, since SMT / SIGP_SET_MULTI_THREADING is not supported.
 *
 * Keep the code structure close to arch/s390/kernel/processor.c.
 */

static void show_facilities(int fd)
{
    size_t sizeof_stfl_bytes = 2048;
    g_autofree uint8_t *stfl_bytes = g_new0(uint8_t, sizeof_stfl_bytes);
    unsigned int bit;

    dprintf(fd, "facilities      :");
    s390_get_feat_block(S390_FEAT_TYPE_STFL, stfl_bytes);
    for (bit = 0; bit < sizeof_stfl_bytes * 8; bit++) {
        if (test_be_bit(bit, stfl_bytes)) {
            dprintf(fd, " %d", bit);
        }
    }
    dprintf(fd, "\n");
}

static int cpu_ident(unsigned long n)
{
    return deposit32(0, CPU_ID_BITS - CPU_PHYS_ADDR_BITS, CPU_PHYS_ADDR_BITS,
                     n);
}

static void show_cpu_summary(CPUArchState *cpu_env, int fd)
{
    S390CPUModel *model = env_archcpu(cpu_env)->model;
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t elf_hwcap = get_elf_hwcap();
    const char *hwcap_str;
    int i;

    dprintf(fd, "vendor_id       : IBM/S390\n"
                "# processors    : %i\n"
                "bogomips per cpu: 13370.00\n",
            num_cpus);
    dprintf(fd, "max thread id   : 0\n");
    dprintf(fd, "features\t: ");
    for (i = 0; i < sizeof(elf_hwcap) * 8; i++) {
        if (!(elf_hwcap & (1 << i))) {
            continue;
        }
        hwcap_str = elf_hwcap_str(i);
        if (hwcap_str) {
            dprintf(fd, "%s ", hwcap_str);
        }
    }
    dprintf(fd, "\n");
    show_facilities(fd);
    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "processor %d: "
               "version = %02X,  "
               "identification = %06X,  "
               "machine = %04X\n",
               i, model->cpu_ver, cpu_ident(i), model->def->type);
    }
}

static void show_cpu_ids(CPUArchState *cpu_env, int fd, unsigned long n)
{
    S390CPUModel *model = env_archcpu(cpu_env)->model;

    dprintf(fd, "version         : %02X\n", model->cpu_ver);
    dprintf(fd, "identification  : %06X\n", cpu_ident(n));
    dprintf(fd, "machine         : %04X\n", model->def->type);
}

static void show_cpuinfo(CPUArchState *cpu_env, int fd, unsigned long n)
{
    dprintf(fd, "\ncpu number      : %ld\n", n);
    show_cpu_ids(cpu_env, fd, n);
}

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int i;

    show_cpu_summary(cpu_env, fd);
    for (i = 0; i < num_cpus; i++) {
        show_cpuinfo(cpu_env, fd, i);
    }
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* S390X_TARGET_PROC_H */
