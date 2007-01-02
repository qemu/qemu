#undef DEBUG_OP

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK(v) ((v) & 3)
#else
#define GET_LMASK(v) (((v) & 3) ^ 3)
#endif

void glue(do_lwl, MEMSUFFIX) (uint32_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif

    switch (GET_LMASK(T0)) {
    case 0:
        T0 = (int32_t)tmp;
        break;
    case 1:
        T0 = (int32_t)((tmp << 8) | (T1 & 0x000000FF));
        break;
    case 2:
        T0 = (int32_t)((tmp << 16) | (T1 & 0x0000FFFF));
        break;
    case 3:
        T0 = (int32_t)((tmp << 24) | (T1 & 0x00FFFFFF));
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - %08x " TLSZ " => " TLSZ "\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

void glue(do_lwr, MEMSUFFIX) (uint32_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif

    switch (GET_LMASK(T0)) {
    case 0:
        T0 = (int32_t)((tmp >> 24) | (T1 & 0xFFFFFF00));
        break;
    case 1:
        T0 = (int32_t)((tmp >> 16) | (T1 & 0xFFFF0000));
        break;
    case 2:
        T0 = (int32_t)((tmp >> 8) | (T1 & 0xFF000000));
        break;
    case 3:
        T0 = (int32_t)tmp;
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - %08x " TLSZ " => " TLSZ "\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

uint32_t glue(do_swl, MEMSUFFIX) (uint32_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = tmp;
#endif

    switch (GET_LMASK(T0)) {
    case 0:
        tmp = (int32_t)T1;
        break;
    case 1:
        tmp = (int32_t)((tmp & 0xFF000000) | ((uint32_t)T1 >> 8));
        break;
    case 2:
        tmp = (int32_t)((tmp & 0xFFFF0000) | ((uint32_t)T1 >> 16));
        break;
    case 3:
        tmp = (int32_t)((tmp & 0xFFFFFF00) | ((uint32_t)T1 >> 24));
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => %08x\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
    return tmp;
}

uint32_t glue(do_swr, MEMSUFFIX) (uint32_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = tmp;
#endif

    switch (GET_LMASK(T0)) {
    case 0:
        tmp = (int32_t)((tmp & 0x00FFFFFF) | (T1 << 24));
        break;
    case 1:
        tmp = (int32_t)((tmp & 0x0000FFFF) | (T1 << 16));
        break;
    case 2:
        tmp = (int32_t)((tmp & 0x000000FF) | (T1 << 8));
        break;
    case 3:
        tmp = (int32_t)T1;
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => %08x\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
    return tmp;
}

#ifdef MIPS_HAS_MIPS64

# ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK64(v) ((v) & 4)
#else
#define GET_LMASK64(v) (((v) & 4) ^ 4)
#endif

void glue(do_ldl, MEMSUFFIX) (uint64_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif

    switch (GET_LMASK64(T0)) {
    case 0:
        T0 = tmp;
        break;
    case 1:
        T0 = (tmp << 8) | (T1 & 0x00000000000000FFULL);
        break;
    case 2:
        T0 = (tmp << 16) | (T1 & 0x000000000000FFFFULL);
        break;
    case 3:
        T0 = (tmp << 24) | (T1 & 0x0000000000FFFFFFULL);
        break;
    case 4:
        T0 = (tmp << 32) | (T1 & 0x00000000FFFFFFFFULL);
        break;
    case 5:
        T0 = (tmp << 40) | (T1 & 0x000000FFFFFFFFFFULL);
        break;
    case 6:
        T0 = (tmp << 48) | (T1 & 0x0000FFFFFFFFFFFFULL);
        break;
    case 7:
        T0 = (tmp << 56) | (T1 & 0x00FFFFFFFFFFFFFFULL);
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => " TLSZ "\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

void glue(do_ldr, MEMSUFFIX) (uint64_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = T0;
#endif

    switch (GET_LMASK64(T0)) {
    case 0:
        T0 = (tmp >> 56) | (T1 & 0xFFFFFFFFFFFFFF00ULL);
        break;
    case 1:
        T0 = (tmp >> 48) | (T1 & 0xFFFFFFFFFFFF0000ULL);
        break;
    case 2:
        T0 = (tmp >> 40) | (T1 & 0xFFFFFFFFFF000000ULL);
        break;
    case 3:
        T0 = (tmp >> 32) | (T1 & 0xFFFFFFFF00000000ULL);
        break;
    case 4:
        T0 = (tmp >> 24) | (T1 & 0xFFFFFF0000000000ULL);
        break;
    case 5:
        T0 = (tmp >> 16) | (T1 & 0xFFFF000000000000ULL);
        break;
    case 6:
        T0 = (tmp >> 8) | (T1 & 0xFF00000000000000ULL);
        break;
    case 7:
        T0 = tmp;
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => " TLSZ "\n",
                __func__, sav, tmp, T1, T0);
    }
#endif
    RETURN();
}

uint64_t glue(do_sdl, MEMSUFFIX) (uint64_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = tmp;
#endif

    switch (GET_LMASK64(T0)) {
    case 0:
        tmp = T1;
        break;
    case 1:
        tmp = (tmp & 0xFF00000000000000ULL) | (T1 >> 8);
        break;
    case 2:
        tmp = (tmp & 0xFFFF000000000000ULL) | (T1 >> 16);
        break;
    case 3:
        tmp = (tmp & 0xFFFFFF0000000000ULL) | (T1 >> 24);
        break;
    case 4:
        tmp = (tmp & 0xFFFFFFFF00000000ULL) | (T1 >> 32);
        break;
    case 5:
        tmp = (tmp & 0xFFFFFFFFFF000000ULL) | (T1 >> 40);
        break;
    case 6:
        tmp = (tmp & 0xFFFFFFFFFFFF0000ULL) | (T1 >> 48);
        break;
    case 7:
        tmp = (tmp & 0xFFFFFFFFFFFFFF00ULL) | (T1 >> 56);
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => " TLSZ "\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
    return tmp;
}

uint64_t glue(do_sdr, MEMSUFFIX) (uint64_t tmp)
{
#if defined (DEBUG_OP)
    target_ulong sav = tmp;
#endif

    switch (GET_LMASK64(T0)) {
    case 0:
        tmp = (tmp & 0x00FFFFFFFFFFFFFFULL) | (T1 << 56);
        break;
    case 1:
        tmp = (tmp & 0x0000FFFFFFFFFFFFULL) | (T1 << 48);
        break;
    case 2:
        tmp = (tmp & 0x000000FFFFFFFFFFULL) | (T1 << 40);
        break;
    case 3:
        tmp = (tmp & 0x00000000FFFFFFFFULL) | (T1 << 32);
        break;
    case 4:
        tmp = (tmp & 0x0000000000FFFFFFULL) | (T1 << 24);
        break;
    case 5:
        tmp = (tmp & 0x000000000000FFFFULL) | (T1 << 16);
        break;
    case 6:
        tmp = (tmp & 0x00000000000000FFULL) | (T1 << 8);
        break;
    case 7:
        tmp = T1;
        break;
    }
#if defined (DEBUG_OP)
    if (logfile) {
        fprintf(logfile, "%s: " TLSZ " - " TLSZ " " TLSZ " => " TLSZ "\n",
                __func__, T0, sav, T1, tmp);
    }
#endif
    RETURN();
    return tmp;
}

#endif /* MIPS_HAS_MIPS64 */
