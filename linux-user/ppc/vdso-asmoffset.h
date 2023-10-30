/*
 * Size of dummy stack frame allocated when calling signal handler.
 * See arch/powerpc/include/asm/ptrace.h.
 */
#ifdef TARGET_ABI32
# define SIGNAL_FRAMESIZE                   64
#else
# define SIGNAL_FRAMESIZE                   128
#endif

#ifdef TARGET_ABI32
# define offsetof_sigframe_mcontext         0x20
# define offsetof_rt_sigframe_mcontext      0x140
# define offsetof_mcontext_fregs            0xc0
# define offsetof_mcontext_vregs            0x1d0
#else
# define offsetof_rt_sigframe_mcontext      0xe8
# define offsetof_mcontext_fregs            0x180
# define offsetof_mcontext_vregs_ptr        0x288
#endif
