/*
 * CPU thread main loop - common bits for user and system mode emulation
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "qom/cpu.h"
#include "sysemu/cpus.h"

static QemuMutex qemu_cpu_list_lock;
static QemuCond exclusive_cond;
static QemuCond exclusive_resume;
static QemuCond qemu_work_cond;

static int pending_cpus;

void qemu_init_cpu_list(void)
{
    /* This is needed because qemu_init_cpu_list is also called by the
     * child process in a fork.  */
    pending_cpus = 0;

    qemu_mutex_init(&qemu_cpu_list_lock);
    qemu_cond_init(&exclusive_cond);
    qemu_cond_init(&exclusive_resume);
    qemu_cond_init(&qemu_work_cond);
}

void cpu_list_lock(void)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
}

void cpu_list_unlock(void)
{
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

static bool cpu_index_auto_assigned;

static int cpu_get_free_index(void)
{
    CPUState *some_cpu;
    int cpu_index = 0;

    cpu_index_auto_assigned = true;
    CPU_FOREACH(some_cpu) {
        cpu_index++;
    }
    return cpu_index;
}

static void finish_safe_work(CPUState *cpu)
{
    cpu_exec_start(cpu);
    cpu_exec_end(cpu);
}

void cpu_list_add(CPUState *cpu)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
    if (cpu->cpu_index == UNASSIGNED_CPU_INDEX) {
        cpu->cpu_index = cpu_get_free_index();
        assert(cpu->cpu_index != UNASSIGNED_CPU_INDEX);
    } else {
        assert(!cpu_index_auto_assigned);
    }
    QTAILQ_INSERT_TAIL(&cpus, cpu, node);
    qemu_mutex_unlock(&qemu_cpu_list_lock);

    finish_safe_work(cpu);
}

void cpu_list_remove(CPUState *cpu)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
    if (!QTAILQ_IN_USE(cpu, node)) {
        /* there is nothing to undo since cpu_exec_init() hasn't been called */
        qemu_mutex_unlock(&qemu_cpu_list_lock);
        return;
    }

    assert(!(cpu_index_auto_assigned && cpu != QTAILQ_LAST(&cpus, CPUTailQ)));

    QTAILQ_REMOVE(&cpus, cpu, node);
    cpu->cpu_index = UNASSIGNED_CPU_INDEX;
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

struct qemu_work_item {
    struct qemu_work_item *next;
    run_on_cpu_func func;
    void *data;
    bool free, done;
};

static void queue_work_on_cpu(CPUState *cpu, struct qemu_work_item *wi)
{
    qemu_mutex_lock(&cpu->work_mutex);
    if (cpu->queued_work_first == NULL) {
        cpu->queued_work_first = wi;
    } else {
        cpu->queued_work_last->next = wi;
    }
    cpu->queued_work_last = wi;
    wi->next = NULL;
    wi->done = false;
    qemu_mutex_unlock(&cpu->work_mutex);

    qemu_cpu_kick(cpu);
}

void do_run_on_cpu(CPUState *cpu, run_on_cpu_func func, void *data,
                   QemuMutex *mutex)
{
    struct qemu_work_item wi;

    if (qemu_cpu_is_self(cpu)) {
        func(cpu, data);
        return;
    }

    wi.func = func;
    wi.data = data;
    wi.done = false;
    wi.free = false;

    queue_work_on_cpu(cpu, &wi);
    while (!atomic_mb_read(&wi.done)) {
        CPUState *self_cpu = current_cpu;

        qemu_cond_wait(&qemu_work_cond, mutex);
        current_cpu = self_cpu;
    }
}

void async_run_on_cpu(CPUState *cpu, run_on_cpu_func func, void *data)
{
    struct qemu_work_item *wi;

    wi = g_malloc0(sizeof(struct qemu_work_item));
    wi->func = func;
    wi->data = data;
    wi->free = true;

    queue_work_on_cpu(cpu, wi);
}

/* Wait for pending exclusive operations to complete.  The CPU list lock
   must be held.  */
static inline void exclusive_idle(void)
{
    while (pending_cpus) {
        qemu_cond_wait(&exclusive_resume, &qemu_cpu_list_lock);
    }
}

/* Start an exclusive operation.
   Must only be called from outside cpu_exec.  */
void start_exclusive(void)
{
    CPUState *other_cpu;

    qemu_mutex_lock(&qemu_cpu_list_lock);
    exclusive_idle();

    /* Make all other cpus stop executing.  */
    pending_cpus = 1;
    CPU_FOREACH(other_cpu) {
        if (other_cpu->running) {
            pending_cpus++;
            qemu_cpu_kick(other_cpu);
        }
    }
    while (pending_cpus > 1) {
        qemu_cond_wait(&exclusive_cond, &qemu_cpu_list_lock);
    }

    /* Can release mutex, no one will enter another exclusive
     * section until end_exclusive resets pending_cpus to 0.
     */
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

/* Finish an exclusive operation.  */
void end_exclusive(void)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
    pending_cpus = 0;
    qemu_cond_broadcast(&exclusive_resume);
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

/* Wait for exclusive ops to finish, and begin cpu execution.  */
void cpu_exec_start(CPUState *cpu)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
    exclusive_idle();
    cpu->running = true;
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

/* Mark cpu as not executing, and release pending exclusive ops.  */
void cpu_exec_end(CPUState *cpu)
{
    qemu_mutex_lock(&qemu_cpu_list_lock);
    cpu->running = false;
    if (pending_cpus > 1) {
        pending_cpus--;
        if (pending_cpus == 1) {
            qemu_cond_signal(&exclusive_cond);
        }
    }
    qemu_mutex_unlock(&qemu_cpu_list_lock);
}

void process_queued_cpu_work(CPUState *cpu)
{
    struct qemu_work_item *wi;

    if (cpu->queued_work_first == NULL) {
        return;
    }

    qemu_mutex_lock(&cpu->work_mutex);
    while (cpu->queued_work_first != NULL) {
        wi = cpu->queued_work_first;
        cpu->queued_work_first = wi->next;
        if (!cpu->queued_work_first) {
            cpu->queued_work_last = NULL;
        }
        qemu_mutex_unlock(&cpu->work_mutex);
        wi->func(cpu, wi->data);
        qemu_mutex_lock(&cpu->work_mutex);
        if (wi->free) {
            g_free(wi);
        } else {
            atomic_mb_set(&wi->done, true);
        }
    }
    qemu_mutex_unlock(&cpu->work_mutex);
    qemu_cond_broadcast(&qemu_work_cond);
}
