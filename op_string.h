
void OPPROTO glue(glue(op_movs, SUFFIX), STRING_SUFFIX)(void)
{
    int v, inc;
    inc = (DF << SHIFT);
    v = glue(ldu, SUFFIX)(SI_ADDR);
    glue(st, SUFFIX)(DI_ADDR, v);
    inc = (DF << SHIFT);
    INC_SI();
    INC_DI();
}

void OPPROTO glue(glue(op_rep_movs, SUFFIX), STRING_SUFFIX)(void)
{
    int v, inc;
    inc = (DF << SHIFT);
    while (CX != 0) {
        v = glue(ldu, SUFFIX)(SI_ADDR);
        glue(st, SUFFIX)(DI_ADDR, v);
        INC_SI();
        INC_DI();
        DEC_CX();
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_stos, SUFFIX), STRING_SUFFIX)(void)
{
    int inc;
    glue(st, SUFFIX)(DI_ADDR, EAX);
    inc = (DF << SHIFT);
    INC_DI();
}

void OPPROTO glue(glue(op_rep_stos, SUFFIX), STRING_SUFFIX)(void)
{
    int inc;
    inc = (DF << SHIFT);
    while (CX != 0) {
        glue(st, SUFFIX)(DI_ADDR, EAX);
        INC_DI();
        DEC_CX();
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_lods, SUFFIX), STRING_SUFFIX)(void)
{
    int v, inc;
    v = glue(ldu, SUFFIX)(SI_ADDR);
#if SHIFT == 0
    EAX = (EAX & ~0xff) | v;
#elif SHIFT == 1
    EAX = (EAX & ~0xffff) | v;
#else
    EAX = v;
#endif
    inc = (DF << SHIFT);
    INC_SI();
}

/* don't know if it is used */
void OPPROTO glue(glue(op_rep_lods, SUFFIX), STRING_SUFFIX)(void)
{
    int v, inc;
    inc = (DF << SHIFT);
    while (CX != 0) {
        v = glue(ldu, SUFFIX)(SI_ADDR);
#if SHIFT == 0
        EAX = (EAX & ~0xff) | v;
#elif SHIFT == 1
        EAX = (EAX & ~0xffff) | v;
#else
        EAX = v;
#endif
        INC_SI();
        DEC_CX();
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_scas, SUFFIX), STRING_SUFFIX)(void)
{
    int v, inc;

    v = glue(ldu, SUFFIX)(DI_ADDR);
    inc = (DF << SHIFT);
    INC_DI();
    CC_SRC = EAX;
    CC_DST = EAX - v;
}

void OPPROTO glue(glue(op_repz_scas, SUFFIX), STRING_SUFFIX)(void)
{
    int v1, v2, inc;

    if (CX != 0) {
        /* NOTE: the flags are not modified if CX == 0 */
        v1 = EAX & DATA_MASK;
        inc = (DF << SHIFT);
        do {
            v2 = glue(ldu, SUFFIX)(DI_ADDR);
            INC_DI();
            DEC_CX();
            if (v1 != v2)
                break;
        } while (CX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_repnz_scas, SUFFIX), STRING_SUFFIX)(void)
{
    int v1, v2, inc;

    if (CX != 0) {
        /* NOTE: the flags are not modified if CX == 0 */
        v1 = EAX & DATA_MASK;
        inc = (DF << SHIFT);
        do {
            v2 = glue(ldu, SUFFIX)(DI_ADDR);
            INC_DI();
            DEC_CX();
            if (v1 == v2)
                break;
        } while (CX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_cmps, SUFFIX), STRING_SUFFIX)(void)
{
    int v1, v2, inc;
    v1 = glue(ldu, SUFFIX)(SI_ADDR);
    v2 = glue(ldu, SUFFIX)(DI_ADDR);
    inc = (DF << SHIFT);
    INC_SI();
    INC_DI();
    CC_SRC = v1;
    CC_DST = v1 - v2;
}

void OPPROTO glue(glue(op_repz_cmps, SUFFIX), STRING_SUFFIX)(void)
{
    int v1, v2, inc;
    if (CX != 0) {
        inc = (DF << SHIFT);
        do {
            v1 = glue(ldu, SUFFIX)(SI_ADDR);
            v2 = glue(ldu, SUFFIX)(DI_ADDR);
            INC_SI();
            INC_DI();
            DEC_CX();
            if (v1 != v2)
                break;
        } while (CX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_repnz_cmps, SUFFIX), STRING_SUFFIX)(void)
{
    int v1, v2, inc;
    if (CX != 0) {
        inc = (DF << SHIFT);
        do {
            v1 = glue(ldu, SUFFIX)(SI_ADDR);
            v2 = glue(ldu, SUFFIX)(DI_ADDR);
            INC_SI();
            INC_DI();
            DEC_CX();
            if (v1 == v2)
                break;
        } while (CX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_outs, SUFFIX), STRING_SUFFIX)(void)
{
    int v, dx, inc;
    dx = EDX & 0xffff;
    v = glue(ldu, SUFFIX)(SI_ADDR);
    glue(cpu_x86_out, SUFFIX)(env, dx, v);
    inc = (DF << SHIFT);
    INC_SI();
}

void OPPROTO glue(glue(op_rep_outs, SUFFIX), STRING_SUFFIX)(void)
{
    int v, dx, inc;
    inc = (DF << SHIFT);
    dx = EDX & 0xffff;
    while (CX != 0) {
        v = glue(ldu, SUFFIX)(SI_ADDR);
        glue(cpu_x86_out, SUFFIX)(env, dx, v);
        INC_SI();
        DEC_CX();
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_ins, SUFFIX), STRING_SUFFIX)(void)
{
    int v, dx, inc;
    dx = EDX & 0xffff;
    v = glue(cpu_x86_in, SUFFIX)(env, dx);
    glue(st, SUFFIX)(DI_ADDR, v);
    inc = (DF << SHIFT);
    INC_DI();
}

void OPPROTO glue(glue(op_rep_ins, SUFFIX), STRING_SUFFIX)(void)
{
    int v, dx, inc;
    inc = (DF << SHIFT);
    dx = EDX & 0xffff;
    while (CX != 0) {
        v = glue(cpu_x86_in, SUFFIX)(env, dx);
        glue(st, SUFFIX)(DI_ADDR, v);
        INC_DI();
        DEC_CX();
    }
    FORCE_RET();
}

#undef STRING_SUFFIX
#undef SI_ADDR
#undef DI_ADDR
#undef INC_SI
#undef INC_DI
#undef CX
#undef DEC_CX
