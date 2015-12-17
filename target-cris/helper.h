DEF_HELPER_2(raise_exception, void, env, i32)
DEF_HELPER_2(tlb_flush_pid, void, env, i32)
DEF_HELPER_2(spc_write, void, env, i32)
DEF_HELPER_1(rfe, void, env)
DEF_HELPER_1(rfn, void, env)

DEF_HELPER_3(movl_sreg_reg, void, env, i32, i32)
DEF_HELPER_3(movl_reg_sreg, void, env, i32, i32)

DEF_HELPER_FLAGS_1(lz, TCG_CALL_NO_SE, i32, i32)
DEF_HELPER_FLAGS_4(btst, TCG_CALL_NO_SE, i32, env, i32, i32, i32)

DEF_HELPER_FLAGS_4(evaluate_flags_muls, TCG_CALL_NO_SE, i32, env, i32, i32, i32)
DEF_HELPER_FLAGS_4(evaluate_flags_mulu, TCG_CALL_NO_SE, i32, env, i32, i32, i32)
DEF_HELPER_FLAGS_5(evaluate_flags_mcp, TCG_CALL_NO_SE, i32, env,
                                                      i32, i32, i32, i32)
DEF_HELPER_FLAGS_5(evaluate_flags_alu_4, TCG_CALL_NO_SE, i32, env,
                                                        i32, i32, i32, i32)
DEF_HELPER_FLAGS_5(evaluate_flags_sub_4, TCG_CALL_NO_SE, i32, env,
                                                        i32, i32, i32, i32)
DEF_HELPER_FLAGS_3(evaluate_flags_move_4, TCG_CALL_NO_SE, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(evaluate_flags_move_2, TCG_CALL_NO_SE, i32, env, i32, i32)
DEF_HELPER_1(evaluate_flags, void, env)
DEF_HELPER_1(top_evaluate_flags, void, env)
