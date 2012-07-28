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
