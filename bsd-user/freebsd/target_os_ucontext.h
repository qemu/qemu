/*
 * FreeBSD has a common ucontext definition for all architectures.
 *
 * Copyright 2021 Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause
 */
#ifndef TARGET_OS_UCONTEXT_H
#define TARGET_OS_UCONTEXT_H

/*
 * Defines the common bits for all of FreeBSD's architectures. Has to be
 * included AFTER the MD target_mcontext_t is defined, however, so can't
 * be in the grab-bag that is target_os_signal.h.
 */

/* See FreeBSD's sys/ucontext.h */
#define TARGET_MC_GET_CLEAR_RET 0x0001

/* FreeBSD's sys/_ucontext.h structures */
typedef struct target_ucontext {
    target_sigset_t     uc_sigmask;
    target_mcontext_t   uc_mcontext;
    abi_ulong           uc_link;
    target_stack_t      uc_stack;
    int32_t             uc_flags;
    int32_t             __spare__[4];
} target_ucontext_t;

G_STATIC_ASSERT(TARGET_MCONTEXT_SIZE == sizeof(target_mcontext_t));
G_STATIC_ASSERT(TARGET_UCONTEXT_SIZE == sizeof(target_ucontext_t));

struct target_sigframe;

abi_long set_sigtramp_args(CPUArchState *env, int sig,
                           struct target_sigframe *frame,
                           abi_ulong frame_addr,
                           struct target_sigaction *ka);
abi_long get_mcontext(CPUArchState *env, target_mcontext_t *mcp, int flags);
abi_long set_mcontext(CPUArchState *env, target_mcontext_t *mcp, int srflag);
abi_long get_ucontext_sigreturn(CPUArchState *env, abi_ulong target_sf,
                                abi_ulong *target_uc);

#endif /* TARGET_OS_UCONTEXT_H */
