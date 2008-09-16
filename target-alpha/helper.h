#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, helper_tb_flush, (void))

DEF_HELPER(uint64_t, helper_amask, (uint64_t))

DEF_HELPER(uint64_t, helper_ctpop, (uint64_t))
DEF_HELPER(uint64_t, helper_ctlz, (uint64_t))
DEF_HELPER(uint64_t, helper_cttz, (uint64_t))
