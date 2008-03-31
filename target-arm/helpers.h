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

DEF_HELPER_1_3(sel_flags, uint32_t, (uint32_t, uint32_t, uint32_t))
DEF_HELPER_0_1(exception, void, (uint32_t))
DEF_HELPER_0_0(wfi, void, (void))

DEF_HELPER_0_2(cpsr_write, void, (uint32_t, uint32_t))
DEF_HELPER_1_0(cpsr_read, uint32_t, (void))

#undef DEF_HELPER
#undef DEF_HELPER_0_0
#undef DEF_HELPER_0_1
#undef DEF_HELPER_0_2
#undef DEF_HELPER_1_0
#undef DEF_HELPER_1_1
#undef DEF_HELPER_1_2
#undef DEF_HELPER_1_3
#undef GEN_HELPER
