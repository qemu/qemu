#include "def-helper.h"

DEF_HELPER_2(fcmpo, i32, i64, i64)
DEF_HELPER_2(fcmpu, i32, i64, i64)

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

DEF_HELPER_0(float_check_status, void)
#ifdef CONFIG_SOFTFLOAT
DEF_HELPER_0(reset_fpstatus, void)
#endif
DEF_HELPER_2(compute_fprf, i32, i64, i32)
DEF_HELPER_2(store_fpscr, void, i64, i32)
DEF_HELPER_1(fpscr_setbit, void, i32)

DEF_HELPER_1(fctiw, i64, i64)
DEF_HELPER_1(fctiwz, i64, i64)
#if defined(TARGET_PPC64)
DEF_HELPER_1(fcfid, i64, i64)
DEF_HELPER_1(fctid, i64, i64)
DEF_HELPER_1(fctidz, i64, i64)
#endif
DEF_HELPER_1(frsp, i64, i64)
DEF_HELPER_1(frin, i64, i64)
DEF_HELPER_1(friz, i64, i64)
DEF_HELPER_1(frip, i64, i64)
DEF_HELPER_1(frim, i64, i64)

DEF_HELPER_2(fadd, i64, i64, i64)
DEF_HELPER_2(fsub, i64, i64, i64)
DEF_HELPER_2(fmul, i64, i64, i64)
DEF_HELPER_2(fdiv, i64, i64, i64)
DEF_HELPER_3(fmadd, i64, i64, i64, i64)
DEF_HELPER_3(fmsub, i64, i64, i64, i64)
DEF_HELPER_3(fnmadd, i64, i64, i64, i64)
DEF_HELPER_3(fnmsub, i64, i64, i64, i64)
DEF_HELPER_1(fabs, i64, i64)
DEF_HELPER_1(fnabs, i64, i64)
DEF_HELPER_1(fneg, i64, i64)
DEF_HELPER_1(fsqrt, i64, i64);
DEF_HELPER_1(fre, i64, i64);
DEF_HELPER_1(fres, i64, i64);
DEF_HELPER_1(frsqrte, i64, i64);
DEF_HELPER_3(fsel, i64, i64, i64, i64)

#include "def-helper.h"
