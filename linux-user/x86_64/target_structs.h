/*
 * X86-64 specific structures for linux-user
 *
 * Copyright (c) 2013 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef X86_64_TARGET_STRUCTS_H
#define X86_64_TARGET_STRUCTS_H

#include "../generic/target_structs.h"

/* The x86 definition differs from the generic one in that the
 * two padding fields exist whether the ABI is 32 bits or 64 bits.
 */
#define TARGET_SEMID64_DS
struct target_semid64_ds {
    struct target_ipc_perm sem_perm;
    abi_ulong sem_otime;
    abi_ulong __unused1;
    abi_ulong sem_ctime;
    abi_ulong __unused2;
    abi_ulong sem_nsems;
    abi_ulong __unused3;
    abi_ulong __unused4;
};

#endif
