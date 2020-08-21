DEF_HELPER_FLAGS_2(raise_exception, TCG_CALL_NO_WG, noreturn, env, i32)

DEF_HELPER_FLAGS_3(divs, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(divu, TCG_CALL_NO_WG, i32, env, i32, i32)

DEF_HELPER_FLAGS_3(fadd, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(frsub, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fmul, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fdiv, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_2(flt, TCG_CALL_NO_WG, i32, env, i32)
DEF_HELPER_FLAGS_2(fint, TCG_CALL_NO_WG, i32, env, i32)
DEF_HELPER_FLAGS_2(fsqrt, TCG_CALL_NO_WG, i32, env, i32)

DEF_HELPER_FLAGS_3(fcmp_un, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_lt, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_eq, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_le, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_gt, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_ne, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(fcmp_ge, TCG_CALL_NO_WG, i32, env, i32, i32)

DEF_HELPER_FLAGS_2(pcmpbf, TCG_CALL_NO_RWG_SE, i32, i32, i32)
#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_3(mmu_read, i32, env, i32, i32)
DEF_HELPER_4(mmu_write, void, env, i32, i32, i32)
#endif

DEF_HELPER_FLAGS_2(stackprot, TCG_CALL_NO_WG, void, env, tl)

DEF_HELPER_2(get, i32, i32, i32)
DEF_HELPER_3(put, void, i32, i32, i32)
