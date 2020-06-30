#if XCHAL_HAVE_DFP || XCHAL_HAVE_FP_DIV
#define DFPU 1
#else
#define DFPU 0
#endif

#define FCR_RM_NEAREST 0
#define FCR_RM_TRUNC   1
#define FCR_RM_CEIL    2
#define FCR_RM_FLOOR   3

#define FSR__ 0x00000000
#define FSR_I 0x00000080
#define FSR_U 0x00000100
#define FSR_O 0x00000200
#define FSR_Z 0x00000400
#define FSR_V 0x00000800

#define FSR_UI (FSR_U | FSR_I)
#define FSR_OI (FSR_O | FSR_I)

#define F32_0           0x00000000
#define F32_0_5         0x3f000000
#define F32_1           0x3f800000
#define F32_MAX         0x7f7fffff
#define F32_PINF        0x7f800000
#define F32_NINF        0xff800000

#define F32_DNAN        0x7fc00000
#define F32_SNAN(v)     (0x7f800000 | (v))
#define F32_QNAN(v)     (0x7fc00000 | (v))

#define F32_MINUS       0x80000000

#define F64_0           0x0000000000000000
#define F64_MIN_NORM    0x0010000000000000
#define F64_1           0x3ff0000000000000
#define F64_MAX_2       0x7fe0000000000000
#define F64_MAX         0x7fefffffffffffff
#define F64_PINF        0x7ff0000000000000
#define F64_NINF        0xfff0000000000000

#define F64_DNAN        0x7ff8000000000000
#define F64_SNAN(v)     (0x7ff0000000000000 | (v))
#define F64_QNAN(v)     (0x7ff8000000000000 | (v))

#define F64_MINUS       0x8000000000000000

.macro test_op1_rm op, fr0, fr1, v0, r, sr
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    \op     \fr1, \fr0
    check_res \fr1, \r, \sr
.endm

.macro test_op2_rm op, fr0, fr1, fr2, v0, v1, r, sr
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    \op     \fr2, \fr0, \fr1
    check_res \fr2, \r, \sr
.endm

.macro test_op3_rm op, fr0, fr1, fr2, fr3, v0, v1, v2, r, sr
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    movfp   \fr2, \v2
    \op     \fr0, \fr1, \fr2
    check_res \fr3, \r, \sr
.endm

.macro test_op1_ex op, fr0, fr1, v0, rm, r, sr
    movi    a2, \rm
    wur     a2, fcr
    test_op1_rm \op, \fr0, \fr1, \v0, \r, \sr
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_op1_rm \op, \fr0, \fr1, \v0, \r, \sr
.endm

.macro test_op2_ex op, fr0, fr1, fr2, v0, v1, rm, r, sr
    movi    a2, \rm
    wur     a2, fcr
    test_op2_rm \op, \fr0, \fr1, \fr2, \v0, \v1, \r, \sr
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_op2_rm \op, \fr0, \fr1, \fr2, \v0, \v1, \r, \sr
.endm

.macro test_op3_ex op, fr0, fr1, fr2, fr3, v0, v1, v2, rm, r, sr
    movi    a2, \rm
    wur     a2, fcr
    test_op3_rm \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, \r, \sr
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_op3_rm \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, \r, \sr
.endm

.macro test_op1 op, fr0, fr1, v0, r0, r1, r2, r3, sr0, sr1, sr2, sr3
    test_op1_ex \op, \fr0, \fr1, \v0, 0, \r0, \sr0
    test_op1_ex \op, \fr0, \fr1, \v0, 1, \r1, \sr1
    test_op1_ex \op, \fr0, \fr1, \v0, 2, \r2, \sr2
    test_op1_ex \op, \fr0, \fr1, \v0, 3, \r3, \sr3
.endm

.macro test_op2 op, fr0, fr1, fr2, v0, v1, r0, r1, r2, r3, sr0, sr1, sr2, sr3
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 0, \r0, \sr0
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 1, \r1, \sr1
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 2, \r2, \sr2
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 3, \r3, \sr3
.endm

.macro test_op3 op, fr0, fr1, fr2, fr3, v0, v1, v2, r0, r1, r2, r3, sr0, sr1, sr2, sr3
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 0, \r0, \sr0
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 1, \r1, \sr1
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 2, \r2, \sr2
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 3, \r3, \sr3
.endm

.macro test_op2_cpe op
    set_vector  kernel, 2f
    movi    a2, 0
    wsr     a2, cpenable
1:
    \op     f2, f0, f1
    test_fail
2:
    rsr     a2, excvaddr
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 32
    assert  eq, a2, a3

    set_vector  kernel, 0
    movi    a2, 1
    wsr     a2, cpenable
.endm
