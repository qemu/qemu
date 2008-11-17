#include "def-helper.h"

DEF_HELPER_0(fcmpo, i32)
DEF_HELPER_0(fcmpu, i32)

DEF_HELPER_0(load_cr, tl)
DEF_HELPER_2(store_cr, void, tl, i32)

#if defined(TARGET_PPC64)
DEF_HELPER_2(mulhd, i64, i64, i64)
DEF_HELPER_2(mulhdu, i64, i64, i64)
DEF_HELPER_2(mulldo, i64, i64, i64)
#endif

DEF_HELPER_1(cntlzw, tl, tl)
DEF_HELPER_1(popcntb, tl, tl)
DEF_HELPER_2(sraw, tl, tl, tl)
#if defined(TARGET_PPC64)
DEF_HELPER_1(cntlzd, tl, tl)
DEF_HELPER_1(popcntb_64, tl, tl)
DEF_HELPER_2(srad, tl, tl, tl)
#endif

DEF_HELPER_1(cntlsw32, i32, i32)
DEF_HELPER_1(cntlzw32, i32, i32)
DEF_HELPER_2(brinc, tl, tl, tl)

#include "def-helper.h"
