/*
 *  Nios II Semihosting syscall interface.
 *  This code is derived from m68k-semi.c.
 *  The semihosting protocol implemented here is described in the
 *  libgloss sources:
 *  https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=libgloss/nios2/nios2-semi.txt;hb=HEAD
 *
 *  Copyright (c) 2017-2019 Mentor Graphics
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "semihosting/syscalls.h"
#include "semihosting/softmmu-uaccess.h"
#include "qemu/log.h"

#define HOSTED_EXIT  0
#define HOSTED_INIT_SIM 1
#define HOSTED_OPEN 2
#define HOSTED_CLOSE 3
#define HOSTED_READ 4
#define HOSTED_WRITE 5
#define HOSTED_LSEEK 6
#define HOSTED_RENAME 7
#define HOSTED_UNLINK 8
#define HOSTED_STAT 9
#define HOSTED_FSTAT 10
#define HOSTED_GETTIMEOFDAY 11
#define HOSTED_ISATTY 12
#define HOSTED_SYSTEM 13

static int host_to_gdb_errno(int err)
{
#define E(X)  case E##X: return GDB_E##X
    switch (err) {
    E(PERM);
    E(NOENT);
    E(INTR);
    E(BADF);
    E(ACCES);
    E(FAULT);
    E(BUSY);
    E(EXIST);
    E(NODEV);
    E(NOTDIR);
    E(ISDIR);
    E(INVAL);
    E(NFILE);
    E(MFILE);
    E(FBIG);
    E(NOSPC);
    E(SPIPE);
    E(ROFS);
    E(NAMETOOLONG);
    default:
        return GDB_EUNKNOWN;
    }
#undef E
}

static void nios2_semi_u32_cb(CPUState *cs, uint64_t ret, int err)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    target_ulong args = env->regs[R_ARG1];

    if (put_user_u32(ret, args) ||
        put_user_u32(host_to_gdb_errno(err), args + 4)) {
        /*
         * The nios2 semihosting ABI does not provide any way to report this
         * error to the guest, so the best we can do is log it in qemu.
         * It is always a guest error not to pass us a valid argument block.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: return value "
                      "discarded because argument block not writable\n");
    }
}

static void nios2_semi_u64_cb(CPUState *cs, uint64_t ret, int err)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    target_ulong args = env->regs[R_ARG1];

    if (put_user_u32(ret >> 32, args) ||
        put_user_u32(ret, args + 4) ||
        put_user_u32(host_to_gdb_errno(err), args + 8)) {
        /* No way to report this via nios2 semihosting ABI; just log it */
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: return value "
                      "discarded because argument block not writable\n");
    }
}

/*
 * Read the input value from the argument block; fail the semihosting
 * call if the memory read fails.
 */
#define GET_ARG(n) do {                                 \
    if (get_user_ual(arg ## n, args + (n) * 4)) {       \
        goto failed;                                    \
    }                                                   \
} while (0)

#define GET_ARG64(n) do {                               \
    if (get_user_ual(arg ## n, args + (n) * 4)) {       \
        goto failed64;                                  \
    }                                                   \
} while (0)

void do_nios2_semihosting(CPUNios2State *env)
{
    CPUState *cs = env_cpu(env);
    int nr;
    uint32_t args;
    target_ulong arg0, arg1, arg2, arg3;

    nr = env->regs[R_ARG0];
    args = env->regs[R_ARG1];
    switch (nr) {
    case HOSTED_EXIT:
        gdb_exit(env->regs[R_ARG0]);
        exit(env->regs[R_ARG0]);

    case HOSTED_OPEN:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        semihost_sys_open(cs, nios2_semi_u32_cb, arg0, arg1, arg2, arg3);
        break;

    case HOSTED_CLOSE:
        GET_ARG(0);
        semihost_sys_close(cs, nios2_semi_u32_cb, arg0);
        break;

    case HOSTED_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_read(cs, nios2_semi_u32_cb, arg0, arg1, arg2);
        break;

    case HOSTED_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_write(cs, nios2_semi_u32_cb, arg0, arg1, arg2);
        break;

    case HOSTED_LSEEK:
        GET_ARG64(0);
        GET_ARG64(1);
        GET_ARG64(2);
        GET_ARG64(3);
        semihost_sys_lseek(cs, nios2_semi_u64_cb, arg0,
                           deposit64(arg2, arg1, 32, 32), arg3);
        break;

    case HOSTED_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        semihost_sys_rename(cs, nios2_semi_u32_cb, arg0, arg1, arg2, arg3);
        break;

    case HOSTED_UNLINK:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_remove(cs, nios2_semi_u32_cb, arg0, arg1);
        break;

    case HOSTED_STAT:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        semihost_sys_stat(cs, nios2_semi_u32_cb, arg0, arg1, arg2);
        break;

    case HOSTED_FSTAT:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_fstat(cs, nios2_semi_u32_cb, arg0, arg1);
        break;

    case HOSTED_GETTIMEOFDAY:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_gettimeofday(cs, nios2_semi_u32_cb, arg0, arg1);
        break;

    case HOSTED_ISATTY:
        GET_ARG(0);
        semihost_sys_isatty(cs, nios2_semi_u32_cb, arg0);
        break;

    case HOSTED_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        semihost_sys_system(cs, nios2_semi_u32_cb, arg0, arg1);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: unsupported "
                      "semihosting syscall %d\n", nr);
        nios2_semi_u32_cb(cs, -1, ENOSYS);
        break;

    failed:
        nios2_semi_u32_cb(cs, -1, EFAULT);
        break;
    failed64:
        nios2_semi_u64_cb(cs, -1, EFAULT);
        break;
    }
}
