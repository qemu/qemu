/* External helpers */
void glue(do_lsw, MEMSUFFIX) (int dst);
void glue(do_stsw, MEMSUFFIX) (int src);

static inline uint16_t glue(ld16r, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp = glue(lduw, MEMSUFFIX)(EA);
    return ((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
}

static inline int32_t glue(ld16rs, MEMSUFFIX) (target_ulong EA)
{
    int16_t tmp = glue(lduw, MEMSUFFIX)(EA);
    return ((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
}

static inline uint32_t glue(ld32r, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(EA);
    return ((tmp & 0xFF000000) >> 24) | ((tmp & 0x00FF0000) >> 8) |
        ((tmp & 0x0000FF00) << 8) | ((tmp & 0x000000FF) << 24);
}

static inline void glue(st16r, MEMSUFFIX) (target_ulong EA, uint16_t data)
{
    uint16_t tmp = ((data & 0xFF00) >> 8) | ((data & 0x00FF) << 8);
    glue(stw, MEMSUFFIX)(EA, tmp);
}

static inline void glue(st32r, MEMSUFFIX) (target_ulong EA, uint32_t data)
{
    uint32_t tmp = ((data & 0xFF000000) >> 24) | ((data & 0x00FF0000) >> 8) |
        ((data & 0x0000FF00) << 8) | ((data & 0x000000FF) << 24);
    glue(stl, MEMSUFFIX)(EA, tmp);
}

/***                             Integer load                              ***/
#define PPC_LD_OP(name, op)                                                   \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    T1 = glue(op, MEMSUFFIX)(T0);                                             \
    RETURN();                                                                 \
}

#define PPC_ST_OP(name, op)                                                   \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)(T0, T1);                                              \
    RETURN();                                                                 \
}

PPC_LD_OP(bz, ldub);
PPC_LD_OP(ha, ldsw);
PPC_LD_OP(hz, lduw);
PPC_LD_OP(wz, ldl);

PPC_LD_OP(ha_le, ld16rs);
PPC_LD_OP(hz_le, ld16r);
PPC_LD_OP(wz_le, ld32r);

/***                              Integer store                            ***/
PPC_ST_OP(b, stb);
PPC_ST_OP(h, stw);
PPC_ST_OP(w, stl);

PPC_ST_OP(h_le, st16r);
PPC_ST_OP(w_le, st32r);

/***                Integer load and store with byte reverse               ***/
PPC_LD_OP(hbr, ld16r);
PPC_LD_OP(wbr, ld32r);
PPC_ST_OP(hbr, st16r);
PPC_ST_OP(wbr, st32r);

PPC_LD_OP(hbr_le, lduw);
PPC_LD_OP(wbr_le, ldl);
PPC_ST_OP(hbr_le, stw);
PPC_ST_OP(wbr_le, stl);

/***                    Integer load and store multiple                    ***/
PPC_OP(glue(lmw, MEMSUFFIX))
{
    int dst = PARAM(1);

    for (; dst < 32; dst++, T0 += 4) {
        ugpr(dst) = glue(ldl, MEMSUFFIX)(T0);
    }
    RETURN();
}

PPC_OP(glue(stmw, MEMSUFFIX))
{
    int src = PARAM(1);

    for (; src < 32; src++, T0 += 4) {
        glue(stl, MEMSUFFIX)(T0, ugpr(src));
    }
    RETURN();
}

PPC_OP(glue(lmw_le, MEMSUFFIX))
{
    int dst = PARAM(1);

    for (; dst < 32; dst++, T0 += 4) {
        ugpr(dst) = glue(ld32r, MEMSUFFIX)(T0);
    }
    RETURN();
}

PPC_OP(glue(stmw_le, MEMSUFFIX))
{
    int src = PARAM(1);

    for (; src < 32; src++, T0 += 4) {
        glue(st32r, MEMSUFFIX)(T0, ugpr(src));
    }
    RETURN();
}

/***                    Integer load and store strings                     ***/
PPC_OP(glue(lswi, MEMSUFFIX))
{
    glue(do_lsw, MEMSUFFIX)(PARAM(1));
    RETURN();
}

void glue(do_lsw_le, MEMSUFFIX) (int dst);
PPC_OP(glue(lswi_le, MEMSUFFIX))
{
    glue(do_lsw_le, MEMSUFFIX)(PARAM(1));
    RETURN();
}

/* PPC32 specification says we must generate an exception if
 * rA is in the range of registers to be loaded.
 * In an other hand, IBM says this is valid, but rA won't be loaded.
 * For now, I'll follow the spec...
 */
PPC_OP(glue(lswx, MEMSUFFIX))
{
    if (T1 > 0) {
        if ((PARAM(1) < PARAM(2) && (PARAM(1) + T1) > PARAM(2)) ||
            (PARAM(1) < PARAM(3) && (PARAM(1) + T1) > PARAM(3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw, MEMSUFFIX)(PARAM(1));
        }
    }
    RETURN();
}

PPC_OP(glue(lswx_le, MEMSUFFIX))
{
    if (T1 > 0) {
        if ((PARAM(1) < PARAM(2) && (PARAM(1) + T1) > PARAM(2)) ||
            (PARAM(1) < PARAM(3) && (PARAM(1) + T1) > PARAM(3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_le, MEMSUFFIX)(PARAM(1));
        }
    }
    RETURN();
}

PPC_OP(glue(stsw, MEMSUFFIX))
{
    glue(do_stsw, MEMSUFFIX)(PARAM(1));
    RETURN();
}

void glue(do_stsw_le, MEMSUFFIX) (int src);
PPC_OP(glue(stsw_le, MEMSUFFIX))
{
    glue(do_stsw_le, MEMSUFFIX)(PARAM(1));
    RETURN();
}

/***                         Floating-point store                          ***/
#define PPC_STF_OP(name, op)                                                  \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)(T0, FT1);                                     \
    RETURN();                                                                 \
}

PPC_STF_OP(fd, stfq);
PPC_STF_OP(fs, stfl);

static inline void glue(stfqr, MEMSUFFIX) (target_ulong EA, double d)
{
    union {
        double d;
        uint64_t u;
    } u;

    u.d = d;
    u.u = ((u.u & 0xFF00000000000000ULL) >> 56) |
        ((u.u & 0x00FF000000000000ULL) >> 40) |
        ((u.u & 0x0000FF0000000000ULL) >> 24) |
        ((u.u & 0x000000FF00000000ULL) >> 8) |
        ((u.u & 0x00000000FF000000ULL) << 8) |
        ((u.u & 0x0000000000FF0000ULL) << 24) |
        ((u.u & 0x000000000000FF00ULL) << 40) |
        ((u.u & 0x00000000000000FFULL) << 56);
    glue(stfq, MEMSUFFIX)(EA, u.d);
}

static inline void glue(stflr, MEMSUFFIX) (target_ulong EA, float f)
{
    union {
        float f;
        uint32_t u;
    } u;

    u.f = f;
    u.u = ((u.u & 0xFF000000UL) >> 24) |
        ((u.u & 0x00FF0000ULL) >> 8) |
        ((u.u & 0x0000FF00UL) << 8) |
        ((u.u & 0x000000FFULL) << 24);
    glue(stfl, MEMSUFFIX)(EA, u.f);
}

PPC_STF_OP(fd_le, stfqr);
PPC_STF_OP(fs_le, stflr);

/***                         Floating-point load                           ***/
#define PPC_LDF_OP(name, op)                                                  \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    FT1 = glue(op, MEMSUFFIX)(T0);                                    \
    RETURN();                                                                 \
}

PPC_LDF_OP(fd, ldfq);
PPC_LDF_OP(fs, ldfl);

static inline double glue(ldfqr, MEMSUFFIX) (target_ulong EA)
{
    union {
        double d;
        uint64_t u;
    } u;

    u.d = glue(ldfq, MEMSUFFIX)(EA);
    u.u = ((u.u & 0xFF00000000000000ULL) >> 56) |
        ((u.u & 0x00FF000000000000ULL) >> 40) |
        ((u.u & 0x0000FF0000000000ULL) >> 24) |
        ((u.u & 0x000000FF00000000ULL) >> 8) |
        ((u.u & 0x00000000FF000000ULL) << 8) |
        ((u.u & 0x0000000000FF0000ULL) << 24) |
        ((u.u & 0x000000000000FF00ULL) << 40) |
        ((u.u & 0x00000000000000FFULL) << 56);

    return u.d;
}

static inline float glue(ldflr, MEMSUFFIX) (target_ulong EA)
{
    union {
        float f;
        uint32_t u;
    } u;

    u.f = glue(ldfl, MEMSUFFIX)(EA);
    u.u = ((u.u & 0xFF000000UL) >> 24) |
        ((u.u & 0x00FF0000ULL) >> 8) |
        ((u.u & 0x0000FF00UL) << 8) |
        ((u.u & 0x000000FFULL) << 24);

    return u.f;
}

PPC_LDF_OP(fd_le, ldfqr);
PPC_LDF_OP(fs_le, ldflr);

/* Load and set reservation */
PPC_OP(glue(lwarx, MEMSUFFIX))
{
    if (T0 & 0x03) {
        do_raise_exception(EXCP_ALIGN);
    } else {
       T1 = glue(ldl, MEMSUFFIX)(T0);
       regs->reserve = T0;
    }
    RETURN();
}

PPC_OP(glue(lwarx_le, MEMSUFFIX))
{
    if (T0 & 0x03) {
        do_raise_exception(EXCP_ALIGN);
    } else {
       T1 = glue(ld32r, MEMSUFFIX)(T0);
       regs->reserve = T0;
    }
    RETURN();
}

/* Store with reservation */
PPC_OP(glue(stwcx, MEMSUFFIX))
{
    if (T0 & 0x03) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (regs->reserve != T0) {
            env->crf[0] = xer_ov;
        } else {
            glue(stl, MEMSUFFIX)(T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = 0;
    RETURN();
}

PPC_OP(glue(stwcx_le, MEMSUFFIX))
{
    if (T0 & 0x03) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (regs->reserve != T0) {
            env->crf[0] = xer_ov;
        } else {
            glue(st32r, MEMSUFFIX)(T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = 0;
    RETURN();
}

PPC_OP(glue(dcbz, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)(T0 + 0x00, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x04, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x08, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x0C, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x10, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x14, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x18, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x1C, 0);
    RETURN();
}

/* External access */
PPC_OP(glue(eciwx, MEMSUFFIX))
{
    T1 = glue(ldl, MEMSUFFIX)(T0);
    RETURN();
}

PPC_OP(glue(ecowx, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)(T0, T1);
    RETURN();
}

PPC_OP(glue(eciwx_le, MEMSUFFIX))
{
    T1 = glue(ld32r, MEMSUFFIX)(T0);
    RETURN();
}

PPC_OP(glue(ecowx_le, MEMSUFFIX))
{
    glue(st32r, MEMSUFFIX)(T0, T1);
    RETURN();
}

#undef MEMSUFFIX
