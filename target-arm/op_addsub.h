/*
 * ARMv6 integer SIMD operations.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#ifdef ARITH_GE
#define GE_ARG , void *gep
#define DECLARE_GE uint32_t ge = 0
#define SET_GE *(uint32_t *)gep = ge
#else
#define GE_ARG
#define DECLARE_GE do{}while(0)
#define SET_GE do{}while(0)
#endif

#define RESULT(val, n, width) \
    res |= ((uint32_t)(glue(glue(uint,width),_t))(val)) << (n * width)

uint32_t HELPER(glue(PFX,add16))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD16(a, b, 0);
    ADD16(a >> 16, b >> 16, 1);
    SET_GE;
    return res;
}

uint32_t HELPER(glue(PFX,add8))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD8(a, b, 0);
    ADD8(a >> 8, b >> 8, 1);
    ADD8(a >> 16, b >> 16, 2);
    ADD8(a >> 24, b >> 24, 3);
    SET_GE;
    return res;
}

uint32_t HELPER(glue(PFX,sub16))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB16(a, b, 0);
    SUB16(a >> 16, b >> 16, 1);
    SET_GE;
    return res;
}

uint32_t HELPER(glue(PFX,sub8))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB8(a, b, 0);
    SUB8(a >> 8, b >> 8, 1);
    SUB8(a >> 16, b >> 16, 2);
    SUB8(a >> 24, b >> 24, 3);
    SET_GE;
    return res;
}

uint32_t HELPER(glue(PFX,subaddx))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    ADD16(a, b, 0);
    SUB16(a >> 16, b >> 16, 1);
    SET_GE;
    return res;
}

uint32_t HELPER(glue(PFX,addsubx))(uint32_t a, uint32_t b GE_ARG)
{
    uint32_t res = 0;
    DECLARE_GE;

    SUB16(a, b, 0);
    ADD16(a >> 16, b >> 16, 1);
    SET_GE;
    return res;
}

#undef GE_ARG
#undef DECLARE_GE
#undef SET_GE
#undef RESULT

#undef ARITH_GE
#undef PFX
#undef ADD16
#undef SUB16
#undef ADD8
#undef SUB8
