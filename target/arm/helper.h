DEF_HELPER_FLAGS_1(sxtb16, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(uxtb16, TCG_CALL_NO_RWG_SE, i32, i32)

DEF_HELPER_3(add_setq, i32, env, i32, i32)
DEF_HELPER_3(add_saturate, i32, env, i32, i32)
DEF_HELPER_3(sub_saturate, i32, env, i32, i32)
DEF_HELPER_3(add_usaturate, i32, env, i32, i32)
DEF_HELPER_3(sub_usaturate, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(sdiv, TCG_CALL_NO_RWG, s32, env, s32, s32)
DEF_HELPER_FLAGS_3(udiv, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_1(rbit, TCG_CALL_NO_RWG_SE, i32, i32)

#define PAS_OP(pfx)  \
    DEF_HELPER_3(pfx ## add8, i32, i32, i32, ptr) \
    DEF_HELPER_3(pfx ## sub8, i32, i32, i32, ptr) \
    DEF_HELPER_3(pfx ## sub16, i32, i32, i32, ptr) \
    DEF_HELPER_3(pfx ## add16, i32, i32, i32, ptr) \
    DEF_HELPER_3(pfx ## addsubx, i32, i32, i32, ptr) \
    DEF_HELPER_3(pfx ## subaddx, i32, i32, i32, ptr)

PAS_OP(s)
PAS_OP(u)
#undef PAS_OP

#define PAS_OP(pfx)  \
    DEF_HELPER_2(pfx ## add8, i32, i32, i32) \
    DEF_HELPER_2(pfx ## sub8, i32, i32, i32) \
    DEF_HELPER_2(pfx ## sub16, i32, i32, i32) \
    DEF_HELPER_2(pfx ## add16, i32, i32, i32) \
    DEF_HELPER_2(pfx ## addsubx, i32, i32, i32) \
    DEF_HELPER_2(pfx ## subaddx, i32, i32, i32)
PAS_OP(q)
PAS_OP(sh)
PAS_OP(uq)
PAS_OP(uh)
#undef PAS_OP

DEF_HELPER_3(ssat, i32, env, i32, i32)
DEF_HELPER_3(usat, i32, env, i32, i32)
DEF_HELPER_3(ssat16, i32, env, i32, i32)
DEF_HELPER_3(usat16, i32, env, i32, i32)

DEF_HELPER_FLAGS_2(usad8, TCG_CALL_NO_RWG_SE, i32, i32, i32)

DEF_HELPER_FLAGS_3(sel_flags, TCG_CALL_NO_RWG_SE,
                   i32, i32, i32, i32)
DEF_HELPER_2(exception_internal, noreturn, env, i32)
DEF_HELPER_3(exception_with_syndrome, noreturn, env, i32, i32)
DEF_HELPER_4(exception_with_syndrome_el, noreturn, env, i32, i32, i32)
DEF_HELPER_2(exception_bkpt_insn, noreturn, env, i32)
DEF_HELPER_2(exception_swstep, noreturn, env, i32)
DEF_HELPER_2(exception_pc_alignment, noreturn, env, tl)
DEF_HELPER_1(setend, void, env)
DEF_HELPER_2(wfi, void, env, i32)
DEF_HELPER_1(wfe, void, env)
DEF_HELPER_1(yield, void, env)
DEF_HELPER_1(pre_hvc, void, env)
DEF_HELPER_2(pre_smc, void, env, i32)
DEF_HELPER_1(vesb, void, env)

DEF_HELPER_3(cpsr_write, void, env, i32, i32)
DEF_HELPER_2(cpsr_write_eret, void, env, i32)
DEF_HELPER_1(cpsr_read, i32, env)

DEF_HELPER_3(v7m_msr, void, env, i32, i32)
DEF_HELPER_2(v7m_mrs, i32, env, i32)

DEF_HELPER_2(v7m_bxns, void, env, i32)
DEF_HELPER_2(v7m_blxns, void, env, i32)

DEF_HELPER_3(v7m_tt, i32, env, i32, i32)

DEF_HELPER_1(v7m_preserve_fp_state, void, env)

DEF_HELPER_2(v7m_vlstm, void, env, i32)
DEF_HELPER_2(v7m_vlldm, void, env, i32)

DEF_HELPER_2(v8m_stackcheck, void, env, i32)

DEF_HELPER_FLAGS_2(check_bxj_trap, TCG_CALL_NO_WG, void, env, i32)

DEF_HELPER_4(access_check_cp_reg, cptr, env, i32, i32, i32)
DEF_HELPER_FLAGS_2(lookup_cp_reg, TCG_CALL_NO_RWG_SE, cptr, env, i32)
DEF_HELPER_3(set_cp_reg, void, env, cptr, i32)
DEF_HELPER_2(get_cp_reg, i32, env, cptr)
DEF_HELPER_3(set_cp_reg64, void, env, cptr, i64)
DEF_HELPER_2(get_cp_reg64, i64, env, cptr)

DEF_HELPER_2(get_r13_banked, i32, env, i32)
DEF_HELPER_3(set_r13_banked, void, env, i32, i32)

DEF_HELPER_3(mrs_banked, i32, env, i32, i32)
DEF_HELPER_4(msr_banked, void, env, i32, i32, i32)

DEF_HELPER_2(get_user_reg, i32, env, i32)
DEF_HELPER_3(set_user_reg, void, env, i32, i32)

DEF_HELPER_FLAGS_1(rebuild_hflags_m32_newel, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_2(rebuild_hflags_m32, TCG_CALL_NO_RWG, void, env, int)
DEF_HELPER_FLAGS_1(rebuild_hflags_a32_newel, TCG_CALL_NO_RWG, void, env)
DEF_HELPER_FLAGS_2(rebuild_hflags_a32, TCG_CALL_NO_RWG, void, env, int)
DEF_HELPER_FLAGS_2(rebuild_hflags_a64, TCG_CALL_NO_RWG, void, env, int)

DEF_HELPER_FLAGS_5(probe_access, TCG_CALL_NO_WG, void, env, tl, i32, i32, i32)

DEF_HELPER_1(vfp_get_fpscr, i32, env)
DEF_HELPER_2(vfp_set_fpscr, void, env, i32)

DEF_HELPER_3(vfp_addh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_adds, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_addd, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_subh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_subs, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_subd, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_mulh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_muls, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_muld, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_divh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_divs, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_divd, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_maxh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_maxs, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_maxd, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_minh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_mins, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_mind, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_maxnumh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_maxnums, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_maxnumd, f64, f64, f64, ptr)
DEF_HELPER_3(vfp_minnumh, f16, f16, f16, ptr)
DEF_HELPER_3(vfp_minnums, f32, f32, f32, ptr)
DEF_HELPER_3(vfp_minnumd, f64, f64, f64, ptr)
DEF_HELPER_1(vfp_negh, f16, f16)
DEF_HELPER_1(vfp_negs, f32, f32)
DEF_HELPER_1(vfp_negd, f64, f64)
DEF_HELPER_1(vfp_absh, f16, f16)
DEF_HELPER_1(vfp_abss, f32, f32)
DEF_HELPER_1(vfp_absd, f64, f64)
DEF_HELPER_2(vfp_sqrth, f16, f16, env)
DEF_HELPER_2(vfp_sqrts, f32, f32, env)
DEF_HELPER_2(vfp_sqrtd, f64, f64, env)
DEF_HELPER_3(vfp_cmph, void, f16, f16, env)
DEF_HELPER_3(vfp_cmps, void, f32, f32, env)
DEF_HELPER_3(vfp_cmpd, void, f64, f64, env)
DEF_HELPER_3(vfp_cmpeh, void, f16, f16, env)
DEF_HELPER_3(vfp_cmpes, void, f32, f32, env)
DEF_HELPER_3(vfp_cmped, void, f64, f64, env)

DEF_HELPER_2(vfp_fcvtds, f64, f32, env)
DEF_HELPER_2(vfp_fcvtsd, f32, f64, env)
DEF_HELPER_FLAGS_2(bfcvt, TCG_CALL_NO_RWG, i32, f32, ptr)
DEF_HELPER_FLAGS_2(bfcvt_pair, TCG_CALL_NO_RWG, i32, i64, ptr)

DEF_HELPER_2(vfp_uitoh, f16, i32, ptr)
DEF_HELPER_2(vfp_uitos, f32, i32, ptr)
DEF_HELPER_2(vfp_uitod, f64, i32, ptr)
DEF_HELPER_2(vfp_sitoh, f16, i32, ptr)
DEF_HELPER_2(vfp_sitos, f32, i32, ptr)
DEF_HELPER_2(vfp_sitod, f64, i32, ptr)

DEF_HELPER_2(vfp_touih, i32, f16, ptr)
DEF_HELPER_2(vfp_touis, i32, f32, ptr)
DEF_HELPER_2(vfp_touid, i32, f64, ptr)
DEF_HELPER_2(vfp_touizh, i32, f16, ptr)
DEF_HELPER_2(vfp_touizs, i32, f32, ptr)
DEF_HELPER_2(vfp_touizd, i32, f64, ptr)
DEF_HELPER_2(vfp_tosih, s32, f16, ptr)
DEF_HELPER_2(vfp_tosis, s32, f32, ptr)
DEF_HELPER_2(vfp_tosid, s32, f64, ptr)
DEF_HELPER_2(vfp_tosizh, s32, f16, ptr)
DEF_HELPER_2(vfp_tosizs, s32, f32, ptr)
DEF_HELPER_2(vfp_tosizd, s32, f64, ptr)

DEF_HELPER_3(vfp_toshh_round_to_zero, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toslh_round_to_zero, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_touhh_round_to_zero, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toulh_round_to_zero, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toshs_round_to_zero, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_tosls_round_to_zero, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_touhs_round_to_zero, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_touls_round_to_zero, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_toshd_round_to_zero, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_tosld_round_to_zero, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_touhd_round_to_zero, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_tould_round_to_zero, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_touhh, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toshh, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toulh, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_toslh, i32, f16, i32, ptr)
DEF_HELPER_3(vfp_touqh, i64, f16, i32, ptr)
DEF_HELPER_3(vfp_tosqh, i64, f16, i32, ptr)
DEF_HELPER_3(vfp_toshs, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_tosls, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_tosqs, i64, f32, i32, ptr)
DEF_HELPER_3(vfp_touhs, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_touls, i32, f32, i32, ptr)
DEF_HELPER_3(vfp_touqs, i64, f32, i32, ptr)
DEF_HELPER_3(vfp_toshd, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_tosld, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_tosqd, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_touhd, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_tould, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_touqd, i64, f64, i32, ptr)
DEF_HELPER_3(vfp_shtos, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_sltos, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_sqtos, f32, i64, i32, ptr)
DEF_HELPER_3(vfp_uhtos, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_ultos, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_uqtos, f32, i64, i32, ptr)
DEF_HELPER_3(vfp_shtod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_sltod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_sqtod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_uhtod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_ultod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_uqtod, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_shtoh, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_uhtoh, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_sltoh, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_ultoh, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_sqtoh, f16, i64, i32, ptr)
DEF_HELPER_3(vfp_uqtoh, f16, i64, i32, ptr)

DEF_HELPER_3(vfp_shtos_round_to_nearest, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_sltos_round_to_nearest, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_uhtos_round_to_nearest, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_ultos_round_to_nearest, f32, i32, i32, ptr)
DEF_HELPER_3(vfp_shtod_round_to_nearest, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_sltod_round_to_nearest, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_uhtod_round_to_nearest, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_ultod_round_to_nearest, f64, i64, i32, ptr)
DEF_HELPER_3(vfp_shtoh_round_to_nearest, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_uhtoh_round_to_nearest, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_sltoh_round_to_nearest, f16, i32, i32, ptr)
DEF_HELPER_3(vfp_ultoh_round_to_nearest, f16, i32, i32, ptr)

DEF_HELPER_FLAGS_2(set_rmode, TCG_CALL_NO_RWG, i32, i32, ptr)

DEF_HELPER_FLAGS_3(vfp_fcvt_f16_to_f32, TCG_CALL_NO_RWG, f32, f16, ptr, i32)
DEF_HELPER_FLAGS_3(vfp_fcvt_f32_to_f16, TCG_CALL_NO_RWG, f16, f32, ptr, i32)
DEF_HELPER_FLAGS_3(vfp_fcvt_f16_to_f64, TCG_CALL_NO_RWG, f64, f16, ptr, i32)
DEF_HELPER_FLAGS_3(vfp_fcvt_f64_to_f16, TCG_CALL_NO_RWG, f16, f64, ptr, i32)

DEF_HELPER_4(vfp_muladdd, f64, f64, f64, f64, ptr)
DEF_HELPER_4(vfp_muladds, f32, f32, f32, f32, ptr)
DEF_HELPER_4(vfp_muladdh, f16, f16, f16, f16, ptr)

DEF_HELPER_FLAGS_2(recpe_f16, TCG_CALL_NO_RWG, f16, f16, ptr)
DEF_HELPER_FLAGS_2(recpe_f32, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(recpe_f64, TCG_CALL_NO_RWG, f64, f64, ptr)
DEF_HELPER_FLAGS_2(rsqrte_f16, TCG_CALL_NO_RWG, f16, f16, ptr)
DEF_HELPER_FLAGS_2(rsqrte_f32, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(rsqrte_f64, TCG_CALL_NO_RWG, f64, f64, ptr)
DEF_HELPER_FLAGS_1(recpe_u32, TCG_CALL_NO_RWG, i32, i32)
DEF_HELPER_FLAGS_1(rsqrte_u32, TCG_CALL_NO_RWG, i32, i32)
DEF_HELPER_FLAGS_4(neon_tbl, TCG_CALL_NO_RWG, i64, env, i32, i64, i64)

DEF_HELPER_3(shl_cc, i32, env, i32, i32)
DEF_HELPER_3(shr_cc, i32, env, i32, i32)
DEF_HELPER_3(sar_cc, i32, env, i32, i32)
DEF_HELPER_3(ror_cc, i32, env, i32, i32)

DEF_HELPER_FLAGS_2(rinth_exact, TCG_CALL_NO_RWG, f16, f16, ptr)
DEF_HELPER_FLAGS_2(rints_exact, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(rintd_exact, TCG_CALL_NO_RWG, f64, f64, ptr)
DEF_HELPER_FLAGS_2(rinth, TCG_CALL_NO_RWG, f16, f16, ptr)
DEF_HELPER_FLAGS_2(rints, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(rintd, TCG_CALL_NO_RWG, f64, f64, ptr)

DEF_HELPER_FLAGS_2(vjcvt, TCG_CALL_NO_RWG, i32, f64, env)
DEF_HELPER_FLAGS_2(fjcvtzs, TCG_CALL_NO_RWG, i64, f64, ptr)

DEF_HELPER_FLAGS_3(check_hcr_el2_trap, TCG_CALL_NO_WG, void, env, i32, i32)

/* neon_helper.c */
DEF_HELPER_FLAGS_3(neon_qadd_u8, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_qadd_s8, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_qadd_u16, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_qadd_s16, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_qadd_u32, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_qadd_s32, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_uqadd_s8, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_uqadd_s16, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_uqadd_s32, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_uqadd_s64, TCG_CALL_NO_RWG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(neon_sqadd_u8, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_sqadd_u16, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_sqadd_u32, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(neon_sqadd_u64, TCG_CALL_NO_RWG, i64, env, i64, i64)
DEF_HELPER_3(neon_qsub_u8, i32, env, i32, i32)
DEF_HELPER_3(neon_qsub_s8, i32, env, i32, i32)
DEF_HELPER_3(neon_qsub_u16, i32, env, i32, i32)
DEF_HELPER_3(neon_qsub_s16, i32, env, i32, i32)
DEF_HELPER_3(neon_qsub_u32, i32, env, i32, i32)
DEF_HELPER_3(neon_qsub_s32, i32, env, i32, i32)
DEF_HELPER_3(neon_qadd_u64, i64, env, i64, i64)
DEF_HELPER_3(neon_qadd_s64, i64, env, i64, i64)
DEF_HELPER_3(neon_qsub_u64, i64, env, i64, i64)
DEF_HELPER_3(neon_qsub_s64, i64, env, i64, i64)

DEF_HELPER_2(neon_hadd_s8, i32, i32, i32)
DEF_HELPER_2(neon_hadd_u8, i32, i32, i32)
DEF_HELPER_2(neon_hadd_s16, i32, i32, i32)
DEF_HELPER_2(neon_hadd_u16, i32, i32, i32)
DEF_HELPER_2(neon_hadd_s32, s32, s32, s32)
DEF_HELPER_2(neon_hadd_u32, i32, i32, i32)
DEF_HELPER_2(neon_rhadd_s8, i32, i32, i32)
DEF_HELPER_2(neon_rhadd_u8, i32, i32, i32)
DEF_HELPER_2(neon_rhadd_s16, i32, i32, i32)
DEF_HELPER_2(neon_rhadd_u16, i32, i32, i32)
DEF_HELPER_2(neon_rhadd_s32, s32, s32, s32)
DEF_HELPER_2(neon_rhadd_u32, i32, i32, i32)
DEF_HELPER_2(neon_hsub_s8, i32, i32, i32)
DEF_HELPER_2(neon_hsub_u8, i32, i32, i32)
DEF_HELPER_2(neon_hsub_s16, i32, i32, i32)
DEF_HELPER_2(neon_hsub_u16, i32, i32, i32)
DEF_HELPER_2(neon_hsub_s32, s32, s32, s32)
DEF_HELPER_2(neon_hsub_u32, i32, i32, i32)

DEF_HELPER_2(neon_pmin_u8, i32, i32, i32)
DEF_HELPER_2(neon_pmin_s8, i32, i32, i32)
DEF_HELPER_2(neon_pmin_u16, i32, i32, i32)
DEF_HELPER_2(neon_pmin_s16, i32, i32, i32)
DEF_HELPER_2(neon_pmax_u8, i32, i32, i32)
DEF_HELPER_2(neon_pmax_s8, i32, i32, i32)
DEF_HELPER_2(neon_pmax_u16, i32, i32, i32)
DEF_HELPER_2(neon_pmax_s16, i32, i32, i32)

DEF_HELPER_2(neon_shl_u16, i32, i32, i32)
DEF_HELPER_2(neon_shl_s16, i32, i32, i32)
DEF_HELPER_2(neon_rshl_u8, i32, i32, i32)
DEF_HELPER_2(neon_rshl_s8, i32, i32, i32)
DEF_HELPER_2(neon_rshl_u16, i32, i32, i32)
DEF_HELPER_2(neon_rshl_s16, i32, i32, i32)
DEF_HELPER_2(neon_rshl_u32, i32, i32, i32)
DEF_HELPER_2(neon_rshl_s32, i32, i32, i32)
DEF_HELPER_2(neon_rshl_u64, i64, i64, i64)
DEF_HELPER_2(neon_rshl_s64, i64, i64, i64)
DEF_HELPER_3(neon_qshl_u8, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_s8, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_u16, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_s16, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_u32, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_s32, i32, env, i32, i32)
DEF_HELPER_3(neon_qshl_u64, i64, env, i64, i64)
DEF_HELPER_3(neon_qshl_s64, i64, env, i64, i64)
DEF_HELPER_3(neon_qshlu_s8, i32, env, i32, i32)
DEF_HELPER_3(neon_qshlu_s16, i32, env, i32, i32)
DEF_HELPER_3(neon_qshlu_s32, i32, env, i32, i32)
DEF_HELPER_3(neon_qshlu_s64, i64, env, i64, i64)
DEF_HELPER_3(neon_qrshl_u8, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_s8, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_u16, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_s16, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_u32, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_s32, i32, env, i32, i32)
DEF_HELPER_3(neon_qrshl_u64, i64, env, i64, i64)
DEF_HELPER_3(neon_qrshl_s64, i64, env, i64, i64)

DEF_HELPER_2(neon_add_u8, i32, i32, i32)
DEF_HELPER_2(neon_add_u16, i32, i32, i32)
DEF_HELPER_2(neon_padd_u8, i32, i32, i32)
DEF_HELPER_2(neon_padd_u16, i32, i32, i32)
DEF_HELPER_2(neon_sub_u8, i32, i32, i32)
DEF_HELPER_2(neon_sub_u16, i32, i32, i32)
DEF_HELPER_2(neon_mul_u8, i32, i32, i32)
DEF_HELPER_2(neon_mul_u16, i32, i32, i32)

DEF_HELPER_2(neon_tst_u8, i32, i32, i32)
DEF_HELPER_2(neon_tst_u16, i32, i32, i32)
DEF_HELPER_2(neon_tst_u32, i32, i32, i32)

DEF_HELPER_1(neon_clz_u8, i32, i32)
DEF_HELPER_1(neon_clz_u16, i32, i32)
DEF_HELPER_1(neon_cls_s8, i32, i32)
DEF_HELPER_1(neon_cls_s16, i32, i32)
DEF_HELPER_1(neon_cls_s32, i32, i32)
DEF_HELPER_1(neon_cnt_u8, i32, i32)
DEF_HELPER_FLAGS_1(neon_rbit_u8, TCG_CALL_NO_RWG_SE, i32, i32)

DEF_HELPER_3(neon_qdmulh_s16, i32, env, i32, i32)
DEF_HELPER_3(neon_qrdmulh_s16, i32, env, i32, i32)
DEF_HELPER_4(neon_qrdmlah_s16, i32, env, i32, i32, i32)
DEF_HELPER_4(neon_qrdmlsh_s16, i32, env, i32, i32, i32)
DEF_HELPER_3(neon_qdmulh_s32, i32, env, i32, i32)
DEF_HELPER_3(neon_qrdmulh_s32, i32, env, i32, i32)
DEF_HELPER_4(neon_qrdmlah_s32, i32, env, s32, s32, s32)
DEF_HELPER_4(neon_qrdmlsh_s32, i32, env, s32, s32, s32)

DEF_HELPER_1(neon_narrow_u8, i32, i64)
DEF_HELPER_1(neon_narrow_u16, i32, i64)
DEF_HELPER_2(neon_unarrow_sat8, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_u8, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_s8, i32, env, i64)
DEF_HELPER_2(neon_unarrow_sat16, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_u16, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_s16, i32, env, i64)
DEF_HELPER_2(neon_unarrow_sat32, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_u32, i32, env, i64)
DEF_HELPER_2(neon_narrow_sat_s32, i32, env, i64)
DEF_HELPER_1(neon_narrow_high_u8, i32, i64)
DEF_HELPER_1(neon_narrow_high_u16, i32, i64)
DEF_HELPER_1(neon_narrow_round_high_u8, i32, i64)
DEF_HELPER_1(neon_narrow_round_high_u16, i32, i64)
DEF_HELPER_1(neon_widen_u8, i64, i32)
DEF_HELPER_1(neon_widen_s8, i64, i32)
DEF_HELPER_1(neon_widen_u16, i64, i32)
DEF_HELPER_1(neon_widen_s16, i64, i32)

DEF_HELPER_2(neon_addl_u16, i64, i64, i64)
DEF_HELPER_2(neon_addl_u32, i64, i64, i64)
DEF_HELPER_2(neon_paddl_u16, i64, i64, i64)
DEF_HELPER_2(neon_paddl_u32, i64, i64, i64)
DEF_HELPER_2(neon_subl_u16, i64, i64, i64)
DEF_HELPER_2(neon_subl_u32, i64, i64, i64)
DEF_HELPER_3(neon_addl_saturate_s32, i64, env, i64, i64)
DEF_HELPER_3(neon_addl_saturate_s64, i64, env, i64, i64)
DEF_HELPER_2(neon_abdl_u16, i64, i32, i32)
DEF_HELPER_2(neon_abdl_s16, i64, i32, i32)
DEF_HELPER_2(neon_abdl_u32, i64, i32, i32)
DEF_HELPER_2(neon_abdl_s32, i64, i32, i32)
DEF_HELPER_2(neon_abdl_u64, i64, i32, i32)
DEF_HELPER_2(neon_abdl_s64, i64, i32, i32)
DEF_HELPER_2(neon_mull_u8, i64, i32, i32)
DEF_HELPER_2(neon_mull_s8, i64, i32, i32)
DEF_HELPER_2(neon_mull_u16, i64, i32, i32)
DEF_HELPER_2(neon_mull_s16, i64, i32, i32)

DEF_HELPER_1(neon_negl_u16, i64, i64)
DEF_HELPER_1(neon_negl_u32, i64, i64)

DEF_HELPER_FLAGS_2(neon_qabs_s8, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qabs_s16, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qabs_s32, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qabs_s64, TCG_CALL_NO_RWG, i64, env, i64)
DEF_HELPER_FLAGS_2(neon_qneg_s8, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qneg_s16, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qneg_s32, TCG_CALL_NO_RWG, i32, env, i32)
DEF_HELPER_FLAGS_2(neon_qneg_s64, TCG_CALL_NO_RWG, i64, env, i64)

DEF_HELPER_3(neon_ceq_f32, i32, i32, i32, ptr)
DEF_HELPER_3(neon_cge_f32, i32, i32, i32, ptr)
DEF_HELPER_3(neon_cgt_f32, i32, i32, i32, ptr)
DEF_HELPER_3(neon_acge_f32, i32, i32, i32, ptr)
DEF_HELPER_3(neon_acgt_f32, i32, i32, i32, ptr)
DEF_HELPER_3(neon_acge_f64, i64, i64, i64, ptr)
DEF_HELPER_3(neon_acgt_f64, i64, i64, i64, ptr)

/* iwmmxt_helper.c */
DEF_HELPER_2(iwmmxt_maddsq, i64, i64, i64)
DEF_HELPER_2(iwmmxt_madduq, i64, i64, i64)
DEF_HELPER_2(iwmmxt_sadb, i64, i64, i64)
DEF_HELPER_2(iwmmxt_sadw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mulslw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mulshw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mululw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_muluhw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_macsw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_macuw, i64, i64, i64)
DEF_HELPER_1(iwmmxt_setpsr_nz, i32, i64)

#define DEF_IWMMXT_HELPER_SIZE_ENV(name) \
DEF_HELPER_3(iwmmxt_##name##b, i64, env, i64, i64) \
DEF_HELPER_3(iwmmxt_##name##w, i64, env, i64, i64) \
DEF_HELPER_3(iwmmxt_##name##l, i64, env, i64, i64) \

DEF_IWMMXT_HELPER_SIZE_ENV(unpackl)
DEF_IWMMXT_HELPER_SIZE_ENV(unpackh)

DEF_HELPER_2(iwmmxt_unpacklub, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackluw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklul, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhub, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhuw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhul, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsb, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsl, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsb, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsl, i64, env, i64)

DEF_IWMMXT_HELPER_SIZE_ENV(cmpeq)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgtu)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgts)

DEF_IWMMXT_HELPER_SIZE_ENV(mins)
DEF_IWMMXT_HELPER_SIZE_ENV(minu)
DEF_IWMMXT_HELPER_SIZE_ENV(maxs)
DEF_IWMMXT_HELPER_SIZE_ENV(maxu)

DEF_IWMMXT_HELPER_SIZE_ENV(subn)
DEF_IWMMXT_HELPER_SIZE_ENV(addn)
DEF_IWMMXT_HELPER_SIZE_ENV(subu)
DEF_IWMMXT_HELPER_SIZE_ENV(addu)
DEF_IWMMXT_HELPER_SIZE_ENV(subs)
DEF_IWMMXT_HELPER_SIZE_ENV(adds)

DEF_HELPER_3(iwmmxt_avgb0, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgb1, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgw0, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgw1, i64, env, i64, i64)

DEF_HELPER_3(iwmmxt_align, i64, i64, i64, i32)
DEF_HELPER_4(iwmmxt_insr, i64, i64, i32, i32, i32)

DEF_HELPER_1(iwmmxt_bcstb, i64, i32)
DEF_HELPER_1(iwmmxt_bcstw, i64, i32)
DEF_HELPER_1(iwmmxt_bcstl, i64, i32)

DEF_HELPER_1(iwmmxt_addcb, i64, i64)
DEF_HELPER_1(iwmmxt_addcw, i64, i64)
DEF_HELPER_1(iwmmxt_addcl, i64, i64)

DEF_HELPER_1(iwmmxt_msbb, i32, i64)
DEF_HELPER_1(iwmmxt_msbw, i32, i64)
DEF_HELPER_1(iwmmxt_msbl, i32, i64)

DEF_HELPER_3(iwmmxt_srlw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_srll, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_srlq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sllw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_slll, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sllq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sraw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sral, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sraq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorl, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_shufh, i64, env, i64, i32)

DEF_HELPER_3(iwmmxt_packuw, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packul, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packuq, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsw, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsl, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsq, i64, env, i64, i64)

DEF_HELPER_3(iwmmxt_muladdsl, i64, i64, i32, i32)
DEF_HELPER_3(iwmmxt_muladdsw, i64, i64, i32, i32)
DEF_HELPER_3(iwmmxt_muladdswl, i64, i64, i32, i32)

DEF_HELPER_FLAGS_2(neon_unzip8, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_unzip16, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qunzip8, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qunzip16, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qunzip32, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_zip8, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_zip16, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qzip8, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qzip16, TCG_CALL_NO_RWG, void, ptr, ptr)
DEF_HELPER_FLAGS_2(neon_qzip32, TCG_CALL_NO_RWG, void, ptr, ptr)

DEF_HELPER_FLAGS_4(crypto_aese, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(crypto_aesmc, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_sha1su0, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha1c, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha1p, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha1m, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(crypto_sha1h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(crypto_sha1su1, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_sha256h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha256h2, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(crypto_sha256su0, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha256su1, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_sha512h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha512h2, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(crypto_sha512su0, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sha512su1, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_sm3tt1a, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm3tt1b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm3tt2a, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm3tt2b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm3partw1, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm3partw2, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_sm4e, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(crypto_sm4ekey, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(crypto_rax1, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(crc32, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)
DEF_HELPER_FLAGS_3(crc32c, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)

DEF_HELPER_FLAGS_5(gvec_qrdmlah_s16, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_qrdmlsh_s16, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_qrdmlah_s32, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_qrdmlsh_s32, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqrdmlah_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlah_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlah_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlah_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_sdot_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_udot_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sdot_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_udot_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_usdot_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_sdot_idx_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_udot_idx_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sdot_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_udot_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sudot_idx_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_usdot_idx_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fcaddh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fcadds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fcaddd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(gvec_fcmlah, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fcmlah_idx, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fcmlas, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fcmlas_idx, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fcmlad, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(neon_paddh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_pmaxh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_pminh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_padds, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_pmaxs, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_pmins, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_sstoh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sitos, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ustoh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uitos, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_tosszh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_tosizs, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_touszh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_touizs, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_vcvt_sf, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_uf, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_fs, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_fu, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_vcvt_sh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_uh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_hs, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_hu, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_vcvt_rm_ss, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_rm_us, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_rm_sh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vcvt_rm_uh, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_vrint_rm_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vrint_rm_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_vrintx_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_vrintx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_frecpe_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_frecpe_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_frecpe_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_frsqrte_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_frsqrte_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_frsqrte_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_fcgt0_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_fcgt0_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_fcge0_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_fcge0_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_fceq0_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_fceq0_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_fcle0_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_fcle0_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_fclt0_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_fclt0_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fadd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fadd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fadd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fsub_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fsub_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fsub_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmul_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmul_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmul_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fabd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fabd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fceq_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fceq_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fcge_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fcge_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fcgt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fcgt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_facge_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_facge_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_facgt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_facgt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmax_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmax_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmin_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmin_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmaxnum_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmaxnum_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fminnum_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fminnum_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_recps_nf_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_recps_nf_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_rsqrts_nf_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_rsqrts_nf_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmla_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmla_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmls_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmls_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_vfma_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_vfma_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_vfms_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_vfms_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_ftsmul_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_ftsmul_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_ftsmul_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmul_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmul_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmul_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmla_nf_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmla_nf_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmls_nf_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmls_nf_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(gvec_fmla_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fmla_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_fmla_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_uqadd_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqadd_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqadd_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqadd_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqadd_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqadd_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqadd_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqadd_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqsub_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqsub_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqsub_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uqsub_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqsub_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqsub_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqsub_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sqsub_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmlal_a32, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmlal_a64, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmlal_idx_a32, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmlal_idx_a64, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_2(frint32_s, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(frint64_s, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(frint32_d, TCG_CALL_NO_RWG, f64, f64, ptr)
DEF_HELPER_FLAGS_2(frint64_d, TCG_CALL_NO_RWG, f64, f64, ptr)

DEF_HELPER_FLAGS_3(gvec_ceq0_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ceq0_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_clt0_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_clt0_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cle0_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cle0_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cgt0_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cgt0_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cge0_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_cge0_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_smulh_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smulh_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smulh_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_smulh_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_umulh_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umulh_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umulh_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_umulh_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_sshl_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sshl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ushl_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_ushl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_pmul_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_pmull_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(neon_pmull_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_ssra_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ssra_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ssra_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ssra_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_usra_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_usra_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_usra_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_usra_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_srshr_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srshr_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srshr_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srshr_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_urshr_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_urshr_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_urshr_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_urshr_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_srsra_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srsra_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srsra_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_srsra_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_ursra_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ursra_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ursra_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_ursra_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_sri_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sri_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sri_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sri_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(gvec_sli_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sli_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sli_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(gvec_sli_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_sabd_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sabd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sabd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_sabd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_uabd_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uabd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uabd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uabd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_saba_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_saba_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_saba_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_saba_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_uaba_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uaba_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uaba_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_uaba_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_mul_idx_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_mul_idx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(gvec_mul_idx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_mla_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_mla_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_mla_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_mls_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_mls_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_mls_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(neon_sqdmulh_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_sqdmulh_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(neon_sqrdmulh_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(neon_sqrdmulh_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqdmulh_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmulh_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmulh_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmulh_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqrdmulh_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqrdmulh_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqrdmulh_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqrdmulh_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqdmulh_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmulh_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmulh_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqrdmulh_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqrdmulh_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqrdmulh_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_fmlal_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fmlal_zzxw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(gvec_xar_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_smmla_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_ummla_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_usmmla_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_bfdot, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_bfdot_idx, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_bfmmla, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(gvec_bfmlal, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(gvec_bfmlal_idx, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_sclamp_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sclamp_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sclamp_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_sclamp_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_uclamp_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uclamp_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uclamp_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_uclamp_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

#ifdef TARGET_AARCH64
#include "tcg/helper-a64.h"
#include "tcg/helper-sve.h"
#include "tcg/helper-sme.h"
#endif

#include "tcg/helper-mve.h"
