#include "def-helper.h"

DEF_HELPER_1(raise_exception, void, i32)
DEF_HELPER_0(debug, void)
DEF_HELPER_FLAGS_3(carry, TCG_CALL_PURE | TCG_CALL_CONST, i32, i32, i32, i32)
DEF_HELPER_2(cmp, i32, i32, i32)
DEF_HELPER_2(cmpu, i32, i32, i32)

DEF_HELPER_2(divs, i32, i32, i32)
DEF_HELPER_2(divu, i32, i32, i32)

DEF_HELPER_2(fadd, i32, i32, i32)
DEF_HELPER_2(frsub, i32, i32, i32)
DEF_HELPER_2(fmul, i32, i32, i32)
DEF_HELPER_2(fdiv, i32, i32, i32)
DEF_HELPER_1(flt, i32, i32)
DEF_HELPER_1(fint, i32, i32)
DEF_HELPER_1(fsqrt, i32, i32)

DEF_HELPER_2(fcmp_un, i32, i32, i32)
DEF_HELPER_2(fcmp_lt, i32, i32, i32)
DEF_HELPER_2(fcmp_eq, i32, i32, i32)
DEF_HELPER_2(fcmp_le, i32, i32, i32)
DEF_HELPER_2(fcmp_gt, i32, i32, i32)
DEF_HELPER_2(fcmp_ne, i32, i32, i32)
DEF_HELPER_2(fcmp_ge, i32, i32, i32)

DEF_HELPER_FLAGS_2(pcmpbf, TCG_CALL_PURE | TCG_CALL_CONST, i32, i32, i32)
#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_1(mmu_read, i32, i32)
DEF_HELPER_2(mmu_write, void, i32, i32)
#endif

DEF_HELPER_4(memalign, void, i32, i32, i32, i32)

DEF_HELPER_2(get, i32, i32, i32)
DEF_HELPER_3(put, void, i32, i32, i32)

#include "def-helper.h"
