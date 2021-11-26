#ifndef HPPA_TARGET_SYSCALL_H
#define HPPA_TARGET_SYSCALL_H

struct target_pt_regs {
    target_ulong gr[32];
    uint64_t     fr[32];
    target_ulong sr[8];
    target_ulong iasq[2];
    target_ulong iaoq[2];
    target_ulong cr27;
    target_ulong __pad0;
    target_ulong orig_r28;
    target_ulong ksp;
    target_ulong kpc;
    target_ulong sar;
    target_ulong iir;
    target_ulong isr;
    target_ulong ior;
    target_ulong ipsw;
};

#define UNAME_MACHINE "parisc"
#define UNAME_MINIMUM_RELEASE "2.6.32"
#define TARGET_CLONE_BACKWARDS
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif /* HPPA_TARGET_SYSCALL_H */
