void glue(do_lsw, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    if (loglevel > 0) {
        fprintf(logfile, "%s: addr=0x%08x count=%d reg=%d\n",
                __func__, T0, T1, dst);
    }
    for (; T1 > 3; T1 -= 4, T0 += 4) {
        ugpr(dst++) = glue(ldl, MEMSUFFIX)(T0);
        if (dst == 32)
            dst = 0;
    }
    if (T1 > 0) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(ldub, MEMSUFFIX)(T0) << sh;
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
        glue(stl, MEMSUFFIX)(T0, ugpr(src++));
        if (src == 32)
            src = 0;
    }
    if (T1 > 0) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(stb, MEMSUFFIX)(T0, (ugpr(src) >> sh) & 0xFF);
    }
}

#undef MEMSUFFIX
