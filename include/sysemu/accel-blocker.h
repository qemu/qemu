/*
 * Accelerator blocking API, to prevent new ioctls from starting and wait the
 * running ones finish.
 * This mechanism differs from pause/resume_all_vcpus() in that it does not
 * release the BQL.
 *
 *  Copyright (c) 2022 Red Hat Inc.
 *
 * Author: Emanuele Giuseppe Esposito       <eesposit@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACCEL_BLOCKER_H
#define ACCEL_BLOCKER_H

#include "qemu/osdep.h"
#include "sysemu/cpus.h"

extern void accel_blocker_init(void);

/*
 * accel_{cpu_}ioctl_begin/end:
 * Mark when ioctl is about to run or just finished.
 *
 * accel_{cpu_}ioctl_begin will block after accel_ioctl_inhibit_begin() is
 * called, preventing new ioctls to run. They will continue only after
 * accel_ioctl_inibith_end().
 */
extern void accel_ioctl_begin(void);
extern void accel_ioctl_end(void);
extern void accel_cpu_ioctl_begin(CPUState *cpu);
extern void accel_cpu_ioctl_end(CPUState *cpu);

/*
 * accel_ioctl_inhibit_begin: start critical section
 *
 * This function makes sure that:
 * 1) incoming accel_{cpu_}ioctl_begin() calls block
 * 2) wait that all ioctls that were already running reach
 *    accel_{cpu_}ioctl_end(), kicking vcpus if necessary.
 *
 * This allows the caller to access shared data or perform operations without
 * worrying of concurrent vcpus accesses.
 */
extern void accel_ioctl_inhibit_begin(void);

/*
 * accel_ioctl_inhibit_end: end critical section started by
 * accel_ioctl_inhibit_begin()
 *
 * This function allows blocked accel_{cpu_}ioctl_begin() to continue.
 */
extern void accel_ioctl_inhibit_end(void);

#endif /* ACCEL_BLOCKER_H */
