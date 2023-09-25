/*
 *  BSD conversion extern declarations
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef QEMU_BSD_H
#define QEMU_BSD_H

#include <sys/types.h>
#include <sys/resource.h>

/* bsd-proc.c */
int target_to_host_resource(int code);
rlim_t target_to_host_rlim(abi_llong target_rlim);
abi_llong host_to_target_rlim(rlim_t rlim);
abi_long host_to_target_rusage(abi_ulong target_addr,
        const struct rusage *rusage);
abi_long host_to_target_wrusage(abi_ulong target_addr,
        const struct __wrusage *wrusage);
int host_to_target_waitstatus(int status);
void h2g_rusage(const struct rusage *rusage,
        struct target_freebsd_rusage *target_rusage);

#endif /* QEMU_BSD_H */
