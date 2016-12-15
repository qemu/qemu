DEF_HELPER_2(excp, noreturn, env, int)
DEF_HELPER_FLAGS_2(tsv, TCG_CALL_NO_WG, void, env, tl)
DEF_HELPER_FLAGS_2(tcond, TCG_CALL_NO_WG, void, env, tl)

DEF_HELPER_FLAGS_3(stby_b, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stby_e, TCG_CALL_NO_WG, void, env, tl, tl)

DEF_HELPER_FLAGS_1(probe_r, TCG_CALL_NO_RWG_SE, tl, tl)
DEF_HELPER_FLAGS_1(probe_w, TCG_CALL_NO_RWG_SE, tl, tl)

DEF_HELPER_FLAGS_1(loaded_fr0, TCG_CALL_NO_RWG, void, env)

DEF_HELPER_FLAGS_2(fsqrt_s, TCG_CALL_NO_RWG, f32, env, f32)
DEF_HELPER_FLAGS_2(frnd_s, TCG_CALL_NO_RWG, f32, env, f32)
DEF_HELPER_FLAGS_3(fadd_s, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fsub_s, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fmpy_s, TCG_CALL_NO_RWG, f32, env, f32, f32)
DEF_HELPER_FLAGS_3(fdiv_s, TCG_CALL_NO_RWG, f32, env, f32, f32)

DEF_HELPER_FLAGS_2(fsqrt_d, TCG_CALL_NO_RWG, f64, env, f64)
DEF_HELPER_FLAGS_2(frnd_d, TCG_CALL_NO_RWG, f64, env, f64)
DEF_HELPER_FLAGS_3(fadd_d, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fsub_d, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fmpy_d, TCG_CALL_NO_RWG, f64, env, f64, f64)
DEF_HELPER_FLAGS_3(fdiv_d, TCG_CALL_NO_RWG, f64, env, f64, f64)

DEF_HELPER_FLAGS_2(fcnv_s_d, TCG_CALL_NO_RWG, f64, env, f32)
DEF_HELPER_FLAGS_2(fcnv_d_s, TCG_CALL_NO_RWG, f32, env, f64)

DEF_HELPER_FLAGS_2(fcnv_w_s, TCG_CALL_NO_RWG, f32, env, s32)
DEF_HELPER_FLAGS_2(fcnv_dw_s, TCG_CALL_NO_RWG, f32, env, s64)
DEF_HELPER_FLAGS_2(fcnv_w_d, TCG_CALL_NO_RWG, f64, env, s32)
DEF_HELPER_FLAGS_2(fcnv_dw_d, TCG_CALL_NO_RWG, f64, env, s64)

DEF_HELPER_FLAGS_2(fcnv_s_w, TCG_CALL_NO_RWG, s32, env, f32)
DEF_HELPER_FLAGS_2(fcnv_d_w, TCG_CALL_NO_RWG, s32, env, f64)
DEF_HELPER_FLAGS_2(fcnv_s_dw, TCG_CALL_NO_RWG, s64, env, f32)
DEF_HELPER_FLAGS_2(fcnv_d_dw, TCG_CALL_NO_RWG, s64, env, f64)

DEF_HELPER_FLAGS_2(fcnv_t_s_w, TCG_CALL_NO_RWG, s32, env, f32)
DEF_HELPER_FLAGS_2(fcnv_t_d_w, TCG_CALL_NO_RWG, s32, env, f64)
DEF_HELPER_FLAGS_2(fcnv_t_s_dw, TCG_CALL_NO_RWG, s64, env, f32)
DEF_HELPER_FLAGS_2(fcnv_t_d_dw, TCG_CALL_NO_RWG, s64, env, f64)

DEF_HELPER_FLAGS_2(fcnv_uw_s, TCG_CALL_NO_RWG, f32, env, i32)
DEF_HELPER_FLAGS_2(fcnv_udw_s, TCG_CALL_NO_RWG, f32, env, i64)
DEF_HELPER_FLAGS_2(fcnv_uw_d, TCG_CALL_NO_RWG, f64, env, i32)
DEF_HELPER_FLAGS_2(fcnv_udw_d, TCG_CALL_NO_RWG, f64, env, i64)

DEF_HELPER_FLAGS_2(fcnv_s_uw, TCG_CALL_NO_RWG, i32, env, f32)
DEF_HELPER_FLAGS_2(fcnv_d_uw, TCG_CALL_NO_RWG, i32, env, f64)
DEF_HELPER_FLAGS_2(fcnv_s_udw, TCG_CALL_NO_RWG, i64, env, f32)
DEF_HELPER_FLAGS_2(fcnv_d_udw, TCG_CALL_NO_RWG, i64, env, f64)

DEF_HELPER_FLAGS_2(fcnv_t_s_uw, TCG_CALL_NO_RWG, i32, env, f32)
DEF_HELPER_FLAGS_2(fcnv_t_d_uw, TCG_CALL_NO_RWG, i32, env, f64)
DEF_HELPER_FLAGS_2(fcnv_t_s_udw, TCG_CALL_NO_RWG, i64, env, f32)
DEF_HELPER_FLAGS_2(fcnv_t_d_udw, TCG_CALL_NO_RWG, i64, env, f64)

DEF_HELPER_FLAGS_5(fcmp_s, TCG_CALL_NO_RWG, void, env, f32, f32, i32, i32)
DEF_HELPER_FLAGS_5(fcmp_d, TCG_CALL_NO_RWG, void, env, f64, f64, i32, i32)

DEF_HELPER_FLAGS_4(fmpyfadd_s, TCG_CALL_NO_RWG, i32, env, i32, i32, i32)
DEF_HELPER_FLAGS_4(fmpynfadd_s, TCG_CALL_NO_RWG, i32, env, i32, i32, i32)
DEF_HELPER_FLAGS_4(fmpyfadd_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64)
DEF_HELPER_FLAGS_4(fmpynfadd_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64)
