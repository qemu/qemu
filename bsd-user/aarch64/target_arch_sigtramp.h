/*
 * ARM AArch64 sigcode for bsd-user
 *
 * Copyright (c) 2015 Stacey D. Son <sson at FreeBSD>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_SIGTRAMP_H
#define TARGET_ARCH_SIGTRAMP_H

/* Compare to ENTRY(sigcode) in arm64/arm64/locore.S */
static inline abi_long setup_sigtramp(abi_ulong offset, unsigned sigf_uc,
        unsigned sys_sigreturn)
{
    int i;
    uint32_t sys_exit = TARGET_FREEBSD_NR_exit;

    uint32_t sigtramp_code[] = {
    /* 1 */ 0x910003e0,                 /* mov x0, sp */
    /* 2 */ 0x91000000 + (sigf_uc << 10), /* add x0, x0, #SIGF_UC */
    /* 3 */ 0xd2800000 + (sys_sigreturn << 5) + 0x8, /* mov x8, #SYS_sigreturn */
    /* 4 */ 0xd4000001,                 /* svc #0 */
    /* 5 */ 0xd2800028 + (sys_exit << 5) + 0x8, /* mov x8, #SYS_exit */
    /* 6 */ 0xd4000001,                 /* svc #0 */
    /* 7 */ 0x17fffffc,                 /* b -4 */
    /* 8 */ sys_sigreturn,
    /* 9 */ sys_exit
    };

    for (i = 0; i < 9; i++) {
        tswap32s(&sigtramp_code[i]);
    }

    return memcpy_to_target(offset, sigtramp_code, TARGET_SZSIGCODE);
}
#endif /* TARGET_ARCH_SIGTRAMP_H */
