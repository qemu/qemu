/* External helpers */
void glue(do_lsw, MEMSUFFIX) (int dst);
void glue(do_stsw, MEMSUFFIX) (int src);

static inline uint16_t glue(ld16r, MEMSUFFIX) (void *EA)
{
    uint16_t tmp = glue(lduw, MEMSUFFIX)(EA);
    return ((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
}

static inline uint32_t glue(ld32r, MEMSUFFIX) (void *EA)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(EA);
    return ((tmp & 0xFF000000) >> 24) | ((tmp & 0x00FF0000) >> 8) |
        ((tmp & 0x0000FF00) << 8) | ((tmp & 0x000000FF) << 24);
}

static inline void glue(st16r, MEMSUFFIX) (void *EA, uint16_t data)
{
    uint16_t tmp = ((data & 0xFF00) >> 8) | ((data & 0x00FF) << 8);
    glue(stw, MEMSUFFIX)(EA, tmp);
}

static inline void glue(st32r, MEMSUFFIX) (void *EA, uint32_t data)
{
    uint32_t tmp = ((data & 0xFF000000) >> 24) | ((data & 0x00FF0000) >> 8) |
        ((data & 0x0000FF00) << 8) | ((data & 0x000000FF) << 24);
    glue(stl, MEMSUFFIX)(EA, tmp);
}

/***                             Integer load                              ***/
#define PPC_LD_OP(name, op)                                                   \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    T1 = glue(op, MEMSUFFIX)((void *)T0);                                     \
    RETURN();                                                                 \
}

#define PPC_ST_OP(name, op)                                                   \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)((void *)T0, T1);                                      \
    RETURN();                                                                 \
}

PPC_LD_OP(bz, ldub);
PPC_LD_OP(ha, ldsw);
PPC_LD_OP(hz, lduw);
PPC_LD_OP(wz, ldl);

/***                              Integer store                            ***/
PPC_ST_OP(b, stb);
PPC_ST_OP(h, stw);
PPC_ST_OP(w, stl);

/***                Integer load and store with byte reverse               ***/
PPC_LD_OP(hbr, ld16r);
PPC_LD_OP(wbr, ld32r);
PPC_ST_OP(hbr, st16r);
PPC_ST_OP(wbr, st32r);

/***                    Integer load and store multiple                    ***/
PPC_OP(glue(lmw, MEMSUFFIX))
{
    int dst = PARAM(1);

    for (; dst < 32; dst++, T0 += 4) {
        ugpr(dst) = glue(ldl, MEMSUFFIX)((void *)T0);
    }
    RETURN();
}

PPC_OP(glue(stmw, MEMSUFFIX))
{
    int src = PARAM(1);

    for (; src < 32; src++, T0 += 4) {
        glue(stl, MEMSUFFIX)((void *)T0, ugpr(src));
    }
    RETURN();
}

/***                    Integer load and store strings                     ***/
PPC_OP(glue(lswi, MEMSUFFIX))
{
    glue(do_lsw, MEMSUFFIX)(PARAM(1));
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

PPC_OP(glue(stsw, MEMSUFFIX))
{
    glue(do_stsw, MEMSUFFIX)(PARAM(1));
    RETURN();
}

/***                         Floating-point store                          ***/
#define PPC_STF_OP(name, op)                                                  \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)((void *)T0, FT1);                                     \
    RETURN();                                                                 \
}

PPC_STF_OP(fd, stfq);
PPC_STF_OP(fs, stfl);

/***                         Floating-point load                           ***/
#define PPC_LDF_OP(name, op)                                                  \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    FT1 = glue(op, MEMSUFFIX)((void *)T0);                                    \
    RETURN();                                                                 \
}

PPC_LDF_OP(fd, ldfq);
PPC_LDF_OP(fs, ldfl);

/* Load and set reservation */
PPC_OP(glue(lwarx, MEMSUFFIX))
{
    if (T0 & 0x03) {
        do_raise_exception(EXCP_ALIGN);
    } else {
       T1 = glue(ldl, MEMSUFFIX)((void *)T0);
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
            glue(stl, MEMSUFFIX)((void *)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = 0;
    RETURN();
}

PPC_OP(glue(dcbz, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x00), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x04), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x08), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x0C), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x10), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x14), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x18), 0);
    glue(stl, MEMSUFFIX)((void *)(T0 + 0x1C), 0);
    RETURN();
}

/* External access */
PPC_OP(glue(eciwx, MEMSUFFIX))
{
    T1 = glue(ldl, MEMSUFFIX)((void *)T0);
    RETURN();
}

PPC_OP(glue(ecowx, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)((void *)T0, T1);
    RETURN();
}

#undef MEMSUFFIX
