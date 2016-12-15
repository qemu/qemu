DEF_HELPER_2(excp, noreturn, env, int)
DEF_HELPER_FLAGS_2(tsv, TCG_CALL_NO_WG, void, env, tl)
DEF_HELPER_FLAGS_2(tcond, TCG_CALL_NO_WG, void, env, tl)

DEF_HELPER_FLAGS_1(loaded_fr0, TCG_CALL_NO_RWG, void, env)
