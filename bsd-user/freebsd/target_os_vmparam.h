/*
 *  FreeBSD VM parameters definitions
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

#ifndef TARGET_OS_VMPARAM_H
#define TARGET_OS_VMPARAM_H

#include "target_arch_vmparam.h"

/* Compare to sys/exec.h */
struct target_ps_strings {
    abi_ulong ps_argvstr;
    uint32_t ps_nargvstr;
    abi_ulong ps_envstr;
    uint32_t ps_nenvstr;
};

extern abi_ulong target_stkbas;
extern abi_ulong target_stksiz;

#define TARGET_PS_STRINGS  ((target_stkbas + target_stksiz) - \
                            sizeof(struct target_ps_strings))

#endif /* TARGET_OS_VMPARAM_H */
