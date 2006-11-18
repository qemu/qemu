/*
 *  Arm "Angel" semihosting syscalls
 * 
 *  Copyright (c) 2005 CodeSourcery, LLC. Written by Paul Brook.
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

#include "qemu.h"

#define ARM_ANGEL_HEAP_SIZE (128 * 1024 * 1024)

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

int open_modeflags[12] = {
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

static inline uint32_t set_swi_errno(TaskState *ts, uint32_t code)
{
  if (code == (uint32_t)-1)
      ts->swi_errno = errno;
  return code;
}

#define ARG(n) tget32(args + n * 4)
uint32_t do_arm_semihosting(CPUState *env)
{
    target_ulong args;
    char * s;
    int nr;
    uint32_t ret;
    TaskState *ts = env->opaque;

    nr = env->regs[0];
    args = env->regs[1];
    switch (nr) {
    case SYS_OPEN:
        s = (char *)g2h(ARG(0));
        if (ARG(1) >= 12)
          return (uint32_t)-1;
        if (strcmp(s, ":tt") == 0) {
            if (ARG(1) < 4)
                return STDIN_FILENO;
            else
                return STDOUT_FILENO;
        }
        return set_swi_errno(ts, open(s, open_modeflags[ARG(1)], 0644));
    case SYS_CLOSE:
        return set_swi_errno(ts, close(ARG(0)));
    case SYS_WRITEC:
        {
          char c = tget8(args);
          /* Write to debug console.  stderr is near enough.  */
          return write(STDERR_FILENO, &c, 1);
        }
    case SYS_WRITE0:
        s = lock_user_string(args);
        ret = write(STDERR_FILENO, s, strlen(s));
        unlock_user(s, args, 0);
        return ret;
    case SYS_WRITE:
        ret = set_swi_errno(ts, write(ARG(0), g2h(ARG(1)), ARG(2)));
        if (ret == (uint32_t)-1)
            return -1;
        return ARG(2) - ret;
    case SYS_READ:
        ret = set_swi_errno(ts, read(ARG(0), g2h(ARG(1)), ARG(2)));
        if (ret == (uint32_t)-1)
            return -1;
        return ARG(2) - ret;
    case SYS_READC:
       /* XXX: Read from debug cosole. Not implemented.  */
        return 0;
    case SYS_ISTTY:
        return isatty(ARG(0));
    case SYS_SEEK:
        ret = set_swi_errno(ts, lseek(ARG(0), ARG(1), SEEK_SET));
	if (ret == (uint32_t)-1)
	  return -1;
	return 0;
    case SYS_FLEN:
        {
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
        return set_swi_errno(ts, remove((char *)g2h(ARG(0))));
    case SYS_RENAME:
        return set_swi_errno(ts, rename((char *)g2h(ARG(0)),
                             (char *)g2h(ARG(2))));
    case SYS_CLOCK:
        return clock() / (CLOCKS_PER_SEC / 100);
    case SYS_TIME:
        return set_swi_errno(ts, time(NULL));
    case SYS_SYSTEM:
        return set_swi_errno(ts, system((char *)g2h(ARG(0))));
    case SYS_ERRNO:
        return ts->swi_errno;
    case SYS_GET_CMDLINE:
        /* XXX: Not implemented.  */
        s = (char *)g2h(ARG(0));
        *s = 0;
        return -1;
    case SYS_HEAPINFO:
        {
            uint32_t *ptr;
            uint32_t limit;

            /* Some C llibraries assume the heap immediately follows .bss, so
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
              
            page_unprotect_range (ARG(0), 32);
            ptr = (uint32_t *)g2h(ARG(0));
            ptr[0] = tswap32(ts->heap_base);
            ptr[1] = tswap32(ts->heap_limit);
            ptr[2] = tswap32(ts->stack_base);
            ptr[3] = tswap32(0); /* Stack limit.  */
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

