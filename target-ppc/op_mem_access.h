/*
 *  PowerPC emulation memory access helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

/* 8 bits accesses */
static always_inline target_ulong glue(ldu8, MEMSUFFIX) (target_ulong EA)
{
    return (uint8_t)glue(ldub, MEMSUFFIX)(EA);
}

static always_inline target_long glue(lds8, MEMSUFFIX) (target_ulong EA)
{
    return (int8_t)glue(ldsb, MEMSUFFIX)(EA);
}

static always_inline void glue(st8, MEMSUFFIX) (target_ulong EA, uint8_t val)
{
    glue(stb, MEMSUFFIX)(EA, val);
}

/* 16 bits accesses */
static always_inline target_ulong glue(ldu16, MEMSUFFIX) (target_ulong EA)
{
    return (uint16_t)glue(lduw, MEMSUFFIX)(EA);
}

static always_inline target_long glue(lds16, MEMSUFFIX) (target_ulong EA)
{
    return (int16_t)glue(ldsw, MEMSUFFIX)(EA);
}

static always_inline void glue(st16, MEMSUFFIX) (target_ulong EA, uint16_t val)
{
    glue(stw, MEMSUFFIX)(EA, val);
}

static always_inline target_ulong glue(ldu16r, MEMSUFFIX) (target_ulong EA)
{
    return (uint16_t)bswap16(glue(lduw, MEMSUFFIX)(EA));
}

static always_inline target_long glue(lds16r, MEMSUFFIX) (target_ulong EA)
{
    return (int16_t)bswap16(glue(lduw, MEMSUFFIX)(EA));
}

static always_inline void glue(st16r, MEMSUFFIX) (target_ulong EA, uint16_t val)
{
    glue(stw, MEMSUFFIX)(EA, bswap16(val));
}

/* 32 bits accesses */
static always_inline uint32_t glue(__ldul, MEMSUFFIX) (target_ulong EA)
{
    return (uint32_t)glue(ldl, MEMSUFFIX)(EA);
}

static always_inline int32_t glue(__ldsl, MEMSUFFIX) (target_ulong EA)
{
    return (int32_t)glue(ldl, MEMSUFFIX)(EA);
}

static always_inline target_ulong glue(ldu32, MEMSUFFIX) (target_ulong EA)
{
    return glue(__ldul, MEMSUFFIX)(EA);
}

static always_inline target_long glue(lds32, MEMSUFFIX) (target_ulong EA)
{
    return glue(__ldsl, MEMSUFFIX)(EA);
}

static always_inline void glue(st32, MEMSUFFIX) (target_ulong EA, uint32_t val)
{
    glue(stl, MEMSUFFIX)(EA, val);
}

static always_inline target_ulong glue(ldu32r, MEMSUFFIX) (target_ulong EA)
{
    return bswap32(glue(__ldul, MEMSUFFIX)(EA));
}

static always_inline target_long glue(lds32r, MEMSUFFIX) (target_ulong EA)
{
    return (int32_t)bswap32(glue(__ldul, MEMSUFFIX)(EA));
}

static always_inline void glue(st32r, MEMSUFFIX) (target_ulong EA, uint32_t val)
{
    glue(stl, MEMSUFFIX)(EA, bswap32(val));
}

/* 64 bits accesses */
static always_inline uint64_t glue(__lduq, MEMSUFFIX) (target_ulong EA)
{
    return (uint64_t)glue(ldq, MEMSUFFIX)(EA);
}

static always_inline int64_t glue(__ldsq, MEMSUFFIX) (target_ulong EA)
{
    return (int64_t)glue(ldq, MEMSUFFIX)(EA);
}

static always_inline uint64_t glue(ldu64, MEMSUFFIX) (target_ulong EA)
{
    return glue(__lduq, MEMSUFFIX)(EA);
}

static always_inline int64_t glue(lds64, MEMSUFFIX) (target_ulong EA)
{
    return glue(__ldsq, MEMSUFFIX)(EA);
}

static always_inline void glue(st64, MEMSUFFIX) (target_ulong EA, uint64_t val)
{
    glue(stq, MEMSUFFIX)(EA, val);
}

static always_inline uint64_t glue(ldu64r, MEMSUFFIX) (target_ulong EA)
{
    return bswap64(glue(__lduq, MEMSUFFIX)(EA));
}

static always_inline int64_t glue(lds64r, MEMSUFFIX) (target_ulong EA)
{
    return (int64_t)bswap64(glue(__lduq, MEMSUFFIX)(EA));
}

static always_inline void glue(st64r, MEMSUFFIX) (target_ulong EA, uint64_t val)
{
    glue(stq, MEMSUFFIX)(EA, bswap64(val));
}
