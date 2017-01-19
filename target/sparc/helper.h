#ifndef TARGET_SPARC64
DEF_HELPER_1(rett, void, env)
DEF_HELPER_2(wrpsr, void, env, tl)
DEF_HELPER_1(rdpsr, tl, env)
DEF_HELPER_1(power_down, void, env)
#else
DEF_HELPER_FLAGS_2(wrpil, TCG_CALL_NO_RWG, void, env, tl)
DEF_HELPER_2(wrgl, void, env, tl)
DEF_HELPER_2(wrpstate, void, env, tl)
DEF_HELPER_1(done, void, env)
DEF_HELPER_1(retry, void, env)
DEF_HELPER_FLAGS_1(flushw, TCG_CALL_NO_WG, void, env)
DEF_HELPER_FLAGS_1(saved, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_1(restored, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_1(rdccr, tl, env)
DEF_HELPER_2(wrccr, void, env, tl)
DEF_HELPER_1(rdcwp, tl, env)
DEF_HELPER_2(wrcwp, void, env, tl)
DEF_HELPER_FLAGS_2(array8, TCG_CALL_NO_RWG_SE, tl, tl, tl)
DEF_HELPER_FLAGS_2(set_softint, TCG_CALL_NO_RWG, void, env, i64)
DEF_HELPER_FLAGS_2(clear_softint, TCG_CALL_NO_RWG, void, env, i64)
DEF_HELPER_FLAGS_2(write_softint, TCG_CALL_NO_RWG, void, env, i64)
DEF_HELPER_FLAGS_2(tick_set_count, TCG_CALL_NO_RWG, void, ptr, i64)
DEF_HELPER_FLAGS_3(tick_get_count, TCG_CALL_NO_WG, i64, env, ptr, int)
DEF_HELPER_FLAGS_2(tick_set_limit, TCG_CALL_NO_RWG, void, ptr, i64)
#endif
DEF_HELPER_FLAGS_3(check_align, TCG_CALL_NO_WG, void, env, tl, i32)
DEF_HELPER_1(debug, void, env)
DEF_HELPER_1(save, void, env)
DEF_HELPER_1(restore, void, env)
DEF_HELPER_3(udiv, tl, env, tl, tl)
DEF_HELPER_3(udiv_cc, tl, env, tl, tl)
DEF_HELPER_3(sdiv, tl, env, tl, tl)
DEF_HELPER_3(sdiv_cc, tl, env, tl, tl)
DEF_HELPER_3(taddcctv, tl, env, tl, tl)
DEF_HELPER_3(tsubcctv, tl, env, tl, tl)
#ifdef TARGET_SPARC64
DEF_HELPER_FLAGS_3(sdivx, TCG_CALL_NO_WG, s64, env, s64, s64)
DEF_HELPER_FLAGS_3(udivx, TCG_CALL_NO_WG, i64, env, i64, i64)
#endif
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER_FLAGS_4(ld_asi, TCG_CALL_NO_WG, i64, env, tl, int, i32)
DEF_HELPER_FLAGS_5(st_asi, TCG_CALL_NO_WG, void, env, tl, i64, int, i32)
#endif
DEF_HELPER_FLAGS_1(check_ieee_exceptions, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_3(ldfsr, TCG_CALL_NO_RWG, tl, env, tl, i32)
DEF_HELPER_FLAGS_1(fabss, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_FLAGS_2(fsqrts, TCG_CALL_NO_RWG, f32, env, f32)
DEF_HELPER_FLAGS_2(fsqrtd, TCG_CALL_NO_RWG, f64, env, f64)
DEF_HELPER_FLAGS_3(fcmps, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmpd, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmpes, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmped, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_1(fsqrtq, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_1(fcmpq, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpeq, TCG_CALL_NO_WG, tl, env)
#ifdef TARGET_SPARC64
DEF_HELPER_FLAGS_3(ldxfsr, TCG_CALL_NO_RWG, tl, env, tl, i64)
DEF_HELPER_FLAGS_1(fabsd, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_FLAGS_3(fcmps_fcc1, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmps_fcc2, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmps_fcc3, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmpd_fcc1, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmpd_fcc2, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmpd_fcc3, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmpes_fcc1, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmpes_fcc2, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmpes_fcc3, TCG_CALL_NO_WG, tl, env, f32, f32)
DEF_HELPER_FLAGS_3(fcmped_fcc1, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmped_fcc2, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_3(fcmped_fcc3, TCG_CALL_NO_WG, tl, env, f64, f64)
DEF_HELPER_FLAGS_1(fabsq, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_1(fcmpq_fcc1, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpq_fcc2, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpq_fcc3, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpeq_fcc1, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpeq_fcc2, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_FLAGS_1(fcmpeq_fcc3, TCG_CALL_NO_WG, tl, env)
#endif
DEF_HELPER_2(raise_exception, noreturn, env, int)
#define F_HELPER_0_1(name) \
  DEF_HELPER_FLAGS_1(f ## name, TCG_CALL_NO_RWG, void, env)

DEF_HELPER_FLAGS_3(faddd, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fsubd, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fmuld, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fdivd, TCG_CALL_NO_RWG, f64, env, f64, f64)
F_HELPER_0_1(addq)
F_HELPER_0_1(subq)
F_HELPER_0_1(mulq)
F_HELPER_0_1(divq)

DEF_HELPER_FLAGS_3(fadds, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fsubs, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fmuls, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fdivs, TCG_CALL_NO_RWG, f32, env, f32, f32)

DEF_HELPER_FLAGS_3(fsmuld, TCG_CALL_NO_RWG, f64, env, f32, f32)
DEF_HELPER_FLAGS_3(fdmulq, TCG_CALL_NO_RWG, void, env, f64, f64)

DEF_HELPER_FLAGS_1(fnegs, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_FLAGS_2(fitod, TCG_CALL_NO_RWG_SE, f64, env, s32)
DEF_HELPER_FLAGS_2(fitoq, TCG_CALL_NO_RWG, void, env, s32)

DEF_HELPER_FLAGS_2(fitos, TCG_CALL_NO_RWG, f32, env, s32)

#ifdef TARGET_SPARC64
DEF_HELPER_FLAGS_1(fnegd, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_FLAGS_1(fnegq, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_2(fxtos, TCG_CALL_NO_RWG, f32, env, s64)
DEF_HELPER_FLAGS_2(fxtod, TCG_CALL_NO_RWG, f64, env, s64)
DEF_HELPER_FLAGS_2(fxtoq, TCG_CALL_NO_RWG, void, env, s64)
#endif
DEF_HELPER_FLAGS_2(fdtos, TCG_CALL_NO_RWG, f32, env, f64)
DEF_HELPER_FLAGS_2(fstod, TCG_CALL_NO_RWG, f64, env, f32)
DEF_HELPER_FLAGS_1(fqtos, TCG_CALL_NO_RWG, f32, env)
DEF_HELPER_FLAGS_2(fstoq, TCG_CALL_NO_RWG, void, env, f32)
DEF_HELPER_FLAGS_1(fqtod, TCG_CALL_NO_RWG, f64, env)
DEF_HELPER_FLAGS_2(fdtoq, TCG_CALL_NO_RWG, void, env, f64)
DEF_HELPER_FLAGS_2(fstoi, TCG_CALL_NO_RWG, s32, env, f32)
DEF_HELPER_FLAGS_2(fdtoi, TCG_CALL_NO_RWG, s32, env, f64)
DEF_HELPER_FLAGS_1(fqtoi, TCG_CALL_NO_RWG, s32, env)
#ifdef TARGET_SPARC64
DEF_HELPER_FLAGS_2(fstox, TCG_CALL_NO_RWG, s64, env, f32)
DEF_HELPER_FLAGS_2(fdtox, TCG_CALL_NO_RWG, s64, env, f64)
DEF_HELPER_FLAGS_1(fqtox, TCG_CALL_NO_RWG, s64, env)

DEF_HELPER_FLAGS_2(fpmerge, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmul8x16, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmul8x16al, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmul8x16au, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmul8sux16, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmul8ulx16, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmuld8sux16, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fmuld8ulx16, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(fexpand, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_3(pdist, TCG_CALL_NO_RWG_SE, i64, i64, i64, i64)
DEF_HELPER_FLAGS_2(fpack16, TCG_CALL_NO_RWG_SE, i32, i64, i64)
DEF_HELPER_FLAGS_3(fpack32, TCG_CALL_NO_RWG_SE, i64, i64, i64, i64)
DEF_HELPER_FLAGS_2(fpackfix, TCG_CALL_NO_RWG_SE, i32, i64, i64)
DEF_HELPER_FLAGS_3(bshuffle, TCG_CALL_NO_RWG_SE, i64, i64, i64, i64)
#define VIS_HELPER(name)                                                 \
    DEF_HELPER_FLAGS_2(f ## name ## 16, TCG_CALL_NO_RWG_SE,  \
                       i64, i64, i64)                                    \
    DEF_HELPER_FLAGS_2(f ## name ## 16s, TCG_CALL_NO_RWG_SE, \
                       i32, i32, i32)                                    \
    DEF_HELPER_FLAGS_2(f ## name ## 32, TCG_CALL_NO_RWG_SE,  \
                       i64, i64, i64)                                    \
    DEF_HELPER_FLAGS_2(f ## name ## 32s, TCG_CALL_NO_RWG_SE, \
                       i32, i32, i32)

VIS_HELPER(padd)
VIS_HELPER(psub)
#define VIS_CMPHELPER(name)                                              \
    DEF_HELPER_FLAGS_2(f##name##16, TCG_CALL_NO_RWG_SE,      \
                       i64, i64, i64)                                    \
    DEF_HELPER_FLAGS_2(f##name##32, TCG_CALL_NO_RWG_SE,      \
                       i64, i64, i64)
VIS_CMPHELPER(cmpgt)
VIS_CMPHELPER(cmpeq)
VIS_CMPHELPER(cmple)
VIS_CMPHELPER(cmpne)
#endif
#undef F_HELPER_0_1
#undef VIS_HELPER
#undef VIS_CMPHELPER
DEF_HELPER_1(compute_psr, void, env)
DEF_HELPER_FLAGS_1(compute_C_icc, TCG_CALL_NO_WG_SE, i32, env)
