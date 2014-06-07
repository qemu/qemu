DEF_HELPER_2(raise_exception, void, env, int)
DEF_HELPER_1(debug, void, env)

DEF_HELPER_FLAGS_3(div, TCG_CALL_NO_WG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(udiv, TCG_CALL_NO_WG, i32, env, i32, i32)
