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
#else /* !GEN_HELPER */
#define DEF_HELPER_0_0 DEF_HELPER
#define DEF_HELPER_0_1 DEF_HELPER
#define DEF_HELPER_0_2 DEF_HELPER
#define DEF_HELPER_0_3 DEF_HELPER
#define DEF_HELPER_1_0 DEF_HELPER
#define DEF_HELPER_1_1 DEF_HELPER
#define DEF_HELPER_1_2 DEF_HELPER
#define DEF_HELPER_1_3 DEF_HELPER
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

#undef DEF_HELPER
#undef DEF_HELPER_0_0
#undef DEF_HELPER_0_1
#undef DEF_HELPER_0_2
#undef DEF_HELPER_1_0
#undef DEF_HELPER_1_1
#undef DEF_HELPER_1_2
#undef DEF_HELPER_1_3
#undef GEN_HELPER
