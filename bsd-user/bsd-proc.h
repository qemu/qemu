/*
 *  process related system call shims and definitions
 *
 *  Copyright (c) 2013-2014 Stacey D. Son
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

#ifndef BSD_PROC_H_
#define BSD_PROC_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

/* exit(2) */
static inline abi_long do_bsd_exit(void *cpu_env, abi_long arg1)
{
#ifdef TARGET_GPROF
    _mcleanup();
#endif
    gdb_exit(arg1);
    qemu_plugin_user_exit();
    _exit(arg1);

    return 0;
}

#endif /* !BSD_PROC_H_ */
