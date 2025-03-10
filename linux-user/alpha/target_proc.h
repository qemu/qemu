/*
 * Alpha specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ALPHA_TARGET_PROC_H
#define ALPHA_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int max_cpus = sysconf(_SC_NPROCESSORS_CONF);
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    unsigned long cpu_mask;
    char model[32];
    const char *p, *q;
    int t;

    p = object_class_get_name(OBJECT_CLASS(env_cpu(cpu_env)->cc));
    q = strchr(p, '-');
    t = q - p;
    assert(t < sizeof(model));
    memcpy(model, p, t);
    model[t] = 0;

    t = sched_getaffinity(getpid(), sizeof(cpu_mask), (cpu_set_t *)&cpu_mask);
    if (t < 0) {
        if (num_cpus >= sizeof(cpu_mask) * 8) {
            cpu_mask = -1;
        } else {
            cpu_mask = (1UL << num_cpus) - 1;
        }
    }

    dprintf(fd,
            "cpu\t\t\t: Alpha\n"
            "cpu model\t\t: %s\n"
            "cpu variation\t\t: 0\n"
            "cpu revision\t\t: 0\n"
            "cpu serial number\t: JA00000000\n"
            "system type\t\t: QEMU\n"
            "system variation\t: QEMU_v" QEMU_VERSION "\n"
            "system revision\t\t: 0\n"
            "system serial number\t: AY00000000\n"
            "cycle frequency [Hz]\t: 250000000\n"
            "timer frequency [Hz]\t: 250.00\n"
            "page size [bytes]\t: %d\n"
            "phys. address bits\t: %d\n"
            "max. addr. space #\t: 255\n"
            "BogoMIPS\t\t: 2500.00\n"
            "kernel unaligned acc\t: 0 (pc=0,va=0)\n"
            "user unaligned acc\t: 0 (pc=0,va=0)\n"
            "platform string\t\t: AlphaServer QEMU user-mode VM\n"
            "cpus detected\t\t: %d\n"
            "cpus active\t\t: %d\n"
            "cpu active mask\t\t: %016lx\n"
            "L1 Icache\t\t: n/a\n"
            "L1 Dcache\t\t: n/a\n"
            "L2 cache\t\t: n/a\n"
            "L3 cache\t\t: n/a\n",
            model, TARGET_PAGE_SIZE, TARGET_PHYS_ADDR_SPACE_BITS,
            max_cpus, num_cpus, cpu_mask);

    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* ALPHA_TARGET_PROC_H */
