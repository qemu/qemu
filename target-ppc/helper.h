#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(target_ulong, do_load_cr, (void))
DEF_HELPER(void, do_print_mem_EA, (target_ulong))
DEF_HELPER(void, do_store_cr, (uint32_t))

#if !defined (CONFIG_USER_ONLY)
DEF_HELPER(void, do_store_msr, (target_ulong))
#if defined(TARGET_PPC64)
DEF_HELPER(void, do_store_msr_32, (target_ulong))
#endif
#endif

DEF_HELPER(target_ulong, do_popcntb, (target_ulong))
#if defined(TARGET_PPC64)
DEF_HELPER(target_ulong, do_popcntb_64, (target_ulong))
#endif


