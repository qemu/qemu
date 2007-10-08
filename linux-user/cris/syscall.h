
#define UNAME_MACHINE "cris"

/* pt_regs not only specifices the format in the user-struct during
 * ptrace but is also the frame format used in the kernel prologue/epilogues
 * themselves
 */

struct target_pt_regs {
        unsigned long orig_r10;
        /* pushed by movem r13, [sp] in SAVE_ALL. */
        unsigned long r0;
        unsigned long r1;
        unsigned long r2;
        unsigned long r3;
        unsigned long r4;
        unsigned long r5;
        unsigned long r6;
        unsigned long r7;
        unsigned long r8;
        unsigned long r9;
        unsigned long r10;
        unsigned long r11;
        unsigned long r12;
        unsigned long r13;
        unsigned long acr;
        unsigned long srs;
        unsigned long mof;
        unsigned long spc;
        unsigned long ccs;
        unsigned long srp;
        unsigned long erp; /* This is actually the debugged process' PC */
        /* For debugging purposes; saved only when needed. */
        unsigned long exs;
        unsigned long eda;
};

