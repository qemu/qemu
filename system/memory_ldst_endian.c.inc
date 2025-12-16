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
    return ADDRESS_SPACE_LD_INTERNAL(uw)(ARG1, addr, attrs, result,
                                         DEVICE_ENDIANNESS);
}

uint32_t ADDRESS_SPACE_LD(l)(ARG1_DECL, hwaddr addr,
                             MemTxAttrs attrs, MemTxResult *result)
{
    return ADDRESS_SPACE_LD_INTERNAL(l)(ARG1, addr, attrs, result,
                                        DEVICE_ENDIANNESS);
}

void ADDRESS_SPACE_ST(w)(ARG1_DECL, hwaddr addr, uint16_t val,
                         MemTxAttrs attrs, MemTxResult *result)
{
    ADDRESS_SPACE_ST_INTERNAL(w)(ARG1, addr, val, attrs, result,
                                 DEVICE_ENDIANNESS);
}

void ADDRESS_SPACE_ST(l)(ARG1_DECL, hwaddr addr, uint32_t val,
                         MemTxAttrs attrs, MemTxResult *result)
{
    ADDRESS_SPACE_ST_INTERNAL(l)(ARG1, addr, val, attrs, result,
                                 DEVICE_ENDIANNESS);
}

#undef ADDRESS_SPACE_LD
#undef ADDRESS_SPACE_LD_INTERNAL
#undef ADDRESS_SPACE_ST
#undef ADDRESS_SPACE_ST_INTERNAL

#undef ENDIANNESS
#undef DEVICE_ENDIANNESS
