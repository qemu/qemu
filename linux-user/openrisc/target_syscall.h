#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

struct target_pt_regs {
    union {
        struct {
            /* Named registers */
            uint32_t sr;       /* Stored in place of r0 */
            target_ulong sp;   /* r1 */
        };
        struct {
            /* Old style */
            target_ulong offset[2];
            target_ulong gprs[30];
        };
        struct {
            /* New style */
            target_ulong gpr[32];
        };
    };
    target_ulong pc;
    target_ulong orig_gpr11;   /* For restarting system calls */
    uint32_t syscallno;        /* Syscall number (used by strace) */
    target_ulong dummy;     /* Cheap alignment fix */
};

#define UNAME_MACHINE "openrisc"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif  /* TARGET_SYSCALL_H */
