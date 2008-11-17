#include "def-helper.h"

#ifndef TARGET_SPARC64
DEF_HELPER_0(rett, void)
DEF_HELPER_1(wrpsr, void, tl)
DEF_HELPER_0(rdpsr, tl)
#else
DEF_HELPER_1(wrpstate, void, tl)
DEF_HELPER_0(done, void)
DEF_HELPER_0(retry, void)
DEF_HELPER_0(flushw, void)
DEF_HELPER_0(saved, void)
DEF_HELPER_0(restored, void)
DEF_HELPER_0(rdccr, tl)
DEF_HELPER_1(wrccr, void, tl)
DEF_HELPER_0(rdcwp, tl)
DEF_HELPER_1(wrcwp, void, tl)
DEF_HELPER_2(array8, tl, tl, tl)
DEF_HELPER_2(alignaddr, tl, tl, tl)
DEF_HELPER_1(popc, tl, tl)
DEF_HELPER_3(ldda_asi, void, tl, int, int)
DEF_HELPER_4(ldf_asi, void, tl, int, int, int)
DEF_HELPER_4(stf_asi, void, tl, int, int, int)
DEF_HELPER_4(cas_asi, tl, tl, tl, tl, i32)
DEF_HELPER_4(casx_asi, tl, tl, tl, tl, i32)
DEF_HELPER_1(set_softint, void, i64)
DEF_HELPER_1(clear_softint, void, i64)
DEF_HELPER_1(write_softint, void, i64)
DEF_HELPER_2(tick_set_count, void, ptr, i64)
DEF_HELPER_1(tick_get_count, i64, ptr)
DEF_HELPER_2(tick_set_limit, void, ptr, i64)
#endif
DEF_HELPER_2(check_align, void, tl, i32)
DEF_HELPER_0(debug, void)
DEF_HELPER_0(save, void)
DEF_HELPER_0(restore, void)
DEF_HELPER_1(flush, void, tl)
DEF_HELPER_2(udiv, tl, tl, tl)
DEF_HELPER_2(sdiv, tl, tl, tl)
DEF_HELPER_2(stdf, void, tl, int)
DEF_HELPER_2(lddf, void, tl, int)
DEF_HELPER_2(ldqf, void, tl, int)
DEF_HELPER_2(stqf, void, tl, int)
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER_4(ld_asi, i64, tl, int, int, int)
DEF_HELPER_4(st_asi, void, tl, i64, int, int)
#endif
DEF_HELPER_1(ldfsr, void, i32)
DEF_HELPER_0(check_ieee_exceptions, void)
DEF_HELPER_0(clear_float_exceptions, void)
DEF_HELPER_1(fabss, f32, f32)
DEF_HELPER_1(fsqrts, f32, f32)
DEF_HELPER_0(fsqrtd, void)
DEF_HELPER_2(fcmps, void, f32, f32)
DEF_HELPER_0(fcmpd, void)
DEF_HELPER_2(fcmpes, void, f32, f32)
DEF_HELPER_0(fcmped, void)
DEF_HELPER_0(fsqrtq, void)
DEF_HELPER_0(fcmpq, void)
DEF_HELPER_0(fcmpeq, void)
#ifdef TARGET_SPARC64
DEF_HELPER_1(ldxfsr, void, i64)
DEF_HELPER_0(fabsd, void)
DEF_HELPER_2(fcmps_fcc1, void, f32, f32)
DEF_HELPER_2(fcmps_fcc2, void, f32, f32)
DEF_HELPER_2(fcmps_fcc3, void, f32, f32)
DEF_HELPER_0(fcmpd_fcc1, void)
DEF_HELPER_0(fcmpd_fcc2, void)
DEF_HELPER_0(fcmpd_fcc3, void)
DEF_HELPER_2(fcmpes_fcc1, void, f32, f32)
DEF_HELPER_2(fcmpes_fcc2, void, f32, f32)
DEF_HELPER_2(fcmpes_fcc3, void, f32, f32)
DEF_HELPER_0(fcmped_fcc1, void)
DEF_HELPER_0(fcmped_fcc2, void)
DEF_HELPER_0(fcmped_fcc3, void)
DEF_HELPER_0(fabsq, void)
DEF_HELPER_0(fcmpq_fcc1, void)
DEF_HELPER_0(fcmpq_fcc2, void)
DEF_HELPER_0(fcmpq_fcc3, void)
DEF_HELPER_0(fcmpeq_fcc1, void)
DEF_HELPER_0(fcmpeq_fcc2, void)
DEF_HELPER_0(fcmpeq_fcc3, void)
#endif
DEF_HELPER_1(raise_exception, void, int)
#define F_HELPER_0_0(name) DEF_HELPER_0(f ## name, void)
#define F_HELPER_DQ_0_0(name)                   \
    F_HELPER_0_0(name ## d);                    \
    F_HELPER_0_0(name ## q)

F_HELPER_DQ_0_0(add);
F_HELPER_DQ_0_0(sub);
F_HELPER_DQ_0_0(mul);
F_HELPER_DQ_0_0(div);

DEF_HELPER_2(fadds, f32, f32, f32)
DEF_HELPER_2(fsubs, f32, f32, f32)
DEF_HELPER_2(fmuls, f32, f32, f32)
DEF_HELPER_2(fdivs, f32, f32, f32)

DEF_HELPER_2(fsmuld, void, f32, f32)
F_HELPER_0_0(dmulq);

DEF_HELPER_1(fnegs, f32, f32)
DEF_HELPER_1(fitod, void, s32)
DEF_HELPER_1(fitoq, void, s32)

DEF_HELPER_1(fitos, f32, s32)

#ifdef TARGET_SPARC64
DEF_HELPER_0(fnegd, void)
DEF_HELPER_0(fnegq, void)
DEF_HELPER_0(fxtos, i32)
F_HELPER_DQ_0_0(xto);
#endif
DEF_HELPER_0(fdtos, f32)
DEF_HELPER_1(fstod, void, f32)
DEF_HELPER_0(fqtos, f32)
DEF_HELPER_1(fstoq, void, f32)
F_HELPER_0_0(qtod);
F_HELPER_0_0(dtoq);
DEF_HELPER_1(fstoi, s32, f32)
DEF_HELPER_0(fdtoi, s32)
DEF_HELPER_0(fqtoi, s32)
#ifdef TARGET_SPARC64
DEF_HELPER_1(fstox, void, i32)
F_HELPER_0_0(dtox);
F_HELPER_0_0(qtox);
F_HELPER_0_0(aligndata);

F_HELPER_0_0(pmerge);
F_HELPER_0_0(mul8x16);
F_HELPER_0_0(mul8x16al);
F_HELPER_0_0(mul8x16au);
F_HELPER_0_0(mul8sux16);
F_HELPER_0_0(mul8ulx16);
F_HELPER_0_0(muld8sux16);
F_HELPER_0_0(muld8ulx16);
F_HELPER_0_0(expand);
#define VIS_HELPER(name)                                 \
    F_HELPER_0_0(name##16);                              \
    DEF_HELPER_2(f ## name ## 16s, i32, i32, i32) \
    F_HELPER_0_0(name##32);                              \
    DEF_HELPER_2(f ## name ## 32s, i32, i32, i32)

VIS_HELPER(padd);
VIS_HELPER(psub);
#define VIS_CMPHELPER(name)                              \
    F_HELPER_0_0(name##16);                              \
    F_HELPER_0_0(name##32)
VIS_CMPHELPER(cmpgt);
VIS_CMPHELPER(cmpeq);
VIS_CMPHELPER(cmple);
VIS_CMPHELPER(cmpne);
#endif
#undef F_HELPER_0_0
#undef F_HELPER_DQ_0_0
#undef VIS_HELPER
#undef VIS_CMPHELPER

#include "def-helper.h"
