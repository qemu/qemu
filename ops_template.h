
#define DATA_BITS (1 << (3 + SHIFT))
#define SHIFT_MASK (DATA_BITS - 1)
#define SIGN_MASK (1 << (DATA_BITS - 1))

#if DATA_BITS == 8
#define SUFFIX b
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#define DATA_MASK 0xff
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#define DATA_MASK 0xffff
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#define DATA_STYPE int32_t
#define DATA_MASK 0xffffffff
#else
#error unhandled operand size
#endif

/* dynamic flags computation */

static int glue(compute_all_add, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_DST - CC_SRC;
    cf = (DATA_TYPE)CC_DST < (DATA_TYPE)src1;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_add, SUFFIX)(void)
{
    int src1, cf;
    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST < (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_adc, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_DST - CC_SRC - 1;
    cf = (DATA_TYPE)CC_DST <= (DATA_TYPE)src1;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_adc, SUFFIX)(void)
{
    int src1, cf;
    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST <= (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_sub, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;
    cf = (DATA_TYPE)src1 < (DATA_TYPE)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sub, SUFFIX)(void)
{
    int src1, src2, cf;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;
    cf = (DATA_TYPE)src1 < (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_sbb, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST - 1;
    cf = (DATA_TYPE)src1 <= (DATA_TYPE)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sbb, SUFFIX)(void)
{
    int src1, src2, cf;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST - 1;
    cf = (DATA_TYPE)src1 <= (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_logic, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    cf = 0;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = 0;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_logic, SUFFIX)(void)
{
    return 0;
}

static int glue(compute_all_inc, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_DST - 1;
    src2 = 1;
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = ((CC_DST & DATA_MASK) == SIGN_MASK) << 11;
    return cf | pf | af | zf | sf | of;
}

#if DATA_BITS == 32
static int glue(compute_c_inc, SUFFIX)(void)
{
    return CC_SRC;
}
#endif

static int glue(compute_all_dec, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_DST + 1;
    src2 = 1;
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = ((CC_DST & DATA_MASK) == ((uint32_t)SIGN_MASK - 1)) << 11;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_shl, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    cf = (CC_SRC >> (DATA_BITS - 1)) & CC_C;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    /* of is defined if shift count == 1 */
    of = lshift(CC_SRC ^ CC_DST, 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

#if DATA_BITS == 32
static int glue(compute_c_shl, SUFFIX)(void)
{
    return CC_SRC & 1;
}
#endif

static int glue(compute_all_sar, SUFFIX)(void)
{
    int cf, pf, af, zf, sf, of;
    cf = CC_SRC & 1;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    /* of is defined if shift count == 1 */
    of = lshift(CC_SRC ^ CC_DST, 12 - DATA_BITS) & CC_O; 
    return cf | pf | af | zf | sf | of;
}

/* various optimized jumps cases */

void OPPROTO glue(op_jb_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    if ((DATA_TYPE)src1 < (DATA_TYPE)src2)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

void OPPROTO glue(op_jz_sub, SUFFIX)(void)
{
    if ((DATA_TYPE)CC_DST == 0)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

void OPPROTO glue(op_jbe_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    if ((DATA_TYPE)src1 <= (DATA_TYPE)src2)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

void OPPROTO glue(op_js_sub, SUFFIX)(void)
{
    if (CC_DST & SIGN_MASK)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

void OPPROTO glue(op_jl_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    if ((DATA_STYPE)src1 < (DATA_STYPE)src2)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

void OPPROTO glue(op_jle_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    if ((DATA_STYPE)src1 <= (DATA_STYPE)src2)
        PC = PARAM1;
    else
        PC = PARAM2;
    FORCE_RET();
}

/* various optimized set cases */

void OPPROTO glue(op_setb_T0_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    T0 = ((DATA_TYPE)src1 < (DATA_TYPE)src2);
}

void OPPROTO glue(op_setz_T0_sub, SUFFIX)(void)
{
    T0 = ((DATA_TYPE)CC_DST == 0);
}

void OPPROTO glue(op_setbe_T0_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    T0 = ((DATA_TYPE)src1 <= (DATA_TYPE)src2);
}

void OPPROTO glue(op_sets_T0_sub, SUFFIX)(void)
{
    T0 = lshift(CC_DST, -(DATA_BITS - 1)) & 1;
}

void OPPROTO glue(op_setl_T0_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    T0 = ((DATA_STYPE)src1 < (DATA_STYPE)src2);
}

void OPPROTO glue(op_setle_T0_sub, SUFFIX)(void)
{
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;

    T0 = ((DATA_STYPE)src1 <= (DATA_STYPE)src2);
}

/* shifts */

void OPPROTO glue(glue(op_rol, SUFFIX), _T0_T1_cc)(void)
{
    int count, src;
    count = T1 & SHIFT_MASK;
    if (count) {
        CC_SRC = cc_table[CC_OP].compute_all() & ~(CC_O | CC_C);
        src = T0;
        T0 &= DATA_MASK;
        T0 = (T0 << count) | (T0 >> (DATA_BITS - count));
        CC_SRC |= (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) | 
            (T0 & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_ror, SUFFIX), _T0_T1_cc)(void)
{
    int count, src;
    count = T1 & SHIFT_MASK;
    if (count) {
        CC_SRC = cc_table[CC_OP].compute_all() & ~(CC_O | CC_C);
        src = T0;
        T0 &= DATA_MASK;
        T0 = (T0 >> count) | (T0 << (DATA_BITS - count));
        CC_SRC |= (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) | 
            ((T0 >> (DATA_BITS - 1)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_rcl, SUFFIX), _T0_T1_cc)(void)
{
    int count, res, eflags;
    unsigned int src;

    count = T1 & 0x1f;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = cc_table[CC_OP].compute_all();
        T0 &= DATA_MASK;
        src = T0;
        res = (T0 << count) | ((eflags & CC_C) << (count - 1));
        if (count > 1)
            res |= T0 >> (DATA_BITS + 1 - count);
        T0 = res;
        CC_SRC = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) | 
            ((src >> (DATA_BITS - count)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_rcr, SUFFIX), _T0_T1_cc)(void)
{
    int count, res, eflags;
    unsigned int src;

    count = T1 & 0x1f;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = cc_table[CC_OP].compute_all();
        T0 &= DATA_MASK;
        src = T0;
        res = (T0 >> count) | ((eflags & CC_C) << (DATA_BITS - count));
        if (count > 1)
            res |= T0 << (DATA_BITS + 1 - count);
        T0 = res;
        CC_SRC = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) | 
            ((src >> (count - 1)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shl, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = (DATA_TYPE)T0 << (count - 1);
        T0 = T0 << count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shr, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        T0 &= DATA_MASK;
        CC_SRC = T0 >> (count - 1);
        T0 = T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_sar, SUFFIX), _T0_T1_cc)(void)
{
    int count, src;
    count = T1 & 0x1f;
    if (count) {
        src = (DATA_STYPE)T0;
        CC_SRC = src >> (count - 1);
        T0 = src >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}

#if DATA_BITS == 16
/* XXX: overflow flag might be incorrect in some cases in shldw */
void OPPROTO glue(glue(op_shld, SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    unsigned int res;
    count = PARAM1;
    T1 &= 0xffff;
    res = T1 | (T0 << 16);
    CC_SRC = res >> (32 - count);
    res <<= count;
    if (count > 16)
        res |= T1 << (count - 16);
    T0 = res >> 16;
    CC_DST = T0;
}

void OPPROTO glue(glue(op_shld, SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    unsigned int res;
    count = ECX & 0x1f;
    if (count) {
        T1 &= 0xffff;
        res = T1 | (T0 << 16);
        CC_SRC = res >> (32 - count);
        res <<= count;
        if (count > 16)
          res |= T1 << (count - 16);
        T0 = res >> 16;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
}

void OPPROTO glue(glue(op_shrd, SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    unsigned int res;

    count = PARAM1;
    res = (T0 & 0xffff) | (T1 << 16);
    CC_SRC = res >> (count - 1);
    res >>= count;
    if (count > 16)
        res |= T1 << (32 - count);
    T0 = res;
    CC_DST = T0;
}


void OPPROTO glue(glue(op_shrd, SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    unsigned int res;

    count = ECX & 0x1f;
    if (count) {
        res = (T0 & 0xffff) | (T1 << 16);
        CC_SRC = res >> (count - 1);
        res >>= count;
        if (count > 16)
            res |= T1 << (32 - count);
        T0 = res;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
}
#endif

#if DATA_BITS == 32
void OPPROTO glue(glue(op_shld, SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    count = PARAM1;
    T0 &= DATA_MASK;
    T1 &= DATA_MASK;
    CC_SRC = T0 << (count - 1);
    T0 = (T0 << count) | (T1 >> (DATA_BITS - count));
    CC_DST = T0;
}

void OPPROTO glue(glue(op_shld, SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    count = ECX & 0x1f;
    if (count) {
        T0 &= DATA_MASK;
        T1 &= DATA_MASK;
        CC_SRC = T0 << (count - 1);
        T0 = (T0 << count) | (T1 >> (DATA_BITS - count));
        CC_DST = T0;
        CC_OP = CC_OP_SHLB + SHIFT;
    }
}

void OPPROTO glue(glue(op_shrd, SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    count = PARAM1;
    T0 &= DATA_MASK;
    T1 &= DATA_MASK;
    CC_SRC = T0 >> (count - 1);
    T0 = (T0 >> count) | (T1 << (DATA_BITS - count));
    CC_DST = T0;
}


void OPPROTO glue(glue(op_shrd, SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    count = ECX & 0x1f;
    if (count) {
        T0 &= DATA_MASK;
        T1 &= DATA_MASK;
        CC_SRC = T0 >> (count - 1);
        T0 = (T0 >> count) | (T1 << (DATA_BITS - count));
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
}
#endif

/* carry add/sub (we only need to set CC_OP differently) */

void OPPROTO glue(glue(op_adc, SUFFIX), _T0_T1_cc)(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    CC_SRC = T0;
    T0 = T0 + T1 + cf;
    CC_DST = T0;
    CC_OP = CC_OP_ADDB + SHIFT + cf * 3;
}

void OPPROTO glue(glue(op_sbb, SUFFIX), _T0_T1_cc)(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    CC_SRC = T0;
    T0 = T0 - T1 - cf;
    CC_DST = T0;
    CC_OP = CC_OP_SUBB + SHIFT + cf * 3;
}

/* bit operations */
#if DATA_BITS >= 16

void OPPROTO glue(glue(op_bt, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    CC_SRC = T0 >> count;
}

void OPPROTO glue(glue(op_bts, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    CC_SRC = T0 >> count;
    T0 |= (1 << count);
}

void OPPROTO glue(glue(op_btr, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    CC_SRC = T0 >> count;
    T0 &= ~(1 << count);
}

void OPPROTO glue(glue(op_btc, SUFFIX), _T0_T1_cc)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    CC_SRC = T0 >> count;
    T0 ^= (1 << count);
}

#endif

/* string operations */
/* XXX: maybe use lower level instructions to ease exception handling */

void OPPROTO glue(op_movs, SUFFIX)(void)
{
    int v;
    v = glue(ldu, SUFFIX)((void *)ESI);
    glue(st, SUFFIX)((void *)EDI, v);
    ESI += (DF << SHIFT);
    EDI += (DF << SHIFT);
}

void OPPROTO glue(op_rep_movs, SUFFIX)(void)
{
    int v, inc;
    inc = (DF << SHIFT);
    while (ECX != 0) {
        v = glue(ldu, SUFFIX)((void *)ESI);
        glue(st, SUFFIX)((void *)EDI, v);
        ESI += inc;
        EDI += inc;
        ECX--;
    }
}

void OPPROTO glue(op_stos, SUFFIX)(void)
{
    glue(st, SUFFIX)((void *)EDI, EAX);
    EDI += (DF << SHIFT);
}

void OPPROTO glue(op_rep_stos, SUFFIX)(void)
{
    int inc;
    inc = (DF << SHIFT);
    while (ECX != 0) {
        glue(st, SUFFIX)((void *)EDI, EAX);
        EDI += inc;
        ECX--;
    }
}

void OPPROTO glue(op_lods, SUFFIX)(void)
{
    int v;
    v = glue(ldu, SUFFIX)((void *)ESI);
#if SHIFT == 0
    EAX = (EAX & ~0xff) | v;
#elif SHIFT == 1
    EAX = (EAX & ~0xffff) | v;
#else
    EAX = v;
#endif
    ESI += (DF << SHIFT);
}

/* don't know if it is used */
void OPPROTO glue(op_rep_lods, SUFFIX)(void)
{
    int v, inc;
    inc = (DF << SHIFT);
    while (ECX != 0) {
        v = glue(ldu, SUFFIX)((void *)ESI);
#if SHIFT == 0
        EAX = (EAX & ~0xff) | v;
#elif SHIFT == 1
        EAX = (EAX & ~0xffff) | v;
#else
        EAX = v;
#endif
        ESI += inc;
        ECX--;
    }
}

void OPPROTO glue(op_scas, SUFFIX)(void)
{
    int v;

    v = glue(ldu, SUFFIX)((void *)EDI);
    EDI += (DF << SHIFT);
    CC_SRC = EAX;
    CC_DST = EAX - v;
}

void OPPROTO glue(op_repz_scas, SUFFIX)(void)
{
    int v1, v2, inc;

    if (ECX != 0) {
        /* NOTE: the flags are not modified if ECX == 0 */
        v1 = EAX & DATA_MASK;
        inc = (DF << SHIFT);
        do {
            v2 = glue(ldu, SUFFIX)((void *)EDI);
            EDI += inc;
            ECX--;
            if (v1 != v2)
                break;
        } while (ECX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
}

void OPPROTO glue(op_repnz_scas, SUFFIX)(void)
{
    int v1, v2, inc;

    if (ECX != 0) {
        /* NOTE: the flags are not modified if ECX == 0 */
        v1 = EAX & DATA_MASK;
        inc = (DF << SHIFT);
        do {
            v2 = glue(ldu, SUFFIX)((void *)EDI);
            EDI += inc;
            ECX--;
            if (v1 == v2)
                break;
        } while (ECX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
}

void OPPROTO glue(op_cmps, SUFFIX)(void)
{
    int v1, v2;
    v1 = glue(ldu, SUFFIX)((void *)ESI);
    v2 = glue(ldu, SUFFIX)((void *)EDI);
    ESI += (DF << SHIFT);
    EDI += (DF << SHIFT);
    CC_SRC = v1;
    CC_DST = v1 - v2;
}

void OPPROTO glue(op_repz_cmps, SUFFIX)(void)
{
    int v1, v2, inc;
    if (ECX != 0) {
        inc = (DF << SHIFT);
        do {
            v1 = glue(ldu, SUFFIX)((void *)ESI);
            v2 = glue(ldu, SUFFIX)((void *)EDI);
            ESI += inc;
            EDI += inc;
            ECX--;
            if (v1 != v2)
                break;
        } while (ECX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
}

void OPPROTO glue(op_repnz_cmps, SUFFIX)(void)
{
    int v1, v2, inc;
    if (ECX != 0) {
        inc = (DF << SHIFT);
        do {
            v1 = glue(ldu, SUFFIX)((void *)ESI);
            v2 = glue(ldu, SUFFIX)((void *)EDI);
            ESI += inc;
            EDI += inc;
            ECX--;
            if (v1 == v2)
                break;
        } while (ECX != 0);
        CC_SRC = v1;
        CC_DST = v1 - v2;
        CC_OP = CC_OP_SUBB + SHIFT;
    }
}

/* port I/O */

void OPPROTO glue(op_outs, SUFFIX)(void)
{
    int v, dx;
    dx = EDX & 0xffff;
    v = glue(ldu, SUFFIX)((void *)ESI);
    glue(cpu_x86_out, SUFFIX)(dx, v);
    ESI += (DF << SHIFT);
}

void OPPROTO glue(op_rep_outs, SUFFIX)(void)
{
    int v, dx, inc;
    inc = (DF << SHIFT);
    dx = EDX & 0xffff;
    while (ECX != 0) {
        v = glue(ldu, SUFFIX)((void *)ESI);
        glue(cpu_x86_out, SUFFIX)(dx, v);
        ESI += inc;
        ECX--;
    }
}

void OPPROTO glue(op_ins, SUFFIX)(void)
{
    int v, dx;
    dx = EDX & 0xffff;
    v = glue(cpu_x86_in, SUFFIX)(dx);
    glue(st, SUFFIX)((void *)EDI, v);
    EDI += (DF << SHIFT);
}

void OPPROTO glue(op_rep_ins, SUFFIX)(void)
{
    int v, dx, inc;
    inc = (DF << SHIFT);
    dx = EDX & 0xffff;
    while (ECX != 0) {
        v = glue(cpu_x86_in, SUFFIX)(dx);
        glue(st, SUFFIX)((void *)EDI, v);
        EDI += (DF << SHIFT);
        ECX--;
    }
}

void OPPROTO glue(glue(op_out, SUFFIX), _T0_T1)(void)
{
    glue(cpu_x86_out, SUFFIX)(T0 & 0xffff, T1 & DATA_MASK);
}

void OPPROTO glue(glue(op_in, SUFFIX), _T0_T1)(void)
{
    T1 = glue(cpu_x86_in, SUFFIX)(T0 & 0xffff);
}

#undef DATA_BITS
#undef SHIFT_MASK
#undef SIGN_MASK
#undef DATA_TYPE
#undef DATA_STYPE
#undef DATA_MASK
#undef SUFFIX
