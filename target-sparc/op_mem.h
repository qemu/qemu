/***                             Integer load                              ***/
#define SPARC_LD_OP(name, qp)                                                 \
void OPPROTO glue(glue(op_, name), MEMSUFFIX)(void)                           \
{                                                                             \
    T1 = glue(qp, MEMSUFFIX)((void *)T0);                                     \
}

#define SPARC_ST_OP(name, op)                                                 \
void OPPROTO glue(glue(op_, name), MEMSUFFIX)(void)                           \
{                                                                             \
    glue(op, MEMSUFFIX)((void *)T0, T1);                                      \
}

SPARC_LD_OP(ld, ldl);
SPARC_LD_OP(ldub, ldub);
SPARC_LD_OP(lduh, lduw);
SPARC_LD_OP(ldsb, ldsb);
SPARC_LD_OP(ldsh, ldsw);

/***                              Integer store                            ***/
SPARC_ST_OP(st, stl);
SPARC_ST_OP(stb, stb);
SPARC_ST_OP(sth, stw);

void OPPROTO glue(op_std, MEMSUFFIX)(void)
{
    glue(stl, MEMSUFFIX)((void *) T0, T1);
    glue(stl, MEMSUFFIX)((void *) (T0 + 4), T2);
}

void OPPROTO glue(op_ldstub, MEMSUFFIX)(void)
{
    T1 = glue(ldub, MEMSUFFIX)((void *) T0);
    glue(stb, MEMSUFFIX)((void *) T0, 0xff);     /* XXX: Should be Atomically */
}

void OPPROTO glue(op_swap, MEMSUFFIX)(void)
{
    unsigned int tmp = glue(ldl, MEMSUFFIX)((void *) T0);
    glue(stl, MEMSUFFIX)((void *) T0, T1);       /* XXX: Should be Atomically */
    T1 = tmp;
}

void OPPROTO glue(op_ldd, MEMSUFFIX)(void)
{
#if 1
    T1 = glue(ldl, MEMSUFFIX)((void *) T0);
    T0 = glue(ldl, MEMSUFFIX)((void *) (T0 + 4));
#else
    glue(do_ldd, MEMSUFFIX)(T0);
#endif
}

/***                         Floating-point store                          ***/
void OPPROTO glue(op_stf, MEMSUFFIX) (void)
{
    glue(stfl, MEMSUFFIX)((void *) T0, FT0);
}

void OPPROTO glue(op_stdf, MEMSUFFIX) (void)
{
    glue(stfq, MEMSUFFIX)((void *) T0, DT0);
}

/***                         Floating-point load                           ***/
void OPPROTO glue(op_ldf, MEMSUFFIX) (void)
{
    FT0 = glue(ldfl, MEMSUFFIX)((void *) T0);
}

void OPPROTO glue(op_lddf, MEMSUFFIX) (void)
{
    DT0 = glue(ldfq, MEMSUFFIX)((void *) T0);
}
#undef MEMSUFFIX
