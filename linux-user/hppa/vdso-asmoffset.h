#define sizeof_rt_sigframe              584
#define offsetof_sigcontext             160
#define offsetof_sigcontext_gr          0x4
#define offsetof_sigcontext_fr          0x88
#define offsetof_sigcontext_iaoq        0x190
#define offsetof_sigcontext_sar         0x198

/* arch/parisc/include/asm/rt_sigframe.h */
#define SIGFRAME                        64
#define FUNCTIONCALLFRAME               48
#define PARISC_RT_SIGFRAME_SIZE32 \
    (((sizeof_rt_sigframe) + FUNCTIONCALLFRAME + SIGFRAME) & -SIGFRAME)
