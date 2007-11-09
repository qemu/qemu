/*
 *  MIPS emulation memory micro-operations for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Standard loads and stores */
void glue(op_lb, MEMSUFFIX) (void)
{
    T0 = glue(ldsb, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_lbu, MEMSUFFIX) (void)
{
    T0 = glue(ldub, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_sb, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, T1);
    FORCE_RET();
}

void glue(op_lh, MEMSUFFIX) (void)
{
    T0 = glue(ldsw, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_lhu, MEMSUFFIX) (void)
{
    T0 = glue(lduw, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_sh, MEMSUFFIX) (void)
{
    glue(stw, MEMSUFFIX)(T0, T1);
    FORCE_RET();
}

void glue(op_lw, MEMSUFFIX) (void)
{
    T0 = glue(ldl, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_lwu, MEMSUFFIX) (void)
{
    T0 = (uint32_t)glue(ldl, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_sw, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)(T0, T1);
    FORCE_RET();
}

/* "half" load and stores.  We must do the memory access inline,
   or fault handling won't work.  */

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK(v) ((v) & 3)
#define GET_OFFSET(addr, offset) (addr + (offset))
#else
#define GET_LMASK(v) (((v) & 3) ^ 3)
#define GET_OFFSET(addr, offset) (addr - (offset))
#endif

void glue(op_lwl, MEMSUFFIX) (void)
{
    target_ulong tmp;

    tmp = glue(ldub, MEMSUFFIX)(T0);
    T1 = (T1 & 0x00FFFFFF) | (tmp << 24);

    if (GET_LMASK(T0) <= 2) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 1));
        T1 = (T1 & 0xFF00FFFF) | (tmp << 16);
    }

    if (GET_LMASK(T0) <= 1) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 2));
        T1 = (T1 & 0xFFFF00FF) | (tmp << 8);
    }

    if (GET_LMASK(T0) == 0) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 3));
        T1 = (T1 & 0xFFFFFF00) | tmp;
    }
    T1 = (int32_t)T1;
    FORCE_RET();
}

void glue(op_lwr, MEMSUFFIX) (void)
{
    target_ulong tmp;

    tmp = glue(ldub, MEMSUFFIX)(T0);
    T1 = (T1 & 0xFFFFFF00) | tmp;

    if (GET_LMASK(T0) >= 1) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -1));
        T1 = (T1 & 0xFFFF00FF) | (tmp << 8);
    }

    if (GET_LMASK(T0) >= 2) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -2));
        T1 = (T1 & 0xFF00FFFF) | (tmp << 16);
    }

    if (GET_LMASK(T0) == 3) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -3));
        T1 = (T1 & 0x00FFFFFF) | (tmp << 24);
    }
    T1 = (int32_t)T1;
    FORCE_RET();
}

void glue(op_swl, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, (uint8_t)(T1 >> 24));

    if (GET_LMASK(T0) <= 2)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 1), (uint8_t)(T1 >> 16));

    if (GET_LMASK(T0) <= 1)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 2), (uint8_t)(T1 >> 8));

    if (GET_LMASK(T0) == 0)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 3), (uint8_t)T1);

    FORCE_RET();
}

void glue(op_swr, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, (uint8_t)T1);

    if (GET_LMASK(T0) >= 1)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -1), (uint8_t)(T1 >> 8));

    if (GET_LMASK(T0) >= 2)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -2), (uint8_t)(T1 >> 16));

    if (GET_LMASK(T0) == 3)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -3), (uint8_t)(T1 >> 24));

    FORCE_RET();
}

void glue(op_ll, MEMSUFFIX) (void)
{
    T1 = T0;
    T0 = glue(ldl, MEMSUFFIX)(T0);
    env->CP0_LLAddr = T1;
    FORCE_RET();
}

void glue(op_sc, MEMSUFFIX) (void)
{
    CALL_FROM_TB0(dump_sc);
    if (T0 & 0x3) {
        env->CP0_BadVAddr = T0;
        CALL_FROM_TB1(do_raise_exception, EXCP_AdES);
    }
    if (T0 == env->CP0_LLAddr) {
        glue(stl, MEMSUFFIX)(T0, T1);
        T0 = 1;
    } else {
        T0 = 0;
    }
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
void glue(op_ld, MEMSUFFIX) (void)
{
    T0 = glue(ldq, MEMSUFFIX)(T0);
    FORCE_RET();
}

void glue(op_sd, MEMSUFFIX) (void)
{
    glue(stq, MEMSUFFIX)(T0, T1);
    FORCE_RET();
}

/* "half" load and stores.  We must do the memory access inline,
   or fault handling won't work.  */

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK64(v) ((v) & 7)
#else
#define GET_LMASK64(v) (((v) & 7) ^ 7)
#endif

void glue(op_ldl, MEMSUFFIX) (void)
{
    uint64_t tmp;

    tmp = glue(ldub, MEMSUFFIX)(T0);
    T1 = (T1 & 0x00FFFFFFFFFFFFFFULL) | (tmp << 56);

    if (GET_LMASK64(T0) <= 6) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 1));
        T1 = (T1 & 0xFF00FFFFFFFFFFFFULL) | (tmp << 48);
    }

    if (GET_LMASK64(T0) <= 5) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 2));
        T1 = (T1 & 0xFFFF00FFFFFFFFFFULL) | (tmp << 40);
    }

    if (GET_LMASK64(T0) <= 4) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 3));
        T1 = (T1 & 0xFFFFFF00FFFFFFFFULL) | (tmp << 32);
    }

    if (GET_LMASK64(T0) <= 3) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 4));
        T1 = (T1 & 0xFFFFFFFF00FFFFFFULL) | (tmp << 24);
    }

    if (GET_LMASK64(T0) <= 2) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 5));
        T1 = (T1 & 0xFFFFFFFFFF00FFFFULL) | (tmp << 16);
    }

    if (GET_LMASK64(T0) <= 1) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 6));
        T1 = (T1 & 0xFFFFFFFFFFFF00FFULL) | (tmp << 8);
    }

    if (GET_LMASK64(T0) == 0) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, 7));
        T1 = (T1 & 0xFFFFFFFFFFFFFF00ULL) | tmp;
    }

    FORCE_RET();
}

void glue(op_ldr, MEMSUFFIX) (void)
{
    uint64_t tmp;

    tmp = glue(ldub, MEMSUFFIX)(T0);
    T1 = (T1 & 0xFFFFFFFFFFFFFF00ULL) | tmp;

    if (GET_LMASK64(T0) >= 1) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -1));
        T1 = (T1 & 0xFFFFFFFFFFFF00FFULL) | (tmp  << 8);
    }

    if (GET_LMASK64(T0) >= 2) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -2));
        T1 = (T1 & 0xFFFFFFFFFF00FFFFULL) | (tmp << 16);
    }

    if (GET_LMASK64(T0) >= 3) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -3));
        T1 = (T1 & 0xFFFFFFFF00FFFFFFULL) | (tmp << 24);
    }

    if (GET_LMASK64(T0) >= 4) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -4));
        T1 = (T1 & 0xFFFFFF00FFFFFFFFULL) | (tmp << 32);
    }

    if (GET_LMASK64(T0) >= 5) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -5));
        T1 = (T1 & 0xFFFF00FFFFFFFFFFULL) | (tmp << 40);
    }

    if (GET_LMASK64(T0) >= 6) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -6));
        T1 = (T1 & 0xFF00FFFFFFFFFFFFULL) | (tmp << 48);
    }

    if (GET_LMASK64(T0) == 7) {
        tmp = glue(ldub, MEMSUFFIX)(GET_OFFSET(T0, -7));
        T1 = (T1 & 0x00FFFFFFFFFFFFFFULL) | (tmp << 56);
    }

    FORCE_RET();
}

void glue(op_sdl, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, (uint8_t)(T1 >> 56));

    if (GET_LMASK64(T0) <= 6)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 1), (uint8_t)(T1 >> 48));

    if (GET_LMASK64(T0) <= 5)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 2), (uint8_t)(T1 >> 40));

    if (GET_LMASK64(T0) <= 4)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 3), (uint8_t)(T1 >> 32));

    if (GET_LMASK64(T0) <= 3)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 4), (uint8_t)(T1 >> 24));

    if (GET_LMASK64(T0) <= 2)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 5), (uint8_t)(T1 >> 16));

    if (GET_LMASK64(T0) <= 1)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 6), (uint8_t)(T1 >> 8));

    if (GET_LMASK64(T0) <= 0)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, 7), (uint8_t)T1);

    FORCE_RET();
}

void glue(op_sdr, MEMSUFFIX) (void)
{
    glue(stb, MEMSUFFIX)(T0, (uint8_t)T1);

    if (GET_LMASK64(T0) >= 1)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -1), (uint8_t)(T1 >> 8));

    if (GET_LMASK64(T0) >= 2)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -2), (uint8_t)(T1 >> 16));

    if (GET_LMASK64(T0) >= 3)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -3), (uint8_t)(T1 >> 24));

    if (GET_LMASK64(T0) >= 4)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -4), (uint8_t)(T1 >> 32));

    if (GET_LMASK64(T0) >= 5)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -5), (uint8_t)(T1 >> 40));

    if (GET_LMASK64(T0) >= 6)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -6), (uint8_t)(T1 >> 48));

    if (GET_LMASK64(T0) == 7)
        glue(stb, MEMSUFFIX)(GET_OFFSET(T0, -7), (uint8_t)(T1 >> 56));

    FORCE_RET();
}

void glue(op_lld, MEMSUFFIX) (void)
{
    T1 = T0;
    T0 = glue(ldq, MEMSUFFIX)(T0);
    env->CP0_LLAddr = T1;
    FORCE_RET();
}

void glue(op_scd, MEMSUFFIX) (void)
{
    CALL_FROM_TB0(dump_sc);
    if (T0 & 0x7) {
        env->CP0_BadVAddr = T0;
        CALL_FROM_TB1(do_raise_exception, EXCP_AdES);
    }
    if (T0 == env->CP0_LLAddr) {
        glue(stq, MEMSUFFIX)(T0, T1);
        T0 = 1;
    } else {
        T0 = 0;
    }
    FORCE_RET();
}
#endif /* TARGET_MIPS64 */

void glue(op_lwc1, MEMSUFFIX) (void)
{
    WT0 = glue(ldl, MEMSUFFIX)(T0);
    FORCE_RET();
}
void glue(op_swc1, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)(T0, WT0);
    FORCE_RET();
}
void glue(op_ldc1, MEMSUFFIX) (void)
{
    DT0 = glue(ldq, MEMSUFFIX)(T0);
    FORCE_RET();
}
void glue(op_sdc1, MEMSUFFIX) (void)
{
    glue(stq, MEMSUFFIX)(T0, DT0);
    FORCE_RET();
}
void glue(op_luxc1, MEMSUFFIX) (void)
{
    DT0 = glue(ldq, MEMSUFFIX)(T0 & ~0x7);
    FORCE_RET();
}
void glue(op_suxc1, MEMSUFFIX) (void)
{
    glue(stq, MEMSUFFIX)(T0 & ~0x7, DT0);
    FORCE_RET();
}
