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

#define ADDRESS_SPACE_LD(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, SUFFIX))
#define ADDRESS_SPACE_LD_INTERNAL(size) \
    glue(glue(address_space_ld, size), glue(_internal, SUFFIX))

#define ADDRESS_SPACE_ST(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, SUFFIX))
#define ADDRESS_SPACE_ST_INTERNAL(size) \
    glue(glue(address_space_st, size), glue(_internal, SUFFIX))

uint16_t ADDRESS_SPACE_LD(uw)(ARG1_DECL, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return ADDRESS_SPACE_LD_INTERNAL(m)(ARG1, MO_ENDIAN | MO_16,
                                        addr, attrs, result);
}

uint32_t ADDRESS_SPACE_LD(l)(ARG1_DECL, hwaddr addr,
                             MemTxAttrs attrs, MemTxResult *result)
{
    return ADDRESS_SPACE_LD_INTERNAL(m)(ARG1, MO_ENDIAN | MO_32,
                                        addr, attrs, result);
}

uint64_t ADDRESS_SPACE_LD(q)(ARG1_DECL, hwaddr addr,
                             MemTxAttrs attrs, MemTxResult *result)
{
    return ADDRESS_SPACE_LD_INTERNAL(m)(ARG1, MO_ENDIAN | MO_64,
                                        addr, attrs, result);
}

void ADDRESS_SPACE_ST(w)(ARG1_DECL, hwaddr addr, uint16_t val,
                         MemTxAttrs attrs, MemTxResult *result)
{
    ADDRESS_SPACE_ST_INTERNAL(m)(ARG1, MO_ENDIAN | MO_16,
                                 addr, val, attrs, result);
}

void ADDRESS_SPACE_ST(l)(ARG1_DECL, hwaddr addr, uint32_t val,
                         MemTxAttrs attrs, MemTxResult *result)
{
    ADDRESS_SPACE_ST_INTERNAL(m)(ARG1, MO_ENDIAN | MO_32,
                                 addr, val, attrs, result);
}

void ADDRESS_SPACE_ST(q)(ARG1_DECL, hwaddr addr, uint64_t val,
                         MemTxAttrs attrs, MemTxResult *result)
{
    ADDRESS_SPACE_ST_INTERNAL(m)(ARG1, MO_ENDIAN | MO_64,
                                 addr, val, attrs, result);
}

#undef ADDRESS_SPACE_LD
#undef ADDRESS_SPACE_LD_INTERNAL
#undef ADDRESS_SPACE_ST
#undef ADDRESS_SPACE_ST_INTERNAL

#undef ENDIANNESS
#undef MO_ENDIAN
