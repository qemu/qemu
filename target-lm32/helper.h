#include "def-helper.h"

DEF_HELPER_1(raise_exception, void, i32)
DEF_HELPER_0(hlt, void)
DEF_HELPER_1(wcsr_im, void, i32)
DEF_HELPER_1(wcsr_ip, void, i32)
DEF_HELPER_1(wcsr_jtx, void, i32)
DEF_HELPER_1(wcsr_jrx, void, i32)
DEF_HELPER_0(rcsr_im, i32)
DEF_HELPER_0(rcsr_ip, i32)
DEF_HELPER_0(rcsr_jtx, i32)
DEF_HELPER_0(rcsr_jrx, i32)

#include "def-helper.h"
