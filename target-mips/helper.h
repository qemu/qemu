#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, do_raise_exception_err, (int excp, int err))
DEF_HELPER(void, do_raise_exception, (int excp))
DEF_HELPER(void, do_interrupt_restart, (void))

DEF_HELPER(void, do_clo, (void))
DEF_HELPER(void, do_clz, (void))
#ifdef TARGET_MIPS64
DEF_HELPER(void, do_dclo, (void))
DEF_HELPER(void, do_dclz, (void))
#endif
