#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, helper_ldtlb, (void))
DEF_HELPER(void, helper_raise_illegal_instruction, (void))
DEF_HELPER(void, helper_raise_slot_illegal_instruction, (void))
DEF_HELPER(void, helper_debug, (void))
DEF_HELPER(void, helper_sleep, (void))
DEF_HELPER(void, helper_trapa, (uint32_t))

DEF_HELPER(uint32_t, helper_addv, (uint32_t, uint32_t))
DEF_HELPER(uint32_t, helper_addc, (uint32_t, uint32_t))
DEF_HELPER(uint32_t, helper_subv, (uint32_t, uint32_t))
DEF_HELPER(uint32_t, helper_subc, (uint32_t, uint32_t))
DEF_HELPER(uint32_t, helper_negc, (uint32_t))
DEF_HELPER(void, helper_macl, (uint32_t, uint32_t))
DEF_HELPER(void, helper_macw, (uint32_t, uint32_t))

DEF_HELPER(void, helper_ld_fpscr, (uint32_t))
