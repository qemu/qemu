/*
 * DSP56300 emulator
 *
 * Copyright (c) 2015 espes
 *
 * Adapted from Hatari DSP M56001 emulation
 * (C) 2003-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "dsp_cpu.h"

#define TRACE_DSP_DISASM 0
#define TRACE_DSP_DISASM_REG 0
#define TRACE_DSP_DISASM_MEM 0

#define DPRINTF(s, ...) printf(s, ## __VA_ARGS__)

#define BITMASK(x)  ((1<<(x))-1)
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

// #define DSP_COUNT_IPS     /* Count instruction per seconds */


/**********************************
 *  Defines
 **********************************/

#define SIGN_PLUS  0
#define SIGN_MINUS 1

/**********************************
 *  Functions
 **********************************/

static void dsp_postexecute_update_pc(dsp_core_t* dsp);
static void dsp_postexecute_interrupts(dsp_core_t* dsp);

static uint32_t read_memory_p(dsp_core_t* dsp, uint32_t address);
static uint32_t read_memory_disasm(dsp_core_t* dsp, int space, uint32_t address);

static void write_memory_raw(dsp_core_t* dsp, int space, uint32_t address, uint32_t value);
static void write_memory_disasm(dsp_core_t* dsp, int space, uint32_t address, uint32_t value);

static void dsp_write_reg(dsp_core_t* dsp, uint32_t numreg, uint32_t value); 

static void dsp_stack_push(dsp_core_t* dsp, uint32_t curpc, uint32_t cursr, uint16_t sshOnly);
static void dsp_stack_pop(dsp_core_t* dsp, uint32_t *curpc, uint32_t *cursr);
static void dsp_compute_ssh_ssl(dsp_core_t* dsp);

/* 56bits arithmetic */
static uint16_t dsp_abs56(uint32_t *dest);
static uint16_t dsp_asl56(uint32_t *dest, int n);
static uint16_t dsp_asr56(uint32_t *dest, int n);
static uint16_t dsp_add56(uint32_t *source, uint32_t *dest);
static uint16_t dsp_sub56(uint32_t *source, uint32_t *dest);
static void dsp_mul56(uint32_t source1, uint32_t source2, uint32_t *dest, uint8_t signe);
static void dsp_rnd56(dsp_core_t* dsp, uint32_t *dest);
static uint32_t dsp_signextend(int bits, uint32_t v);

static const dsp_interrupt_t dsp_interrupt[12] = {
    {DSP_INTER_RESET    ,   0x00, 0, "Reset"},
    {DSP_INTER_ILLEGAL  ,   0x3e, 0, "Illegal"},
    {DSP_INTER_STACK_ERROR  ,   0x02, 0, "Stack Error"},
    {DSP_INTER_TRACE    ,   0x04, 0, "Trace"},
    {DSP_INTER_SWI      ,   0x06, 0, "Swi"},
    {DSP_INTER_HOST_COMMAND ,   0xff, 1, "Host Command"},
    {DSP_INTER_HOST_RCV_DATA,   0x20, 1, "Host receive"},
    {DSP_INTER_HOST_TRX_DATA,   0x22, 1, "Host transmit"},
    {DSP_INTER_SSI_RCV_DATA_E,  0x0e, 2, "SSI receive with exception"},
    {DSP_INTER_SSI_RCV_DATA ,   0x0c, 2, "SSI receive"},
    {DSP_INTER_SSI_TRX_DATA_E,  0x12, 2, "SSI transmit with exception"},
    {DSP_INTER_SSI_TRX_DATA ,   0x10, 2, "SSI tramsmit"}
};

static const int registers_tcc[16][2] = {
    {DSP_REG_B,DSP_REG_A},
    {DSP_REG_A,DSP_REG_B},
    {DSP_REG_NULL,DSP_REG_NULL},
    {DSP_REG_NULL,DSP_REG_NULL},

    {DSP_REG_NULL,DSP_REG_NULL},
    {DSP_REG_NULL,DSP_REG_NULL},
    {DSP_REG_NULL,DSP_REG_NULL},
    {DSP_REG_NULL,DSP_REG_NULL},

    {DSP_REG_X0,DSP_REG_A},
    {DSP_REG_X0,DSP_REG_B},
    {DSP_REG_Y0,DSP_REG_A},
    {DSP_REG_Y0,DSP_REG_B},

    {DSP_REG_X1,DSP_REG_A},
    {DSP_REG_X1,DSP_REG_B},
    {DSP_REG_Y1,DSP_REG_A},
    {DSP_REG_Y1,DSP_REG_B}
};

static const int registers_mask[64] = {
    0, 0, 0, 0,
    24, 24, 24, 24,
    24, 24, 8, 8,
    24, 24, 24, 24,
    
    16, 16, 16, 16,
    16, 16, 16, 16,
    16, 16, 16, 16,
    16, 16, 16, 16,
    
    16, 16, 16, 16,
    16, 16, 16, 16,
    0, 0, 0, 0,
    0, 0, 0, 0,

    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 16, 8, 6,
    16, 16, 16, 16
};

#include "dsp_emu.inl"

#include "dsp_dis.inl"

typedef bool (*match_func_t)(uint32_t op);

typedef struct OpcodeEntry {
    const char* template;
    const char* name;
    dis_func_t dis_func;
    emu_func_t emu_func;
    match_func_t match_func;
} OpcodeEntry;

static bool match_MMMRRR(uint32_t op)
{
    uint32_t RRR = (op >> 8) & BITMASK(3);
    uint32_t MMM = (op >> 11) & BITMASK(3);
    if (MMM == 0x6) {
        return RRR == 0x0 || RRR == 0x4;
    }
    return true;
}

static const OpcodeEntry nonparallel_opcodes[] = {
    { "0000000101iiiiii1000d000", "add #xx, D", dis_add_imm, emu_add_imm },
    { "00000001010000001100d000", "add #xxxx, D", dis_add_long, emu_add_long },
    { "0000000101iiiiii1000d110", "and #xx, D", dis_and_imm, emu_and_imm },
    { "00000001010000001100d110", "and #xxxx, D", dis_and_long, emu_and_long },
    { "00000000iiiiiiii101110EE", "andi #xx, D", dis_andi, emu_andi },
    { "0000110000011101SiiiiiiD", "asl #ii, S2, D", dis_asl_imm, emu_asl_imm },
    { "0000110000011110010SsssD", "asl S1, S2, D", NULL, NULL },
    { "0000110000011100SiiiiiiD", "asr #ii, S2, D", dis_asr_imm, emu_asr_imm },
    { "0000110000011110011SsssD", "asr S1, S2, D", NULL, NULL },
    { "00001101000100000100CCCC", "bcc xxxx", dis_bcc_long, emu_bcc_long }, //??
    { "00000101CCCC01aaaa0aaaaa", "bcc xxx", dis_bcc_imm, emu_bcc_imm },
    { "0000110100011RRR0100CCCC", "bcc Rn", NULL, NULL },
    { "0000101101MMMRRR0S00bbbb", "bchg #n, [X or Y]:ea", dis_bchg_ea, emu_bchg_ea, match_MMMRRR },
    { "0000101100aaaaaa0S00bbbb", "bchg #n, [X or Y]:aa", dis_bchg_aa, emu_bchg_aa },
    { "0000101110pppppp0S00bbbb", "bchg #n, [X or Y]:pp", dis_bchg_pp, emu_bchg_pp },
    { "0000000101qqqqqq0S0bbbbb", "bchg #n, [X or Y]:qq", NULL, NULL },
    { "0000101111DDDDDD010bbbbb", "bchg, #n, D", dis_bchg_reg, emu_bchg_reg },
    { "0000101001MMMRRR0S00bbbb", "bclr #n, [X or Y]:ea", dis_bclr_ea, emu_bclr_ea, match_MMMRRR },
    { "0000101000aaaaaa0S00bbbb", "bclr #n, [X or Y]:aa", dis_bclr_aa, emu_bclr_aa },
    { "0000101010pppppp0S00bbbb", "bclr #n, [X or Y]:pp", dis_bclr_pp, emu_bclr_pp },
    { "0000000100qqqqqq0S00bbbb", "bclr #n, [X or Y]:qq", NULL, NULL },
    { "0000101011DDDDDD010bbbbb", "bclr #n, D", dis_bclr_reg, emu_bclr_reg },
    { "000011010001000011000000", "bra xxxx", dis_bra_long, emu_bra_long },
    { "00000101000011aaaa0aaaaa", "bra xxx", dis_bra_imm, emu_bra_imm },
    { "0000110100011RRR11000000", "bra Rn", NULL, NULL },
    { "0000110010MMMRRR0S0bbbbb", "brclr #n, [X or Y]:ea, xxxx", NULL, NULL, match_MMMRRR },
    { "0000110010aaaaaa1S0bbbbb", "brclr #n, [X or Y]:aa, xxxx", NULL, NULL },
    { "0000110011pppppp0S0bbbbb", "brclr #n, [X or Y]:pp, xxxx", dis_brclr_pp, emu_brclr_pp },
    { "0000010010qqqqqq0S0bbbbb", "brclr #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000110011DDDDDD100bbbbb", "brclr #n, S, xxxx", dis_brclr_reg, emu_brclr_reg },
    { "00000000000000100001CCCC", "brkcc", NULL, NULL },
    { "0000110010MMMRRR0S1bbbbb", "brset #n, [X or Y]:ea, xxxx", NULL, NULL, match_MMMRRR },
    { "0000110010aaaaaa1S1bbbbb", "brset #n, [X or Y]:aa, xxxx", NULL, NULL },
    { "0000110011pppppp0S1bbbbb", "brset #n, [X or Y]:pp, xxxx", dis_brset_pp, emu_brset_pp },
    { "0000010010qqqqqq0S1bbbbb", "brset #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000110011DDDDDD101bbbbb", "brset #n, S, xxxx", dis_brset_reg, emu_brset_reg },
    { "00001101000100000000CCCC", "bscc xxxx", NULL, NULL },
    { "00000101CCCC00aaaa0aaaaa", "bscc xxx", NULL, NULL },
    { "0000110100011RRR0000CCCC", "bscc Rn", NULL, NULL },
    { "0000110110MMMRRR0S0bbbbb", "bsclr #n, [X or Y]:ea, xxxx", NULL, NULL, match_MMMRRR },
    { "0000110110aaaaaa1S0bbbbb", "bsclr #n, [X or Y]:aa, xxxx", NULL, NULL },
    { "0000010010qqqqqq1S0bbbbb", "bsclr #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000110111pppppp0S0bbbbb", "bsclr #n, [X or Y]:pp, xxxx", NULL, NULL },
    { "0000110111DDDDDD100bbbbb", "bsclr, #n, S, xxxx", NULL, NULL },
    { "0000101001MMMRRR0S1bbbbb", "bset #n, [X or Y]:ea", dis_bset_ea, emu_bset_ea, match_MMMRRR },
    { "0000101000aaaaaa0S1bbbbb", "bset #n, [X or Y]:aa", dis_bset_aa, emu_bset_aa },
    { "0000101010pppppp0S1bbbbb", "bset #n, [X or Y]:pp", dis_bset_pp, emu_bset_pp },
    { "0000000100qqqqqq0S1bbbbb", "bset #n, [X or Y]:qq", NULL, NULL },
    { "0000101011DDDDDD011bbbbb", "bset, #n, D", dis_bset_reg, emu_bset_reg },
    { "000011010001000010000000", "bsr xxxx", dis_bsr_long, emu_bsr_long },
    { "00000101000010aaaa0aaaaa", "bsr xxx", dis_bsr_imm, emu_bsr_imm },
    { "0000110100011RRR10000000", "bsr Rn", NULL, NULL },
    { "0000110110MMMRRR0S1bbbbb", "bsset #n, [X or Y]:ea, xxxx", NULL, NULL, match_MMMRRR },
    { "0000110110aaaaaa1S1bbbbb", "bsset #n, [X or Y]:aa, xxxx", NULL, NULL },
    { "0000110111pppppp0S1bbbbb", "bsset #n, [X or Y]:pp, xxxx", NULL, NULL },
    { "0000010010qqqqqq1S1bbbbb", "bsset #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000110111DDDDDD101bbbbb", "bsset #n, S, xxxx", NULL, NULL },
    { "0000101101MMMRRR0S10bbbb", "btst #n, [X or Y]:ea", dis_btst_ea, emu_btst_ea, match_MMMRRR },
    { "0000101100aaaaaa0S10bbbb", "btst #n, [X or Y]:aa", dis_btst_aa, emu_btst_aa },
    { "0000101110pppppp0S10bbbb", "btst #n, [X or Y]:pp", dis_btst_pp, emu_btst_pp },
    { "0000000101qqqqqq0S10bbbb", "btst #n, [X or Y]:qq", NULL, NULL },
    { "0000101111DDDDDD0110bbbb", "btst #n, D", dis_btst_reg, emu_btst_reg },
    { "0000110000011110000000SD", "clb S, D", NULL, NULL },
    { "0000000101iiiiii1000d101", "cmp #xx, S2", dis_cmp_imm, emu_cmp_imm },
    { "00000001010000001100d101", "cmp #xxxx, S2", dis_cmp_long, emu_cmp_long },
    { "00001100000111111111gggd", "cmpu S1, S2", dis_cmpu, emu_cmpu },
    { "000000000000001000000000", "debug", NULL, NULL },
    { "00000000000000110000CCCC", "debugcc", NULL, NULL },
    { "00000000000000000000101d", "dec D", NULL, NULL, /*dis_dec, emu_dec*/ },
    { "000000011000000001JJd000", "div S, D", dis_div, emu_div },
    { "000000010010010s1sdkQQQQ", "dmac S1, S2, D", NULL, NULL },
    { "0000011001MMMRRR0S000000", "do [X or Y]:ea, expr", dis_do_ea, emu_do_ea, match_MMMRRR },
    { "0000011000aaaaaa0S000000", "do [X or Y]:aa, expr", dis_do_aa, emu_do_aa },
    { "00000110iiiiiiii1000hhhh", "do #xxx, expr", dis_do_imm, emu_do_imm },
    { "0000011011DDDDDD00000000", "do S, expr", dis_do_reg, emu_do_reg },
    { "000000000000001000000011", "do_f", NULL, NULL },
    { "0000011001MMMRRR0S010000", "dor [X or Y]:ea, label", NULL, NULL, match_MMMRRR },
    { "0000011000aaaaaa0S010000", "dor [X or Y]:aa, label", NULL, NULL },
    { "00000110iiiiiiii1001hhhh", "dor #xxx, label", dis_dor_imm, emu_dor_imm },
    { "0000011011DDDDDD00010000", "dor S, label", dis_dor_reg, emu_dor_reg },
    { "000000000000001000000010", "dor_f", NULL, NULL },
    { "000000000000000010001100", "enddo", NULL, emu_enddo },
    { "0000000101iiiiii1000d011", "eor #xx, D", NULL, NULL },
    { "00000001010000001100d011", "eor #xxxx, D", NULL, NULL },
    { "0000110000011010000sSSSD", "extract S1, S2, D", NULL, NULL },
    { "0000110000011000000s000D", "extract #CO, S2, D", NULL, NULL },
    { "0000110000011010100sSSSD", "extractu S1, S2, D", NULL, NULL },
    { "0000110000011000100s000D", "extractu #CO, S2, D", NULL, NULL },
    { "000000000000000000000101", "ill", NULL, emu_illegal },
    { "00000000000000000000100d", "inc D", NULL, NULL },
    { "00001100000110110qqqSSSD", "insert S1, S2, D", NULL, NULL },
    { "00001100000110010qqq000D", "insert #CO, S2, D", NULL, NULL },
    { "00001110CCCCaaaaaaaaaaaa", "jcc xxx", dis_jcc_imm, emu_jcc_imm },
    { "0000101011MMMRRR1010CCCC", "jcc ea", dis_jcc_ea, emu_jcc_ea, match_MMMRRR },
    { "0000101001MMMRRR1S00bbbb", "jclr #n, [X or Y]:ea, xxxx", dis_jclr_ea, emu_jclr_ea, match_MMMRRR },
    { "0000101000aaaaaa1S00bbbb", "jclr #n, [X or Y]:aa, xxxx", dis_jclr_aa, emu_jclr_aa },
    { "0000101010pppppp1S00bbbb", "jclr #n, [X or Y]:pp, xxxx", dis_jclr_pp, emu_jclr_pp },
    { "0000000110qqqqqq1S00bbbb", "jclr #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000101011DDDDDD0000bbbb", "jclr #n, S, xxxx", dis_jclr_reg, emu_jclr_reg },
    { "0000101011MMMRRR10000000", "jmp ea", dis_jmp_ea, emu_jmp_ea, match_MMMRRR },
    { "000011000000aaaaaaaaaaaa", "jmp xxx", dis_jmp_imm, emu_jmp_imm },
    { "00001111CCCCaaaaaaaaaaaa", "jscc xxx", dis_jscc_imm, emu_jscc_imm },
    { "0000101111MMMRRR1010CCCC", "jscc ea", dis_jscc_ea, emu_jscc_ea, match_MMMRRR },
    { "0000101101MMMRRR1S00bbbb", "jsclr #n, [X or Y]:ea, xxxx", dis_jsclr_ea, emu_jsclr_ea, match_MMMRRR },
    { "0000101100MMMRRR1S00bbbb", "jsclr #n, [X or Y]:aa, xxxx", dis_jsclr_aa, emu_jsclr_aa, match_MMMRRR },
    { "0000101110pppppp1S0bbbbb", "jsclr #n, [X or Y]:pp, xxxx", dis_jsclr_pp, emu_jsclr_pp },
    { "0000000111qqqqqq1S0bbbbb", "jsclr #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000101111DDDDDD000bbbbb", "jsclr #n, S, xxxx", dis_jsclr_reg, emu_jsclr_reg },
    { "0000101001MMMRRR1S10bbbb", "jset #n, [X or Y]:ea, xxxx", dis_jset_ea, emu_jset_ea, match_MMMRRR },
    { "0000101000MMMRRR1S10bbbb", "jset #n, [X or Y]:aa, xxxx", dis_jset_aa, emu_jset_aa, match_MMMRRR },
    { "0000101010pppppp1S10bbbb", "jset #n, [X or Y]:pp, xxxx", dis_jset_pp, emu_jset_pp },
    { "0000000110qqqqqq1S10bbbb", "jset #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000101011DDDDDD0010bbbb", "jset #n, S, xxxx", dis_jset_reg, emu_jset_reg },
    { "0000101111MMMRRR10000000", "jsr ea", dis_jsr_ea, emu_jsr_ea, match_MMMRRR },
    { "000011010000aaaaaaaaaaaa", "jsr xxx", dis_jsr_imm, emu_jsr_imm },
    { "0000101101MMMRRR1S10bbbb", "jsset #n, [X or Y]:ea, xxxx", dis_jsset_ea, emu_jsset_ea, match_MMMRRR },
    { "0000101100aaaaaa1S10bbbb", "jsset #n, [X or Y]:aa, xxxx", dis_jsset_aa, emu_jsset_aa },
    { "0000101110pppppp1S1bbbbb", "jsset #n, [X or Y]:pp, xxxx", dis_jsset_pp, emu_jsset_pp },
    { "0000000111qqqqqq1S1bbbbb", "jsset #n, [X or Y]:qq, xxxx", NULL, NULL },
    { "0000101111DDDDDD001bbbbb", "jsset #n, S, xxxx", dis_jsset_reg, emu_jsset_reg },
    { "0000010011000RRR000ddddd", "lra Rn, D", NULL, NULL },
    { "0000010001000000010ddddd", "lra xxxx, D", NULL, NULL },
    { "000011000001111010iiiiiD", "lsl #ii, D", NULL, NULL },
    { "00001100000111100001sssD", "lsl S, D", NULL, NULL },
    { "000011000001111011iiiiiD", "lsr #ii, D", NULL, NULL },
    { "00001100000111100011sssD", "lsr S, D", NULL, NULL },
    { "00000100010MMRRR000ddddd", "lua ea, D", dis_lua, emu_lua },
    { "0000010000aaaRRRaaaadddd", "lua (Rn + aa), D", dis_lua_rel, emu_lua_rel },
    { "00000001000sssss11QQdk10", "mac S, #n, D", NULL, NULL },
    { "000000010100000111qqdk10", "maci #xxxx, S, D", NULL, NULL },
    { "00000001001001101sdkQQQQ", "mac_s_u S1, S2, D", NULL, NULL },
    { "00000001000sssss11QQdk11", "macr S1, S2, D", NULL, NULL },
    { "000000010100000111qqdk11", "macri #xxxx, S, D", NULL, NULL },
    { "00001100000110111000sssD", "merge S, D", NULL, NULL },
    { "0000101001110RRR1WDDDDDD", "move X:(Rn + xxxx) <-> R", dis_move_x_long, emu_move_x_long },
    { "0000101101110RRR1WDDDDDD", "move Y:(Rn + xxxx) <-> R", NULL, NULL },
    { "0000001aaaaaaRRR1a0WDDDD", "move X:(Rn + xxx) <-> R", dis_move_x_imm, emu_move_x_imm },
    { "0000001aaaaaaRRR1a1WDDDD", "move Y:(Rn + xxx) <-> R", dis_move_y_imm, emu_move_y_imm },
    { "00000101W1MMMRRR0s1ddddd", "movec [X or Y]:ea <-> R", dis_movec_ea, emu_movec_ea, match_MMMRRR },
    { "00000101W0aaaaaa0s1ddddd", "movec [X or Y]:aa <-> R", dis_movec_aa, emu_movec_aa, match_MMMRRR },
    { "00000100W1eeeeee101ddddd", "movec R1, R2", dis_movec_reg, emu_movec_reg },
    { "00000101iiiiiiii101ddddd", "movec #xx, D1", dis_movec_imm, emu_movec_imm },
    { "00000111W1MMMRRR10dddddd", "movem P:ea <-> R", dis_movem_ea, emu_movem_ea, match_MMMRRR },
    { "00000111W0aaaaaa00dddddd", "movem P:ea <-> R", dis_movem_aa, emu_movem_aa, match_MMMRRR },
    { "0000100sW1MMMRRR1Spppppp", "movep [X or Y]:ea <-> [X or Y]:pp", dis_movep_23, emu_movep_23, match_MMMRRR },
    { "00000111W1MMMRRR0Sqqqqqq", "movep [X or Y]:ea <-> X:qq", dis_movep_x_qq, emu_movep_x_qq, match_MMMRRR },
    { "00000111W0MMMRRR1Sqqqqqq", "movep [X or Y]:ea <-> Y:qq", NULL, NULL, match_MMMRRR },
    { "0000100sW1MMMRRR01pppppp", "movep [X or Y]:pp <-> P:ea", dis_movep_1, emu_movep_1, match_MMMRRR },
    { "000000001WMMMRRR0sqqqqqq", "movep [X or Y]:qq <-> P:ea", NULL, NULL, match_MMMRRR },
    { "0000100sW1dddddd00pppppp", "movep [X or Y]:pp <-> R", dis_movep_0, emu_movep_0 },
    { "00000100W1dddddd1q0qqqqq", "movep X:qq <-> R", NULL, NULL },
    { "00000100W1dddddd0q1qqqqq", "movep Y:qq <-> R", NULL, NULL },
    { "00000001000sssss11QQdk00", "mpy S, #n, D", NULL, NULL },
    { "00000001001001111sdkQQQQ", "mpy_s_u S1, S2, D", NULL, NULL },
    { "000000010100000111qqdk00", "mpyi #xxxx, S, D", dis_mpyi, emu_mpyi },
    { "00000001000sssss11QQdk01", "mpyr S, #n, D", NULL, NULL },
    { "000000010100000111qqdk01", "mpyri #xxxx, S, D", NULL, NULL },
    { "000000000000000000000000", "nop", NULL, emu_nop},
    { "0000000111011RRR0001d101", "norm Rn, D", dis_norm, emu_norm },
    { "00001100000111100010sssD", "normf S, D", NULL, NULL },
    { "0000000101iiiiii1000d010", "or #xx, D", NULL, NULL },
    { "00000001010000001100d010", "or #xxxx, D", dis_or_long, emu_or_long },
    { "00000000iiiiiiii111110EE", "ori #xx, D", dis_ori, emu_ori },
    { "000000000000000000000011", "pflush", NULL, NULL },
    { "000000000000000000000001", "pflushun", NULL, NULL },
    { "000000000000000000000010", "pfree", NULL, NULL },
    { "0000101111MMMRRR10000001", "plock ea", NULL, NULL, match_MMMRRR },
    { "000000000000000000001111", "plockr xxxx", NULL, NULL },
    { "0000101011MMMRRR10000001", "punlock ea", NULL, NULL, match_MMMRRR },
    { "000000000000000000001110", "punlockr xxxx", NULL, NULL },
    { "0000011001MMMRRR0S100000", "rep [X or Y]:ea", dis_rep_ea, emu_rep_ea, match_MMMRRR },
    { "0000011000aaaaaa0S100000", "rep [X or Y]:aa", dis_rep_aa, emu_rep_aa },
    { "00000110iiiiiiii1010hhhh", "rep #xxx", dis_rep_imm, emu_rep_imm },
    { "0000011011dddddd00100000", "rep S", dis_rep_reg, emu_rep_reg },
    { "000000000000000010000100", "reset", NULL, emu_reset },
    { "000000000000000000000100", "rti", NULL, emu_rti },
    { "000000000000000000001100", "rts", NULL, emu_rts },
    { "000000000000000010000111", "stop", NULL, emu_stop },
    { "0000000101iiiiii1000d100", "sub #xx, D", dis_sub_imm, emu_sub_imm },
    { "00000001010000001100d100", "sub #xxxx, D", dis_sub_long, emu_sub_long },
    { "00000010CCCC00000JJJd000", "tcc S1, D1", dis_tcc, emu_tcc },
    { "00000011CCCC0ttt0JJJdTTT", "tcc S1,D2 S2,D2", dis_tcc, emu_tcc },
    { "00000010CCCC1ttt00000TTT", "tcc S2, D2", dis_tcc, emu_tcc },
    { "000000000000000000000110", "trap", NULL, NULL },
    { "00000000000000000001CCCC", "trapcc", NULL, NULL },
    { "0000101S11MMMRRR110i0000", "vsl", NULL, NULL, match_MMMRRR },
    { "000000000000000010000110", "wait", NULL, emu_wait },
};

static bool matches_initialised;
static uint32_t nonparallel_matches[ARRAYSIZE(nonparallel_opcodes)][2];

/**********************************
 *  Emulator kernel
 **********************************/

void dsp56k_reset_cpu(dsp_core_t* dsp)
{
    int i;
    if (!matches_initialised) {
        matches_initialised = true;
        for (i=0; i<ARRAYSIZE(nonparallel_opcodes); i++) {
            const OpcodeEntry t = nonparallel_opcodes[i];
            assert(strlen(t.template) == 24);

            uint32_t mask = 0;
            uint32_t match = 0;
            int j;
            for (j=0; j<24; j++) {
                if (t.template[j] == '0' || t.template[j] == '1') {
                    mask |= 1 << (24-j-1);
                    match |= (t.template[j] - '0') << (24-j-1);
                }
            }

            nonparallel_matches[i][0] = mask;
            nonparallel_matches[i][1] = match;
        }
    }

    /* Memory */
    memset(dsp->periph, 0, sizeof(dsp->periph));
    memset(dsp->stack, 0, sizeof(dsp->stack));
    memset(dsp->registers, 0, sizeof(dsp->registers));
    
    /* Registers */
    dsp->pc = 0x0000;
    dsp->registers[DSP_REG_OMR]=0x02;
    for (i=0;i<8;i++) {
        dsp->registers[DSP_REG_M0+i]=0x00ffff;
    }

    /* Interruptions */
    memset(dsp->interrupt_is_pending, 0, sizeof(dsp->interrupt_is_pending));
    dsp->interrupt_state = DSP_INTERRUPT_NONE;
    dsp->interrupt_instr_fetch = -1;
    dsp->interrupt_save_pc = -1;
    dsp->interrupt_counter = 0;
    dsp->interrupt_pipeline_count = 0;
    for (i=0;i<5;i++) {
        dsp->interrupt_ipl[i] = 3;
    }
    for (i=5;i<12;i++) {
        dsp->interrupt_ipl[i] = -1;
    }

    /* Misc */
    dsp->loop_rep = 0;


    /* runtime shit */

    dsp->executing_for_disasm = false;
    // start_time = SDL_GetTicks();
    dsp->num_inst = 0;

    dsp->exception_debugging = true;
    dsp->disasm_prev_inst_pc = 0xFFFFFFFF;
}

static OpcodeEntry lookup_opcode(uint32_t op) {
    OpcodeEntry r = {0};
    int i;
    for (i=0; i<ARRAYSIZE(nonparallel_opcodes); i++) {
        if ((op & nonparallel_matches[i][0]) == nonparallel_matches[i][1]) {
            if (nonparallel_opcodes[i].match_func 
                && !nonparallel_opcodes[i].match_func(op)) continue;
            if (r.template != NULL) {
                printf("qqq %x %s\n", op, r.template);
            }
            assert(r.template == NULL);
            r = nonparallel_opcodes[i];
        }
    }
    return r;
}

static uint16_t disasm_instruction(dsp_core_t* dsp, dsp_trace_disasm_t mode)
{
    dsp->disasm_mode = mode;
    if (mode == DSP_TRACE_MODE) {
        if (dsp->disasm_prev_inst_pc == dsp->pc) {
            if (!dsp->disasm_is_looping) {
                printf( "Looping on DSP instruction at PC = $%04x\n", dsp->disasm_prev_inst_pc);
                dsp->disasm_is_looping = true;
            }
            return 0;
        }
    }

    dsp->disasm_prev_inst_pc = dsp->pc;
    dsp->disasm_is_looping = false;

    dsp->disasm_cur_inst = dsp56k_read_memory(dsp, DSP_SPACE_P, dsp->pc);
    dsp->disasm_cur_inst_len = 1;

    dsp->disasm_parallelmove_name[0] = 0;

    if (dsp->disasm_cur_inst < 0x100000) {
        const OpcodeEntry op = lookup_opcode(dsp->disasm_cur_inst);
        if (op.template) {
            if (op.dis_func) {
                op.dis_func(dsp);
            } else {
                sprintf(dsp->disasm_str_instr, "%s", op.name);
            }
        } else {
            dis_undefined(dsp);
        }
    } else {
        dis_pm(dsp);
        sprintf(dsp->disasm_str_instr, "%s %s",
            disasm_opcodes_alu[dsp->disasm_cur_inst & BITMASK(8)], dsp->disasm_parallelmove_name);
    }
    return dsp->disasm_cur_inst_len;
}

static void disasm_reg_save(dsp_core_t* dsp)
{
    memcpy(dsp->disasm_registers_save, dsp->registers , sizeof(dsp->disasm_registers_save));
#ifdef DSP_DISASM_REG_PC
    dsp->pc_save = dsp->pc;
#endif
}

static void disasm_reg_compare(dsp_core_t* dsp)
{
    int i;
    bool bRegA = false;
    bool bRegB = false;
    
    for (i=4; i<64; i++) {
        if (dsp->disasm_registers_save[i] == dsp->registers[i]) {
            continue;
        }

        switch(i) {
            case DSP_REG_X0:
            case DSP_REG_X1:
            case DSP_REG_Y0:
            case DSP_REG_Y1:
                printf("\tReg: %s  $%06x -> $%06x\n",
                    registers_name[i], dsp->disasm_registers_save[i], dsp->registers[i]);
                break;
            case DSP_REG_R0:
            case DSP_REG_R1:
            case DSP_REG_R2:
            case DSP_REG_R3:
            case DSP_REG_R4:
            case DSP_REG_R5:
            case DSP_REG_R6:
            case DSP_REG_R7:
            case DSP_REG_M0:
            case DSP_REG_M1:
            case DSP_REG_M2:
            case DSP_REG_M3:
            case DSP_REG_M4:
            case DSP_REG_M5:
            case DSP_REG_M6:
            case DSP_REG_M7:
            case DSP_REG_N0:
            case DSP_REG_N1:
            case DSP_REG_N2:
            case DSP_REG_N3:
            case DSP_REG_N4:
            case DSP_REG_N5:
            case DSP_REG_N6:
            case DSP_REG_N7:
            case DSP_REG_SR:
            case DSP_REG_LA:
            case DSP_REG_LC:
                printf("\tReg: %s  $%04x -> $%04x\n",
                    registers_name[i], dsp->disasm_registers_save[i], dsp->registers[i]);
                break;
            case DSP_REG_OMR:
            case DSP_REG_SP:
            case DSP_REG_SSH:
            case DSP_REG_SSL:
                printf("\tReg: %s  $%02x -> $%02x\n",
                    registers_name[i], dsp->disasm_registers_save[i], dsp->registers[i]);
                break;
            case DSP_REG_A0:
            case DSP_REG_A1:
            case DSP_REG_A2:
                if (bRegA == false) {
                    printf("\tReg: a   $%02x:%06x:%06x -> $%02x:%06x:%06x\n",
                        dsp->disasm_registers_save[DSP_REG_A2], dsp->disasm_registers_save[DSP_REG_A1], dsp->disasm_registers_save[DSP_REG_A0],
                        dsp->registers[DSP_REG_A2], dsp->registers[DSP_REG_A1], dsp->registers[DSP_REG_A0]
                    );
                    bRegA = true;
                }
                break;
            case DSP_REG_B0:
            case DSP_REG_B1:
            case DSP_REG_B2:
                if (bRegB == false) {
                    printf("\tReg: b   $%02x:%06x:%06x -> $%02x:%06x:%06x\n",
                        dsp->disasm_registers_save[DSP_REG_B2], dsp->disasm_registers_save[DSP_REG_B1], dsp->disasm_registers_save[DSP_REG_B0],
                        dsp->registers[DSP_REG_B2], dsp->registers[DSP_REG_B1], dsp->registers[DSP_REG_B0]
                    );
                    bRegB = true;
                }
                break;
        }
    }

#ifdef DSP_DISASM_REG_PC
    if (pc_save != dsp->pc) {
        printf("\tReg: pc  $%04x -> $%04x\n", pc_save, dsp->pc);
    }
#endif
}

static const char* disasm_get_instruction_text(dsp_core_t* dsp)
{
    const int len = sizeof(dsp->disasm_str_instr);
    // uint64_t count, cycles;
    // uint16_t cycle_diff;
    // float percentage;
    int offset;

    if (dsp->disasm_is_looping) {
        dsp->disasm_str_instr2[0] = 0;
    }
    if (dsp->disasm_cur_inst_len == 1) {
        offset = sprintf(dsp->disasm_str_instr2, "p:%04x  %06x         (%02d cyc)  %-*s\n", dsp->disasm_prev_inst_pc, dsp->disasm_cur_inst, dsp->instr_cycle, len, dsp->disasm_str_instr);
    } else {
        offset = sprintf(dsp->disasm_str_instr2, "p:%04x  %06x %06x  (%02d cyc)  %-*s\n", dsp->disasm_prev_inst_pc, dsp->disasm_cur_inst, read_memory_p(dsp, dsp->disasm_prev_inst_pc + 1), dsp->instr_cycle, len, dsp->disasm_str_instr);
    }
    // if (offset > 2 && Profile_DspAddressData(dsp->disasm_prev_inst_pc, &percentage, &count, &cycles, &cycle_diff)) {
    //     offset -= 2;
    //     sprintf(str_instr2+offset, "%5.2f%% (%"PRId64", %"PRId64", %d)\n",
    //             percentage, count, cycles, cycle_diff);
    // }
    return dsp->disasm_str_instr2;
}

/**
 * Execute one instruction in trace mode at a given PC address.
 * */
uint16_t dsp56k_execute_one_disasm_instruction(dsp_core_t* dsp, FILE *out, uint32_t pc)
{
    dsp_core_t dsp_core_save;

    /* Set DSP in disasm mode */
    dsp->executing_for_disasm = true;

    /* Save DSP context before executing instruction */
    memcpy(&dsp_core_save, dsp, sizeof(dsp_core_t));

    /* execute and disasm instruction */
    dsp->pc = pc;

    /* Disasm instruction */
    uint16_t instruction_length = disasm_instruction(dsp, DSP_DISASM_MODE) - 1;

    /* Execute instruction at address given in parameter to get the number of cycles it takes */
    dsp56k_execute_instruction(dsp);

    fprintf(out, "%s", disasm_get_instruction_text(dsp));

    /* Restore DSP context after executing instruction */
    memcpy(dsp, &dsp_core_save, sizeof(dsp_core_t));
    
    /* Unset DSP in disasm mode */
    dsp->executing_for_disasm = false;

    return instruction_length;
}

void dsp56k_execute_instruction(dsp_core_t* dsp)
{
    uint32_t disasm_return = 0;
    dsp->disasm_memory_ptr = 0;

    /* Decode and execute current instruction */
    dsp->cur_inst = read_memory_p(dsp, dsp->pc);
    
    /* Initialize instruction size and cycle counter */
    dsp->cur_inst_len = 1;
    dsp->instr_cycle = 2;

    /* Disasm current instruction ? (trace mode only) */
    if (TRACE_DSP_DISASM) {    
        /* Call disasm_instruction only when DSP is called in trace mode */
        if (!dsp->executing_for_disasm) {
            disasm_return = disasm_instruction(dsp, DSP_TRACE_MODE);
            
            if (disasm_return) {
                printf( "%s", disasm_get_instruction_text(dsp));
            }
            if (disasm_return != 0 && TRACE_DSP_DISASM_REG) {
                /* DSP regs trace enabled only if DSP DISASM is enabled */
                disasm_reg_save(dsp);
            }
        }
    }
            
    if (dsp->cur_inst < 0x100000) {
        const OpcodeEntry op = lookup_opcode(dsp->cur_inst);
        if (op.emu_func) {
            op.emu_func(dsp);
        } else {
            printf("%x - %s\n", dsp->cur_inst, op.name);
            emu_undefined(dsp);
        }
    } else {
        /* Do parallel move read */
        opcodes_parmove[(dsp->cur_inst>>20) & BITMASK(4)](dsp);
    }

    /* Disasm current instruction ? (trace mode only) */
    if (TRACE_DSP_DISASM) {
        /* Display only when DSP is called in trace mode */
        if (!dsp->executing_for_disasm) {
            if (disasm_return != 0) {
                // printf( "%s", disasm_get_instruction_text(dsp));

                /* DSP regs trace enabled only if DSP DISASM is enabled */
                if (TRACE_DSP_DISASM_REG)
                    disasm_reg_compare(dsp);

                if (TRACE_DSP_DISASM_MEM) {
                    /* 1 memory change to display ? */
                    if (dsp->disasm_memory_ptr == 1)
                        printf( "\t%s\n", dsp->str_disasm_memory[0]);
                    /* 2 memory changes to display ? */
                    else if (dsp->disasm_memory_ptr == 2) {
                        printf( "\t%s\n", dsp->str_disasm_memory[0]);
                        printf( "\t%s\n", dsp->str_disasm_memory[1]);
                    }
                }
            }
        }
    }

    /* Process the PC */
    dsp_postexecute_update_pc(dsp);

    /* Process Interrupts */
    dsp_postexecute_interrupts(dsp);

#ifdef DSP_COUNT_IPS
    ++dsp->num_inst;
    if ((dsp->num_inst & 63) == 0) {
        /* Evaluate time after <N> instructions have been executed to avoid asking too frequently */
        uint32_t cur_time = SDL_GetTicks();
        if (cur_time-start_time>1000) {
            printf( "Dsp: %d i/s\n", (dsp->num_inst*1000)/(cur_time-start_time));
            start_time=cur_time;
            dsp->num_inst=0;
        }
    }
#endif
}

/**********************************
 *  Update the PC
**********************************/

static void dsp_postexecute_update_pc(dsp_core_t* dsp)
{
    /* When running a REP, PC must stay on the current instruction */
    if (dsp->loop_rep) {
        /* Is PC on the instruction to repeat ? */      
        if (dsp->pc_on_rep==0) {
            --dsp->registers[DSP_REG_LC];
            dsp->registers[DSP_REG_LC] &= BITMASK(16);

            if (dsp->registers[DSP_REG_LC] > 0) {
                dsp->cur_inst_len = 0;   /* Stay on this instruction */
            } else {
                dsp->loop_rep = 0;
                dsp->registers[DSP_REG_LC] = dsp->registers[DSP_REG_LCSAVE];
            }
        } else {
            /* Init LC at right value */
            if (dsp->registers[DSP_REG_LC] == 0) {
                dsp->registers[DSP_REG_LC] = 0x010000;
            }
            dsp->pc_on_rep = 0;
        }
    }

    /* Normal execution, go to next instruction */
    dsp->pc += dsp->cur_inst_len;

    /* When running a DO loop, we test the end of loop with the */
    /* updated PC, pointing to last instruction of the loop */
    if (dsp->registers[DSP_REG_SR] & (1<<DSP_SR_LF)) {

        /* Did we execute the last instruction in loop ? */
        if (dsp->pc == dsp->registers[DSP_REG_LA] + 1) {        
            --dsp->registers[DSP_REG_LC];
            dsp->registers[DSP_REG_LC] &= BITMASK(16);

            if (dsp->registers[DSP_REG_LC] == 0) {
                /* end of loop */
                uint32_t saved_pc, saved_sr;

                dsp_stack_pop(dsp, &saved_pc, &saved_sr);
                dsp->registers[DSP_REG_SR] &= 0x7f;
                dsp->registers[DSP_REG_SR] |= saved_sr & (1<<DSP_SR_LF);
                dsp_stack_pop(dsp, &dsp->registers[DSP_REG_LA], &dsp->registers[DSP_REG_LC]);
            } else {
                /* Loop one more time */
                dsp->pc = dsp->registers[DSP_REG_SSH];
            }
        }
    }
}

/**********************************
 *  Interrupts
**********************************/

/* Post a new interrupt to the interrupt table */
void dsp56k_add_interrupt(dsp_core_t* dsp, uint16_t inter)
{
    /* detect if this interrupt is used or not */
    if (dsp->interrupt_ipl[inter] == -1)
        return;

    /* add this interrupt to the pending interrupts table */
    if (dsp->interrupt_is_pending[inter] == 0) { 
        dsp->interrupt_is_pending[inter] = 1;
        dsp->interrupt_counter ++;
    }
}

static void dsp_postexecute_interrupts(dsp_core_t* dsp)
{
    uint32_t index, instr, i;
    int32_t ipl_to_raise, ipl_sr;

    /* REP is not interruptible */
    if (dsp->loop_rep) {
        return;
    }

    /* A fast interrupt can not be interrupted. */
    if (dsp->interrupt_state == DSP_INTERRUPT_DISABLED) {

        switch (dsp->interrupt_pipeline_count) {
            case 5:
                dsp->interrupt_pipeline_count --;
                return;
            case 4:
                /* Prefetch interrupt instruction 1 */
                dsp->interrupt_save_pc = dsp->pc;
                dsp->pc = dsp->interrupt_instr_fetch;

                /* is it a LONG interrupt ? */
                instr = read_memory_p(dsp, dsp->interrupt_instr_fetch);
                if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
                    dsp->interrupt_state = DSP_INTERRUPT_LONG;
                    dsp_stack_push(dsp, dsp->interrupt_save_pc, dsp->registers[DSP_REG_SR], 0); 
                    dsp->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
                                            (1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
                                            (1<<DSP_SR_I0)|(1<<DSP_SR_I1));
                    dsp->registers[DSP_REG_SR] |= dsp->interrupt_ipl_to_raise<<DSP_SR_I0;
                }
                dsp->interrupt_pipeline_count --;
                return;
            case 3:
                /* Prefetch interrupt instruction 2 */
                if (dsp->pc == dsp->interrupt_instr_fetch+1) {
                    instr = read_memory_p(dsp, dsp->pc);
                    if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
                        dsp->interrupt_state = DSP_INTERRUPT_LONG;
                        dsp_stack_push(dsp, dsp->interrupt_save_pc, dsp->registers[DSP_REG_SR], 0); 
                        dsp->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
                                                (1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
                                                (1<<DSP_SR_I0)|(1<<DSP_SR_I1));
                        dsp->registers[DSP_REG_SR] |= dsp->interrupt_ipl_to_raise<<DSP_SR_I0;
                    }
                }
                dsp->interrupt_pipeline_count --;
                return;
            case 2:
                /* 1 instruction executed after interrupt */
                /* before re enable interrupts */
                /* Was it a FAST interrupt ? */
                if (dsp->pc == dsp->interrupt_instr_fetch+2) {
                    dsp->pc = dsp->interrupt_save_pc;
                }
                dsp->interrupt_pipeline_count --;
                return;
            case 1:
                /* Last instruction executed after interrupt */
                /* before re enable interrupts */
                dsp->interrupt_pipeline_count --;
                return;
            case 0:
                /* Re enable interrupts */
                /* All 6 instruction are done, Interrupts can be enabled again */
                dsp->interrupt_save_pc = -1;
                dsp->interrupt_instr_fetch = -1;
                dsp->interrupt_state = DSP_INTERRUPT_NONE;
                break;
        }
    }

    /* Trace Interrupt ? */
    if (dsp->registers[DSP_REG_SR] & (1<<DSP_SR_T)) {
        dsp56k_add_interrupt(dsp, DSP_INTER_TRACE);
    }

    /* No interrupt to execute */
    if (dsp->interrupt_counter == 0) {
        return;
    }

    /* search for an interrupt */
    ipl_sr = (dsp->registers[DSP_REG_SR]>>DSP_SR_I0) & BITMASK(2);
    index = 0xffff;
    ipl_to_raise = -1;

    /* Arbitrate between all pending interrupts */
    for (i=0; i<12; i++) {
        if (dsp->interrupt_is_pending[i] == 1) {

            /* level 3 interrupt ? */
            if (dsp->interrupt_ipl[i] == 3) {
                index = i;
                break;
            }

            /* level 0, 1 ,2 interrupt ? */
            /* if interrupt is masked in SR, don't process it */
            if (dsp->interrupt_ipl[i] < ipl_sr)
                continue;

            /* if interrupt is lower or equal than current arbitrated interrupt */
            if (dsp->interrupt_ipl[i] <= ipl_to_raise)
                continue;

            /* save current arbitrated interrupt */
            index = i;
            ipl_to_raise = dsp->interrupt_ipl[i];
        }
    }

    /* If there's no interrupt to process, return */
    if (index == 0xffff) {
        return;
    }

    /* remove this interrupt from the pending interrupts table */
    dsp->interrupt_is_pending[index] = 0;
    dsp->interrupt_counter --;

    /* process arbritrated interrupt */
    ipl_to_raise = dsp->interrupt_ipl[index] + 1;
    if (ipl_to_raise > 3) {
        ipl_to_raise = 3;
    }

    dsp->interrupt_instr_fetch = dsp_interrupt[index].vectorAddr;
    dsp->interrupt_pipeline_count = 5;
    dsp->interrupt_state = DSP_INTERRUPT_DISABLED;
    dsp->interrupt_ipl_to_raise = ipl_to_raise;

    DPRINTF("Dsp interrupt: %s\n", dsp_interrupt[index].name);

    /* SSI receive data with exception ? */
    if (dsp->interrupt_instr_fetch == 0xe) {
        // dsp->periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_ROE);
        assert(false);
    }

    /* SSI transmit data with exception ? */
    else if (dsp->interrupt_instr_fetch == 0x12) {
        // dsp->periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_TUE);
        assert(false);
    }

    /* host command ? */
    else if (dsp->interrupt_instr_fetch == 0xff) {
        /* Clear HC and HCP interrupt */
        // dsp->periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff - (1<<DSP_HOST_HSR_HCP);
        // dsp->hostport[CPU_HOST_CVR] &= 0xff - (1<<CPU_HOST_CVR_HC);  

        // dsp->interrupt_instr_fetch = dsp->hostport[CPU_HOST_CVR] & BITMASK(5);
        // dsp->interrupt_instr_fetch *= 2;    
        assert(false);
    }
}

/**********************************
 *  Read/Write memory functions
 **********************************/

static uint32_t read_memory_p(dsp_core_t* dsp, uint32_t address)
{
    assert((address & 0xFF000000) == 0);
    assert(address < DSP_PRAM_SIZE);
    uint32_t r = dsp->pram[address];
    assert((r & 0xFF000000) == 0);
    return r;
}

uint32_t dsp56k_read_memory(dsp_core_t* dsp, int space, uint32_t address)
{
    assert((address & 0xFF000000) == 0);

    if (space == DSP_SPACE_X) {
        if (address >= DSP_PERIPH_BASE) {
            assert(dsp->read_peripheral);
            return dsp->read_peripheral(dsp, address);
        } else if (address >= DSP_MIXBUFFER_BASE && address < DSP_MIXBUFFER_BASE+DSP_MIXBUFFER_SIZE) {
            return dsp->mixbuffer[address-DSP_MIXBUFFER_BASE];
        } else if (address >= DSP_MIXBUFFER_READ_BASE && address < DSP_MIXBUFFER_READ_BASE+DSP_MIXBUFFER_SIZE) {
            return dsp->mixbuffer[address-DSP_MIXBUFFER_READ_BASE];
        } else {
            assert(address < DSP_XRAM_SIZE);
            return dsp->xram[address];
        }
    } else if (space == DSP_SPACE_Y) {
        assert(address < DSP_YRAM_SIZE);
        return dsp->yram[address];
    } else if (space == DSP_SPACE_P) {
        return read_memory_p(dsp, address);
    } else {
        assert(false);
        return 0;
    }
}

void dsp56k_write_memory(dsp_core_t* dsp, int space, uint32_t address, uint32_t value)
{
    assert((value & 0xFF000000) == 0);
    assert((address & 0xFF000000) == 0);

    if (TRACE_DSP_DISASM_MEM)
        write_memory_disasm(dsp, space, address, value);
    else    
        write_memory_raw(dsp, space, address, value);
}

static void write_memory_raw(dsp_core_t* dsp, int space, uint32_t address, uint32_t value)
{
    assert((value & 0xFF000000) == 0);
    assert((address & 0xFF000000) == 0);

    if (space == DSP_SPACE_X) {
        if (address >= DSP_PERIPH_BASE) {
            assert(dsp->write_peripheral);
            dsp->write_peripheral(dsp, address, value);
            return;
        } else if (address >= DSP_MIXBUFFER_BASE && address < DSP_MIXBUFFER_BASE+DSP_MIXBUFFER_SIZE) {
            dsp->mixbuffer[address-DSP_MIXBUFFER_BASE] = value;
        } else if (address >= DSP_MIXBUFFER_READ_BASE && address < DSP_MIXBUFFER_READ_BASE+DSP_MIXBUFFER_SIZE) {
            dsp->mixbuffer[address-DSP_MIXBUFFER_READ_BASE] = value;
        } else {
            assert(address < DSP_XRAM_SIZE);
            dsp->xram[address] = value;
        }
    } else if (space == DSP_SPACE_Y) {
        assert(address < DSP_YRAM_SIZE);
        dsp->yram[address] = value;
    } else if (space == DSP_SPACE_P) {
        assert(address < DSP_PRAM_SIZE);
        dsp->pram[address] = value;
    } else {
        assert(false);
    }
}

static uint32_t read_memory_disasm(dsp_core_t* dsp, int space, uint32_t address)
{
    return dsp56k_read_memory(dsp, space, address);
}

static void write_memory_disasm(dsp_core_t* dsp, int space, uint32_t address, uint32_t value)
{
    uint32_t oldvalue, curvalue;
    char space_c;

    oldvalue = read_memory_disasm(dsp, space, address);

    write_memory_raw(dsp, space, address, value);

    switch(space) {
        case DSP_SPACE_X:
            space_c = 'x';
            break;
        case DSP_SPACE_Y:
            space_c = 'y';
            break;
        case DSP_SPACE_P:
            space_c = 'p';
            break;
        default:
            assert(false);
    }

    curvalue = read_memory_disasm(dsp, space, address);
    if (dsp->disasm_memory_ptr < ARRAYSIZE(dsp->str_disasm_memory)) {
        sprintf(dsp->str_disasm_memory[dsp->disasm_memory_ptr], "Mem: %c:0x%04x  0x%06x -> 0x%06x", space_c, address, oldvalue, curvalue);
        dsp->disasm_memory_ptr ++;
    }
}

static void dsp_write_reg(dsp_core_t* dsp, uint32_t numreg, uint32_t value)
{
    uint32_t stack_error;

    switch (numreg) {
        case DSP_REG_A:
            dsp->registers[DSP_REG_A0] = 0;
            dsp->registers[DSP_REG_A1] = value;
            dsp->registers[DSP_REG_A2] = value & (1<<23) ? 0xff : 0x0;
            break;
        case DSP_REG_B:
            dsp->registers[DSP_REG_B0] = 0;
            dsp->registers[DSP_REG_B1] = value;
            dsp->registers[DSP_REG_B2] = value & (1<<23) ? 0xff : 0x0;
            break;
        case DSP_REG_OMR:
            dsp->registers[DSP_REG_OMR] = value & 0xc7;
            break;
        case DSP_REG_SR:
            dsp->registers[DSP_REG_SR] = value & 0xaf7f;
            break;
        case DSP_REG_SP:
            stack_error = dsp->registers[DSP_REG_SP] & (3<<DSP_SP_SE);
            if ((stack_error==0) && (value & (3<<DSP_SP_SE))) {
                /* Stack underflow or overflow detected, raise interrupt */
                dsp56k_add_interrupt(dsp, DSP_INTER_STACK_ERROR);
                dsp->registers[DSP_REG_SP] = value & (3<<DSP_SP_SE);
                if (!dsp->executing_for_disasm) {
                    printf( "Dsp: Stack Overflow or Underflow\n");
                }
                if (dsp->exception_debugging) {
                    assert(false);
                }
            } else {
                dsp->registers[DSP_REG_SP] = value & BITMASK(6);
            } 
            dsp_compute_ssh_ssl(dsp);
            break;
        case DSP_REG_SSH:
            dsp_stack_push(dsp, value, 0, 1);
            break;
        case DSP_REG_SSL:
            numreg = dsp->registers[DSP_REG_SP] & BITMASK(4);
            if (numreg == 0) {
                value = 0;
            }
            dsp->stack[1][numreg] = value & BITMASK(16);
            dsp->registers[DSP_REG_SSL] = value & BITMASK(16);
            break;
        default:
            dsp->registers[numreg] = value; 
            dsp->registers[numreg] &= BITMASK(registers_mask[numreg]);
            break;
    }
}

/**********************************
 *  Stack push/pop
 **********************************/

static void dsp_stack_push(dsp_core_t* dsp, uint32_t curpc, uint32_t cursr, uint16_t sshOnly)
{
    uint32_t stack_error, underflow, stack;

    stack_error = dsp->registers[DSP_REG_SP] & (1<<DSP_SP_SE);
    underflow = dsp->registers[DSP_REG_SP] & (1<<DSP_SP_UF);
    stack = (dsp->registers[DSP_REG_SP] & BITMASK(4)) + 1;


    if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
        /* Stack full, raise interrupt */
        dsp56k_add_interrupt(dsp, DSP_INTER_STACK_ERROR);
        if (!dsp->executing_for_disasm)
            printf("Dsp: Stack Overflow\n");
        if (dsp->exception_debugging)
            assert(false);
    }
    
    dsp->registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
    stack &= BITMASK(4);

    if (stack) {
        /* SSH part */
        dsp->stack[0][stack] = curpc & BITMASK(16);
        /* SSL part, if instruction is not like "MOVEC xx, SSH"  */
        if (sshOnly == 0) {
            dsp->stack[1][stack] = cursr & BITMASK(16);
        }
    } else {
        dsp->stack[0][0] = 0;
        dsp->stack[1][0] = 0;
    }

    /* Update SSH and SSL registers */
    dsp->registers[DSP_REG_SSH] = dsp->stack[0][stack];
    dsp->registers[DSP_REG_SSL] = dsp->stack[1][stack];
}

static void dsp_stack_pop(dsp_core_t* dsp, uint32_t *newpc, uint32_t *newsr)
{
    uint32_t stack_error, underflow, stack;

    stack_error = dsp->registers[DSP_REG_SP] & (1<<DSP_SP_SE);
    underflow = dsp->registers[DSP_REG_SP] & (1<<DSP_SP_UF);
    stack = (dsp->registers[DSP_REG_SP] & BITMASK(4)) - 1;

    if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
        /* Stack empty*/
        dsp56k_add_interrupt(dsp, DSP_INTER_STACK_ERROR);
        if (!dsp->executing_for_disasm)
            printf("Dsp: Stack underflow\n");
        if (dsp->exception_debugging)
            assert(false);
    }

    dsp->registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
    stack &= BITMASK(4);
    *newpc = dsp->registers[DSP_REG_SSH];
    *newsr = dsp->registers[DSP_REG_SSL];

    dsp->registers[DSP_REG_SSH] = dsp->stack[0][stack];
    dsp->registers[DSP_REG_SSL] = dsp->stack[1][stack];
}

static void dsp_compute_ssh_ssl(dsp_core_t* dsp)
{
    uint32_t stack;

    stack = dsp->registers[DSP_REG_SP];
    stack &= BITMASK(4);
    dsp->registers[DSP_REG_SSH] = dsp->stack[0][stack];
    dsp->registers[DSP_REG_SSL] = dsp->stack[1][stack];
}



/**********************************
 *  56bit arithmetic
 **********************************/

/* source,dest[0] is 55:48 */
/* source,dest[1] is 47:24 */
/* source,dest[2] is 23:00 */

static uint16_t dsp_abs56(uint32_t *dest)
{
    uint32_t zerodest[3];
    uint16_t newsr;

    /* D=|D| */

    if (dest[0] & (1<<7)) {
        zerodest[0] = zerodest[1] = zerodest[2] = 0;

        newsr = dsp_sub56(dest, zerodest);

        dest[0] = zerodest[0];
        dest[1] = zerodest[1];
        dest[2] = zerodest[2];
    } else {
        newsr = 0;
    }

    return newsr;
}

static uint16_t dsp_asl56(uint32_t *dest, int n)
{
    /* Shift left dest n bits: D<<=n */

    uint64_t dest_v = dest[2] | ((uint64_t)dest[1] << 24) | ((uint64_t)dest[0] << 48);

    uint32_t carry = (dest_v >> (56-n)) & 1;

    uint64_t dest_s = dest_v << n;
    dest[2] = dest_s & BITMASK(24);
    dest[1] = (dest_s >> 24) & BITMASK(24);
    dest[0] = (dest_s >> 48) & BITMASK(8);

    uint32_t overflow = (dest_v >> (56-n)) != 0;
    uint32_t v = ((dest_v >> 55) & 1) != ((dest_s >> 55) & 1);

    return (overflow<<DSP_SR_L)|(v<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static uint16_t dsp_asr56(uint32_t *dest, int n)
{
    /* Shift right dest n bits: D>>=n */

    uint64_t dest_v = dest[2] | ((uint64_t)dest[1] << 24) | ((uint64_t)dest[0] << 48);

    uint16_t carry = (dest_v >> (n-1)) & 1;
    
    dest_v >>= n;
    dest[2] = dest_v & BITMASK(24);
    dest[1] = (dest_v >> 24) & BITMASK(24);
    dest[0] = (dest_v >> 48) & BITMASK(8);

    return (carry<<DSP_SR_C);
}

static uint16_t dsp_add56(uint32_t *source, uint32_t *dest)
{
    uint16_t overflow, carry, flg_s, flg_d, flg_r;

    flg_s = (source[0]>>7) & 1;
    flg_d = (dest[0]>>7) & 1;

    /* Add source to dest: D = D+S */
    dest[2] += source[2];
    dest[1] += source[1]+((dest[2]>>24) & 1);
    dest[0] += source[0]+((dest[1]>>24) & 1);

    carry = (dest[0]>>8) & 1;

    dest[2] &= BITMASK(24);
    dest[1] &= BITMASK(24);
    dest[0] &= BITMASK(8);

    flg_r = (dest[0]>>7) & 1;

    /*set overflow*/
    overflow = (flg_s ^ flg_r) & (flg_d ^ flg_r);

    return (overflow<<DSP_SR_L)|(overflow<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static uint16_t dsp_sub56(uint32_t *source, uint32_t *dest)
{
    uint16_t overflow, carry, flg_s, flg_d, flg_r, dest_save;

    dest_save = dest[0];

    /* Subtract source from dest: D = D-S */
    dest[2] -= source[2];
    dest[1] -= source[1]+((dest[2]>>24) & 1);
    dest[0] -= source[0]+((dest[1]>>24) & 1);

    carry = (dest[0]>>8) & 1;

    dest[2] &= BITMASK(24);
    dest[1] &= BITMASK(24);
    dest[0] &= BITMASK(8);

    flg_s = (source[0]>>7) & 1;
    flg_d = (dest_save>>7) & 1;
    flg_r = (dest[0]>>7) & 1;

    /* set overflow */
    overflow = (flg_s ^ flg_d) & (flg_r ^ flg_d);

    return (overflow<<DSP_SR_L)|(overflow<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static void dsp_mul56(uint32_t source1, uint32_t source2, uint32_t *dest, uint8_t signe)
{
    uint32_t part[4], zerodest[3], value;

    /* Multiply: D = S1*S2 */
    if (source1 & (1<<23)) {
        signe ^= 1;
        source1 = (1<<24) - source1;
    }
    if (source2 & (1<<23)) {
        signe ^= 1;
        source2 = (1<<24) - source2;
    }

    /* bits 0-11 * bits 0-11 */
    part[0]=(source1 & BITMASK(12))*(source2 & BITMASK(12));
    /* bits 12-23 * bits 0-11 */
    part[1]=((source1>>12) & BITMASK(12))*(source2 & BITMASK(12));
    /* bits 0-11 * bits 12-23 */
    part[2]=(source1 & BITMASK(12))*((source2>>12)  & BITMASK(12));
    /* bits 12-23 * bits 12-23 */
    part[3]=((source1>>12) & BITMASK(12))*((source2>>12) & BITMASK(12));

    /* Calc dest 2 */
    dest[2] = part[0];
    dest[2] += (part[1] & BITMASK(12)) << 12;
    dest[2] += (part[2] & BITMASK(12)) << 12;

    /* Calc dest 1 */
    dest[1] = (part[1]>>12) & BITMASK(12);
    dest[1] += (part[2]>>12) & BITMASK(12);
    dest[1] += part[3];

    /* Calc dest 0 */
    dest[0] = 0;

    /* Add carries */
    value = (dest[2]>>24) & BITMASK(8);
    if (value) {
        dest[1] += value;
        dest[2] &= BITMASK(24);
    }
    value = (dest[1]>>24) & BITMASK(8);
    if (value) {
        dest[0] += value;
        dest[1] &= BITMASK(24);
    }

    /* Get rid of extra sign bit */
    dsp_asl56(dest, 1);

    if (signe) {
        zerodest[0] = zerodest[1] = zerodest[2] = 0;

        dsp_sub56(dest, zerodest);

        dest[0] = zerodest[0];
        dest[1] = zerodest[1];
        dest[2] = zerodest[2];
    }
}

static void dsp_rnd56(dsp_core_t* dsp, uint32_t *dest)
{
    uint32_t rnd_const[3];

    rnd_const[0] = 0;

    /* Scaling mode S0 */
    if (dsp->registers[DSP_REG_SR] & (1<<DSP_SR_S0)) {
        rnd_const[1] = 1;
        rnd_const[2] = 0;
        dsp_add56(rnd_const, dest);

        if ((dest[2]==0) && ((dest[1] & 1) == 0)) {
            dest[1] &= (0xffffff - 0x3);
        }
        dest[1] &= 0xfffffe;
        dest[2]=0;
    }
    /* Scaling mode S1 */
    else if (dsp->registers[DSP_REG_SR] & (1<<DSP_SR_S1)) {
        rnd_const[1] = 0;
        rnd_const[2] = (1<<22);
        dsp_add56(rnd_const, dest);
   
        if ((dest[2] & 0x7fffff) == 0){
            dest[2] = 0;
        }
        dest[2] &= 0x800000;
    }
    /* No Scaling */
    else {
        rnd_const[1] = 0;
        rnd_const[2] = (1<<23);
        dsp_add56(rnd_const, dest);

        if (dest[2] == 0) {
            dest[1] &= 0xfffffe;
        }
        dest[2]=0;
    }
}

static uint32_t dsp_signextend(int bits, uint32_t v) {
    const int shift = sizeof(int)*8 - bits;
    assert(shift > 0);
    return (uint32_t)(((int32_t)v << shift) >> shift);
}


