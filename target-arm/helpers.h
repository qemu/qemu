#define DEF_HELPER(name, ret, args) ret glue(helper_,name) args;

#ifdef GEN_HELPER
#define DEF_HELPER_0_0(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(void) \
{ \
    tcg_gen_helper_0_0(helper_##name); \
}
#define DEF_HELPER_0_1(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv arg1) \
{ \
    tcg_gen_helper_0_1(helper_##name, arg1); \
}
#define DEF_HELPER_0_2(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv arg1, TCGv arg2) \
{ \
    tcg_gen_helper_0_2(helper_##name, arg1, arg2); \
}
#define DEF_HELPER_0_3(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name( \
    TCGv arg1, TCGv arg2, TCGv arg3) \
{ \
    tcg_gen_helper_0_3(helper_##name, arg1, arg2, arg3); \
}
#define DEF_HELPER_1_0(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv ret) \
{ \
    tcg_gen_helper_1_0(helper_##name, ret); \
}
#define DEF_HELPER_1_1(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv ret, TCGv arg1) \
{ \
    tcg_gen_helper_1_1(helper_##name, ret, arg1); \
}
#define DEF_HELPER_1_2(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv ret, TCGv arg1, TCGv arg2) \
{ \
    tcg_gen_helper_1_2(helper_##name, ret, arg1, arg2); \
}
#define DEF_HELPER_1_3(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv ret, \
    TCGv arg1, TCGv arg2, TCGv arg3) \
{ \
    tcg_gen_helper_1_3(helper_##name, ret, arg1, arg2, arg3); \
}
#define DEF_HELPER_1_4(name, ret, args) \
DEF_HELPER(name, ret, args) \
static inline void gen_helper_##name(TCGv ret, \
    TCGv arg1, TCGv arg2, TCGv arg3, TCGv arg4) \
{ \
    tcg_gen_helper_1_4(helper_##name, ret, arg1, arg2, arg3, arg4); \
}
#else /* !GEN_HELPER */
#define DEF_HELPER_0_0 DEF_HELPER
#define DEF_HELPER_0_1 DEF_HELPER
#define DEF_HELPER_0_2 DEF_HELPER
#define DEF_HELPER_0_3 DEF_HELPER
#define DEF_HELPER_1_0 DEF_HELPER
#define DEF_HELPER_1_1 DEF_HELPER
#define DEF_HELPER_1_2 DEF_HELPER
#define DEF_HELPER_1_3 DEF_HELPER
#define DEF_HELPER_1_4 DEF_HELPER
#define HELPER(x) glue(helper_,x)
#endif

DEF_HELPER_1_1(clz, uint32_t, (uint32_t))
DEF_HELPER_1_1(sxtb16, uint32_t, (uint32_t))
DEF_HELPER_1_1(uxtb16, uint32_t, (uint32_t))

DEF_HELPER_1_2(add_setq, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(add_saturate, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sub_saturate, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(add_usaturate, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sub_usaturate, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_1(double_saturate, uint32_t, (int32_t))
DEF_HELPER_1_2(sdiv, int32_t, (int32_t, int32_t))
DEF_HELPER_1_2(udiv, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_1(rbit, uint32_t, (uint32_t))
DEF_HELPER_1_1(abs, uint32_t, (uint32_t))

#define PAS_OP(pfx)  \
    DEF_HELPER_1_3(pfx ## add8, uint32_t, (uint32_t, uint32_t, uint32_t *)) \
    DEF_HELPER_1_3(pfx ## sub8, uint32_t, (uint32_t, uint32_t, uint32_t *)) \
    DEF_HELPER_1_3(pfx ## sub16, uint32_t, (uint32_t, uint32_t, uint32_t *)) \
    DEF_HELPER_1_3(pfx ## add16, uint32_t, (uint32_t, uint32_t, uint32_t *)) \
    DEF_HELPER_1_3(pfx ## addsubx, uint32_t, (uint32_t, uint32_t, uint32_t *)) \
    DEF_HELPER_1_3(pfx ## subaddx, uint32_t, (uint32_t, uint32_t, uint32_t *))

PAS_OP(s)
PAS_OP(u)
#undef PAS_OP

#define PAS_OP(pfx)  \
    DEF_HELPER_1_2(pfx ## add8, uint32_t, (uint32_t, uint32_t)) \
    DEF_HELPER_1_2(pfx ## sub8, uint32_t, (uint32_t, uint32_t)) \
    DEF_HELPER_1_2(pfx ## sub16, uint32_t, (uint32_t, uint32_t)) \
    DEF_HELPER_1_2(pfx ## add16, uint32_t, (uint32_t, uint32_t)) \
    DEF_HELPER_1_2(pfx ## addsubx, uint32_t, (uint32_t, uint32_t)) \
    DEF_HELPER_1_2(pfx ## subaddx, uint32_t, (uint32_t, uint32_t))
PAS_OP(q)
PAS_OP(sh)
PAS_OP(uq)
PAS_OP(uh)
#undef PAS_OP

DEF_HELPER_1_2(ssat, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(usat, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(ssat16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(usat16, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(usad8, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_1(logicq_cc, uint32_t, (uint64_t))

DEF_HELPER_1_3(sel_flags, uint32_t, (uint32_t, uint32_t, uint32_t))
DEF_HELPER_0_1(exception, void, (uint32_t))
DEF_HELPER_0_0(wfi, void, (void))

DEF_HELPER_0_2(cpsr_write, void, (uint32_t, uint32_t))
DEF_HELPER_1_0(cpsr_read, uint32_t, (void))

DEF_HELPER_0_3(v7m_msr, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_2(v7m_mrs, uint32_t, (CPUState *, uint32_t))

DEF_HELPER_0_3(set_cp15, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_2(get_cp15, uint32_t, (CPUState *, uint32_t))

DEF_HELPER_0_3(set_cp, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_2(get_cp, uint32_t, (CPUState *, uint32_t))

DEF_HELPER_1_2(get_r13_banked, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_0_3(set_r13_banked, void, (CPUState *, uint32_t, uint32_t))

DEF_HELPER_0_2(mark_exclusive, void, (CPUState *, uint32_t))
DEF_HELPER_1_2(test_exclusive, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_0_1(clrex, void, (CPUState *))

DEF_HELPER_1_1(get_user_reg, uint32_t, (uint32_t))
DEF_HELPER_0_2(set_user_reg, void, (uint32_t, uint32_t))

DEF_HELPER_1_1(vfp_get_fpscr, uint32_t, (CPUState *))
DEF_HELPER_0_2(vfp_set_fpscr, void, (CPUState *, uint32_t))

DEF_HELPER_1_3(vfp_adds, float32, (float32, float32, CPUState *))
DEF_HELPER_1_3(vfp_addd, float64, (float64, float64, CPUState *))
DEF_HELPER_1_3(vfp_subs, float32, (float32, float32, CPUState *))
DEF_HELPER_1_3(vfp_subd, float64, (float64, float64, CPUState *))
DEF_HELPER_1_3(vfp_muls, float32, (float32, float32, CPUState *))
DEF_HELPER_1_3(vfp_muld, float64, (float64, float64, CPUState *))
DEF_HELPER_1_3(vfp_divs, float32, (float32, float32, CPUState *))
DEF_HELPER_1_3(vfp_divd, float64, (float64, float64, CPUState *))
DEF_HELPER_1_1(vfp_negs, float32, (float32))
DEF_HELPER_1_1(vfp_negd, float64, (float64))
DEF_HELPER_1_1(vfp_abss, float32, (float32))
DEF_HELPER_1_1(vfp_absd, float64, (float64))
DEF_HELPER_1_2(vfp_sqrts, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_sqrtd, float64, (float64, CPUState *))
DEF_HELPER_0_3(vfp_cmps, void, (float32, float32, CPUState *))
DEF_HELPER_0_3(vfp_cmpd, void, (float64, float64, CPUState *))
DEF_HELPER_0_3(vfp_cmpes, void, (float32, float32, CPUState *))
DEF_HELPER_0_3(vfp_cmped, void, (float64, float64, CPUState *))

DEF_HELPER_1_2(vfp_fcvtds, float64, (float32, CPUState *))
DEF_HELPER_1_2(vfp_fcvtsd, float32, (float64, CPUState *))

DEF_HELPER_1_2(vfp_uitos, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_uitod, float64, (float32, CPUState *))
DEF_HELPER_1_2(vfp_sitos, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_sitod, float64, (float32, CPUState *))

DEF_HELPER_1_2(vfp_touis, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_touid, float32, (float64, CPUState *))
DEF_HELPER_1_2(vfp_touizs, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_touizd, float32, (float64, CPUState *))
DEF_HELPER_1_2(vfp_tosis, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_tosid, float32, (float64, CPUState *))
DEF_HELPER_1_2(vfp_tosizs, float32, (float32, CPUState *))
DEF_HELPER_1_2(vfp_tosizd, float32, (float64, CPUState *))

DEF_HELPER_1_3(vfp_toshs, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_tosls, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_touhs, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_touls, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_toshd, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_tosld, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_touhd, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_tould, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_shtos, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_sltos, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_uhtos, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_ultos, float32, (float32, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_shtod, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_sltod, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_uhtod, float64, (float64, uint32_t, CPUState *))
DEF_HELPER_1_3(vfp_ultod, float64, (float64, uint32_t, CPUState *))

DEF_HELPER_1_3(recps_f32, float32, (float32, float32, CPUState *))
DEF_HELPER_1_3(rsqrts_f32, float32, (float32, float32, CPUState *))
DEF_HELPER_1_2(recpe_f32, float32, (float32, CPUState *))
DEF_HELPER_1_2(rsqrte_f32, float32, (float32, CPUState *))
DEF_HELPER_1_2(recpe_u32, uint32_t, (uint32_t, CPUState *))
DEF_HELPER_1_2(rsqrte_u32, uint32_t, (uint32_t, CPUState *))
DEF_HELPER_1_4(neon_tbl, uint32_t, (uint32_t, uint32_t, uint32_t, uint32_t))
DEF_HELPER_1_2(neon_add_saturate_u64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_add_saturate_s64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_sub_saturate_u64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_sub_saturate_s64, uint64_t, (uint64_t, uint64_t))

DEF_HELPER_1_2(add_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(adc_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sub_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sbc_cc, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(shl, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(shr, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sar, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(ror, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(shl_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(shr_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(sar_cc, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(ror_cc, uint32_t, (uint32_t, uint32_t))

/* neon_helper.c */
DEF_HELPER_1_3(neon_qadd_u8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qadd_s8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qadd_u16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qadd_s16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qsub_u8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qsub_s8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qsub_u16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qsub_s16, uint32_t, (CPUState *, uint32_t, uint32_t))

DEF_HELPER_1_2(neon_hadd_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hadd_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hadd_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hadd_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hadd_s32, int32_t, (int32_t, int32_t))
DEF_HELPER_1_2(neon_hadd_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rhadd_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rhadd_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rhadd_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rhadd_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rhadd_s32, int32_t, (int32_t, int32_t))
DEF_HELPER_1_2(neon_rhadd_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hsub_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hsub_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hsub_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hsub_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_hsub_s32, int32_t, (int32_t, int32_t))
DEF_HELPER_1_2(neon_hsub_u32, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(neon_cgt_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_s32, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(neon_min_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_min_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_min_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_min_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_min_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_min_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmin_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_pmax_s32, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(neon_abd_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_s32, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(neon_shl_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_shl_u64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_shl_s64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_rshl_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_s8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_s16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_s32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_rshl_u64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_rshl_s64, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_3(neon_qshl_u8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_s8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_u16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_s16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_u32, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_s32, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qshl_u64, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(neon_qshl_s64, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(neon_qrshl_u8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_s8, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_u16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_s16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_u32, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_s32, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrshl_u64, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(neon_qrshl_s64, uint64_t, (CPUState *, uint64_t, uint64_t))

DEF_HELPER_1_2(neon_add_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_add_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_padd_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_padd_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_sub_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_sub_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mul_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mul_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mul_p8, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_2(neon_tst_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_tst_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_tst_u32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_ceq_u8, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_ceq_u16, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_ceq_u32, uint32_t, (uint32_t, uint32_t))

DEF_HELPER_1_1(neon_abs_s8, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_abs_s16, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_clz_u8, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_clz_u16, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_cls_s8, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_cls_s16, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_cls_s32, uint32_t, (uint32_t))
DEF_HELPER_1_1(neon_cnt_u8, uint32_t, (uint32_t))

DEF_HELPER_1_3(neon_qdmulh_s16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrdmulh_s16, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qdmulh_s32, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(neon_qrdmulh_s32, uint32_t, (CPUState *, uint32_t, uint32_t))

DEF_HELPER_1_1(neon_narrow_u8, uint32_t, (uint64_t))
DEF_HELPER_1_1(neon_narrow_u16, uint32_t, (uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_u8, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_s8, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_u16, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_s16, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_u32, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(neon_narrow_sat_s32, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_1(neon_narrow_high_u8, uint32_t, (uint64_t))
DEF_HELPER_1_1(neon_narrow_high_u16, uint32_t, (uint64_t))
DEF_HELPER_1_1(neon_narrow_round_high_u8, uint32_t, (uint64_t))
DEF_HELPER_1_1(neon_narrow_round_high_u16, uint32_t, (uint64_t))
DEF_HELPER_1_1(neon_widen_u8, uint64_t, (uint32_t))
DEF_HELPER_1_1(neon_widen_s8, uint64_t, (uint32_t))
DEF_HELPER_1_1(neon_widen_u16, uint64_t, (uint32_t))
DEF_HELPER_1_1(neon_widen_s16, uint64_t, (uint32_t))

DEF_HELPER_1_2(neon_addl_u16, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_addl_u32, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_paddl_u16, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_paddl_u32, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_subl_u16, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(neon_subl_u32, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_3(neon_addl_saturate_s32, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(neon_addl_saturate_s64, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_2(neon_abdl_u16, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abdl_s16, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abdl_u32, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abdl_s32, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abdl_u64, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abdl_s64, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mull_u8, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mull_s8, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mull_u16, uint64_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mull_s16, uint64_t, (uint32_t, uint32_t))

DEF_HELPER_1_1(neon_negl_u16, uint64_t, (uint64_t))
DEF_HELPER_1_1(neon_negl_u32, uint64_t, (uint64_t))
DEF_HELPER_1_1(neon_negl_u64, uint64_t, (uint64_t))

DEF_HELPER_1_2(neon_qabs_s8, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(neon_qabs_s16, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(neon_qabs_s32, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(neon_qneg_s8, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(neon_qneg_s16, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(neon_qneg_s32, uint32_t, (CPUState *, uint32_t))

DEF_HELPER_0_0(neon_trn_u8, void, (void))
DEF_HELPER_0_0(neon_trn_u16, void, (void))
DEF_HELPER_0_0(neon_unzip_u8, void, (void))
DEF_HELPER_0_0(neon_zip_u8, void, (void))
DEF_HELPER_0_0(neon_zip_u16, void, (void))

DEF_HELPER_1_2(neon_min_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_max_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_abd_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_add_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_sub_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_mul_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_ceq_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cge_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_cgt_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_acge_f32, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_1_2(neon_acgt_f32, uint32_t, (uint32_t, uint32_t))

/* iwmmxt_helper.c */
DEF_HELPER_1_2(iwmmxt_maddsq, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_madduq, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_sadb, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_sadw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_mulslw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_mulshw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_mululw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_muluhw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_macsw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_2(iwmmxt_macuw, uint64_t, (uint64_t, uint64_t))
DEF_HELPER_1_1(iwmmxt_setpsr_nz, uint32_t, (uint64_t))

#define DEF_IWMMXT_HELPER_SIZE_ENV(name) \
DEF_HELPER_1_3(iwmmxt_##name##b, uint64_t, (CPUState *, uint64_t, uint64_t)) \
DEF_HELPER_1_3(iwmmxt_##name##w, uint64_t, (CPUState *, uint64_t, uint64_t)) \
DEF_HELPER_1_3(iwmmxt_##name##l, uint64_t, (CPUState *, uint64_t, uint64_t)) \

DEF_IWMMXT_HELPER_SIZE_ENV(unpackl)
DEF_IWMMXT_HELPER_SIZE_ENV(unpackh)

DEF_HELPER_1_2(iwmmxt_unpacklub, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackluw, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpacklul, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhub, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhuw, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhul, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpacklsb, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpacklsw, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpacklsl, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhsb, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhsw, uint64_t, (CPUState *, uint64_t))
DEF_HELPER_1_2(iwmmxt_unpackhsl, uint64_t, (CPUState *, uint64_t))

DEF_IWMMXT_HELPER_SIZE_ENV(cmpeq)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgtu)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgts)

DEF_IWMMXT_HELPER_SIZE_ENV(mins)
DEF_IWMMXT_HELPER_SIZE_ENV(minu)
DEF_IWMMXT_HELPER_SIZE_ENV(maxs)
DEF_IWMMXT_HELPER_SIZE_ENV(maxu)

DEF_IWMMXT_HELPER_SIZE_ENV(subn)
DEF_IWMMXT_HELPER_SIZE_ENV(addn)
DEF_IWMMXT_HELPER_SIZE_ENV(subu)
DEF_IWMMXT_HELPER_SIZE_ENV(addu)
DEF_IWMMXT_HELPER_SIZE_ENV(subs)
DEF_IWMMXT_HELPER_SIZE_ENV(adds)

DEF_HELPER_1_3(iwmmxt_avgb0, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_avgb1, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_avgw0, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_avgw1, uint64_t, (CPUState *, uint64_t, uint64_t))

DEF_HELPER_1_2(iwmmxt_msadb, uint64_t, (uint64_t, uint64_t))

DEF_HELPER_1_3(iwmmxt_align, uint64_t, (uint64_t, uint64_t, uint32_t))
DEF_HELPER_1_4(iwmmxt_insr, uint64_t, (uint64_t, uint32_t, uint32_t, uint32_t))

DEF_HELPER_1_1(iwmmxt_bcstb, uint64_t, (uint32_t))
DEF_HELPER_1_1(iwmmxt_bcstw, uint64_t, (uint32_t))
DEF_HELPER_1_1(iwmmxt_bcstl, uint64_t, (uint32_t))

DEF_HELPER_1_1(iwmmxt_addcb, uint64_t, (uint64_t))
DEF_HELPER_1_1(iwmmxt_addcw, uint64_t, (uint64_t))
DEF_HELPER_1_1(iwmmxt_addcl, uint64_t, (uint64_t))

DEF_HELPER_1_1(iwmmxt_msbb, uint32_t, (uint64_t))
DEF_HELPER_1_1(iwmmxt_msbw, uint32_t, (uint64_t))
DEF_HELPER_1_1(iwmmxt_msbl, uint32_t, (uint64_t))

DEF_HELPER_1_3(iwmmxt_srlw, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_srll, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_srlq, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_sllw, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_slll, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_sllq, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_sraw, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_sral, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_sraq, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_rorw, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_rorl, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_rorq, uint64_t, (CPUState *, uint64_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_shufh, uint64_t, (CPUState *, uint64_t, uint32_t))

DEF_HELPER_1_3(iwmmxt_packuw, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_packul, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_packuq, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_packsw, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_packsl, uint64_t, (CPUState *, uint64_t, uint64_t))
DEF_HELPER_1_3(iwmmxt_packsq, uint64_t, (CPUState *, uint64_t, uint64_t))

DEF_HELPER_1_3(iwmmxt_muladdsl, uint64_t, (uint64_t, uint32_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_muladdsw, uint64_t, (uint64_t, uint32_t, uint32_t))
DEF_HELPER_1_3(iwmmxt_muladdswl, uint64_t, (uint64_t, uint32_t, uint32_t))

#undef DEF_HELPER
#undef DEF_HELPER_0_0
#undef DEF_HELPER_0_1
#undef DEF_HELPER_0_2
#undef DEF_HELPER_1_0
#undef DEF_HELPER_1_1
#undef DEF_HELPER_1_2
#undef DEF_HELPER_1_3
#undef GEN_HELPER
