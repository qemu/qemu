/*
 * QEMU TCG Multi Threaded vCPUs implementation
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPUS_MTTCG_H
#define TCG_CPUS_MTTCG_H

/*
 * In the multi-threaded case each vCPU has its own thread. The TLS
 * variable current_cpu can be used deep in the code to find the
 * current CPUState for a given thread.
 */

void *tcg_cpu_thread_fn(void *arg);

#endif /* TCG_CPUS_MTTCG_H */
