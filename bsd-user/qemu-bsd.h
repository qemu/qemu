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
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uuid.h>
#include <sys/wait.h>
#include <netinet/in.h>

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

/* bsd-mem.c */
void target_to_host_ipc_perm__locked(struct ipc_perm *host_ip,
        struct target_ipc_perm *target_ip);
void host_to_target_ipc_perm__locked(struct target_ipc_perm *target_ip,
        struct ipc_perm *host_ip);
abi_long target_to_host_shmid_ds(struct shmid_ds *host_sd,
        abi_ulong target_addr);
abi_long host_to_target_shmid_ds(abi_ulong target_addr,
        struct shmid_ds *host_sd);

#endif /* QEMU_BSD_H */
