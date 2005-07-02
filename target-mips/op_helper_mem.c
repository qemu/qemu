void glue(do_lwl, MEMSUFFIX) (void)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif
    uint32_t tmp;

    tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    /* XXX: this is valid only in big-endian mode
     *      should be reverted for little-endian...
     */
    switch (T0 & 3) {
    case 0:
        T0 = tmp;
        break;
    case 1:
        T0 = (tmp << 8) | (T1 & 0x000000FF);
        break;
    case 2:
        T0 = (tmp << 16) | (T1 & 0x0000FFFF);
        break;
    case 3:
        T0 = (tmp << 24) | (T1 & 0x00FFFFFF);
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: %08x - %08x %08x => %08x\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

void glue(do_lwr, MEMSUFFIX) (void)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif
    uint32_t tmp;

    tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
    /* XXX: this is valid only in big-endian mode
     *      should be reverted for little-endian...
     */
    switch (T0 & 3) {
    case 0:
        T0 = (tmp >> 24) | (T1 & 0xFFFFFF00);
        break;
    case 1:
        T0 = (tmp >> 16) | (T1 & 0xFFFF0000);
        break;
    case 2:
        T0 = (tmp >> 8) | (T1 & 0xFF000000);
        break;
    case 3:
        T0 = tmp;
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: %08x - %08x %08x => %08x\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

void glue(do_swl, MEMSUFFIX) (void)
{
#if defined (DEBUG_OP)
    target_ulong sav;
#endif
    uint32_t tmp;

    tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
#if defined (DEBUG_OP)
    sav = tmp;
#endif
    /* XXX: this is valid only in big-endian mode
     *      should be reverted for little-endian...
     */
    switch (T0 & 3) {
    case 0:
        tmp = T1;
        break;
    case 1:
        tmp = (tmp & 0xFF000000) | (T1 >> 8);
        break;
    case 2:
        tmp = (tmp & 0xFFFF0000) | (T1 >> 16);
        break;
    case 3:
        tmp = (tmp & 0xFFFFFF00) | (T1 >> 24);
        break;
    }
    glue(stl, MEMSUFFIX)(T0 & ~3, tmp);
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: %08x - %08x %08x => %08x\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
}

void glue(do_swr, MEMSUFFIX) (void)
{
#if defined (DEBUG_OP)
    target_ulong sav;
#endif
    uint32_t tmp;

    tmp = glue(ldl, MEMSUFFIX)(T0 & ~3);
#if defined (DEBUG_OP)
    sav = tmp;
#endif
    /* XXX: this is valid only in big-endian mode
     *      should be reverted for little-endian...
     */
    switch (T0 & 3) {
    case 0:
        tmp = (tmp & 0x00FFFFFF) | (T1 << 24);
        break;
    case 1:
        tmp = (tmp & 0x0000FFFF) | (T1 << 16);
        break;
    case 2:
        tmp = (tmp & 0x000000FF) | (T1 << 8);
        break;
    case 3:
        tmp = T1;
        break;
    }
    glue(stl, MEMSUFFIX)(T0 & ~3, tmp);
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: %08x - %08x %08x => %08x\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
}
