#ifdef TARGET_ABI32
# define sizeof_rt_sigframe     0x2b0
# define offsetof_uc_mcontext   0x120
# define offsetof_freg0         0x80
#else
# define sizeof_rt_sigframe     0x340
# define offsetof_uc_mcontext   0x130
# define offsetof_freg0         0x100
#endif
