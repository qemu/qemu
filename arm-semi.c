/*
 *  Arm "Angel" semihosting syscalls
 * 
 *  Copyright (c) 2005, 2007 CodeSourcery.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "cpu.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"

#define ARM_ANGEL_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "vl.h"
#endif

#define SYS_OPEN        0x01
#define SYS_CLOSE       0x02
#define SYS_WRITEC      0x03
#define SYS_WRITE0      0x04
#define SYS_WRITE       0x05
#define SYS_READ        0x06
#define SYS_READC       0x07
#define SYS_ISTTY       0x09
#define SYS_SEEK        0x0a
#define SYS_FLEN        0x0c
#define SYS_TMPNAM      0x0d
#define SYS_REMOVE      0x0e
#define SYS_RENAME      0x0f
#define SYS_CLOCK       0x10
#define SYS_TIME        0x11
#define SYS_SYSTEM      0x12
#define SYS_ERRNO       0x13
#define SYS_GET_CMDLINE 0x15
#define SYS_HEAPINFO    0x16
#define SYS_EXIT        0x18

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
static inline uint32_t set_swi_errno(CPUState *env, uint32_t code)
{
    return code;
}

static uint32_t softmmu_tget32(CPUState *env, uint32_t addr)
{
    uint32_t val;

    cpu_memory_rw_debug(env, addr, (uint8_t *)&val, 4, 0);
    return tswap32(val);
}
static uint32_t softmmu_tget8(CPUState *env, uint32_t addr)
{
    uint8_t val;

    cpu_memory_rw_debug(env, addr, &val, 1, 0);
    return val;
}
#define tget32(p) softmmu_tget32(env, p)
#define tget8(p) softmmu_tget8(env, p)

static void *softmmu_lock_user(CPUState *env, uint32_t addr, uint32_t len,
                               int copy)
{
    char *p;
    /* TODO: Make this something that isn't fixed size.  */
    p = malloc(len);
    if (copy)
        cpu_memory_rw_debug(env, addr, p, len, 0);
    return p;
}
#define lock_user(p, len, copy) softmmu_lock_user(env, p, len, copy)
static char *softmmu_lock_user_string(CPUState *env, uint32_t addr)
{
    char *p;
    char *s;
    uint8_t c;
    /* TODO: Make this something that isn't fixed size.  */
    s = p = malloc(1024);
    do {
        cpu_memory_rw_debug(env, addr, &c, 1, 0);
        addr++;
        *(p++) = c;
    } while (c);
    return s;
}
#define lock_user_string(p) softmmu_lock_user_string(env, p)
static void softmmu_unlock_user(CPUState *env, void *p, target_ulong addr,
                                target_ulong len)
{
    if (len)
        cpu_memory_rw_debug(env, addr, p, len, 1);
    free(p);
}
#define unlock_user(s, args, len) softmmu_unlock_user(env, s, args, len)
#endif

static target_ulong arm_semi_syscall_len;

static void arm_semi_cb(CPUState *env, target_ulong ret, target_ulong err)
{
#ifdef CONFIG_USER_ONLY
    TaskState *ts = env->opaque;
#endif
    if (ret == (target_ulong)-1) {
#ifdef CONFIG_USER_ONLY
        ts->swi_errno = err;
#endif
        env->regs[0] = ret;
    } else {
        /* Fixup syscalls that use nonstardard return conventions.  */
        switch (env->regs[0]) {
        case SYS_WRITE:
        case SYS_READ:
            env->regs[0] = arm_semi_syscall_len - ret;
            break;
        case SYS_SEEK:
            env->regs[0] = 0;
            break;
        default:
            env->regs[0] = ret;
            break;
        }
    }
}

#define ARG(n) tget32(args + (n) * 4)
#define SET_ARG(n, val) tput32(args + (n) * 4,val)
uint32_t do_arm_semihosting(CPUState *env)
{
    target_ulong args;
    char * s;
    int nr;
    uint32_t ret;
    uint32_t len;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = env->opaque;
#else
    CPUState *ts = env;
#endif

    nr = env->regs[0];
    args = env->regs[1];
    switch (nr) {
    case SYS_OPEN:
        s = lock_user_string(ARG(0));
        if (ARG(1) >= 12)
          return (uint32_t)-1;
        if (strcmp(s, ":tt") == 0) {
            if (ARG(1) < 4)
                return STDIN_FILENO;
            else
                return STDOUT_FILENO;
        }
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "open,%s,%x,1a4", ARG(0), (int)ARG(2),
                           gdb_open_modeflags[ARG(1)]);
            return env->regs[0];
        } else {
            ret = set_swi_errno(ts, open(s, open_modeflags[ARG(1)], 0644));
        }
        unlock_user(s, ARG(0), 0);
        return ret;
    case SYS_CLOSE:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "close,%x", ARG(0));
            return env->regs[0];
        } else {
            return set_swi_errno(ts, close(ARG(0)));
        }
    case SYS_WRITEC:
        {
          char c = tget8(args);
          /* Write to debug console.  stderr is near enough.  */
          if (use_gdb_syscalls()) {
                gdb_do_syscall(arm_semi_cb, "write,2,%x,1", args);
                return env->regs[0];
          } else {
                return write(STDERR_FILENO, &c, 1);
          }
        }
    case SYS_WRITE0:
        s = lock_user_string(args);
        len = strlen(s);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "write,2,%x,%x\n", args, len);
            ret = env->regs[0];
        } else {
            ret = write(STDERR_FILENO, s, len);
        }
        unlock_user(s, args, 0);
        return ret;
    case SYS_WRITE:
        len = ARG(2);
        if (use_gdb_syscalls()) {
            arm_semi_syscall_len = len;
            gdb_do_syscall(arm_semi_cb, "write,%x,%x,%x", ARG(0), ARG(1), len);
            return env->regs[0];
        } else {
            s = lock_user(ARG(1), len, 1);
            ret = set_swi_errno(ts, write(ARG(0), s, len));
            unlock_user(s, ARG(1), 0);
            if (ret == (uint32_t)-1)
                return -1;
            return len - ret;
        }
    case SYS_READ:
        len = ARG(2);
        if (use_gdb_syscalls()) {
            arm_semi_syscall_len = len;
            gdb_do_syscall(arm_semi_cb, "read,%x,%x,%x", ARG(0), ARG(1), len);
            return env->regs[0];
        } else {
            s = lock_user(ARG(1), len, 0);
            do
              ret = set_swi_errno(ts, read(ARG(0), s, len));
            while (ret == -1 && errno == EINTR);
            unlock_user(s, ARG(1), len);
            if (ret == (uint32_t)-1)
                return -1;
            return len - ret;
        }
    case SYS_READC:
       /* XXX: Read from debug cosole. Not implemented.  */
        return 0;
    case SYS_ISTTY:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "isatty,%x", ARG(0));
            return env->regs[0];
        } else {
            return isatty(ARG(0));
        }
    case SYS_SEEK:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "fseek,%x,%x,0", ARG(0), ARG(1));
            return env->regs[0];
        } else {
            ret = set_swi_errno(ts, lseek(ARG(0), ARG(1), SEEK_SET));
            if (ret == (uint32_t)-1)
              return -1;
            return 0;
        }
    case SYS_FLEN:
        if (use_gdb_syscalls()) {
            /* TODO: Use stat syscall.  */
            return -1;
        } else {
            struct stat buf;
            ret = set_swi_errno(ts, fstat(ARG(0), &buf));
            if (ret == (uint32_t)-1)
                return -1;
            return buf.st_size;
        }
    case SYS_TMPNAM:
        /* XXX: Not implemented.  */
        return -1;
    case SYS_REMOVE:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "unlink,%s", ARG(0), (int)ARG(1));
            ret = env->regs[0];
        } else {
            s = lock_user_string(ARG(0));
            ret =  set_swi_errno(ts, remove(s));
            unlock_user(s, ARG(0), 0);
        }
        return ret;
    case SYS_RENAME:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "rename,%s,%s",
                           ARG(0), (int)ARG(1), ARG(2), (int)ARG(3));
            return env->regs[0];
        } else {
            char *s2;
            s = lock_user_string(ARG(0));
            s2 = lock_user_string(ARG(2));
            ret = set_swi_errno(ts, rename(s, s2));
            unlock_user(s2, ARG(2), 0);
            unlock_user(s, ARG(0), 0);
            return ret;
        }
    case SYS_CLOCK:
        return clock() / (CLOCKS_PER_SEC / 100);
    case SYS_TIME:
        return set_swi_errno(ts, time(NULL));
    case SYS_SYSTEM:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(arm_semi_cb, "system,%s", ARG(0), (int)ARG(1));
            return env->regs[0];
        } else {
            s = lock_user_string(ARG(0));
            ret = set_swi_errno(ts, system(s));
            unlock_user(s, ARG(0), 0);
        }
    case SYS_ERRNO:
#ifdef CONFIG_USER_ONLY
        return ts->swi_errno;
#else
        return 0;
#endif
    case SYS_GET_CMDLINE:
#ifdef CONFIG_USER_ONLY
        /* Build a commandline from the original argv.  */
        {
            char **arg = ts->info->host_argv;
            int len = ARG(1);
            /* lock the buffer on the ARM side */
            char *cmdline_buffer = (char*)lock_user(ARG(0), len, 0);

            s = cmdline_buffer;
            while (*arg && len > 2) {
                int n = strlen(*arg);

                if (s != cmdline_buffer) {
                    *(s++) = ' ';
                    len--;
                }
                if (n >= len)
                    n = len - 1;
                memcpy(s, *arg, n);
                s += n;
                len -= n;
                arg++;
            }
            /* Null terminate the string.  */
            *s = 0;
            len = s - cmdline_buffer;

            /* Unlock the buffer on the ARM side.  */
            unlock_user(cmdline_buffer, ARG(0), len);

            /* Adjust the commandline length argument.  */
            SET_ARG(1, len);

            /* Return success if commandline fit into buffer.  */
            return *arg ? -1 : 0;
        }
#else
      return -1;
#endif
    case SYS_HEAPINFO:
        {
            uint32_t *ptr;
            uint32_t limit;

#ifdef CONFIG_USER_ONLY
            /* Some C libraries assume the heap immediately follows .bss, so
               allocate it using sbrk.  */
            if (!ts->heap_limit) {
                long ret;

                ts->heap_base = do_brk(0);
                limit = ts->heap_base + ARM_ANGEL_HEAP_SIZE;
                /* Try a big heap, and reduce the size if that fails.  */
                for (;;) {
                    ret = do_brk(limit);
                    if (ret != -1)
                        break;
                    limit = (ts->heap_base >> 1) + (limit >> 1);
                }
                ts->heap_limit = limit;
            }
              
            ptr = lock_user(ARG(0), 16, 0);
            ptr[0] = tswap32(ts->heap_base);
            ptr[1] = tswap32(ts->heap_limit);
            ptr[2] = tswap32(ts->stack_base);
            ptr[3] = tswap32(0); /* Stack limit.  */
            unlock_user(ptr, ARG(0), 16);
#else
            limit = ram_size;
            ptr = lock_user(ARG(0), 16, 0);
            /* TODO: Make this use the limit of the loaded application.  */
            ptr[0] = tswap32(limit / 2);
            ptr[1] = tswap32(limit);
            ptr[2] = tswap32(limit); /* Stack base */
            ptr[3] = tswap32(0); /* Stack limit.  */
            unlock_user(ptr, ARG(0), 16);
#endif
            return 0;
        }
    case SYS_EXIT:
        exit(0);
    default:
        fprintf(stderr, "qemu: Unsupported SemiHosting SWI 0x%02x\n", nr);
        cpu_dump_state(env, stderr, fprintf, 0);
        abort();
    }
}
