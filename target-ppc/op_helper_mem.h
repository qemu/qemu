void glue(do_lsw, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    if (loglevel > 0) {
        fprintf(logfile, "%s: addr=0x%08x count=%d reg=%d\n",
                __func__, T0, T1, dst);
    }
    for (; T1 > 3; T1 -= 4, T0 += 4) {
        ugpr(dst++) = glue(_ldl, MEMSUFFIX)((void *)T0, ACCESS_INT);
        if (dst == 32)
            dst = 0;
    }
    if (T1 > 0) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(_ldub, MEMSUFFIX)((void *)T0, ACCESS_INT) << sh;
        }
        ugpr(dst) = tmp;
    }
}

void glue(do_stsw, MEMSUFFIX) (int src)
{
    int sh;

    if (loglevel > 0) {
        fprintf(logfile, "%s: addr=0x%08x count=%d reg=%d\n",
                __func__, T0, T1, src);
    }
    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(_stl, MEMSUFFIX)((void *)T0, ugpr(src++), ACCESS_INT);
        if (src == 32)
            src = 0;
    }
    if (T1 > 0) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(_stb, MEMSUFFIX)((void *)T0, (ugpr(src) >> sh) & 0xFF,
                                  ACCESS_INT);
    }
}

#undef MEMSUFFIX
