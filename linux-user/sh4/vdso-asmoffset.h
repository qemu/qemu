/*
 * Offsets into target signal frames for sh4 vdso.
 *
 * From linux-user/sh4/signal.c:
 *
 * struct target_sigcontext {
 *     target_ulong oldmask;           //   0
 *     target_ulong sc_gregs[16];      //   4
 *     target_ulong sc_pc;             //  68
 *     target_ulong sc_pr;             //  72
 *     target_ulong sc_sr;             //  76
 *     target_ulong sc_gbr;            //  80
 *     target_ulong sc_mach;           //  84
 *     target_ulong sc_macl;           //  88
 *     target_ulong sc_fpregs[16];     //  92
 *     target_ulong sc_xfpregs[16];    // 156
 *     unsigned int sc_fpscr;          // 220
 *     unsigned int sc_fpul;           // 224
 *     unsigned int sc_ownedfp;        // 228
 * };                                  // sizeof = 232
 *
 * struct sigframe    { sigcontext sc; ... }
 * struct rt_sigframe { siginfo info[128]; ucontext uc; }
 *   ucontext = { flags[4], link[4], stack[12], sigcontext mcontext; ... }
 *   => mcontext at rt_sigframe + 128 + 20 = rt_sigframe + 148
 */

/* Offset of sigcontext within sigframe (CFA base for sigreturn). */
#define SIGFRAME_SC_OFFSET      0

/* Offset of tuc_mcontext within rt_sigframe (CFA base for rt_sigreturn). */
#define RT_SIGFRAME_SC_OFFSET   148

/* Offsets within struct sigcontext. */
#define SC_GREGS    4
#define SC_PC       68
#define SC_PR       72
#define SC_SR       76
#define SC_GBR      80
#define SC_MACH     84
#define SC_MACL     88
#define SC_FPREGS   92
#define SC_XFPREGS  156
#define SC_FPSCR    220
#define SC_FPUL     224
