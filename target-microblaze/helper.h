#include "exec/def-helper.h"

DEF_HELPER_2(raise_exception, void, env, i32)
DEF_HELPER_1(debug, void, env)
DEF_HELPER_FLAGS_3(carry, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)
DEF_HELPER_2(cmp, i32, i32, i32)
DEF_HELPER_2(cmpu, i32, i32, i32)
DEF_HELPER_FLAGS_1(clz, TCG_CALL_NO_RWG_SE, i32, i32)

DEF_HELPER_3(divs, i32, env, i32, i32)
DEF_HELPER_3(divu, i32, env, i32, i32)

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
DEF_HELPER_2(mmu_read, i32, env, i32)
DEF_HELPER_3(mmu_write, void, env, i32, i32)
#endif

DEF_HELPER_5(memalign, void, env, i32, i32, i32, i32)
DEF_HELPER_2(stackprot, void, env, i32)

DEF_HELPER_2(get, i32, i32, i32)
DEF_HELPER_3(put, void, i32, i32, i32)

#include "exec/def-helper.h"
