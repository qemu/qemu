#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, helper_tb_flush, (void))

DEF_HELPER(void, helper_excp, (int, int))
DEF_HELPER(uint64_t, helper_amask, (uint64_t))
DEF_HELPER(uint64_t, helper_load_pcc, (void))
DEF_HELPER(uint64_t, helper_load_implver, (void))
DEF_HELPER(uint64_t, helper_rc, (void))
DEF_HELPER(uint64_t, helper_rs, (void))

DEF_HELPER(uint64_t, helper_addqv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_addlv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_subqv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_sublv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mullv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mulqv, (uint64_t, uint64_t))
DEF_HELPER(uint64_t, helper_umulh, (uint64_t, uint64_t))

DEF_HELPER(uint64_t, helper_ctpop, (uint64_t))
DEF_HELPER(uint64_t, helper_ctlz, (uint64_t))
DEF_HELPER(uint64_t, helper_cttz, (uint64_t))

DEF_HELPER(uint64_t, helper_mskbl, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_insbl, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mskwl, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_inswl, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mskll, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_insll, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_zap, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_zapnot, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mskql, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_insql, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mskwh, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_inswh, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_msklh, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_inslh, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_mskqh, (int64_t, uint64_t))
DEF_HELPER(uint64_t, helper_insqh, (int64_t, uint64_t))

DEF_HELPER(uint64_t, helper_cmpbge, (uint64_t, uint64_t))
