void OPPROTO glue(glue(op_ldub, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldub, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_ldsb, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldsb, MEMSUFFIX)((int8_t *)A0);
}

void OPPROTO glue(glue(op_lduw, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(lduw, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_ldsw, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldsw, MEMSUFFIX)((int8_t *)A0);
}

void OPPROTO glue(glue(op_ldl, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldl, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_ldub, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldub, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_ldsb, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldsb, MEMSUFFIX)((int8_t *)A0);
}

void OPPROTO glue(glue(op_lduw, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(lduw, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_ldsw, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldsw, MEMSUFFIX)((int8_t *)A0);
}

void OPPROTO glue(glue(op_ldl, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldl, MEMSUFFIX)((uint8_t *)A0);
}

void OPPROTO glue(glue(op_stb, MEMSUFFIX), _T0_A0)(void)
{
    glue(stb, MEMSUFFIX)((uint8_t *)A0, T0);
}

void OPPROTO glue(glue(op_stw, MEMSUFFIX), _T0_A0)(void)
{
    glue(stw, MEMSUFFIX)((uint8_t *)A0, T0);
}

void OPPROTO glue(glue(op_stl, MEMSUFFIX), _T0_A0)(void)
{
    glue(stl, MEMSUFFIX)((uint8_t *)A0, T0);
}

#if 0
void OPPROTO glue(glue(op_stb, MEMSUFFIX), _T1_A0)(void)
{
    glue(stb, MEMSUFFIX)((uint8_t *)A0, T1);
}
#endif

void OPPROTO glue(glue(op_stw, MEMSUFFIX), _T1_A0)(void)
{
    glue(stw, MEMSUFFIX)((uint8_t *)A0, T1);
}

void OPPROTO glue(glue(op_stl, MEMSUFFIX), _T1_A0)(void)
{
    glue(stl, MEMSUFFIX)((uint8_t *)A0, T1);
}

#undef MEMSUFFIX
