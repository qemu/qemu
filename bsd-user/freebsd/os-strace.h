/*
 *  FreeBSD dependent strace print functions
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

#include "target_arch_sysarch.h"    /* architecture dependent functions */


static inline void do_os_print_sysarch(const struct syscallname *name,
        abi_long arg1, abi_long arg2, abi_long arg3, abi_long arg4,
        abi_long arg5, abi_long arg6)
{
    /* This is arch dependent */
    do_freebsd_arch_print_sysarch(name, arg1, arg2, arg3, arg4, arg5, arg6);
}
