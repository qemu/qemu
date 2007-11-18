/*
 * ARMv6 integer SIMD operations.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#ifdef ARITH_GE
#define DECLARE_GE uint32_t ge = 0
#define SET_GE env->GE = ge
#else
#define DECLARE_GE do{}while(0)
#define SET_GE do{}while(0)
#endif

#define RESULT(val, n, width) \
    res |= ((uint32_t)(glue(glue(uint,width),_t))(val)) << (n * width)

void OPPROTO glue(glue(op_,PFX),add16_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD16(T0, T1, 0);
    ADD16(T0 >> 16, T1 >> 16, 1);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

void OPPROTO glue(glue(op_,PFX),add8_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD8(T0, T1, 0);
    ADD8(T0 >> 8, T1 >> 8, 1);
    ADD8(T0 >> 16, T1 >> 16, 2);
    ADD8(T0 >> 24, T1 >> 24, 3);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

void OPPROTO glue(glue(op_,PFX),sub16_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB16(T0, T1, 0);
    SUB16(T0 >> 16, T1 >> 16, 1);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

void OPPROTO glue(glue(op_,PFX),sub8_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB8(T0, T1, 0);
    SUB8(T0 >> 8, T1 >> 8, 1);
    SUB8(T0 >> 16, T1 >> 16, 2);
    SUB8(T0 >> 24, T1 >> 24, 3);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

void OPPROTO glue(glue(op_,PFX),subaddx_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD16(T0, T1, 0);
    SUB16(T0 >> 16, T1 >> 16, 1);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

void OPPROTO glue(glue(op_,PFX),addsubx_T0_T1)(void)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB16(T0, T1, 0);
    ADD16(T0 >> 16, T1 >> 16, 1);
    SET_GE;
    T0 = res;
    FORCE_RET();
}

#undef DECLARE_GE
#undef SET_GE
#undef RESULT

#undef ARITH_GE
#undef PFX
#undef ADD16
#undef SUB16
#undef ADD8
#undef SUB8
