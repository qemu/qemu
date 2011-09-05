#include "def-helper.h"

DEF_HELPER_1(exception, void, i32)
DEF_HELPER_2(exception_cause, void, i32, i32)
DEF_HELPER_3(exception_cause_vaddr, void, i32, i32, i32)
DEF_HELPER_1(nsa, i32, i32)
DEF_HELPER_1(nsau, i32, i32)
DEF_HELPER_1(wsr_windowbase, void, i32)
DEF_HELPER_3(entry, void, i32, i32, i32)
DEF_HELPER_1(retw, i32, i32)
DEF_HELPER_1(rotw, void, i32)
DEF_HELPER_2(window_check, void, i32, i32)
DEF_HELPER_0(restore_owb, void)
DEF_HELPER_1(movsp, void, i32)
DEF_HELPER_1(wsr_lbeg, void, i32)
DEF_HELPER_1(wsr_lend, void, i32)
DEF_HELPER_1(simcall, void, env)
DEF_HELPER_0(dump_state, void)

#include "def-helper.h"
