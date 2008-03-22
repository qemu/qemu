#ifdef TARGET_ABI32
#define ADDR(x) ((x) & 0xffffffff)
#else
#define ADDR(x) (x)
#endif

#ifdef __i386__
/***                              Integer store                            ***/
void OPPROTO glue(op_std, MEMSUFFIX)(void)
{
    uint64_t tmp = ((uint64_t)T1 << 32) | (uint64_t)(T2 & 0xffffffff);

    glue(stq, MEMSUFFIX)(ADDR(T0), tmp);
}

#endif /* __i386__ */
/***                         Floating-point store                          ***/
void OPPROTO glue(op_stdf, MEMSUFFIX) (void)
{
    glue(stfq, MEMSUFFIX)(ADDR(T0), DT0);
}

/***                         Floating-point load                           ***/
void OPPROTO glue(op_lddf, MEMSUFFIX) (void)
{
    DT0 = glue(ldfq, MEMSUFFIX)(ADDR(T0));
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO glue(op_ldqf, MEMSUFFIX) (void)
{
    // XXX add 128 bit load
    CPU_QuadU u;

    u.ll.upper = glue(ldq, MEMSUFFIX)(ADDR(T0));
    u.ll.lower = glue(ldq, MEMSUFFIX)(ADDR(T0 + 8));
    QT0 = u.q;
}

void OPPROTO glue(op_stqf, MEMSUFFIX) (void)
{
    // XXX add 128 bit store
    CPU_QuadU u;

    u.q = QT0;
    glue(stq, MEMSUFFIX)(ADDR(T0), u.ll.upper);
    glue(stq, MEMSUFFIX)(ADDR(T0 + 8), u.ll.lower);
}
#endif

#undef MEMSUFFIX
