/*
 * QEMU KVM support -- x86 virtual energy-related MSR.
 *
 * Copyright 2024 Red Hat, Inc. 2024
 *
 *  Author:
 *      Anthony Harivel <aharivel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VMSR_ENERGY_H
#define VMSR_ENERGY_H

#include <stdint.h>
#include "qemu/osdep.h"
#include "io/channel-socket.h"
#include "hw/i386/topology.h"

/*
 * Define the interval time in micro seconds between 2 samples of
 * energy related MSRs
 */
#define MSR_ENERGY_THREAD_SLEEP_US 1000000.0

/*
 * Thread statistic
 * @ thread_id: TID (thread ID)
 * @ is_vcpu: true if TID is vCPU thread
 * @ cpu_id: CPU number last executed on
 * @ pkg_id: package number of the CPU
 * @ vcpu_id: vCPU ID
 * @ vpkg: virtual package number
 * @ acpi_id: APIC id of the vCPU
 * @ utime: amount of clock ticks the thread
 *          has been scheduled in User mode
 * @ stime: amount of clock ticks the thread
 *          has been scheduled in System mode
 * @ delta_ticks: delta of utime+stime between
 *          the two samples (before/after sleep)
 */
struct vmsr_thread_stat {
    unsigned int thread_id;
    bool is_vcpu;
    unsigned int cpu_id;
    unsigned int pkg_id;
    unsigned int vpkg_id;
    unsigned int vcpu_id;
    unsigned long acpi_id;
    unsigned long long *utime;
    unsigned long long *stime;
    unsigned long long delta_ticks;
};

/*
 * Package statistic
 * @ e_start: package energy counter before the sleep
 * @ e_end: package energy counter after the sleep
 * @ e_delta: delta of package energy counter
 * @ e_ratio: store the energy ratio of non-vCPU thread
 * @ nb_vcpu: number of vCPU running on this package
 */
struct vmsr_package_energy_stat {
    uint64_t e_start;
    uint64_t e_end;
    uint64_t e_delta;
    uint64_t e_ratio;
    unsigned int nb_vcpu;
};

typedef struct vmsr_thread_stat vmsr_thread_stat;
typedef struct vmsr_package_energy_stat vmsr_package_energy_stat;

char *vmsr_compute_default_paths(void);
void vmsr_read_thread_stat(pid_t pid,
                      unsigned int thread_id,
                      unsigned long long *utime,
                      unsigned long long *stime,
                      unsigned int *cpu_id);

QIOChannelSocket *vmsr_open_socket(const char *path);
uint64_t vmsr_read_msr(uint32_t reg, uint32_t cpu_id,
                       uint32_t tid, QIOChannelSocket *sioc);
void vmsr_delta_ticks(vmsr_thread_stat *thd_stat, int i);
unsigned int vmsr_get_maxcpus(void);
unsigned int vmsr_get_max_physical_package(unsigned int max_cpus);
unsigned int vmsr_count_cpus_per_package(unsigned int *package_count,
                                         unsigned int max_pkgs);
int vmsr_get_physical_package_id(int cpu_id);
pid_t *vmsr_get_thread_ids(pid_t pid, unsigned int *num_threads);
double vmsr_get_ratio(uint64_t e_delta,
                      unsigned long long delta_ticks,
                      unsigned int maxticks);
void vmsr_init_topo_info(X86CPUTopoInfo *topo_info, const MachineState *ms);
int is_rapl_enabled(void);
#endif /* VMSR_ENERGY_H */
