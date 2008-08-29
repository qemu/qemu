#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, helper_ldtlb, (void))
DEF_HELPER(void, helper_raise_illegal_instruction, (void))
DEF_HELPER(void, helper_raise_slot_illegal_instruction, (void))
DEF_HELPER(void, helper_debug, (void))
DEF_HELPER(void, helper_sleep, (void))
DEF_HELPER(void, helper_trapa, (uint32_t))
