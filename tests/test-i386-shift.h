
#define exec_op glue(exec_, OP)
#define exec_opl glue(glue(exec_, OP), l)
#define exec_opw glue(glue(exec_, OP), w)
#define exec_opb glue(glue(exec_, OP), b)

#define EXECSHIFT(size, res, s1, flags) \
    asm ("push %4\n\t"\
         "popf\n\t"\
         stringify(OP) size " %%cl, %" size "0\n\t" \
         "pushf\n\t"\
         "popl %1\n\t"\
         : "=q" (res), "=g" (flags)\
         : "c" (s1), "0" (res), "1" (flags));

void exec_opl(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECSHIFT("", res, s1, flags);
    /* overflow is undefined if count != 1 */
    if (s1 != 1)
      flags &= ~CC_O;
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "l", s0, s1, res, iflags, flags & CC_MASK);
}

void exec_opw(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECSHIFT("w", res, s1, flags);
    /* overflow is undefined if count != 1 */
    if (s1 != 1)
      flags &= ~CC_O;
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "w", s0, s1, res, iflags, flags & CC_MASK);
}

void exec_opb(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECSHIFT("b", res, s1, flags);
    /* overflow is undefined if count != 1 */
    if (s1 != 1)
      flags &= ~CC_O;
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "b", s0, s1, res, iflags, flags & CC_MASK);
}

void exec_op(int s0, int s1)
{
    exec_opl(s0, s1, 0);
    exec_opw(s0, s1, 0);
    exec_opb(s0, s1, 0);
#ifdef OP_CC
    exec_opl(s0, s1, CC_C);
    exec_opw(s0, s1, CC_C);
    exec_opb(s0, s1, CC_C);
#endif
}

void glue(test_, OP)(void)
{
    int i;
    for(i = 0; i < 32; i++)
        exec_op(0x12345678, i);
    for(i = 0; i < 32; i++)
        exec_op(0x82345678, i);
}

void *glue(_test_, OP) __init_call = glue(test_, OP);

#undef OP
#undef OP_CC
