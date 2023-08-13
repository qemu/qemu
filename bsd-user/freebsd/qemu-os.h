/*
 *  FreeBSD conversion extern declarations
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

#ifndef QEMU_OS_H
#define QEMU_OS_H

/* qemu/osdep.h pulls in the rest */

#include <sys/acl.h>
#include <sys/mount.h>
#include <sys/timex.h>
#include <sys/rtprio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct freebsd11_stat;

/* os-stat.c */
abi_long h2t_freebsd11_stat(abi_ulong target_addr,
        struct freebsd11_stat *host_st);
abi_long h2t_freebsd11_nstat(abi_ulong target_addr,
        struct freebsd11_stat *host_st);
abi_long t2h_freebsd_fhandle(fhandle_t *host_fh, abi_ulong target_addr);
abi_long h2t_freebsd_fhandle(abi_ulong target_addr, fhandle_t *host_fh);
abi_long h2t_freebsd11_statfs(abi_ulong target_addr,
    struct freebsd11_statfs *host_statfs);
abi_long target_to_host_fcntl_cmd(int cmd);
abi_long h2t_freebsd_stat(abi_ulong target_addr,
        struct stat *host_st);
abi_long h2t_freebsd_statfs(abi_ulong target_addr,
    struct statfs *host_statfs);

#endif /* QEMU_OS_H */
