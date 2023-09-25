/*
 *  process related system call shims and definitions
 *
 *  Copyright (c) 2013-14 Stacey D. Son
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

#ifndef BSD_USER_FREEBSD_OS_PROC_H
#define BSD_USER_FREEBSD_OS_PROC_H

#include <sys/param.h>
#include <sys/procctl.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/procdesc.h>
#include <sys/wait.h>
#include <unistd.h>

#include "target_arch_cpu.h"

/* execve(2) */
static inline abi_long do_freebsd_execve(abi_ulong path_or_fd, abi_ulong argp,
        abi_ulong envp)
{

    return freebsd_exec_common(path_or_fd, argp, envp, 0);
}

/* fexecve(2) */
static inline abi_long do_freebsd_fexecve(abi_ulong path_or_fd, abi_ulong argp,
        abi_ulong envp)
{

    return freebsd_exec_common(path_or_fd, argp, envp, 1);
}

#endif /* BSD_USER_FREEBSD_OS_PROC_H */
