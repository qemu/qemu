
void glue(glue(test_, OP), b)(int op0, int op1) 
{
    int res, s1, s0, flags;
    s0 = op0;
    s1 = op1;
    res = s0;
    flags = 0;
    asm ("push %4\n\t"
         "popf\n\t"
         stringify(OP)"b %b2\n\t" 
         "pushf\n\t"
         "popl %1\n\t"
         : "=a" (res), "=g" (flags)
         : "q" (s1), "0" (res), "1" (flags));
    printf("%-10s A=%08x B=%08x R=%08x CC=%04x\n",
           stringify(OP) "b", s0, s1, res, flags & CC_MASK);
}

void glue(glue(test_, OP), w)(int op0h, int op0, int op1) 
{
    int res, s1, flags, resh;
    s1 = op1;
    resh = op0h;
    res = op0;
    flags = 0;
    asm ("push %5\n\t"
         "popf\n\t"
         stringify(OP) "w %w3\n\t" 
         "pushf\n\t"
         "popl %1\n\t"
         : "=a" (res), "=g" (flags), "=d" (resh)
         : "q" (s1), "0" (res), "1" (flags), "2" (resh));
    printf("%-10s AH=%08x AL=%08x B=%08x RH=%08x RL=%08x CC=%04x\n",
           stringify(OP) "w", op0h, op0, s1, resh, res, flags & CC_MASK);
}

void glue(glue(test_, OP), l)(int op0h, int op0, int op1) 
{
    int res, s1, flags, resh;
    s1 = op1;
    resh = op0h;
    res = op0;
    flags = 0;
    asm ("push %5\n\t"
         "popf\n\t"
         stringify(OP) "l %3\n\t" 
         "pushf\n\t"
         "popl %1\n\t"
         : "=a" (res), "=g" (flags), "=d" (resh)
         : "q" (s1), "0" (res), "1" (flags), "2" (resh));
    printf("%-10s AH=%08x AL=%08x B=%08x RH=%08x RL=%08x CC=%04x\n",
           stringify(OP) "l", op0h, op0, s1, resh, res, flags & CC_MASK);
}

#undef OP
