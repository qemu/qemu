#include "def-helper.h"

DEF_HELPER_1(raise_exception, void, i32)
DEF_HELPER_1(tlb_flush_pid, void, i32)
DEF_HELPER_1(spc_write, void, i32)
DEF_HELPER_3(dump, void, i32, i32, i32)
DEF_HELPER_0(rfe, void);
DEF_HELPER_0(rfn, void);

DEF_HELPER_2(movl_sreg_reg, void, i32, i32)
DEF_HELPER_2(movl_reg_sreg, void, i32, i32)

DEF_HELPER_0(evaluate_flags_muls, void)
DEF_HELPER_0(evaluate_flags_mulu, void)
DEF_HELPER_0(evaluate_flags_mcp, void)
DEF_HELPER_0(evaluate_flags_alu_4, void)
DEF_HELPER_0(evaluate_flags_sub_4, void)
DEF_HELPER_0(evaluate_flags_move_4, void)
DEF_HELPER_0(evaluate_flags_move_2, void)
DEF_HELPER_0(evaluate_flags, void)
DEF_HELPER_0(top_evaluate_flags, void)

#include "def-helper.h"
