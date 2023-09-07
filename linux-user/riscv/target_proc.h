/*
 * RISC-V specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef RISCV_TARGET_PROC_H
#define RISCV_TARGET_PROC_H

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    int i;
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    RISCVCPU *cpu = env_archcpu(cpu_env);
    const RISCVCPUConfig *cfg = riscv_cpu_cfg((CPURISCVState *) cpu_env);
    char *isa_string = riscv_isa_string(cpu);
    const char *mmu;

    if (cfg->mmu) {
        mmu = (cpu_env->xl == MXL_RV32) ? "sv32"  : "sv48";
    } else {
        mmu = "none";
    }

    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "processor\t: %d\n", i);
        dprintf(fd, "hart\t\t: %d\n", i);
        dprintf(fd, "isa\t\t: %s\n", isa_string);
        dprintf(fd, "mmu\t\t: %s\n", mmu);
        dprintf(fd, "uarch\t\t: qemu\n\n");
    }

    g_free(isa_string);
    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* RISCV_TARGET_PROC_H */
