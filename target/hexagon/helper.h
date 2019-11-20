/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

DEF_HELPER_2(raise_exception, noreturn, env, i32)
#if HEX_DEBUG
DEF_HELPER_1(debug_start_packet, void, env)
#endif
DEF_HELPER_2(new_value, s32, env, int)
#if HEX_DEBUG
DEF_HELPER_3(debug_check_store_width, void, env, int, int)
#endif
DEF_HELPER_1(commit_hvx_stores, void, env)
#if HEX_DEBUG
DEF_HELPER_3(debug_commit_end, void, env, int, int)
#endif
DEF_HELPER_3(sfrecipa_val, s32, env, s32, s32)
DEF_HELPER_3(sfrecipa_pred, s32, env, s32, s32)
DEF_HELPER_2(sfinvsqrta_val, s32, env, s32)
DEF_HELPER_2(sfinvsqrta_pred, s32, env, s32)

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) HELPER
#include "qemu.odef"
#undef DEF_QEMU

#if HEX_DEBUG
DEF_HELPER_2(debug_value, void, env, s32)
DEF_HELPER_2(debug_value_i64, void, env, s64)
#endif
