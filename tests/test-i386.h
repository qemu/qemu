
#define exec_op glue(exec_, OP)
#define exec_opl glue(glue(exec_, OP), l)
#define exec_opw glue(glue(exec_, OP), w)
#define exec_opb glue(glue(exec_, OP), b)

#define EXECOP2(size, res, s1, flags) \
    asm ("push %4\n\t"\
         "popf\n\t"\
         stringify(OP) size " %" size "2, %" size "0\n\t" \
         "pushf\n\t"\
         "popl %1\n\t"\
         : "=q" (res), "=g" (flags)\
         : "q" (s1), "0" (res), "1" (flags));

#define EXECOP1(size, res, flags) \
    asm ("push %3\n\t"\
         "popf\n\t"\
         stringify(OP) size " %" size "0\n\t" \
         "pushf\n\t"\
         "popl %1\n\t"\
         : "=q" (res), "=g" (flags)\
         : "0" (res), "1" (flags));

#ifdef OP1
void exec_opl(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP1("", res, flags);
    printf("%-10s A=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "l", s0, res, iflags, flags & CC_MASK);
}

void exec_opw(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP1("w", res, flags);
    printf("%-10s A=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "w", s0, res, iflags, flags & CC_MASK);
}

void exec_opb(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP1("b", res, flags);
    printf("%-10s A=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "b", s0, res, iflags, flags & CC_MASK);
}
#else
void exec_opl(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP2("", res, s1, flags);
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "l", s0, s1, res, iflags, flags & CC_MASK);
}

void exec_opw(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP2("w", res, s1, flags);
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "w", s0, s1, res, iflags, flags & CC_MASK);
}

void exec_opb(int s0, int s1, int iflags)
{
    int res, flags;
    res = s0;
    flags = iflags;
    EXECOP2("b", res, s1, flags);
    printf("%-10s A=%08x B=%08x R=%08x CCIN=%04x CC=%04x\n",
           stringify(OP) "b", s0, s1, res, iflags, flags & CC_MASK);
}
#endif

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
    exec_op(0x12345678, 0x812FADA);
    exec_op(0x12341, 0x12341);
    exec_op(0x12341, -0x12341);
    exec_op(0xffffffff, 0);
    exec_op(0xffffffff, -1);
    exec_op(0xffffffff, 1);
    exec_op(0xffffffff, 2);
    exec_op(0x7fffffff, 0);
    exec_op(0x7fffffff, 1);
    exec_op(0x7fffffff, -1);
    exec_op(0x80000000, -1);
    exec_op(0x80000000, 1);
    exec_op(0x80000000, -2);
    exec_op(0x12347fff, 0);
    exec_op(0x12347fff, 1);
    exec_op(0x12347fff, -1);
    exec_op(0x12348000, -1);
    exec_op(0x12348000, 1);
    exec_op(0x12348000, -2);
    exec_op(0x12347f7f, 0);
    exec_op(0x12347f7f, 1);
    exec_op(0x12347f7f, -1);
    exec_op(0x12348080, -1);
    exec_op(0x12348080, 1);
    exec_op(0x12348080, -2);
}

void *glue(_test_, OP) __init_call = glue(test_, OP);

#undef OP
#undef OP_CC
