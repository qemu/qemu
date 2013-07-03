/*
 * Support for virtio hypercalls on s390
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "cpu.h"
#include "hw/s390x/s390-virtio.h"

#define MAX_DIAG_SUBCODES 255

static s390_virtio_fn s390_diag500_table[MAX_DIAG_SUBCODES];

void s390_register_virtio_hypercall(uint64_t code, s390_virtio_fn fn)
{
    assert(code < MAX_DIAG_SUBCODES);
    assert(!s390_diag500_table[code]);

    s390_diag500_table[code] = fn;
}

int s390_virtio_hypercall(CPUS390XState *env)
{
    s390_virtio_fn fn = s390_diag500_table[env->regs[1]];

    if (!fn) {
        return -EINVAL;
    }

    return fn(&env->regs[2]);
}
