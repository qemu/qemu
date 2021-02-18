/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_TARGET_SYSCALL_H
#define HEXAGON_TARGET_SYSCALL_H

struct target_pt_regs {
    abi_long sepc;
    abi_long sp;
};

#define UNAME_MACHINE "hexagon"
#define UNAME_MINIMUM_RELEASE "4.15.0"

#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#define TARGET_MCL_CURRENT  1
#define TARGET_MCL_FUTURE   2
#define TARGET_MCL_ONFAULT  4

#endif
