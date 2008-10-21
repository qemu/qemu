#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(uint32_t, helper_fcmpo, (void))
DEF_HELPER(uint32_t, helper_fcmpu, (void))

DEF_HELPER(uint32_t, helper_load_cr, (void))
DEF_HELPER(void, helper_store_cr, (target_ulong, uint32_t))
