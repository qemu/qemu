#include "def-helper.h"

#ifndef TARGET_SPARC64
DEF_HELPER_0(rett, void)
DEF_HELPER_1(wrpsr, void, tl)
DEF_HELPER_0(rdpsr, tl)
#else
DEF_HELPER_1(wrpil, void, tl)
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
DEF_HELPER_3(array8, tl, env, tl, tl)
DEF_HELPER_3(alignaddr, tl, env, tl, tl)
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
DEF_HELPER_1(debug, void, env)
DEF_HELPER_0(save, void)
DEF_HELPER_0(restore, void)
DEF_HELPER_2(udiv, tl, tl, tl)
DEF_HELPER_2(udiv_cc, tl, tl, tl)
DEF_HELPER_2(sdiv, tl, tl, tl)
DEF_HELPER_2(sdiv_cc, tl, tl, tl)
DEF_HELPER_2(stdf, void, tl, int)
DEF_HELPER_2(lddf, void, tl, int)
DEF_HELPER_2(ldqf, void, tl, int)
DEF_HELPER_2(stqf, void, tl, int)
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER_4(ld_asi, i64, tl, int, int, int)
DEF_HELPER_4(st_asi, void, tl, i64, int, int)
#endif
DEF_HELPER_2(ldfsr, void, env, i32)
DEF_HELPER_1(check_ieee_exceptions, void, env)
DEF_HELPER_1(clear_float_exceptions, void, env)
DEF_HELPER_1(fabss, f32, f32)
DEF_HELPER_2(fsqrts, f32, env, f32)
DEF_HELPER_1(fsqrtd, void, env)
DEF_HELPER_3(fcmps, void, env, f32, f32)
DEF_HELPER_1(fcmpd, void, env)
DEF_HELPER_3(fcmpes, void, env, f32, f32)
DEF_HELPER_1(fcmped, void, env)
DEF_HELPER_1(fsqrtq, void, env)
DEF_HELPER_1(fcmpq, void, env)
DEF_HELPER_1(fcmpeq, void, env)
#ifdef TARGET_SPARC64
DEF_HELPER_2(ldxfsr, void, env, i64)
DEF_HELPER_1(fabsd, void, env)
DEF_HELPER_3(fcmps_fcc1, void, env, f32, f32)
DEF_HELPER_3(fcmps_fcc2, void, env, f32, f32)
DEF_HELPER_3(fcmps_fcc3, void, env, f32, f32)
DEF_HELPER_1(fcmpd_fcc1, void, env)
DEF_HELPER_1(fcmpd_fcc2, void, env)
DEF_HELPER_1(fcmpd_fcc3, void, env)
DEF_HELPER_3(fcmpes_fcc1, void, env, f32, f32)
DEF_HELPER_3(fcmpes_fcc2, void, env, f32, f32)
DEF_HELPER_3(fcmpes_fcc3, void, env, f32, f32)
DEF_HELPER_1(fcmped_fcc1, void, env)
DEF_HELPER_1(fcmped_fcc2, void, env)
DEF_HELPER_1(fcmped_fcc3, void, env)
DEF_HELPER_1(fabsq, void, env)
DEF_HELPER_1(fcmpq_fcc1, void, env)
DEF_HELPER_1(fcmpq_fcc2, void, env)
DEF_HELPER_1(fcmpq_fcc3, void, env)
DEF_HELPER_1(fcmpeq_fcc1, void, env)
DEF_HELPER_1(fcmpeq_fcc2, void, env)
DEF_HELPER_1(fcmpeq_fcc3, void, env)
#endif
DEF_HELPER_2(raise_exception, void, env, int)
DEF_HELPER_0(shutdown, void)
#define F_HELPER_0_1(name) DEF_HELPER_1(f ## name, void, env)
#define F_HELPER_DQ_0_1(name)                   \
    F_HELPER_0_1(name ## d);                    \
    F_HELPER_0_1(name ## q)

F_HELPER_DQ_0_1(add);
F_HELPER_DQ_0_1(sub);
F_HELPER_DQ_0_1(mul);
F_HELPER_DQ_0_1(div);

DEF_HELPER_3(fadds, f32, env, f32, f32)
DEF_HELPER_3(fsubs, f32, env, f32, f32)
DEF_HELPER_3(fmuls, f32, env, f32, f32)
DEF_HELPER_3(fdivs, f32, env, f32, f32)

DEF_HELPER_3(fsmuld, void, env, f32, f32)
F_HELPER_0_1(dmulq);

DEF_HELPER_1(fnegs, f32, f32)
DEF_HELPER_2(fitod, void, env, s32)
DEF_HELPER_2(fitoq, void, env, s32)

DEF_HELPER_2(fitos, f32, env, s32)

#ifdef TARGET_SPARC64
DEF_HELPER_1(fnegd, void, env)
DEF_HELPER_1(fnegq, void, env)
DEF_HELPER_1(fxtos, i32, env)
F_HELPER_DQ_0_1(xto);
#endif
DEF_HELPER_1(fdtos, f32, env)
DEF_HELPER_2(fstod, void, env, f32)
DEF_HELPER_1(fqtos, f32, env)
DEF_HELPER_2(fstoq, void, env, f32)
F_HELPER_0_1(qtod);
F_HELPER_0_1(dtoq);
DEF_HELPER_2(fstoi, s32, env, f32)
DEF_HELPER_1(fdtoi, s32, env)
DEF_HELPER_1(fqtoi, s32, env)
#ifdef TARGET_SPARC64
DEF_HELPER_2(fstox, void, env, i32)
F_HELPER_0_1(dtox);
F_HELPER_0_1(qtox);
F_HELPER_0_1(aligndata);

F_HELPER_0_1(pmerge);
F_HELPER_0_1(mul8x16);
F_HELPER_0_1(mul8x16al);
F_HELPER_0_1(mul8x16au);
F_HELPER_0_1(mul8sux16);
F_HELPER_0_1(mul8ulx16);
F_HELPER_0_1(muld8sux16);
F_HELPER_0_1(muld8ulx16);
F_HELPER_0_1(expand);
#define VIS_HELPER(name)                                 \
    F_HELPER_0_1(name##16);                              \
    DEF_HELPER_3(f ## name ## 16s, i32, env, i32, i32)   \
    F_HELPER_0_1(name##32);                              \
    DEF_HELPER_3(f ## name ## 32s, i32, env, i32, i32)

VIS_HELPER(padd);
VIS_HELPER(psub);
#define VIS_CMPHELPER(name)                              \
    DEF_HELPER_1(f##name##16, i64, env);                 \
    DEF_HELPER_1(f##name##32, i64, env)
VIS_CMPHELPER(cmpgt);
VIS_CMPHELPER(cmpeq);
VIS_CMPHELPER(cmple);
VIS_CMPHELPER(cmpne);
#endif
#undef F_HELPER_0_1
#undef F_HELPER_DQ_0_1
#undef VIS_HELPER
#undef VIS_CMPHELPER
DEF_HELPER_1(compute_psr, void, env);
DEF_HELPER_1(compute_C_icc, i32, env);

#include "def-helper.h"
