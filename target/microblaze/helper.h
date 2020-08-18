DEF_HELPER_FLAGS_2(raise_exception, TCG_CALL_NO_WG, noreturn, env, i32)

DEF_HELPER_FLAGS_3(divs, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(divu, TCG_CALL_NO_WG, i32, env, i32, i32)

DEF_HELPER_3(fadd, i32, env, i32, i32)
DEF_HELPER_3(frsub, i32, env, i32, i32)
DEF_HELPER_3(fmul, i32, env, i32, i32)
DEF_HELPER_3(fdiv, i32, env, i32, i32)
DEF_HELPER_2(flt, i32, env, i32)
DEF_HELPER_2(fint, i32, env, i32)
DEF_HELPER_2(fsqrt, i32, env, i32)

DEF_HELPER_3(fcmp_un, i32, env, i32, i32)
DEF_HELPER_3(fcmp_lt, i32, env, i32, i32)
DEF_HELPER_3(fcmp_eq, i32, env, i32, i32)
DEF_HELPER_3(fcmp_le, i32, env, i32, i32)
DEF_HELPER_3(fcmp_gt, i32, env, i32, i32)
DEF_HELPER_3(fcmp_ne, i32, env, i32, i32)
DEF_HELPER_3(fcmp_ge, i32, env, i32, i32)

DEF_HELPER_FLAGS_2(pcmpbf, TCG_CALL_NO_RWG_SE, i32, i32, i32)
#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_3(mmu_read, i32, env, i32, i32)
DEF_HELPER_4(mmu_write, void, env, i32, i32, i32)
#endif

DEF_HELPER_5(memalign, void, env, tl, i32, i32, i32)
DEF_HELPER_2(stackprot, void, env, tl)

DEF_HELPER_2(get, i32, i32, i32)
DEF_HELPER_3(put, void, i32, i32, i32)
