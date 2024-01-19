/*
 * Dummy cpu thread code
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/rcu.h"
#include "sysemu/cpus.h"
#include "qemu/guest-random.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"

static void *dummy_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

#ifndef _WIN32
    sigset_t waitset;
    int r;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);
#endif

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        bql_unlock();
#ifndef _WIN32
        do {
            int sig;
            r = sigwait(&waitset, &sig);
        } while (r == -1 && (errno == EAGAIN || errno == EINTR));
        if (r == -1) {
            perror("sigwait");
            exit(1);
        }
#else
        qemu_sem_wait(&cpu->sem);
#endif
        bql_lock();
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug);

    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

void dummy_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/DUMMY",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, dummy_cpu_thread_fn, cpu,
                       QEMU_THREAD_JOINABLE);
#ifdef _WIN32
    qemu_sem_init(&cpu->sem, 0);
#endif
}
