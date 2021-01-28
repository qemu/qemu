/*
 *  sparc dependent system call definitions
 *
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
#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

struct target_pt_regs {
	abi_ulong psr;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong u_regs[16];
};

#define UNAME_MACHINE           "sun4"
#define TARGET_HW_MACHINE       "sparc"
#define TARGET_HW_MACHINE_ARCH  "sparc"

#define TARGET_SPARC_UTRAP_INSTALL      1
#define TARGET_SPARC_SIGTRAMP_INSTALL   2

#endif /* TARGET_SYSCALL_H */
