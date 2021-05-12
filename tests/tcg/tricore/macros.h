/* Helpers */
#define LI(reg, val)           \
    mov.u reg, lo:val;         \
    movh DREG_TEMP_LI, up:val; \
    or reg, reg, DREG_TEMP_LI; \

/* Address definitions */
#define TESTDEV_ADDR 0xf0000000
/* Register definitions */
#define DREG_RS1 %d0
#define DREG_CALC_RESULT %d1
#define DREG_TEMP_LI %d10
#define DREG_TEMP %d11
#define DREG_TEST_NUM %d14
#define DREG_CORRECT_RESULT %d15

#define DREG_DEV_ADDR %a15

/* Test case wrappers */
#define TEST_CASE(num, testreg, correct, code...) \
test_ ## num:                                     \
    code;                                         \
    LI(DREG_CORRECT_RESULT, correct)              \
    mov DREG_TEST_NUM, num;                       \
    jne testreg, DREG_CORRECT_RESULT, fail        \

/* Actual test case type
 * e.g inst %dX, %dY      -> TEST_D_D
 *     inst %dX, %dY, %dZ -> TEST_D_DD
 *     inst %eX, %dY, %dZ -> TEST_E_DD
 */
#define TEST_D_D(insn, num, result, rs1)      \
    TEST_CASE(num, DREG_CALC_RESULT, result,  \
    LI(DREG_RS1, rs1);                        \
    insn DREG_CALC_RESULT, DREG_RS1;          \
    )

/* Pass/Fail handling part */
#define TEST_PASSFAIL                       \
        j pass;                             \
fail:                                       \
        LI(DREG_TEMP, TESTDEV_ADDR)         \
        mov.a DREG_DEV_ADDR, DREG_TEMP;     \
        st.w [DREG_DEV_ADDR], DREG_TEST_NUM;\
        debug;                              \
        j fail;                             \
pass:                                       \
        LI(DREG_TEMP, TESTDEV_ADDR)         \
        mov.a DREG_DEV_ADDR, DREG_TEMP;     \
        mov DREG_TEST_NUM, 0;               \
        st.w [DREG_DEV_ADDR], DREG_TEST_NUM;\
        debug;                              \
        j pass;
