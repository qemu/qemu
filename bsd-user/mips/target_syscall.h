/*
 *  mips system call definitions
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
#ifndef _MIPS_SYSCALL_H_
#define _MIPS_SYSCALL_H_

/*
 * struct target_pt_regs defines the way the registers are stored on the stack
 * during a system call.
 */

struct target_pt_regs {
    /* Saved main processor registers. */
    abi_ulong regs[32];

    /* Saved special registers. */
    abi_ulong cp0_status;
    abi_ulong lo;
    abi_ulong hi;
    abi_ulong cp0_badvaddr;
    abi_ulong cp0_cause;
    abi_ulong cp0_epc;
};

#if defined(TARGET_WORDS_BIGENDIAN)
#define UNAME_MACHINE "mips"
#else
#define UNAME_MACHINE "mipsel"
#endif

#define TARGET_HW_MACHINE       "mips"
#define TARGET_HW_MACHINE_ARCH   UNAME_MACHINE

/* sysarch() commands */
#define TARGET_MIPS_SET_TLS     1
#define TARGET_MIPS_GET_TLS     2

#endif /* !_MIPS_SYSCALL_H_ */
