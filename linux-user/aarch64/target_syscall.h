#ifndef AARCH64_TARGET_SYSCALL_H
#define AARCH64_TARGET_SYSCALL_H

struct target_pt_regs {
    uint64_t        regs[31];
    uint64_t        sp;
    uint64_t        pc;
    uint64_t        pstate;
};

#if defined(TARGET_WORDS_BIGENDIAN)
#define UNAME_MACHINE "aarch64_be"
#else
#define UNAME_MACHINE "aarch64"
#endif
#define UNAME_MINIMUM_RELEASE "3.8.0"
#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ       2048
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#define TARGET_PR_SVE_SET_VL  50
#define TARGET_PR_SVE_GET_VL  51

#define TARGET_PR_PAC_RESET_KEYS 54
# define TARGET_PR_PAC_APIAKEY   (1 << 0)
# define TARGET_PR_PAC_APIBKEY   (1 << 1)
# define TARGET_PR_PAC_APDAKEY   (1 << 2)
# define TARGET_PR_PAC_APDBKEY   (1 << 3)
# define TARGET_PR_PAC_APGAKEY   (1 << 4)

#define TARGET_PR_SET_TAGGED_ADDR_CTRL 55
#define TARGET_PR_GET_TAGGED_ADDR_CTRL 56
# define TARGET_PR_TAGGED_ADDR_ENABLE  (1UL << 0)
/* MTE tag check fault modes */
# define TARGET_PR_MTE_TCF_SHIFT       1
# define TARGET_PR_MTE_TCF_NONE        (0UL << TARGET_PR_MTE_TCF_SHIFT)
# define TARGET_PR_MTE_TCF_SYNC        (1UL << TARGET_PR_MTE_TCF_SHIFT)
# define TARGET_PR_MTE_TCF_ASYNC       (2UL << TARGET_PR_MTE_TCF_SHIFT)
# define TARGET_PR_MTE_TCF_MASK        (3UL << TARGET_PR_MTE_TCF_SHIFT)
/* MTE tag inclusion mask */
# define TARGET_PR_MTE_TAG_SHIFT       3
# define TARGET_PR_MTE_TAG_MASK        (0xffffUL << TARGET_PR_MTE_TAG_SHIFT)

#endif /* AARCH64_TARGET_SYSCALL_H */
