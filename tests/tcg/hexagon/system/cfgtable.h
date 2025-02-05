/*
 *  Copyright(c) 2023-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CFGTABLE_H
#define CFGTABLE_H

#include <stdint.h>

static uint32_t read_cfgtable_field(uint32_t offset)
{
    uint32_t val;
    asm volatile("r0 = cfgbase\n\t"
                 "r0 = asl(r0, #5)\n\t"
                 "%0 = memw_phys(%1, r0)\n\t"
                 : "=r"(val)
                 : "r"(offset)
                 : "r0");
    return val;
}

#define GET_SUBSYSTEM_BASE() (read_cfgtable_field(0x8) << 16)
#define GET_FASTL2VIC_BASE() (read_cfgtable_field(0x28) << 16)

static uintptr_t get_vtcm_base(void)
{
#if __HEXAGON_ARCH__ == 65
    return 0xD8200000L;
#elif __HEXAGON_ARCH__ >= 66
    int vtcm_offset = 0x038;
    return read_cfgtable_field(vtcm_offset) << 16;
#else
#error "unsupported hexagon revision"
#endif
}

#endif /* CFGTABLE_H */
