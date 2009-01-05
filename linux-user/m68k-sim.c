/*
 *  m68k simulator syscall interface
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
 *  Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "qemu.h"

#define SYS_EXIT        1
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_BRK         17
#define SYS_FSTAT       28
#define SYS_ISATTY      29
#define SYS_LSEEK       199

struct m86k_sim_stat {
    uint16_t sim_st_dev;
    uint16_t sim_st_ino;
    uint32_t sim_st_mode;
    uint16_t sim_st_nlink;
    uint16_t sim_st_uid;
    uint16_t sim_st_gid;
    uint16_t sim_st_rdev;
    uint32_t sim_st_size;
    uint32_t sim_st_atime;
    uint32_t sim_st_mtime;
    uint32_t sim_st_ctime;
    uint32_t sim_st_blksize;
    uint32_t sim_st_blocks;
};

static inline uint32_t check_err(CPUM68KState *env, uint32_t code)
{
  env->dregs[0] = code;
  if (code == (uint32_t)-1) {
      env->dregs[1] = errno;
  } else {
      env->dregs[1] = 0;
  }
  return code;
}

#define SIM_O_APPEND    0x0008
#define SIM_O_CREAT     0x0200
#define SIM_O_TRUNC     0x0400
#define SIM_O_EXCL      0x0800
#define SIM_O_NONBLOCK  0x4000
#define SIM_O_NOCTTY    0x8000
#define SIM_O_SYNC      0x2000

static int translate_openflags(int flags)
{
    int hf;

    switch (flags & 3) {
    case 0: hf = O_RDONLY; break;
    case 1: hf = O_WRONLY; break;
    case 2: hf = O_RDWR; break;
    default: hf = O_RDWR; break;
    }

    if (flags & SIM_O_APPEND) hf |= O_APPEND;
    if (flags & SIM_O_CREAT) hf |= O_CREAT;
    if (flags & SIM_O_TRUNC) hf |= O_TRUNC;
    if (flags & SIM_O_EXCL) hf |= O_EXCL;
    if (flags & SIM_O_NONBLOCK) hf |= O_NONBLOCK;
    if (flags & SIM_O_NOCTTY) hf |= O_NOCTTY;
    if (flags & SIM_O_SYNC) hf |= O_SYNC;

    return hf;
}

#define ARG(x) tswap32(args[x])
void do_m68k_simcall(CPUM68KState *env, int nr)
{
    uint32_t *args;

    args = (uint32_t *)(unsigned long)(env->aregs[7] + 4);
    switch (nr) {
    case SYS_EXIT:
        exit(ARG(0));
    case SYS_READ:
        check_err(env, read(ARG(0), (void *)(unsigned long)ARG(1), ARG(2)));
        break;
    case SYS_WRITE:
        check_err(env, write(ARG(0), (void *)(unsigned long)ARG(1), ARG(2)));
        break;
    case SYS_OPEN:
        check_err(env, open((char *)(unsigned long)ARG(0),
                            translate_openflags(ARG(1)), ARG(2)));
        break;
    case SYS_CLOSE:
        {
            /* Ignore attempts to close stdin/out/err.  */
            int fd = ARG(0);
            if (fd > 2)
              check_err(env, close(fd));
            else
              check_err(env, 0);
            break;
        }
    case SYS_BRK:
        {
            int32_t ret;

            ret = do_brk((abi_ulong)ARG(0));
            if (ret == -ENOMEM)
                ret = -1;
            check_err(env, ret);
        }
        break;
    case SYS_FSTAT:
        {
            struct stat s;
            int rc;
            struct m86k_sim_stat *p;
            rc = check_err(env, fstat(ARG(0), &s));
            if (rc == 0) {
                p = (struct m86k_sim_stat *)(unsigned long)ARG(1);
                p->sim_st_dev = tswap16(s.st_dev);
                p->sim_st_ino = tswap16(s.st_ino);
                p->sim_st_mode = tswap32(s.st_mode);
                p->sim_st_nlink = tswap16(s.st_nlink);
                p->sim_st_uid = tswap16(s.st_uid);
                p->sim_st_gid = tswap16(s.st_gid);
                p->sim_st_rdev = tswap16(s.st_rdev);
                p->sim_st_size = tswap32(s.st_size);
                p->sim_st_atime = tswap32(s.st_atime);
                p->sim_st_mtime = tswap32(s.st_mtime);
                p->sim_st_ctime = tswap32(s.st_ctime);
                p->sim_st_blksize = tswap32(s.st_blksize);
                p->sim_st_blocks = tswap32(s.st_blocks);
            }
        }
        break;
    case SYS_ISATTY:
        check_err(env, isatty(ARG(0)));
        break;
    case SYS_LSEEK:
        check_err(env, lseek(ARG(0), (int32_t)ARG(1), ARG(2)));
        break;
    default:
        cpu_abort(env, "Unsupported m68k sim syscall %d\n", nr);
    }
}
