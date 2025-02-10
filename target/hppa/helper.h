DEF_HELPER_2(excp, noreturn, env, int)

DEF_HELPER_FLAGS_3(stby_b, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stby_b_parallel, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stby_e, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stby_e_parallel, TCG_CALL_NO_WG, void, env, tl, tl)

DEF_HELPER_FLAGS_3(stdby_b, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stdby_b_parallel, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stdby_e, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(stdby_e_parallel, TCG_CALL_NO_WG, void, env, tl, tl)

DEF_HELPER_FLAGS_1(ldc_check, TCG_CALL_NO_RWG, void, tl)

DEF_HELPER_FLAGS_2(hadd_ss, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(hadd_us, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(havg, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_3(hshladd, TCG_CALL_NO_RWG_SE, i64, i64, i64, i32)
DEF_HELPER_FLAGS_3(hshradd, TCG_CALL_NO_RWG_SE, i64, i64, i64, i32)
DEF_HELPER_FLAGS_2(hsub_ss, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(hsub_us, TCG_CALL_NO_RWG_SE, i64, i64, i64)

DEF_HELPER_FLAGS_4(probe, TCG_CALL_NO_WG, tl, env, tl, i32, i32)

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

DEF_HELPER_FLAGS_0(read_interval_timer, TCG_CALL_NO_RWG, tl)

#ifndef CONFIG_USER_ONLY
DEF_HELPER_1(halt, noreturn, env)
DEF_HELPER_1(reset, noreturn, env)
DEF_HELPER_1(rfi, void, env)
DEF_HELPER_1(rfi_r, void, env)
DEF_HELPER_FLAGS_2(b_gate_priv, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(write_interval_timer, TCG_CALL_NO_RWG, void, env, tl)
DEF_HELPER_FLAGS_2(write_eirr, TCG_CALL_NO_RWG, void, env, tl)
DEF_HELPER_FLAGS_2(swap_system_mask, TCG_CALL_NO_RWG, tl, env, tl)
DEF_HELPER_FLAGS_3(itlba_pa11, TCG_CALL_NO_RWG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(itlbp_pa11, TCG_CALL_NO_RWG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(idtlbt_pa20, TCG_CALL_NO_RWG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(iitlbt_pa20, TCG_CALL_NO_RWG, void, env, tl, tl)
DEF_HELPER_FLAGS_2(ptlb, TCG_CALL_NO_RWG, void, env, tl)
DEF_HELPER_FLAGS_2(ptlb_l, TCG_CALL_NO_RWG, void, env, tl)
DEF_HELPER_FLAGS_1(ptlbe, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_2(lpa, TCG_CALL_NO_WG, tl, env, tl)
DEF_HELPER_FLAGS_1(change_prot_id, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_1(update_gva_offset_mask, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_1(diag_btlb, void, env)
DEF_HELPER_1(diag_console_output, void, env)
#endif
