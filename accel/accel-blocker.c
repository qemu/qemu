/*
 * Lock to inhibit accelerator ioctls
 *
 * Copyright (c) 2022 Red Hat Inc.
 *
 * Author: Emanuele Giuseppe Esposito       <eesposit@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/lockcnt.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "system/accel-blocker.h"

static QemuLockCnt accel_in_ioctl_lock;
static QemuEvent accel_in_ioctl_event;

void accel_blocker_init(void)
{
    qemu_lockcnt_init(&accel_in_ioctl_lock);
    qemu_event_init(&accel_in_ioctl_event, false);
}

void accel_ioctl_begin(void)
{
    if (likely(bql_locked())) {
        return;
    }

    /* block if lock is taken in kvm_ioctl_inhibit_begin() */
    qemu_lockcnt_inc(&accel_in_ioctl_lock);
}

void accel_ioctl_end(void)
{
    if (likely(bql_locked())) {
        return;
    }

    qemu_lockcnt_dec(&accel_in_ioctl_lock);
    /* change event to SET. If event was BUSY, wake up all waiters */
    qemu_event_set(&accel_in_ioctl_event);
}

void accel_cpu_ioctl_begin(CPUState *cpu)
{
    if (unlikely(bql_locked())) {
        return;
    }

    /* block if lock is taken in kvm_ioctl_inhibit_begin() */
    qemu_lockcnt_inc(&cpu->in_ioctl_lock);
}

void accel_cpu_ioctl_end(CPUState *cpu)
{
    if (unlikely(bql_locked())) {
        return;
    }

    qemu_lockcnt_dec(&cpu->in_ioctl_lock);
    /* change event to SET. If event was BUSY, wake up all waiters */
    qemu_event_set(&accel_in_ioctl_event);
}

static bool accel_has_to_wait(void)
{
    CPUState *cpu;
    bool needs_to_wait = false;

    CPU_FOREACH(cpu) {
        if (qemu_lockcnt_count(&cpu->in_ioctl_lock)) {
            /* exit the ioctl, if vcpu is running it */
            qemu_cpu_kick(cpu);
            needs_to_wait = true;
        }
    }

    return needs_to_wait || qemu_lockcnt_count(&accel_in_ioctl_lock);
}

void accel_ioctl_inhibit_begin(void)
{
    CPUState *cpu;

    /*
     * We allow to inhibit only when holding the BQL, so we can identify
     * when an inhibitor wants to issue an ioctl easily.
     */
    g_assert(bql_locked());

    /* Block further invocations of the ioctls outside the BQL.  */
    CPU_FOREACH(cpu) {
        qemu_lockcnt_lock(&cpu->in_ioctl_lock);
    }
    qemu_lockcnt_lock(&accel_in_ioctl_lock);

    /* Keep waiting until there are running ioctls */
    while (true) {

        /* Reset event to FREE. */
        qemu_event_reset(&accel_in_ioctl_event);

        if (accel_has_to_wait()) {
            /*
             * If event is still FREE, and there are ioctls still in progress,
             * wait.
             *
             *  If an ioctl finishes before qemu_event_wait(), it will change
             * the event state to SET. This will prevent qemu_event_wait() from
             * blocking, but it's not a problem because if other ioctls are
             * still running the loop will iterate once more and reset the event
             * status to FREE so that it can wait properly.
             *
             * If an ioctls finishes while qemu_event_wait() is blocking, then
             * it will be waken up, but also here the while loop makes sure
             * to re-enter the wait if there are other running ioctls.
             */
            qemu_event_wait(&accel_in_ioctl_event);
        } else {
            /* No ioctl is running */
            return;
        }
    }
}

void accel_ioctl_inhibit_end(void)
{
    CPUState *cpu;

    qemu_lockcnt_unlock(&accel_in_ioctl_lock);
    CPU_FOREACH(cpu) {
        qemu_lockcnt_unlock(&cpu->in_ioctl_lock);
    }
}

