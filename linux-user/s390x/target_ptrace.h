/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef S390X_TARGET_PTRACE_H
#define S390X_TARGET_PTRACE_H

typedef struct {
    abi_ulong mask;
    abi_ulong addr;
} target_psw_t;

struct target_s390_regs {
    target_psw_t psw;
    abi_ulong gprs[16];
    abi_uint acrs[16];
    abi_ulong orig_gpr2;
};

#endif /* S390X_TARGET_PTRACE_H */
