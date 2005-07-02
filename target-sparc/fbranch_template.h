/* FCC1:FCC0: 0 =, 1 <, 2 >, 3 u */

void OPPROTO glue(op_eval_fbne, FCC)(void)
{
// !0
    T2 = FFLAG_SET(FSR_FCC0) | FFLAG_SET(FSR_FCC1); /* L or G or U */
}

void OPPROTO glue(op_eval_fblg, FCC)(void)
{
// 1 or 2
    T2 = FFLAG_SET(FSR_FCC0) ^ FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbul, FCC)(void)
{
// 1 or 3
    T2 = FFLAG_SET(FSR_FCC0);
}

void OPPROTO glue(op_eval_fbl, FCC)(void)
{
// 1
    T2 = FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbug, FCC)(void)
{
// 2 or 3
    T2 = FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbg, FCC)(void)
{
// 2
    T2 = !FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbu, FCC)(void)
{
// 3
    T2 = FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbe, FCC)(void)
{
// 0
    T2 = !FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbue, FCC)(void)
{
// 0 or 3
    T2 = !(FFLAG_SET(FSR_FCC1) ^ FFLAG_SET(FSR_FCC0));
    FORCE_RET();
}

void OPPROTO glue(op_eval_fbge, FCC)(void)
{
// 0 or 2
    T2 = !FFLAG_SET(FSR_FCC0);
}

void OPPROTO glue(op_eval_fbuge, FCC)(void)
{
// !1
    T2 = !(FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1));
}

void OPPROTO glue(op_eval_fble, FCC)(void)
{
// 0 or 1
    T2 = !FFLAG_SET(FSR_FCC1);
}

void OPPROTO glue(op_eval_fbule, FCC)(void)
{
// !2
    T2 = !(!FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1));
}

void OPPROTO glue(op_eval_fbo, FCC)(void)
{
// !3
    T2 = !(FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1));
}

#undef FCC
#undef FFLAG_SET
