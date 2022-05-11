/*
 *  arm sysarch() system call emulation
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

#ifndef TARGET_ARCH_SIGTRAMP_H
#define TARGET_ARCH_SIGTRAMP_H

/* Compare to arm/arm/locore.S ENTRY_NP(sigcode) */
static inline abi_long setup_sigtramp(abi_ulong offset, unsigned sigf_uc,
        unsigned sys_sigreturn)
{
    int i;
    uint32_t sys_exit = TARGET_FREEBSD_NR_exit;
    uint32_t sigtramp_code[] = {
    /* 1 */ 0xE1A0000D,                  /* mov r0, sp */
    /* 2 */ 0xE2800000 + sigf_uc,        /* add r0, r0, #SIGF_UC */
    /* 3 */ 0xE59F700C,                  /* ldr r7, [pc, #12] */
    /* 4 */ 0xEF000000 + sys_sigreturn,  /* swi (SYS_sigreturn) */
    /* 5 */ 0xE59F7008,                  /* ldr r7, [pc, #8] */
    /* 6 */ 0xEF000000 + sys_exit,       /* swi (SYS_exit)*/
    /* 7 */ 0xEAFFFFFA,                  /* b . -16 */
    /* 8 */ sys_sigreturn,
    /* 9 */ sys_exit
    };

    G_STATIC_ASSERT(sizeof(sigtramp_code) == TARGET_SZSIGCODE);

    for (i = 0; i < 9; i++) {
        tswap32s(&sigtramp_code[i]);
    }

    return memcpy_to_target(offset, sigtramp_code, TARGET_SZSIGCODE);
}
#endif /* TARGET_ARCH_SIGTRAMP_H */
