#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

#include "qemu/timer.h"

/* cpus.c */
bool all_cpu_threads_idle(void);
bool qemu_in_vcpu_thread(void);
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);

extern int icount_align_option;

/* Unblock cpu */
void qemu_cpu_kick_self(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);
void cpu_synchronize_all_pre_loadvm(void);

#ifndef CONFIG_USER_ONLY
/* vl.c */
/* *-user doesn't have configurable SMP topology */
extern int smp_cores;
extern int smp_threads;
#endif

void list_cpus(const char *optarg);

#endif
