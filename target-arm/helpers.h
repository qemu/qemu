#define DEF_HELPER(name, ret, args) ret helper_##name args;

#ifdef GEN_HELPER
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
#else /* !GEN_HELPER */
#define DEF_HELPER_1_1 DEF_HELPER
#define DEF_HELPER_1_2 DEF_HELPER
#define HELPER(x) helper_##x
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

#undef DEF_HELPER
#undef DEF_HELPER_1_1
#undef DEF_HELPER_1_2
#undef GEN_HELPER
