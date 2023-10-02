/* Helpers */
#define LI(reg, val)           \
    mov.u reg, lo:val;         \
    movh DREG_TEMP_LI, up:val; \
    or reg, reg, DREG_TEMP_LI; \

#define LIA(reg, val)        \
    LI(DREG_TEMP, val)       \
    mov.a reg, DREG_TEMP;

/* Address definitions */
#define TESTDEV_ADDR 0xf0000000
/* Register definitions */
#define DREG_RS1 %d0
#define DREG_RS2 %d2
#define DREG_RS3 %d4
#define DREG_CALC_RESULT %d5
#define DREG_CALC_PSW %d6
#define DREG_CORRECT_PSW %d7
#define DREG_TEMP_LI %d13
#define DREG_TEMP %d14
#define DREG_TEST_NUM %d8
#define DREG_CORRECT_RESULT %d9
#define DREG_CORRECT_RESULT_2 %d10

#define AREG_ADDR %a0
#define AREG_CORRECT_RESULT %a3

#define DREG_DEV_ADDR %a15

#define EREG_RS1 %e0
#define EREG_RS1_LO %d0
#define EREG_RS1_HI %d1
#define EREG_RS2 %e2
#define EREG_RS2_LO %d2
#define EREG_RS2_HI %d3
#define EREG_CALC_RESULT %e6
#define EREG_CALC_RESULT_LO %d6
#define EREG_CALC_RESULT_HI %d7
#define EREG_CORRECT_RESULT_LO %d0
#define EREG_CORRECT_RESULT_HI %d1

/* Test case wrappers */
#define TEST_CASE(num, testreg, correct, code...) \
test_ ## num:                                     \
    code;                                         \
    LI(DREG_CORRECT_RESULT, correct)              \
    mov DREG_TEST_NUM, num;                       \
    jne testreg, DREG_CORRECT_RESULT, fail;       \
    mov testreg, 0

#define TEST_CASE_E(num, correct_lo, correct_hi, code...)  \
test_ ## num:                                              \
    code;                                                  \
    mov DREG_TEST_NUM, num;                                \
    LI(EREG_CORRECT_RESULT_LO, correct_lo)                 \
    jne EREG_CALC_RESULT_LO, EREG_CORRECT_RESULT_LO, fail; \
    LI(EREG_CORRECT_RESULT_HI, correct_hi)                 \
    jne EREG_CALC_RESULT_HI, EREG_CORRECT_RESULT_HI, fail;

#define TEST_CASE_PSW(num, testreg, correct, correct_psw, code...) \
test_ ## num:                                                      \
    code;                                                          \
    LI(DREG_CORRECT_RESULT, correct)                               \
    mov DREG_TEST_NUM, num;                                        \
    jne testreg, DREG_CORRECT_RESULT, fail;                        \
    mfcr DREG_CALC_PSW, $psw;                                      \
    LI(DREG_CORRECT_PSW, correct_psw)                              \
    mov DREG_TEST_NUM, num;                                        \
    jne DREG_CALC_PSW, DREG_CORRECT_PSW, fail;

#define TEST_LD(insn, num, result, addr_result, ld_pattern) \
test_ ## num:                                               \
    LIA(AREG_ADDR, test_data)                               \
    insn DREG_CALC_RESULT, ld_pattern;                      \
    LI(DREG_CORRECT_RESULT, result)                         \
    mov DREG_TEST_NUM, num;                                 \
    jne DREG_CALC_RESULT, DREG_CORRECT_RESULT, fail;        \
    mov.d DREG_CALC_RESULT, AREG_ADDR;                      \
    LI(DREG_CORRECT_RESULT, addr_result)                    \
    jne DREG_CALC_RESULT, DREG_CORRECT_RESULT, fail;

#define TEST_LD_SRO(insn, num, result, addr_result, ld_pattern)  \
test_ ## num:                                                    \
    LIA(AREG_ADDR, test_data)                                    \
    insn %d15, ld_pattern;                                       \
    LI(DREG_CORRECT_RESULT_2, result)                            \
    mov DREG_TEST_NUM, num;                                      \
    jne %d15, DREG_CORRECT_RESULT_2, fail;                       \
    mov.d DREG_CALC_RESULT, AREG_ADDR;                           \
    LI(DREG_CORRECT_RESULT, addr_result)                         \
    jne DREG_CALC_RESULT, DREG_CORRECT_RESULT, fail;


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

#define TEST_D_D_PSW(insn, num, result, psw, rs1)     \
    TEST_CASE_PSW(num, DREG_CALC_RESULT, result, psw, \
    LI(DREG_RS1, rs1);                                \
    rstv;                                             \
    insn DREG_CORRECT_RESULT, DREG_RS1;               \
    )

#define TEST_D_DDD(insn, num, result, rs1, rs2, rs3)        \
    TEST_CASE(num, DREG_CALC_RESULT, result,                \
    LI(DREG_RS1, rs1);                                      \
    LI(DREG_RS2, rs2);                                      \
    LI(DREG_RS3, rs3);                                      \
    rstv;                                                   \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2, DREG_RS3; \
    )

#define TEST_D_DD_PSW(insn, num, result, psw, rs1, rs2) \
    TEST_CASE_PSW(num, DREG_CALC_RESULT, result, psw,   \
    LI(DREG_RS1, rs1);                                  \
    LI(DREG_RS2, rs2);                                  \
    rstv;                                               \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2;          \
    )

#define TEST_D_DDD_PSW(insn, num, result, psw, rs1, rs2, rs3) \
    TEST_CASE_PSW(num, DREG_CALC_RESULT, result, psw,         \
    LI(DREG_RS1, rs1);                                        \
    LI(DREG_RS2, rs2);                                        \
    LI(DREG_RS3, rs3);                                        \
    rstv;                                                     \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2, DREG_RS3;      \
    )

#define TEST_D_DDI(insn, num, result, rs1, rs2, imm) \
    TEST_CASE(num, DREG_CALC_RESULT, result,         \
    LI(DREG_RS1, rs1);                               \
    LI(DREG_RS2, rs2);                               \
    rstv;                                            \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2, imm;  \
    )

#define TEST_D_DDI_PSW(insn, num, result, psw, rs1, rs2, imm) \
    TEST_CASE_PSW(num, DREG_CALC_RESULT, result, psw,         \
    LI(DREG_RS1, rs1);                                        \
    LI(DREG_RS2, rs2);                                        \
    rstv;                                                     \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2, imm;           \
    )

#define TEST_D_DIDI(insn, num, result, rs1, imm1, rs2, imm2) \
    TEST_CASE(num, DREG_CALC_RESULT, result,                 \
    LI(DREG_RS1, rs1);                                       \
    LI(DREG_RS2, rs1);                                       \
    rstv;                                                    \
    insn DREG_CALC_RESULT, DREG_RS1, imm1, DREG_RS2, imm2;   \
    )

#define TEST_D_DDII(insn, num, result, rs1, rs2, imm1, imm2) \
    TEST_CASE(num, DREG_CALC_RESULT, result,                 \
    LI(DREG_RS1, rs1);                                       \
    LI(DREG_RS2, rs2);                                       \
    rstv;                                                    \
    insn DREG_CALC_RESULT, DREG_RS1, DREG_RS2, imm1, imm2;   \
    )

#define TEST_D_DIE(insn, num, result, rs1, imm1, rs2_lo, rs2_hi)\
    TEST_CASE(num, DREG_CALC_RESULT, result,                    \
    LI(DREG_RS1, rs1);                                          \
    LI(EREG_RS2_LO, rs2_lo);                                    \
    LI(EREG_RS2_HI, rs2_hi);                                    \
    rstv;                                                       \
    insn DREG_CALC_RESULT, DREG_RS1, imm1, EREG_RS2;            \
    )

#define TEST_D_DIII(insn, num, result, rs1, imm1, imm2, imm3)\
    TEST_CASE(num, DREG_CALC_RESULT, result,                 \
    LI(DREG_RS1, rs1);                                       \
    rstv;                                                    \
    insn DREG_CALC_RESULT, DREG_RS1, imm1, imm2, imm3;       \
    )

#define TEST_E_ED(insn, num, res_hi, res_lo, rs1_hi, rs1_lo, rs2) \
    TEST_CASE_E(num, res_lo, res_hi,                              \
    LI(EREG_RS1_LO, rs1_lo);                                      \
    LI(EREG_RS1_HI, rs1_hi);                                      \
    LI(DREG_RS2, rs2);                                            \
    insn EREG_CALC_RESULT, EREG_RS1, DREG_RS2;                    \
    )

#define TEST_E_IDI(insn, num, res_hi, res_lo, imm1, rs1, imm2) \
    TEST_CASE_E(num, res_lo, res_hi,                           \
    LI(DREG_RS1, rs1);                                         \
    rstv;                                                      \
    insn EREG_CALC_RESULT, imm1, DREG_RS1, imm2;               \
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
