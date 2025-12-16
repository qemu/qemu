/*
 *  Physical memory access templates
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2015 Linaro, Inc.
 *  Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

static inline uint64_t glue(ldq_phys, SUFFIX)(ARG1_DECL, hwaddr addr)
{
    return glue(address_space_ldq, SUFFIX)(ARG1, addr,
                                           MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void glue(stq_phys, SUFFIX)(ARG1_DECL, hwaddr addr, uint64_t val)
{
    glue(address_space_stq, SUFFIX)(ARG1, addr, val,
                                    MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint8_t glue(ldub_phys, SUFFIX)(ARG1_DECL, hwaddr addr)
{
    return glue(address_space_ldub, SUFFIX)(ARG1, addr,
                                            MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint64_t glue(ldq_le_phys, SUFFIX)(ARG1_DECL, hwaddr addr)
{
    return glue(address_space_ldq_le, SUFFIX)(ARG1, addr,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint64_t glue(ldq_be_phys, SUFFIX)(ARG1_DECL, hwaddr addr)
{
    return glue(address_space_ldq_be, SUFFIX)(ARG1, addr,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void glue(stb_phys, SUFFIX)(ARG1_DECL, hwaddr addr, uint8_t val)
{
    glue(address_space_stb, SUFFIX)(ARG1, addr, val,
                                    MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void glue(stq_le_phys, SUFFIX)(ARG1_DECL, hwaddr addr, uint64_t val)
{
    glue(address_space_stq_le, SUFFIX)(ARG1, addr, val,
                                       MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void glue(stq_be_phys, SUFFIX)(ARG1_DECL, hwaddr addr, uint64_t val)
{
    glue(address_space_stq_be, SUFFIX)(ARG1, addr, val,
                                       MEMTXATTRS_UNSPECIFIED, NULL);
}

#define ENDIANNESS
#include "system/memory_ldst_phys_endian.h.inc"

#define ENDIANNESS  _le
#include "system/memory_ldst_phys_endian.h.inc"

#define ENDIANNESS  _be
#include "system/memory_ldst_phys_endian.h.inc"

#undef ARG1_DECL
#undef ARG1
#undef SUFFIX
