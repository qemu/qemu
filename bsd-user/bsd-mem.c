/*
 *  memory management system conversion routines
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
#include "qemu/osdep.h"
#include "qemu.h"
#include "qemu-bsd.h"

struct bsd_shm_regions bsd_shm_regions[N_BSD_SHM_REGIONS];

abi_ulong target_brk;
abi_ulong initial_target_brk;

void target_set_brk(abi_ulong new_brk)
{
    target_brk = TARGET_PAGE_ALIGN(new_brk);
    initial_target_brk = target_brk;
}

void target_to_host_ipc_perm__locked(struct ipc_perm *host_ip,
                                     struct target_ipc_perm *target_ip)
{
    __get_user(host_ip->cuid, &target_ip->cuid);
    __get_user(host_ip->cgid, &target_ip->cgid);
    __get_user(host_ip->uid,  &target_ip->uid);
    __get_user(host_ip->gid,  &target_ip->gid);
    __get_user(host_ip->mode, &target_ip->mode);
    __get_user(host_ip->seq,  &target_ip->seq);
    __get_user(host_ip->key,  &target_ip->key);
}

void host_to_target_ipc_perm__locked(struct target_ipc_perm *target_ip,
                                     struct ipc_perm *host_ip)
{
    __put_user(host_ip->cuid, &target_ip->cuid);
    __put_user(host_ip->cgid, &target_ip->cgid);
    __put_user(host_ip->uid,  &target_ip->uid);
    __put_user(host_ip->gid,  &target_ip->gid);
    __put_user(host_ip->mode, &target_ip->mode);
    __put_user(host_ip->seq,  &target_ip->seq);
    __put_user(host_ip->key,  &target_ip->key);
}

