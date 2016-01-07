#ifndef TARGET_SPARC64
DEF_HELPER_1(rett, void, env)
DEF_HELPER_2(wrpsr, void, env, tl)
DEF_HELPER_1(rdpsr, tl, env)
DEF_HELPER_1(power_down, void, env)
#else
DEF_HELPER_2(wrpil, void, env, tl)
DEF_HELPER_2(wrpstate, void, env, tl)
DEF_HELPER_1(done, void, env)
DEF_HELPER_1(retry, void, env)
DEF_HELPER_1(flushw, void, env)
DEF_HELPER_1(saved, void, env)
DEF_HELPER_1(restored, void, env)
DEF_HELPER_1(rdccr, tl, env)
DEF_HELPER_2(wrccr, void, env, tl)
DEF_HELPER_1(rdcwp, tl, env)
DEF_HELPER_2(wrcwp, void, env, tl)
DEF_HELPER_FLAGS_2(array8, TCG_CALL_NO_RWG_SE, tl, tl, tl)
DEF_HELPER_1(popc, tl, tl)
DEF_HELPER_4(ldda_asi, void, env, tl, int, int)
DEF_HELPER_5(ldf_asi, void, env, tl, int, int, int)
DEF_HELPER_5(stf_asi, void, env, tl, int, int, int)
DEF_HELPER_5(casx_asi, tl, env, tl, tl, tl, i32)
DEF_HELPER_2(set_softint, void, env, i64)
DEF_HELPER_2(clear_softint, void, env, i64)
DEF_HELPER_2(write_softint, void, env, i64)
DEF_HELPER_2(tick_set_count, void, ptr, i64)
DEF_HELPER_3(tick_get_count, i64, env, ptr, int)
DEF_HELPER_2(tick_set_limit, void, ptr, i64)
#endif
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER_5(cas_asi, tl, env, tl, tl, tl, i32)
#endif
DEF_HELPER_3(check_align, void, env, tl, i32)
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
DEF_HELPER_3(sdivx, s64, env, s64, s64)
DEF_HELPER_3(udivx, i64, env, i64, i64)
#endif
DEF_HELPER_3(ldqf, void, env, tl, int)
DEF_HELPER_3(stqf, void, env, tl, int)
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER_5(ld_asi, i64, env, tl, int, int, int)
DEF_HELPER_5(st_asi, void, env, tl, i64, int, int)
#endif
DEF_HELPER_2(ldfsr, void, env, i32)
DEF_HELPER_FLAGS_1(fabss, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_2(fsqrts, f32, env, f32)
DEF_HELPER_2(fsqrtd, f64, env, f64)
DEF_HELPER_3(fcmps, void, env, f32, f32)
DEF_HELPER_3(fcmpd, void, env, f64, f64)
DEF_HELPER_3(fcmpes, void, env, f32, f32)
DEF_HELPER_3(fcmped, void, env, f64, f64)
DEF_HELPER_1(fsqrtq, void, env)
DEF_HELPER_1(fcmpq, void, env)
DEF_HELPER_1(fcmpeq, void, env)
#ifdef TARGET_SPARC64
DEF_HELPER_2(ldxfsr, void, env, i64)
DEF_HELPER_FLAGS_1(fabsd, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_3(fcmps_fcc1, void, env, f32, f32)
DEF_HELPER_3(fcmps_fcc2, void, env, f32, f32)
DEF_HELPER_3(fcmps_fcc3, void, env, f32, f32)
DEF_HELPER_3(fcmpd_fcc1, void, env, f64, f64)
DEF_HELPER_3(fcmpd_fcc2, void, env, f64, f64)
DEF_HELPER_3(fcmpd_fcc3, void, env, f64, f64)
DEF_HELPER_3(fcmpes_fcc1, void, env, f32, f32)
DEF_HELPER_3(fcmpes_fcc2, void, env, f32, f32)
DEF_HELPER_3(fcmpes_fcc3, void, env, f32, f32)
DEF_HELPER_3(fcmped_fcc1, void, env, f64, f64)
DEF_HELPER_3(fcmped_fcc2, void, env, f64, f64)
DEF_HELPER_3(fcmped_fcc3, void, env, f64, f64)
DEF_HELPER_1(fabsq, void, env)
DEF_HELPER_1(fcmpq_fcc1, void, env)
DEF_HELPER_1(fcmpq_fcc2, void, env)
DEF_HELPER_1(fcmpq_fcc3, void, env)
DEF_HELPER_1(fcmpeq_fcc1, void, env)
DEF_HELPER_1(fcmpeq_fcc2, void, env)
DEF_HELPER_1(fcmpeq_fcc3, void, env)
#endif
DEF_HELPER_2(raise_exception, noreturn, env, int)
#define F_HELPER_0_1(name) DEF_HELPER_1(f ## name, void, env)

DEF_HELPER_3(faddd, f64, env, f64, f64)
DEF_HELPER_3(fsubd, f64, env, f64, f64)
DEF_HELPER_3(fmuld, f64, env, f64, f64)
DEF_HELPER_3(fdivd, f64, env, f64, f64)
F_HELPER_0_1(addq)
F_HELPER_0_1(subq)
F_HELPER_0_1(mulq)
F_HELPER_0_1(divq)

DEF_HELPER_3(fadds, f32, env, f32, f32)
DEF_HELPER_3(fsubs, f32, env, f32, f32)
DEF_HELPER_3(fmuls, f32, env, f32, f32)
DEF_HELPER_3(fdivs, f32, env, f32, f32)

DEF_HELPER_3(fsmuld, f64, env, f32, f32)
DEF_HELPER_3(fdmulq, void, env, f64, f64)

DEF_HELPER_FLAGS_1(fnegs, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_2(fitod, f64, env, s32)
DEF_HELPER_2(fitoq, void, env, s32)

DEF_HELPER_2(fitos, f32, env, s32)

#ifdef TARGET_SPARC64
DEF_HELPER_FLAGS_1(fnegd, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_1(fnegq, void, env)
DEF_HELPER_2(fxtos, f32, env, s64)
DEF_HELPER_2(fxtod, f64, env, s64)
DEF_HELPER_2(fxtoq, void, env, s64)
#endif
DEF_HELPER_2(fdtos, f32, env, f64)
DEF_HELPER_2(fstod, f64, env, f32)
DEF_HELPER_1(fqtos, f32, env)
DEF_HELPER_2(fstoq, void, env, f32)
DEF_HELPER_1(fqtod, f64, env)
DEF_HELPER_2(fdtoq, void, env, f64)
DEF_HELPER_2(fstoi, s32, env, f32)
DEF_HELPER_2(fdtoi, s32, env, f64)
DEF_HELPER_1(fqtoi, s32, env)
#ifdef TARGET_SPARC64
DEF_HELPER_2(fstox, s64, env, f32)
DEF_HELPER_2(fdtox, s64, env, f64)
DEF_HELPER_1(fqtox, s64, env)

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
DEF_HELPER_1(compute_C_icc, i32, env)
