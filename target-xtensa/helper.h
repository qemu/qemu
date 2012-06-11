#include "def-helper.h"

DEF_HELPER_2(exception, noreturn, env, i32)
DEF_HELPER_3(exception_cause, noreturn, env, i32, i32)
DEF_HELPER_4(exception_cause_vaddr, noreturn, env, i32, i32, i32)
DEF_HELPER_3(debug_exception, noreturn, env, i32, i32)

DEF_HELPER_FLAGS_1(nsa, TCG_CALL_CONST | TCG_CALL_PURE, i32, i32)
DEF_HELPER_FLAGS_1(nsau, TCG_CALL_CONST | TCG_CALL_PURE, i32, i32)
DEF_HELPER_2(wsr_windowbase, void, env, i32)
DEF_HELPER_4(entry, void, env, i32, i32, i32)
DEF_HELPER_2(retw, i32, env, i32)
DEF_HELPER_2(rotw, void, env, i32)
DEF_HELPER_3(window_check, void, env, i32, i32)
DEF_HELPER_1(restore_owb, void, env)
DEF_HELPER_2(movsp, void, env, i32)
DEF_HELPER_2(wsr_lbeg, void, env, i32)
DEF_HELPER_2(wsr_lend, void, env, i32)
DEF_HELPER_1(simcall, void, env)
DEF_HELPER_1(dump_state, void, env)

DEF_HELPER_3(waiti, void, env, i32, i32)
DEF_HELPER_3(timer_irq, void, env, i32, i32)
DEF_HELPER_2(advance_ccount, void, env, i32)
DEF_HELPER_1(check_interrupts, void, env)

DEF_HELPER_2(wsr_rasid, void, env, i32)
DEF_HELPER_FLAGS_3(rtlb0, TCG_CALL_CONST | TCG_CALL_PURE, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(rtlb1, TCG_CALL_CONST | TCG_CALL_PURE, i32, env, i32, i32)
DEF_HELPER_3(itlb, void, env, i32, i32)
DEF_HELPER_3(ptlb, i32, env, i32, i32)
DEF_HELPER_4(wtlb, void, env, i32, i32, i32)

DEF_HELPER_2(wsr_ibreakenable, void, env, i32)
DEF_HELPER_3(wsr_ibreaka, void, env, i32, i32)
DEF_HELPER_3(wsr_dbreaka, void, env, i32, i32)
DEF_HELPER_3(wsr_dbreakc, void, env, i32, i32)

#include "def-helper.h"
