
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
        CC_SRC = v2;
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
        CC_SRC = v2;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
    FORCE_RET();
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
        CC_SRC = v2;
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
        CC_SRC = v2;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
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
