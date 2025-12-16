/*
 *  Physical memory access endian templates
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2015 Linaro, Inc.
 *  Copyright (c) 2016 Red Hat, Inc.
 *  Copyright (c) 2025 Linaro Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define LD_PHYS(size) \
    glue(glue(ld, size), glue(ENDIANNESS, glue(_phys, SUFFIX)))
#define ADDRESS_SPACE_LD(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, SUFFIX))

#define ST_PHYS(size) \
    glue(glue(st, size), glue(ENDIANNESS, glue(_phys, SUFFIX)))
#define ADDRESS_SPACE_ST(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, SUFFIX))

static inline uint16_t LD_PHYS(uw)(ARG1_DECL, hwaddr addr)
{
    return ADDRESS_SPACE_LD(uw)(ARG1, addr, MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint32_t LD_PHYS(l)(ARG1_DECL, hwaddr addr)
{
    return ADDRESS_SPACE_LD(l)(ARG1, addr, MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint64_t LD_PHYS(q)(ARG1_DECL, hwaddr addr)
{
    return ADDRESS_SPACE_LD(q)(ARG1, addr, MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void ST_PHYS(w)(ARG1_DECL, hwaddr addr, uint16_t val)
{
    ADDRESS_SPACE_ST(w)(ARG1, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void ST_PHYS(l)(ARG1_DECL, hwaddr addr, uint32_t val)
{
    ADDRESS_SPACE_ST(l)(ARG1, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline void ST_PHYS(q)(ARG1_DECL, hwaddr addr, uint64_t val)
{
    ADDRESS_SPACE_ST(q)(ARG1, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
}

#undef LD_PHYS
#undef ST_PHYS
#undef ADDRESS_SPACE_LD
#undef ADDRESS_SPACE_ST

#undef ENDIANNESS
