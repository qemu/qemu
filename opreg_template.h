/* templates for various register related operations */

void OPPROTO glue(op_movl_A0,REGNAME)(void)
{
    A0 = REG;
}

void OPPROTO glue(op_addl_A0,REGNAME)(void)
{
    A0 += REG;
}

void OPPROTO glue(glue(op_addl_A0,REGNAME),_s1)(void)
{
    A0 += REG << 1;
}

void OPPROTO glue(glue(op_addl_A0,REGNAME),_s2)(void)
{
    A0 += REG << 2;
}

void OPPROTO glue(glue(op_addl_A0,REGNAME),_s3)(void)
{
    A0 += REG << 3;
}

void OPPROTO glue(op_movl_T0,REGNAME)(void)
{
    T0 = REG;
}

void OPPROTO glue(op_movl_T1,REGNAME)(void)
{
    T1 = REG;
}

void OPPROTO glue(op_movh_T0,REGNAME)(void)
{
    T0 = REG >> 8;
}

void OPPROTO glue(op_movh_T1,REGNAME)(void)
{
    T1 = REG >> 8;
}

void OPPROTO glue(glue(op_movl,REGNAME),_T0)(void)
{
    REG = T0;
}

void OPPROTO glue(glue(op_movl,REGNAME),_T1)(void)
{
    REG = T1;
}

void OPPROTO glue(glue(op_movl,REGNAME),_A0)(void)
{
    REG = A0;
}

/* NOTE: T0 high order bits are ignored */
void OPPROTO glue(glue(op_movw,REGNAME),_T0)(void)
{
    REG = (REG & 0xffff0000) | (T0 & 0xffff);
}

/* NOTE: T0 high order bits are ignored */
void OPPROTO glue(glue(op_movw,REGNAME),_T1)(void)
{
    REG = (REG & 0xffff0000) | (T1 & 0xffff);
}

/* NOTE: A0 high order bits are ignored */
void OPPROTO glue(glue(op_movw,REGNAME),_A0)(void)
{
    REG = (REG & 0xffff0000) | (A0 & 0xffff);
}

/* NOTE: T0 high order bits are ignored */
void OPPROTO glue(glue(op_movb,REGNAME),_T0)(void)
{
    REG = (REG & 0xffffff00) | (T0 & 0xff);
}

/* NOTE: T0 high order bits are ignored */
void OPPROTO glue(glue(op_movh,REGNAME),_T0)(void)
{
    REG = (REG & 0xffff00ff) | ((T0 & 0xff) << 8);
}

/* NOTE: T1 high order bits are ignored */
void OPPROTO glue(glue(op_movb,REGNAME),_T1)(void)
{
    REG = (REG & 0xffffff00) | (T1 & 0xff);
}

/* NOTE: T1 high order bits are ignored */
void OPPROTO glue(glue(op_movh,REGNAME),_T1)(void)
{
    REG = (REG & 0xffff00ff) | ((T1 & 0xff) << 8);
}
