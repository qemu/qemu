/*
 * offsetof(struct sigframe, sc.eip)
 * offsetof(struct rt_sigframe, uc.tuc_mcontext.eip)
 */
#define SIGFRAME_SIGCONTEXT_eip      64
#define RT_SIGFRAME_SIGCONTEXT_eip  220
