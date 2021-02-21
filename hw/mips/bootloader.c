/*
 * Utility for QEMU MIPS to generate it's simple bootloader
 *
 * Instructions used here are carefully selected to keep compatibility with
 * MIPS Release 6.
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "hw/mips/bootloader.h"

typedef enum bl_reg {
    BL_REG_ZERO = 0,
    BL_REG_AT = 1,
    BL_REG_V0 = 2,
    BL_REG_V1 = 3,
    BL_REG_A0 = 4,
    BL_REG_A1 = 5,
    BL_REG_A2 = 6,
    BL_REG_A3 = 7,
    BL_REG_T0 = 8,
    BL_REG_T1 = 9,
    BL_REG_T2 = 10,
    BL_REG_T3 = 11,
    BL_REG_T4 = 12,
    BL_REG_T5 = 13,
    BL_REG_T6 = 14,
    BL_REG_T7 = 15,
    BL_REG_S0 = 16,
    BL_REG_S1 = 17,
    BL_REG_S2 = 18,
    BL_REG_S3 = 19,
    BL_REG_S4 = 20,
    BL_REG_S5 = 21,
    BL_REG_S6 = 22,
    BL_REG_S7 = 23,
    BL_REG_T8 = 24,
    BL_REG_T9 = 25,
    BL_REG_K0 = 26,
    BL_REG_K1 = 27,
    BL_REG_GP = 28,
    BL_REG_SP = 29,
    BL_REG_FP = 30,
    BL_REG_RA = 31,
} bl_reg;

static bool bootcpu_supports_isa(uint64_t isa_mask)
{
    return cpu_supports_isa(&MIPS_CPU(first_cpu)->env, isa_mask);
}

/* Base types */
static void bl_gen_nop(uint32_t **p)
{
    stl_p(*p, 0);
    *p = *p + 1;
}

static void bl_gen_r_type(uint32_t **p, uint8_t opcode,
                          bl_reg rs, bl_reg rt, bl_reg rd,
                          uint8_t shift, uint8_t funct)
{
    uint32_t insn = 0;

    insn = deposit32(insn, 26, 6, opcode);
    insn = deposit32(insn, 21, 5, rs);
    insn = deposit32(insn, 16, 5, rt);
    insn = deposit32(insn, 11, 5, rd);
    insn = deposit32(insn, 6, 5, shift);
    insn = deposit32(insn, 0, 6, funct);

    stl_p(*p, insn);
    *p = *p + 1;
}

static void bl_gen_i_type(uint32_t **p, uint8_t opcode,
                          bl_reg rs, bl_reg rt, uint16_t imm)
{
    uint32_t insn = 0;

    insn = deposit32(insn, 26, 6, opcode);
    insn = deposit32(insn, 21, 5, rs);
    insn = deposit32(insn, 16, 5, rt);
    insn = deposit32(insn, 0, 16, imm);

    stl_p(*p, insn);
    *p = *p + 1;
}

/* Single instructions */
static void bl_gen_dsll(uint32_t **p, bl_reg rd, bl_reg rt, uint8_t sa)
{
    if (bootcpu_supports_isa(ISA_MIPS3)) {
        bl_gen_r_type(p, 0, 0, rt, rd, sa, 0x38);
    } else {
        g_assert_not_reached(); /* unsupported */
    }
}

static void bl_gen_jalr(uint32_t **p, bl_reg rs)
{
    bl_gen_r_type(p, 0, rs, 0, BL_REG_RA, 0, 0x09);
}

static void bl_gen_lui(uint32_t **p, bl_reg rt, uint16_t imm)
{
    /* R6: It's a alias of AUI with RS = 0 */
    bl_gen_i_type(p, 0x0f, 0, rt, imm);
}

static void bl_gen_ori(uint32_t **p, bl_reg rt, bl_reg rs, uint16_t imm)
{
    bl_gen_i_type(p, 0x0d, rs, rt, imm);
}

static void bl_gen_sw(uint32_t **p, bl_reg rt, uint8_t base, uint16_t offset)
{
    bl_gen_i_type(p, 0x2b, base, rt, offset);
}

static void bl_gen_sd(uint32_t **p, bl_reg rt, uint8_t base, uint16_t offset)
{
    if (bootcpu_supports_isa(ISA_MIPS3)) {
        bl_gen_i_type(p, 0x3f, base, rt, offset);
    } else {
        g_assert_not_reached(); /* unsupported */
    }
}

/* Pseudo instructions */
static void bl_gen_li(uint32_t **p, bl_reg rt, uint32_t imm)
{
    bl_gen_lui(p, rt, extract32(imm, 16, 16));
    bl_gen_ori(p, rt, rt, extract32(imm, 0, 16));
}

static void bl_gen_dli(uint32_t **p, bl_reg rt, uint64_t imm)
{
    bl_gen_li(p, rt, extract64(imm, 32, 32));
    bl_gen_dsll(p, rt, rt, 16);
    bl_gen_ori(p, rt, rt, extract64(imm, 16, 16));
    bl_gen_dsll(p, rt, rt, 16);
    bl_gen_ori(p, rt, rt, extract64(imm, 0, 16));
}

static void bl_gen_load_ulong(uint32_t **p, bl_reg rt, target_ulong imm)
{
    if (bootcpu_supports_isa(ISA_MIPS3)) {
        bl_gen_dli(p, rt, imm); /* 64bit */
    } else {
        bl_gen_li(p, rt, imm); /* 32bit */
    }
}

/* Helpers */
void bl_gen_jump_to(uint32_t **p, target_ulong jump_addr)
{
    bl_gen_load_ulong(p, BL_REG_T9, jump_addr);
    bl_gen_jalr(p, BL_REG_T9);
    bl_gen_nop(p); /* delay slot */
}

void bl_gen_jump_kernel(uint32_t **p, target_ulong sp, target_ulong a0,
                        target_ulong a1, target_ulong a2, target_ulong a3,
                        target_ulong kernel_addr)
{
    bl_gen_load_ulong(p, BL_REG_SP, sp);
    bl_gen_load_ulong(p, BL_REG_A0, a0);
    bl_gen_load_ulong(p, BL_REG_A1, a1);
    bl_gen_load_ulong(p, BL_REG_A2, a2);
    bl_gen_load_ulong(p, BL_REG_A3, a3);

    bl_gen_jump_to(p, kernel_addr);
}

void bl_gen_write_ulong(uint32_t **p, target_ulong addr, target_ulong val)
{
    bl_gen_load_ulong(p, BL_REG_K0, val);
    bl_gen_load_ulong(p, BL_REG_K1, addr);
    bl_gen_sd(p, BL_REG_K0, BL_REG_K1, 0x0);
}

void bl_gen_write_u32(uint32_t **p, target_ulong addr, uint32_t val)
{
    bl_gen_li(p, BL_REG_K0, val);
    bl_gen_load_ulong(p, BL_REG_K1, addr);
    bl_gen_sw(p, BL_REG_K0, BL_REG_K1, 0x0);
}

void bl_gen_write_u64(uint32_t **p, target_ulong addr, uint64_t val)
{
    bl_gen_dli(p, BL_REG_K0, val);
    bl_gen_load_ulong(p, BL_REG_K1, addr);
    bl_gen_sd(p, BL_REG_K0, BL_REG_K1, 0x0);
}
