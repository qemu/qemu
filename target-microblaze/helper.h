#include "def-helper.h"

DEF_HELPER_1(raise_exception, void, i32)
DEF_HELPER_0(debug, void)
DEF_HELPER_4(addkc, i32, i32, i32, i32, i32)
DEF_HELPER_4(subkc, i32, i32, i32, i32, i32)
DEF_HELPER_2(cmp, i32, i32, i32)
DEF_HELPER_2(cmpu, i32, i32, i32)

DEF_HELPER_2(divs, i32, i32, i32)
DEF_HELPER_2(divu, i32, i32, i32)

DEF_HELPER_FLAGS_2(pcmpbf, TCG_CALL_PURE | TCG_CALL_CONST, i32, i32, i32)
#if !defined(CONFIG_USER_ONLY)
DEF_HELPER_1(mmu_read, i32, i32)
DEF_HELPER_2(mmu_write, void, i32, i32)
#endif

#include "def-helper.h"
