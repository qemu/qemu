/*
 *  FreeBSD sysctl() and sysarch() system call emulation
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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
#include "target_arch_sysarch.h"

#include <sys/sysctl.h>

/*
 * Length for the fixed length types.
 * 0 means variable length for strings and structures
 * Compare with sys/kern_sysctl.c ctl_size
 * Note: Not all types appear to be used in-tree.
 */
static const int G_GNUC_UNUSED guest_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(abi_int),
        [CTLTYPE_UINT] = sizeof(abi_uint),
        [CTLTYPE_LONG] = sizeof(abi_long),
        [CTLTYPE_ULONG] = sizeof(abi_ulong),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

static const int G_GNUC_UNUSED host_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(int),
        [CTLTYPE_UINT] = sizeof(u_int),
        [CTLTYPE_LONG] = sizeof(long),
        [CTLTYPE_ULONG] = sizeof(u_long),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

#ifdef TARGET_ABI32
/*
 * Limit the amount of available memory to be most of the 32-bit address
 * space. 0x100c000 was arrived at through trial and error as a good
 * definition of 'most'.
 */
static const abi_ulong guest_max_mem = UINT32_MAX - 0x100c000 + 1;

static abi_ulong G_GNUC_UNUSED cap_memory(uint64_t mem)
{
    return MIN(guest_max_mem, mem);
}
#endif

static abi_ulong G_GNUC_UNUSED scale_to_guest_pages(uint64_t pages)
{
    /* Scale pages from host to guest */
    pages = muldiv64(pages, qemu_real_host_page_size(), TARGET_PAGE_SIZE);
#ifdef TARGET_ABI32
    /* cap pages if need be */
    pages = MIN(pages, guest_max_mem / (abi_ulong)TARGET_PAGE_SIZE);
#endif
    return pages;
}

#ifdef TARGET_ABI32
/* Used only for TARGET_ABI32 */
static abi_long G_GNUC_UNUSED h2g_long_sat(long l)
{
    if (l > INT32_MAX) {
        l = INT32_MAX;
    } else if (l < INT32_MIN) {
        l = INT32_MIN;
    }
    return l;
}

static abi_ulong G_GNUC_UNUSED h2g_ulong_sat(u_long ul)
{
    return MIN(ul, UINT32_MAX);
}
#endif

/*
 * placeholder until bsd-user downstream upstreams this with its thread support
 */
#define bsd_get_ncpu() 1

/* sysarch() is architecture dependent. */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}
