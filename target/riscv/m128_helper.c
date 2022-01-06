/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

target_ulong HELPER(divu_i128)(CPURISCVState *env,
                       target_ulong ul, target_ulong uh,
                       target_ulong vl, target_ulong vh)
{
    target_ulong ql, qh;
    Int128 q;

    if (vl == 0 && vh == 0) { /* Handle special behavior on div by zero */
        ql = ~0x0;
        qh = ~0x0;
    } else {
        q = int128_divu(int128_make128(ul, uh), int128_make128(vl, vh));
        ql = int128_getlo(q);
        qh = int128_gethi(q);
    }

    env->retxh = qh;
    return ql;
}

target_ulong HELPER(remu_i128)(CPURISCVState *env,
                       target_ulong ul, target_ulong uh,
                       target_ulong vl, target_ulong vh)
{
    target_ulong rl, rh;
    Int128 r;

    if (vl == 0 && vh == 0) {
        rl = ul;
        rh = uh;
    } else {
        r = int128_remu(int128_make128(ul, uh), int128_make128(vl, vh));
        rl = int128_getlo(r);
        rh = int128_gethi(r);
    }

    env->retxh = rh;
    return rl;
}

target_ulong HELPER(divs_i128)(CPURISCVState *env,
                       target_ulong ul, target_ulong uh,
                       target_ulong vl, target_ulong vh)
{
    target_ulong qh, ql;
    Int128 q;

    if (vl == 0 && vh == 0) { /* Div by zero check */
        ql = ~0x0;
        qh = ~0x0;
    } else if (uh == (1ULL << (TARGET_LONG_BITS - 1)) && ul == 0 &&
               vh == ~0x0 && vl == ~0x0) {
        /* Signed div overflow check (-2**127 / -1) */
        ql = ul;
        qh = uh;
    } else {
        q = int128_divs(int128_make128(ul, uh), int128_make128(vl, vh));
        ql = int128_getlo(q);
        qh = int128_gethi(q);
    }

    env->retxh = qh;
    return ql;
}

target_ulong HELPER(rems_i128)(CPURISCVState *env,
                       target_ulong ul, target_ulong uh,
                       target_ulong vl, target_ulong vh)
{
    target_ulong rh, rl;
    Int128 r;

    if (vl == 0 && vh == 0) {
        rl = ul;
        rh = uh;
    } else {
        r = int128_rems(int128_make128(ul, uh), int128_make128(vl, vh));
        rl = int128_getlo(r);
        rh = int128_gethi(r);
    }

    env->retxh = rh;
    return rl;
}
