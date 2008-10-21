#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(uint32_t, helper_fcmpo, (void))
DEF_HELPER(uint32_t, helper_fcmpu, (void))

DEF_HELPER(uint32_t, helper_load_cr, (void))
DEF_HELPER(void, helper_store_cr, (target_ulong, uint32_t))

DEF_HELPER(target_ulong, helper_cntlzw, (target_ulong t))
DEF_HELPER(target_ulong, helper_popcntb, (target_ulong val))
DEF_HELPER(target_ulong, helper_sraw, (target_ulong, target_ulong))
#if defined(TARGET_PPC64)
DEF_HELPER(target_ulong, helper_cntlzd, (target_ulong t))
DEF_HELPER(target_ulong, helper_popcntb_64, (target_ulong val))
DEF_HELPER(target_ulong, helper_srad, (target_ulong, target_ulong))
#endif

