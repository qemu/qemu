/*
 *  Arm "Angel" semihosting syscalls
 *
 *  Copyright (c) 2005, 2007 CodeSourcery.
 *  Copyright (c) 2019 Linaro
 *  Written by Paul Brook.
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
 *
 *  ARM Semihosting is documented in:
 *     Semihosting for AArch32 and AArch64 Release 2.0
 *     https://static.docs.arm.com/100863/0200/semihosting.pdf
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/semihosting/semihost.h"
#include "hw/semihosting/console.h"
#include "qemu/log.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define ARM_ANGEL_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "exec/gdbstub.h"
#include "qemu/cutils.h"
#endif

#define TARGET_SYS_OPEN        0x01
#define TARGET_SYS_CLOSE       0x02
#define TARGET_SYS_WRITEC      0x03
#define TARGET_SYS_WRITE0      0x04
#define TARGET_SYS_WRITE       0x05
#define TARGET_SYS_READ        0x06
#define TARGET_SYS_READC       0x07
#define TARGET_SYS_ISTTY       0x09
#define TARGET_SYS_SEEK        0x0a
#define TARGET_SYS_FLEN        0x0c
#define TARGET_SYS_TMPNAM      0x0d
#define TARGET_SYS_REMOVE      0x0e
#define TARGET_SYS_RENAME      0x0f
#define TARGET_SYS_CLOCK       0x10
#define TARGET_SYS_TIME        0x11
#define TARGET_SYS_SYSTEM      0x12
#define TARGET_SYS_ERRNO       0x13
#define TARGET_SYS_GET_CMDLINE 0x15
#define TARGET_SYS_HEAPINFO    0x16
#define TARGET_SYS_EXIT        0x18
#define TARGET_SYS_SYNCCACHE   0x19

/* ADP_Stopped_ApplicationExit is used for exit(0),
 * anything else is implemented as exit(1) */
#define ADP_Stopped_ApplicationExit     (0x20026)

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define GDB_O_RDONLY  0x000
#define GDB_O_WRONLY  0x001
#define GDB_O_RDWR    0x002
#define GDB_O_APPEND  0x008
#define GDB_O_CREAT   0x200
#define GDB_O_TRUNC   0x400
#define GDB_O_BINARY  0

static int gdb_open_modeflags[12] = {
    GDB_O_RDONLY,
    GDB_O_RDONLY | GDB_O_BINARY,
    GDB_O_RDWR,
    GDB_O_RDWR | GDB_O_BINARY,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_TRUNC | GDB_O_BINARY,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_TRUNC | GDB_O_BINARY,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_WRONLY | GDB_O_CREAT | GDB_O_APPEND | GDB_O_BINARY,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND,
    GDB_O_RDWR | GDB_O_CREAT | GDB_O_APPEND | GDB_O_BINARY
};

static int open_modeflags[12] = {
    O_RDONLY,
    O_RDONLY | O_BINARY,
    O_RDWR,
    O_RDWR | O_BINARY,
    O_WRONLY | O_CREAT | O_TRUNC,
    O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
    O_RDWR | O_CREAT | O_TRUNC,
    O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
    O_WRONLY | O_CREAT | O_APPEND,
    O_WRONLY | O_CREAT | O_APPEND | O_BINARY,
    O_RDWR | O_CREAT | O_APPEND,
    O_RDWR | O_CREAT | O_APPEND | O_BINARY
};

#ifdef CONFIG_USER_ONLY
static inline uint32_t set_swi_errno(TaskState *ts, uint32_t code)
{
    if (code == (uint32_t)-1)
        ts->swi_errno = errno;
    return code;
}
#else
static inline uint32_t set_swi_errno(CPUARMState *env, uint32_t code)
{
    return code;
}

#include "exec/softmmu-semi.h"
#endif

static target_ulong arm_semi_syscall_len;

#if !defined(CONFIG_USER_ONLY)
static target_ulong syscall_err;
#endif

static void arm_semi_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = cs->opaque;
#endif
    target_ulong reg0 = is_a64(env) ? env->xregs[0] : env->regs[0];

    if (ret == (target_ulong)-1) {
#ifdef CONFIG_USER_ONLY
        ts->swi_errno = err;
#else
        syscall_err = err;
#endif
        reg0 = ret;
    } else {
        /* Fixup syscalls that use nonstardard return conventions.  */
        switch (reg0) {
        case TARGET_SYS_WRITE:
        case TARGET_SYS_READ:
            reg0 = arm_semi_syscall_len - ret;
            break;
        case TARGET_SYS_SEEK:
            reg0 = 0;
            break;
        default:
            reg0 = ret;
            break;
        }
    }
    if (is_a64(env)) {
        env->xregs[0] = reg0;
    } else {
        env->regs[0] = reg0;
    }
}

static target_ulong arm_flen_buf(ARMCPU *cpu)
{
    /* Return an address in target memory of 64 bytes where the remote
     * gdb should write its stat struct. (The format of this structure
     * is defined by GDB's remote protocol and is not target-specific.)
     * We put this on the guest's stack just below SP.
     */
    CPUARMState *env = &cpu->env;
    target_ulong sp;

    if (is_a64(env)) {
        sp = env->xregs[31];
    } else {
        sp = env->regs[13];
    }

    return sp - 64;
}

static void arm_semi_flen_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    /* The size is always stored in big-endian order, extract
       the value. We assume the size always fit in 32 bits.  */
    uint32_t size;
    cpu_memory_rw_debug(cs, arm_flen_buf(cpu) + 32, (uint8_t *)&size, 4, 0);
    size = be32_to_cpu(size);
    if (is_a64(env)) {
        env->xregs[0] = size;
    } else {
        env->regs[0] = size;
    }
#ifdef CONFIG_USER_ONLY
    ((TaskState *)cs->opaque)->swi_errno = err;
#else
    syscall_err = err;
#endif
}

static target_ulong arm_gdb_syscall(ARMCPU *cpu, gdb_syscall_complete_cb cb,
                                    const char *fmt, ...)
{
    va_list va;
    CPUARMState *env = &cpu->env;

    va_start(va, fmt);
    gdb_do_syscallv(cb, fmt, va);
    va_end(va);

    /* FIXME: we are implicitly relying on the syscall completing
     * before this point, which is not guaranteed. We should
     * put in an explicit synchronization between this and
     * the callback function.
     */

    return is_a64(env) ? env->xregs[0] : env->regs[0];
}

/* Read the input value from the argument block; fail the semihosting
 * call if the memory read fails.
 */
#define GET_ARG(n) do {                                 \
    if (is_a64(env)) {                                  \
        if (get_user_u64(arg ## n, args + (n) * 8)) {   \
            return -1;                                  \
        }                                               \
    } else {                                            \
        if (get_user_u32(arg ## n, args + (n) * 4)) {   \
            return -1;                                  \
        }                                               \
    }                                                   \
} while (0)

#define SET_ARG(n, val)                                 \
    (is_a64(env) ?                                      \
     put_user_u64(val, args + (n) * 8) :                \
     put_user_u32(val, args + (n) * 4))

/*
 * Do a semihosting call.
 *
 * The specification always says that the "return register" either
 * returns a specific value or is corrupted, so we don't need to
 * report to our caller whether we are returning a value or trying to
 * leave the register unchanged. We use 0xdeadbeef as the return value
 * when there isn't a defined return value for the call.
 */
target_ulong do_arm_semihosting(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    target_ulong args;
    target_ulong arg0, arg1, arg2, arg3;
    char * s;
    int nr;
    uint32_t ret;
    uint32_t len;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = cs->opaque;
#else
    CPUARMState *ts = env;
#endif

    if (is_a64(env)) {
        /* Note that the syscall number is in W0, not X0 */
        nr = env->xregs[0] & 0xffffffffU;
        args = env->xregs[1];
    } else {
        nr = env->regs[0];
        args = env->regs[1];
    }

    switch (nr) {
    case TARGET_SYS_OPEN:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        s = lock_user_string(arg0);
        if (!s) {
            /* FIXME - should this error code be -TARGET_EFAULT ? */
            return (uint32_t)-1;
        }
        if (arg1 >= 12) {
            unlock_user(s, arg0, 0);
            return (uint32_t)-1;
        }
        if (strcmp(s, ":tt") == 0) {
            int result_fileno = arg1 < 4 ? STDIN_FILENO : STDOUT_FILENO;
            unlock_user(s, arg0, 0);
            return result_fileno;
        }
        if (use_gdb_syscalls()) {
            ret = arm_gdb_syscall(cpu, arm_semi_cb, "open,%s,%x,1a4", arg0,
                                  (int)arg2+1, gdb_open_modeflags[arg1]);
        } else {
            ret = set_swi_errno(ts, open(s, open_modeflags[arg1], 0644));
        }
        unlock_user(s, arg0, 0);
        return ret;
    case TARGET_SYS_CLOSE:
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "close,%x", arg0);
        } else {
            return set_swi_errno(ts, close(arg0));
        }
    case TARGET_SYS_WRITEC:
        qemu_semihosting_console_outc(env, args);
        return 0xdeadbeef;
    case TARGET_SYS_WRITE0:
        return qemu_semihosting_console_outs(env, args);
    case TARGET_SYS_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            arm_semi_syscall_len = len;
            return arm_gdb_syscall(cpu, arm_semi_cb, "write,%x,%x,%x",
                                   arg0, arg1, len);
        } else {
            s = lock_user(VERIFY_READ, arg1, len, 1);
            if (!s) {
                /* Return bytes not written on error */
                return len;
            }
            ret = set_swi_errno(ts, write(arg0, s, len));
            unlock_user(s, arg1, 0);
            if (ret == (uint32_t)-1) {
                ret = 0;
            }
            /* Return bytes not written */
            return len - ret;
        }
    case TARGET_SYS_READ:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            arm_semi_syscall_len = len;
            return arm_gdb_syscall(cpu, arm_semi_cb, "read,%x,%x,%x",
                                   arg0, arg1, len);
        } else {
            s = lock_user(VERIFY_WRITE, arg1, len, 0);
            if (!s) {
                /* return bytes not read */
                return len;
            }
            do {
                ret = set_swi_errno(ts, read(arg0, s, len));
            } while (ret == -1 && errno == EINTR);
            unlock_user(s, arg1, len);
            if (ret == (uint32_t)-1) {
                ret = 0;
            }
            /* Return bytes not read */
            return len - ret;
        }
    case TARGET_SYS_READC:
        qemu_log_mask(LOG_UNIMP, "%s: SYS_READC not implemented", __func__);
        return 0;
    case TARGET_SYS_ISTTY:
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "isatty,%x", arg0);
        } else {
            return isatty(arg0);
        }
    case TARGET_SYS_SEEK:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "lseek,%x,%x,0",
                                   arg0, arg1);
        } else {
            ret = set_swi_errno(ts, lseek(arg0, arg1, SEEK_SET));
            if (ret == (uint32_t)-1)
              return -1;
            return 0;
        }
    case TARGET_SYS_FLEN:
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_flen_cb, "fstat,%x,%x",
                                   arg0, arm_flen_buf(cpu));
        } else {
            struct stat buf;
            ret = set_swi_errno(ts, fstat(arg0, &buf));
            if (ret == (uint32_t)-1)
                return -1;
            return buf.st_size;
        }
    case TARGET_SYS_TMPNAM:
        qemu_log_mask(LOG_UNIMP, "%s: SYS_TMPNAM not implemented", __func__);
        return -1;
    case TARGET_SYS_REMOVE:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            ret = arm_gdb_syscall(cpu, arm_semi_cb, "unlink,%s",
                                  arg0, (int)arg1+1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                /* FIXME - should this error code be -TARGET_EFAULT ? */
                return (uint32_t)-1;
            }
            ret =  set_swi_errno(ts, remove(s));
            unlock_user(s, arg0, 0);
        }
        return ret;
    case TARGET_SYS_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "rename,%s,%s",
                                   arg0, (int)arg1+1, arg2, (int)arg3+1);
        } else {
            char *s2;
            s = lock_user_string(arg0);
            s2 = lock_user_string(arg2);
            if (!s || !s2)
                /* FIXME - should this error code be -TARGET_EFAULT ? */
                ret = (uint32_t)-1;
            else
                ret = set_swi_errno(ts, rename(s, s2));
            if (s2)
                unlock_user(s2, arg2, 0);
            if (s)
                unlock_user(s, arg0, 0);
            return ret;
        }
    case TARGET_SYS_CLOCK:
        return clock() / (CLOCKS_PER_SEC / 100);
    case TARGET_SYS_TIME:
        return set_swi_errno(ts, time(NULL));
    case TARGET_SYS_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            return arm_gdb_syscall(cpu, arm_semi_cb, "system,%s",
                                   arg0, (int)arg1+1);
        } else {
            s = lock_user_string(arg0);
            if (!s) {
                /* FIXME - should this error code be -TARGET_EFAULT ? */
                return (uint32_t)-1;
            }
            ret = set_swi_errno(ts, system(s));
            unlock_user(s, arg0, 0);
            return ret;
        }
    case TARGET_SYS_ERRNO:
#ifdef CONFIG_USER_ONLY
        return ts->swi_errno;
#else
        return syscall_err;
#endif
    case TARGET_SYS_GET_CMDLINE:
        {
            /* Build a command-line from the original argv.
             *
             * The inputs are:
             *     * arg0, pointer to a buffer of at least the size
             *               specified in arg1.
             *     * arg1, size of the buffer pointed to by arg0 in
             *               bytes.
             *
             * The outputs are:
             *     * arg0, pointer to null-terminated string of the
             *               command line.
             *     * arg1, length of the string pointed to by arg0.
             */

            char *output_buffer;
            size_t input_size;
            size_t output_size;
            int status = 0;
#if !defined(CONFIG_USER_ONLY)
            const char *cmdline;
#endif
            GET_ARG(0);
            GET_ARG(1);
            input_size = arg1;
            /* Compute the size of the output string.  */
#if !defined(CONFIG_USER_ONLY)
            cmdline = semihosting_get_cmdline();
            if (cmdline == NULL) {
                cmdline = ""; /* Default to an empty line. */
            }
            output_size = strlen(cmdline) + 1; /* Count terminating 0. */
#else
            unsigned int i;

            output_size = ts->info->arg_end - ts->info->arg_start;
            if (!output_size) {
                /*
                 * We special-case the "empty command line" case (argc==0).
                 * Just provide the terminating 0.
                 */
                output_size = 1;
            }
#endif

            if (output_size > input_size) {
                /* Not enough space to store command-line arguments.  */
                return -1;
            }

            /* Adjust the command-line length.  */
            if (SET_ARG(1, output_size - 1)) {
                /* Couldn't write back to argument block */
                return -1;
            }

            /* Lock the buffer on the ARM side.  */
            output_buffer = lock_user(VERIFY_WRITE, arg0, output_size, 0);
            if (!output_buffer) {
                return -1;
            }

            /* Copy the command-line arguments.  */
#if !defined(CONFIG_USER_ONLY)
            pstrcpy(output_buffer, output_size, cmdline);
#else
            if (output_size == 1) {
                /* Empty command-line.  */
                output_buffer[0] = '\0';
                goto out;
            }

            if (copy_from_user(output_buffer, ts->info->arg_start,
                               output_size)) {
                status = -1;
                goto out;
            }

            /* Separate arguments by white spaces.  */
            for (i = 0; i < output_size - 1; i++) {
                if (output_buffer[i] == 0) {
                    output_buffer[i] = ' ';
                }
            }
        out:
#endif
            /* Unlock the buffer on the ARM side.  */
            unlock_user(output_buffer, arg0, output_size);

            return status;
        }
    case TARGET_SYS_HEAPINFO:
        {
            target_ulong retvals[4];
            target_ulong limit;
            int i;

            GET_ARG(0);

#ifdef CONFIG_USER_ONLY
            /*
             * Some C libraries assume the heap immediately follows .bss, so
             * allocate it using sbrk.
             */
            if (!ts->heap_limit) {
                abi_ulong ret;

                ts->heap_base = do_brk(0);
                limit = ts->heap_base + ARM_ANGEL_HEAP_SIZE;
                /* Try a big heap, and reduce the size if that fails.  */
                for (;;) {
                    ret = do_brk(limit);
                    if (ret >= limit) {
                        break;
                    }
                    limit = (ts->heap_base >> 1) + (limit >> 1);
                }
                ts->heap_limit = limit;
            }

            retvals[0] = ts->heap_base;
            retvals[1] = ts->heap_limit;
            retvals[2] = ts->stack_base;
            retvals[3] = 0; /* Stack limit.  */
#else
            limit = ram_size;
            /* TODO: Make this use the limit of the loaded application.  */
            retvals[0] = limit / 2;
            retvals[1] = limit;
            retvals[2] = limit; /* Stack base */
            retvals[3] = 0; /* Stack limit.  */
#endif

            for (i = 0; i < ARRAY_SIZE(retvals); i++) {
                bool fail;

                if (is_a64(env)) {
                    fail = put_user_u64(retvals[i], arg0 + i * 8);
                } else {
                    fail = put_user_u32(retvals[i], arg0 + i * 4);
                }

                if (fail) {
                    /* Couldn't write back to argument block */
                    return -1;
                }
            }
            return 0;
        }
    case TARGET_SYS_EXIT:
        if (is_a64(env)) {
            /*
             * The A64 version of this call takes a parameter block,
             * so the application-exit type can return a subcode which
             * is the exit status code from the application.
             */
            GET_ARG(0);
            GET_ARG(1);

            if (arg0 == ADP_Stopped_ApplicationExit) {
                ret = arg1;
            } else {
                ret = 1;
            }
        } else {
            /*
             * ARM specifies only Stopped_ApplicationExit as normal
             * exit, everything else is considered an error
             */
            ret = (args == ADP_Stopped_ApplicationExit) ? 0 : 1;
        }
        gdb_exit(env, ret);
        exit(ret);
    case TARGET_SYS_SYNCCACHE:
        /*
         * Clean the D-cache and invalidate the I-cache for the specified
         * virtual address range. This is a nop for us since we don't
         * implement caches. This is only present on A64.
         */
        if (is_a64(env)) {
            return 0;
        }
        /* fall through -- invalid for A32/T32 */
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(cs, stderr, 0);
        abort();
    }
}
