#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

#ifndef TARGET_SPARC64
DEF_HELPER(void, helper_rett, (void))
DEF_HELPER(void, helper_wrpsr, (target_ulong new_psr))
DEF_HELPER(target_ulong, helper_rdpsr, (void))
#else
DEF_HELPER(void, helper_wrpstate, (target_ulong new_state))
DEF_HELPER(void, helper_done, (void))
DEF_HELPER(void, helper_retry, (void))
DEF_HELPER(void, helper_flushw, (void))
DEF_HELPER(void, helper_saved, (void))
DEF_HELPER(void, helper_restored, (void))
DEF_HELPER(target_ulong, helper_rdccr, (void))
DEF_HELPER(void, helper_wrccr, (target_ulong new_ccr))
DEF_HELPER(target_ulong, helper_rdcwp, (void))
DEF_HELPER(void, helper_wrcwp, (target_ulong new_cwp))
DEF_HELPER(target_ulong, helper_array8, (target_ulong pixel_addr, \
                                         target_ulong cubesize))
DEF_HELPER(target_ulong, helper_alignaddr, (target_ulong addr, \
                                            target_ulong offset))
DEF_HELPER(target_ulong, helper_popc, (target_ulong val))
DEF_HELPER(void, helper_ldda_asi, (target_ulong addr, int asi, int rd))
DEF_HELPER(void, helper_ldf_asi, (target_ulong addr, int asi, int size, int rd))
DEF_HELPER(void, helper_stf_asi, (target_ulong addr, int asi, int size, int rd))
DEF_HELPER(target_ulong, helper_cas_asi, (target_ulong addr, \
                                          target_ulong val1, \
                                          target_ulong val2, uint32_t asi))
DEF_HELPER(target_ulong, helper_casx_asi, (target_ulong addr, \
                                           target_ulong val1, \
                                           target_ulong val2, uint32_t asi))
DEF_HELPER(void, helper_tick_set_count, (void *opaque, uint64_t count))
DEF_HELPER(uint64_t, helper_tick_get_count, (void *opaque))
DEF_HELPER(void, helper_tick_set_limit, (void *opaque, uint64_t limit))
#endif
DEF_HELPER(void, helper_trap, (target_ulong nb_trap))
DEF_HELPER(void, helper_trapcc, (target_ulong nb_trap, target_ulong do_trap))
DEF_HELPER(void, helper_check_align, (target_ulong addr, uint32_t align))
DEF_HELPER(void, helper_debug, (void))
DEF_HELPER(void, helper_save, (void))
DEF_HELPER(void, helper_restore, (void))
DEF_HELPER(void, helper_flush, (target_ulong addr))
DEF_HELPER(target_ulong, helper_udiv, (target_ulong a, target_ulong b))
DEF_HELPER(target_ulong, helper_sdiv, (target_ulong a, target_ulong b))
DEF_HELPER(void, helper_stdf, (target_ulong addr, int mem_idx))
DEF_HELPER(void, helper_lddf, (target_ulong addr, int mem_idx))
DEF_HELPER(void, helper_ldqf, (target_ulong addr, int mem_idx))
DEF_HELPER(void, helper_stqf, (target_ulong addr, int mem_idx))
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
DEF_HELPER(uint64_t, helper_ld_asi, (target_ulong addr, int asi, int size, \
                                     int sign))
DEF_HELPER(void, helper_st_asi, (target_ulong addr, uint64_t val, int asi, \
                                 int size))
#endif
DEF_HELPER(void, helper_ldfsr, (uint32_t new_fsr))
DEF_HELPER(void, helper_check_ieee_exceptions, (void))
DEF_HELPER(void, helper_clear_float_exceptions, (void))
DEF_HELPER(float32, helper_fabss, (float32 src))
DEF_HELPER(float32, helper_fsqrts, (float32 src))
DEF_HELPER(void, helper_fsqrtd, (void))
DEF_HELPER(void, helper_fcmps, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmpd, (void))
DEF_HELPER(void, helper_fcmpes, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmped, (void))
DEF_HELPER(void, helper_fsqrtq, (void))
DEF_HELPER(void, helper_fcmpq, (void))
DEF_HELPER(void, helper_fcmpeq, (void))
#ifdef TARGET_SPARC64
DEF_HELPER(void, helper_ldxfsr, (uint64_t new_fsr))
DEF_HELPER(void, helper_fabsd, (void))
DEF_HELPER(void, helper_fcmps_fcc1, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmps_fcc2, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmps_fcc3, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmpd_fcc1, (void))
DEF_HELPER(void, helper_fcmpd_fcc2, (void))
DEF_HELPER(void, helper_fcmpd_fcc3, (void))
DEF_HELPER(void, helper_fcmpes_fcc1, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmpes_fcc2, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmpes_fcc3, (float32 src1, float32 src2))
DEF_HELPER(void, helper_fcmped_fcc1, (void))
DEF_HELPER(void, helper_fcmped_fcc2, (void))
DEF_HELPER(void, helper_fcmped_fcc3, (void))
DEF_HELPER(void, helper_fabsq, (void))
DEF_HELPER(void, helper_fcmpq_fcc1, (void))
DEF_HELPER(void, helper_fcmpq_fcc2, (void))
DEF_HELPER(void, helper_fcmpq_fcc3, (void))
DEF_HELPER(void, helper_fcmpeq_fcc1, (void))
DEF_HELPER(void, helper_fcmpeq_fcc2, (void))
DEF_HELPER(void, helper_fcmpeq_fcc3, (void))
#endif
DEF_HELPER(void, raise_exception, (int tt))
#define F_HELPER_0_0(name) DEF_HELPER(void, helper_f ## name, (void))
#define F_HELPER_DQ_0_0(name)                   \
    F_HELPER_0_0(name ## d);                    \
    F_HELPER_0_0(name ## q)

F_HELPER_DQ_0_0(add);
F_HELPER_DQ_0_0(sub);
F_HELPER_DQ_0_0(mul);
F_HELPER_DQ_0_0(div);

DEF_HELPER(float32, helper_fadds, (float32 src1, float32 src2))
DEF_HELPER(float32, helper_fsubs, (float32 src1, float32 src2))
DEF_HELPER(float32, helper_fmuls, (float32 src1, float32 src2))
DEF_HELPER(float32, helper_fdivs, (float32 src1, float32 src2))

DEF_HELPER(void, helper_fsmuld, (float32 src1, float32 src2))
F_HELPER_0_0(dmulq);

DEF_HELPER(float32, helper_fnegs, (float32 src))
DEF_HELPER(void, helper_fitod, (int32_t src))
DEF_HELPER(void, helper_fitoq, (int32_t src))

DEF_HELPER(float32, helper_fitos, (int32_t src))

#ifdef TARGET_SPARC64
DEF_HELPER(void, helper_fnegd, (void))
DEF_HELPER(void, helper_fnegq, (void))
DEF_HELPER(uint32_t, helper_fxtos, (void))
F_HELPER_DQ_0_0(xto);
#endif
DEF_HELPER(float32, helper_fdtos, (void))
DEF_HELPER(void, helper_fstod, (float32 src))
DEF_HELPER(float32, helper_fqtos, (void))
DEF_HELPER(void, helper_fstoq, (float32 src))
F_HELPER_0_0(qtod);
F_HELPER_0_0(dtoq);
DEF_HELPER(int32_t, helper_fstoi, (float32 src))
DEF_HELPER(int32_t, helper_fdtoi, (void))
DEF_HELPER(int32_t, helper_fqtoi, (void))
#ifdef TARGET_SPARC64
DEF_HELPER(void, helper_fstox, (uint32_t src))
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
    DEF_HELPER(uint32_t, helper_f ## name ## 16s, (uint32_t src1, uint32_t src2))\
    F_HELPER_0_0(name##32);                              \
    DEF_HELPER(uint32_t, helper_f ## name ## 32s, (uint32_t src1, uint32_t src2))

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
