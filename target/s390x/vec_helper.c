/*
 * QEMU TCG support -- s390x vector support instructions
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg/tcg.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

void HELPER(vll)(CPUS390XState *env, void *v1, uint64_t addr, uint64_t bytes)
{
    if (likely(bytes >= 16)) {
        uint64_t t0, t1;

        t0 = cpu_ldq_data_ra(env, addr, GETPC());
        addr = wrap_address(env, addr + 8);
        t1 = cpu_ldq_data_ra(env, addr, GETPC());
        s390_vec_write_element64(v1, 0, t0);
        s390_vec_write_element64(v1, 1, t1);
    } else {
        S390Vector tmp = {};
        int i;

        for (i = 0; i < bytes; i++) {
            uint8_t byte = cpu_ldub_data_ra(env, addr, GETPC());

            s390_vec_write_element8(&tmp, i, byte);
            addr = wrap_address(env, addr + 1);
        }
        *(S390Vector *)v1 = tmp;
    }
}
