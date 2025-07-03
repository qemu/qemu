/* QEMU accelerator interfaces
 *
 * Copyright (c) 2014 Red Hat Inc
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
#ifndef QEMU_ACCEL_H
#define QEMU_ACCEL_H

#include "qom/object.h"
#include "exec/hwaddr.h"

typedef struct AccelState AccelState;
typedef struct AccelClass AccelClass;

#define TYPE_ACCEL "accel"

#define ACCEL_CLASS_SUFFIX  "-" TYPE_ACCEL
#define ACCEL_CLASS_NAME(a) (a ACCEL_CLASS_SUFFIX)

#define ACCEL_CLASS(klass) \
    OBJECT_CLASS_CHECK(AccelClass, (klass), TYPE_ACCEL)
#define ACCEL(obj) \
    OBJECT_CHECK(AccelState, (obj), TYPE_ACCEL)
#define ACCEL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AccelClass, (obj), TYPE_ACCEL)

AccelClass *accel_find(const char *opt_name);
AccelState *current_accel(void);
const char *current_accel_name(void);

void accel_init_interfaces(AccelClass *ac);

int accel_init_machine(AccelState *accel, MachineState *ms);

/* Called just before os_setup_post (ie just before drop OS privs) */
void accel_setup_post(MachineState *ms);

void accel_pre_resume(MachineState *ms, bool step_pending);

/**
 * accel_cpu_instance_init:
 * @cpu: The CPU that needs to do accel-specific object initializations.
 */
void accel_cpu_instance_init(CPUState *cpu);

/**
 * accel_cpu_common_realize:
 * @cpu: The CPU that needs to call accel-specific cpu realization.
 * @errp: currently unused.
 */
bool accel_cpu_common_realize(CPUState *cpu, Error **errp);

/**
 * accel_cpu_common_unrealize:
 * @cpu: The CPU that needs to call accel-specific cpu unrealization.
 */
void accel_cpu_common_unrealize(CPUState *cpu);

/**
 * accel_supported_gdbstub_sstep_flags:
 *
 * Returns the supported single step modes for the configured
 * accelerator.
 */
int accel_supported_gdbstub_sstep_flags(void);

#endif /* QEMU_ACCEL_H */
