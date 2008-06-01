#ifndef DEF_HELPER
#define DEF_HELPER(name, ret, args) ret glue(helper_,name) args;
#endif

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

DEF_HELPER_1_1(bitrev, uint32_t, (uint32_t))
DEF_HELPER_1_1(ff1, uint32_t, (uint32_t))
DEF_HELPER_1_2(sats, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_0_2(divu, void, (CPUState *, uint32_t))
DEF_HELPER_0_2(divs, void, (CPUState *, uint32_t))
DEF_HELPER_1_3(addx_cc, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(subx_cc, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(shl_cc, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(shr_cc, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(sar_cc, uint32_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_2(xflag_lt, uint32_t, (uint32_t, uint32_t))
DEF_HELPER_0_2(set_sr, void, (CPUState *, uint32_t))
DEF_HELPER_0_3(movec, void, (CPUState *, uint32_t, uint32_t))

DEF_HELPER_1_2(f64_to_i32, float32, (CPUState *, float64))
DEF_HELPER_1_2(f64_to_f32, float32, (CPUState *, float64))
DEF_HELPER_1_2(i32_to_f64, float64, (CPUState *, uint32_t))
DEF_HELPER_1_2(f32_to_f64, float64, (CPUState *, float32))
DEF_HELPER_1_2(iround_f64, float64, (CPUState *, float64))
DEF_HELPER_1_2(itrunc_f64, float64, (CPUState *, float64))
DEF_HELPER_1_2(sqrt_f64, float64, (CPUState *, float64))
DEF_HELPER_1_1(abs_f64, float64, (float64))
DEF_HELPER_1_1(chs_f64, float64, (float64))
DEF_HELPER_1_3(add_f64, float64, (CPUState *, float64, float64))
DEF_HELPER_1_3(sub_f64, float64, (CPUState *, float64, float64))
DEF_HELPER_1_3(mul_f64, float64, (CPUState *, float64, float64))
DEF_HELPER_1_3(div_f64, float64, (CPUState *, float64, float64))
DEF_HELPER_1_3(sub_cmp_f64, float64, (CPUState *, float64, float64))
DEF_HELPER_1_2(compare_f64, uint32_t, (CPUState *, float64))

DEF_HELPER_0_3(mac_move, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(macmulf, uint64_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(macmuls, uint64_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_1_3(macmulu, uint64_t, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_0_2(macsats, void, (CPUState *, uint32_t))
DEF_HELPER_0_2(macsatu, void, (CPUState *, uint32_t))
DEF_HELPER_0_2(macsatf, void, (CPUState *, uint32_t))
DEF_HELPER_0_2(mac_set_flags, void, (CPUState *, uint32_t))
DEF_HELPER_0_2(set_macsr, void, (CPUState *, uint32_t))
DEF_HELPER_1_2(get_macf, uint32_t, (CPUState *, uint64_t))
DEF_HELPER_1_1(get_macs, uint32_t, (uint64_t))
DEF_HELPER_1_1(get_macu, uint32_t, (uint64_t))
DEF_HELPER_1_2(get_mac_extf, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_1_2(get_mac_exti, uint32_t, (CPUState *, uint32_t))
DEF_HELPER_0_3(set_mac_extf, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_0_3(set_mac_exts, void, (CPUState *, uint32_t, uint32_t))
DEF_HELPER_0_3(set_mac_extu, void, (CPUState *, uint32_t, uint32_t))

DEF_HELPER_0_2(flush_flags, void, (CPUState *, uint32_t))
DEF_HELPER_0_1(raise_exception, void, (uint32_t))

#undef DEF_HELPER
#undef DEF_HELPER_0_0
#undef DEF_HELPER_0_1
#undef DEF_HELPER_0_2
#undef DEF_HELPER_0_3
#undef DEF_HELPER_1_0
#undef DEF_HELPER_1_1
#undef DEF_HELPER_1_2
#undef DEF_HELPER_1_3
#undef DEF_HELPER_1_4
#undef GEN_HELPER
#undef DEF_HELPER
