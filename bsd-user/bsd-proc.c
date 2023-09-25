/*
 *  BSD process related system call helpers
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
#include "qemu/osdep.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cpuset.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "qemu.h"
#include "qemu-bsd.h"
#include "signal-common.h"

#include "bsd-proc.h"

/*
 * resource/rusage conversion
 */
int target_to_host_resource(int code)
{
    return code;
}

rlim_t target_to_host_rlim(abi_llong target_rlim)
{
    return tswap64(target_rlim);
}

abi_llong host_to_target_rlim(rlim_t rlim)
{
    return tswap64(rlim);
}

