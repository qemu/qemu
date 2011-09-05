#include "def-helper.h"

DEF_HELPER_1(exception, void, i32)
DEF_HELPER_2(exception_cause, void, i32, i32)
DEF_HELPER_3(exception_cause_vaddr, void, i32, i32, i32)
DEF_HELPER_1(nsa, i32, i32)
DEF_HELPER_1(nsau, i32, i32)

#include "def-helper.h"
