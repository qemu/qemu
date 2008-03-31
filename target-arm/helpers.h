#ifndef DEF_HELPER
#define DEF_HELPER(name, ret, args) ret helper_##name args;
#endif

DEF_HELPER(sxtb16, uint32_t, (uint32_t))
DEF_HELPER(uxtb16, uint32_t, (uint32_t))
