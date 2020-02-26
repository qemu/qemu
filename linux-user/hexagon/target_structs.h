/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/*
 * Hexagon specific structures for linux-user
 */
#ifndef HEXAGON_TARGET_STRUCTS_H
#define HEXAGON_TARGET_STRUCTS_H

struct target_ipc_perm {
    abi_int __key;
    abi_int uid;
    abi_int gid;
    abi_int cuid;
    abi_int cgid;
    abi_ushort mode;
    abi_ushort __pad1;
    abi_ushort __seq;
};

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;
    abi_long shm_segsz;
    abi_ulong shm_atime;
    abi_ulong shm_dtime;
    abi_ulong shm_ctime;
    abi_int shm_cpid;
    abi_int shm_lpid;
    abi_ulong shm_nattch;
};

#endif
