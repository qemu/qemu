/*
 *  Source file for nanoMIPS disassembler component of QEMU
 *
 *  Copyright (C) 2018  Wave Computing, Inc.
 *  Copyright (C) 2018  Matthew Fortune <matthew.fortune@mips.com>
 *  Copyright (C) 2018  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/*
 *  Documentation used while implementing this component:
 *
 *  [1] "MIPS® Architecture Base: nanoMIPS32(tm) Instruction Set Technical
 *      Reference Manual", Revision 01.01, April 27, 2018
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"

typedef int64_t int64;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint64_t img_address;

typedef enum  {
    instruction,
    call_instruction,
    branch_instruction,
    return_instruction,
    reserved_block,
    pool,
} TABLE_ENTRY_TYPE;

typedef enum {
    MIPS64_    = 0x00000001,
    XNP_       = 0x00000002,
    XMMS_      = 0x00000004,
    EVA_       = 0x00000008,
    DSP_       = 0x00000010,
    MT_        = 0x00000020,
    EJTAG_     = 0x00000040,
    TLBINV_    = 0x00000080,
    CP0_       = 0x00000100,
    CP1_       = 0x00000200,
    CP2_       = 0x00000400,
    UDI_       = 0x00000800,
    MCU_       = 0x00001000,
    VZ_        = 0x00002000,
    TLB_       = 0x00004000,
    MVH_       = 0x00008000,
    ALL_ATTRIBUTES = 0xffffffffull,
} TABLE_ATTRIBUTE_TYPE;

typedef struct Dis_info {
  img_address m_pc;
  fprintf_function fprintf_func;
  FILE *stream;
  sigjmp_buf buf;
} Dis_info;

typedef bool (*conditional_function)(uint64 instruction);
typedef char * (*disassembly_function)(uint64 instruction,
                                            Dis_info *info);

typedef struct Pool {
    TABLE_ENTRY_TYPE     type;
    const struct Pool    *next_table;
    int                  next_table_size;
    int                  instructions_size;
    uint64               mask;
    uint64               value;
    disassembly_function disassembly;
    conditional_function condition;
    uint64               attributes;
} Pool;

#define IMGASSERTONCE(test)


static char * G_GNUC_PRINTF(1, 2) img_format(const char *format, ...)
{
    char *buffer;
    va_list args;
    va_start(args, format);
    buffer = g_strdup_vprintf(format, args);
    va_end(args);
    return buffer;
}


static char *to_string(img_address a)
{
    return g_strdup_printf("0x%" PRIx64, a);
}


static uint64 extract_bits(uint64 data, uint32 bit_offset, uint32 bit_size)
{
    return (data << (64 - (bit_size + bit_offset))) >> (64 - bit_size);
}


static int64 sign_extend(int64 data, int msb)
{
    uint64 shift = 63 - msb;
    return (data << shift) >> shift;
}


static uint64 renumber_registers(uint64 index, uint64 *register_list,
                               size_t register_list_size, Dis_info *info)
{
    if (index < register_list_size) {
        return register_list[index];
    }

    info->fprintf_func(info->stream, "Invalid register mapping index %" PRIu64
                       ", size of list = %zu", index, register_list_size);
    siglongjmp(info->buf, 1);
}


/*
 * decode_gpr_gpr4() - decoder for 'gpr4' gpr encoding type
 *
 *   Map a 4-bit code to the 5-bit register space according to this pattern:
 *
 *                              1                   0
 *                    5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *                    | | | | | | | | | | | | | | | |
 *                    | | | | | | | | | | | | | | | |
 *                    | | | | | | | | | | | └---------------┐
 *                    | | | | | | | | | | └---------------┐ |
 *                    | | | | | | | | | └---------------┐ | |
 *                    | | | | | | | | └---------------┐ | | |
 *                    | | | | | | | |         | | | | | | | |
 *                    | | | | | | | |         | | | | | | | |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   Used in handling following instructions:
 *
 *     - ADDU[4X4]
 *     - LW[4X4]
 *     - MOVEP[REV]
 *     - MUL[4X4]
 *     - SW[4X4]
 */
static uint64 decode_gpr_gpr4(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  8,  9, 10, 11,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr4_zero() - decoder for 'gpr4.zero' gpr encoding type
 *
 *   Map a 4-bit code to the 5-bit register space according to this pattern:
 *
 *                              1                   0
 *                    5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *                    | | | | | | | | | | | | | | | |
 *                    | | | | | | | | | | | | └---------------------┐
 *                    | | | | | | | | | | | └---------------┐       |
 *                    | | | | | | | | | | └---------------┐ |       |
 *                    | | | | | | | | | └---------------┐ | |       |
 *                    | | | | | | | | └---------------┐ | | |       |
 *                    | | | | | | | |           | | | | | | |       |
 *                    | | | | | | | |           | | | | | | |       |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   This pattern is the same one used for 'gpr4' gpr encoding type, except for
 * the input value 3, that is mapped to the output value 0 instead of 11.
 *
 *   Used in handling following instructions:
 *
 *     - MOVE.BALC
 *     - MOVEP
 *     - SW[4X4]
 */
static uint64 decode_gpr_gpr4_zero(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  8,  9, 10,  0,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr3() - decoder for 'gpr3' gpr encoding type
 *
 *   Map a 3-bit code to the 5-bit register space according to this pattern:
 *
 *                            7 6 5 4 3 2 1 0
 *                            | | | | | | | |
 *                            | | | | | | | |
 *                            | | | └-----------------------┐
 *                            | | └-----------------------┐ |
 *                            | └-----------------------┐ | |
 *                            └-----------------------┐ | | |
 *                                    | | | |         | | | |
 *                            ┌-------┘ | | |         | | | |
 *                            | ┌-------┘ | |         | | | |
 *                            | | ┌-------┘ |         | | | |
 *                            | | | ┌-------┘         | | | |
 *                            | | | |                 | | | |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   Used in handling following instructions:
 *
 *     - ADDIU[R1.SP]
 *     - ADDIU[R2]
 *     - ADDU[16]
 *     - AND[16]
 *     - ANDI[16]
 *     - BEQC[16]
 *     - BEQZC[16]
 *     - BNEC[16]
 *     - BNEZC[16]
 *     - LB[16]
 *     - LBU[16]
 *     - LH[16]
 *     - LHU[16]
 *     - LI[16]
 *     - LW[16]
 *     - LW[GP16]
 *     - LWXS[16]
 *     - NOT[16]
 *     - OR[16]
 *     - SB[16]
 *     - SH[16]
 *     - SLL[16]
 *     - SRL[16]
 *     - SUBU[16]
 *     - SW[16]
 *     - XOR[16]
 */
static uint64 decode_gpr_gpr3(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = { 16, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr3_src_store() - decoder for 'gpr3.src.store' gpr encoding
 *     type
 *
 *   Map a 3-bit code to the 5-bit register space according to this pattern:
 *
 *                            7 6 5 4 3 2 1 0
 *                            | | | | | | | |
 *                            | | | | | | | └-----------------------┐
 *                            | | | └-----------------------┐       |
 *                            | | └-----------------------┐ |       |
 *                            | └-----------------------┐ | |       |
 *                            └-----------------------┐ | | |       |
 *                                    | | |           | | | |       |
 *                            ┌-------┘ | |           | | | |       |
 *                            | ┌-------┘ |           | | | |       |
 *                            | | ┌-------┘           | | | |       |
 *                            | | |                   | | | |       |
 *                            | | |                   | | | |       |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   This pattern is the same one used for 'gpr3' gpr encoding type, except for
 * the input value 0, that is mapped to the output value 0 instead of 16.
 *
 *   Used in handling following instructions:
 *
 *     - SB[16]
 *     - SH[16]
 *     - SW[16]
 *     - SW[GP16]
 */
static uint64 decode_gpr_gpr3_src_store(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  0, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr2_reg1() - decoder for 'gpr2.reg1' gpr encoding type
 *
 *   Map a 2-bit code to the 5-bit register space according to this pattern:
 *
 *                                3 2 1 0
 *                                | | | |
 *                                | | | |
 *                                | | | └-------------------┐
 *                                | | └-------------------┐ |
 *                                | └-------------------┐ | |
 *                                └-------------------┐ | | |
 *                                                    | | | |
 *                                                    | | | |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   Used in handling following instructions:
 *
 *     - MOVEP
 *     - MOVEP[REV]
 */
static uint64 decode_gpr_gpr2_reg1(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr2_reg2() - decoder for 'gpr2.reg2' gpr encoding type
 *
 *   Map a 2-bit code to the 5-bit register space according to this pattern:
 *
 *                                3 2 1 0
 *                                | | | |
 *                                | | | |
 *                                | | | └-----------------┐
 *                                | | └-----------------┐ |
 *                                | └-----------------┐ | |
 *                                └-----------------┐ | | |
 *                                                  | | | |
 *                                                  | | | |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   Used in handling following instructions:
 *
 *     - MOVEP
 *     - MOVEP[REV]
 */
static uint64 decode_gpr_gpr2_reg2(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  5,  6,  7,  8 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


/*
 * decode_gpr_gpr1() - decoder for 'gpr1' gpr encoding type
 *
 *   Map a 1-bit code to the 5-bit register space according to this pattern:
 *
 *                                  1 0
 *                                  | |
 *                                  | |
 *                                  | └---------------------┐
 *                                  └---------------------┐ |
 *                                                        | |
 *                                                        | |
 *                                                        | |
 *                                                        | |
 *    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *      3                   2                   1                   0
 *
 *   Used in handling following instruction:
 *
 *     - MOVE.BALC
 */
static uint64 decode_gpr_gpr1(uint64 d, Dis_info *info)
{
    static uint64 register_list[] = {  4,  5 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]), info);
}


static int64 neg_copy(uint64 d)
{
    return 0ll - d;
}


static uint64 encode_count3_from_count(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


static uint64 encode_shift3_from_shift(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


/* special value for load literal */
static int64 encode_eu_from_s_li16(uint64 d)
{
    IMGASSERTONCE(d < 128);
    return d == 127 ? -1 : (int64)d;
}


static uint64 encode_msbd_from_size(uint64 d)
{
    IMGASSERTONCE(d < 32);
    return d + 1;
}


static uint64 encode_eu_from_u_andi16(uint64 d)
{
    IMGASSERTONCE(d < 16);
    if (d == 12) {
        return 0x00ffull;
    }
    if (d == 13) {
        return 0xffffull;
    }
    return d;
}


/* save16 / restore16   ???? */
static uint64 encode_rt1_from_rt(uint64 d)
{
    return d ? 31 : 30;
}


static const char *GPR(uint64 reg, Dis_info *info)
{
    static const char *gpr_reg[32] = {
        "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
        "a4",   "a5",   "a6",   "a7",   "r12",  "r13",  "r14",  "r15",
        "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
        "r24",  "r25",  "k0",   "k1",   "gp",   "sp",   "fp",   "ra"
    };

    if (reg < 32) {
        return gpr_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid GPR register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static char *save_restore_list(uint64 rt, uint64 count, uint64 gp,
                               Dis_info *info)
{
    char *reg_list[34];
    reg_list[0] = (char *)"";

    assert(count <= 32);
    for (uint64 counter = 0; counter != count; counter++) {
        bool use_gp = gp && (counter == count - 1);
        uint64 this_rt = use_gp ? 28 : ((rt & 0x10) | (rt + counter)) & 0x1f;
        /* glib usage below requires casting away const */
        reg_list[counter + 1] = (char *)GPR(this_rt, info);
    }
    reg_list[count + 1] = NULL;

    return g_strjoinv(",", reg_list);
}


static const char *FPR(uint64 reg, Dis_info *info)
{
    static const char *fpr_reg[32] = {
        "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
        "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"
    };

    if (reg < 32) {
        return fpr_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid FPR register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static const char *AC(uint64 reg, Dis_info *info)
{
    static const char *ac_reg[4] = {
        "ac0",  "ac1",  "ac2",  "ac3"
    };

    if (reg < 4) {
        return ac_reg[reg];
    }

    info->fprintf_func(info->stream, "Invalid AC register index %" PRIu64,
                       reg);
    siglongjmp(info->buf, 1);
}


static char *ADDRESS(uint64 value, int instruction_size, Dis_info *info)
{
    /* token for string replace */
    img_address address = info->m_pc + value + instruction_size;
    /* symbol replacement */
    return to_string(address);
}


static uint64 extract_op_code_value(const uint16 *data, int size)
{
    switch (size) {
    case 16:
        return data[0];
    case 32:
        return ((uint64)data[0] << 16) | data[1];
    case 48:
        return ((uint64)data[0] << 32) | ((uint64)data[1] << 16) | data[2];
    default:
        return data[0];
    }
}


/*
 * Recurse through tables until the instruction is found then return
 * the string and size
 *
 * inputs:
 *      pointer to a word stream,
 *      disassember table and size
 * returns:
 *      instruction size    - negative is error
 *      disassembly string  - on error will constain error string
 */
static int Disassemble(const uint16 *data, char **dis,
                     TABLE_ENTRY_TYPE *type, const Pool *table,
                     int table_size, Dis_info *info)
{
    for (int i = 0; i < table_size; i++) {
        uint64 op_code = extract_op_code_value(data,
                             table[i].instructions_size);
        if ((op_code & table[i].mask) == table[i].value) {
            /* possible match */
            conditional_function cond = table[i].condition;
            if ((cond == NULL) || cond(op_code)) {
                if (table[i].type == pool) {
                    return Disassemble(data, dis, type,
                                       table[i].next_table,
                                       table[i].next_table_size,
                                       info);
                } else if ((table[i].type == instruction) ||
                           (table[i].type == call_instruction) ||
                           (table[i].type == branch_instruction) ||
                           (table[i].type == return_instruction)) {
                    disassembly_function dis_fn = table[i].disassembly;
                    if (dis_fn == 0) {
                        *dis = g_strdup(
                            "disassembler failure - bad table entry");
                        return -6;
                    }
                    *type = table[i].type;
                    *dis = dis_fn(op_code, info);
                    return table[i].instructions_size;
                } else {
                    *dis = g_strdup("reserved instruction");
                    return -2;
                }
            }
        }
    }
    *dis = g_strdup("failed to disassemble");
    return -1;      /* failed to disassemble        */
}


static uint64 extract_code_18_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 19);
    return value;
}


static uint64 extract_shift3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


static uint64 extract_u_11_10_9_8_7_6_5_4_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 9) << 3;
    return value;
}


static uint64 extract_count_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


static uint64 extract_rtz3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


static uint64 extract_u_17_to_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 17) << 1;
    return value;
}


static int64 extract_s__se9_20_19_18_17_16_15_14_13_12_11(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 11, 10);
    value = sign_extend(value, 9);
    return value;
}


static int64 extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 11;
    value |= extract_bits(instruction, 1, 10) << 1;
    value = sign_extend(value, 11);
    return value;
}


static uint64 extract_u_10(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 10, 1);
    return value;
}


static uint64 extract_rtz4_27_26_25_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    value |= extract_bits(instruction, 25, 1) << 3;
    return value;
}


static uint64 extract_sa_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_shift_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_shiftx_10_9_8_7__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 4) << 1;
    return value;
}


static uint64 extract_hint_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_count3_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 3);
    return value;
}


static int64 extract_s__se31_0_11_to_2_20_to_12_s12(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 31;
    value |= extract_bits(instruction, 2, 10) << 21;
    value |= extract_bits(instruction, 12, 9) << 12;
    value = sign_extend(value, 31);
    return value;
}


static int64 extract_s__se7_0_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 7;
    value |= extract_bits(instruction, 1, 6) << 1;
    value = sign_extend(value, 7);
    return value;
}


static uint64 extract_u2_10_9(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 2);
    return value;
}


static uint64 extract_code_25_24_23_22_21_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 10);
    return value;
}


static uint64 extract_rs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_u_2_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 2) << 1;
    return value;
}


static uint64 extract_stripe_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 1);
    return value;
}


static uint64 extract_ac_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 2);
    return value;
}


static uint64 extract_shift_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rdl_25_24(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 24, 1);
    return value;
}


static int64 extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 10;
    value |= extract_bits(instruction, 1, 9) << 1;
    value = sign_extend(value, 10);
    return value;
}


static uint64 extract_eu_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7);
    return value;
}


static uint64 extract_shift_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6);
    return value;
}


static uint64 extract_count_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 4);
    return value;
}


static uint64 extract_code_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


static uint64 extract_u_11_10_9_8_7_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 12);
    return value;
}


static uint64 extract_rs_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_u_20_to_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 18) << 3;
    return value;
}


static uint64 extract_u_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 2;
    return value;
}


static uint64 extract_cofun_25_24_23(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


static uint64 extract_u_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3) << 2;
    return value;
}


static uint64 extract_rd3_3_2_1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 3);
    return value;
}


static uint64 extract_sa_15_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 4);
    return value;
}


static uint64 extract_rt_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_ru_7_6_5_4_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 5);
    return value;
}


static uint64 extract_u_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


static uint64 extract_rsz4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


static int64 extract_s__se21_0_20_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 21;
    value |= extract_bits(instruction, 1, 20) << 1;
    value = sign_extend(value, 21);
    return value;
}


static uint64 extract_op_25_to_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


static uint64 extract_rs4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


static uint64 extract_bit_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    return value;
}


static uint64 extract_rt_41_40_39_38_37(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 37, 5);
    return value;
}


static int64 extract_shift__se5_21_20_19_18_17_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 16, 6);
    value = sign_extend(value, 5);
    return value;
}


static uint64 extract_rd2_3_8(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 1;
    value |= extract_bits(instruction, 8, 1);
    return value;
}


static uint64 extract_code_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


static uint64 extract_size_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_2_s2(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 2, 6) << 2;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_u_15_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 16);
    return value;
}


static uint64 extract_fs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 8);
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_stype_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rtl_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 1);
    return value;
}


static uint64 extract_hs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_sel_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 3);
    return value;
}


static uint64 extract_lsb_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


static uint64 extract_gp_2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 1);
    return value;
}


static uint64 extract_rt3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


static uint64 extract_ft_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_17_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 7);
    return value;
}


static uint64 extract_cs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_rt4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


static uint64 extract_msbt_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


static uint64 extract_u_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6) << 2;
    return value;
}


static uint64 extract_sa_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 3);
    return value;
}


static int64 extract_s__se14_0_13_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 14;
    value |= extract_bits(instruction, 1, 13) << 1;
    value = sign_extend(value, 14);
    return value;
}


static uint64 extract_rs3_6_5_4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 3);
    return value;
}


static uint64 extract_u_31_to_0__s32(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 32) << 32;
    return value;
}


static uint64 extract_shift_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


static uint64 extract_cs_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_shiftx_11_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 6);
    return value;
}


static uint64 extract_rt_9_8_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 5);
    return value;
}


static uint64 extract_op_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_6_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7) << 2;
    return value;
}


static uint64 extract_bit_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 6);
    return value;
}


static uint64 extract_mask_20_19_18_17_16_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 7);
    return value;
}


static uint64 extract_eu_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


static uint64 extract_u_7_6_5_4__s4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 4) << 4;
    return value;
}


static int64 extract_s__se8_15_7_6_5_4_3_s3(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 3, 5) << 3;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


static uint64 extract_ft_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static int64 extract_s__se31_15_to_0_31_to_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 16) << 16;
    value |= extract_bits(instruction, 16, 16);
    value = sign_extend(value, 31);
    return value;
}


static uint64 extract_u_20_19_18_17_16_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 8);
    return value;
}


static uint64 extract_u_17_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 16) << 2;
    return value;
}


static uint64 extract_rd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_c0s_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


static uint64 extract_code_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


static int64 extract_s__se25_0_24_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 25;
    value |= extract_bits(instruction, 1, 24) << 1;
    value = sign_extend(value, 25);
    return value;
}


static uint64 extract_u_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


static uint64 extract_u_3_8__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 3;
    value |= extract_bits(instruction, 8, 1) << 2;
    return value;
}


static uint64 extract_fd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_u_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5) << 2;
    return value;
}


static uint64 extract_rtz4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


static uint64 extract_sel_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


static uint64 extract_ct_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


static uint64 extract_u_20_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 19) << 2;
    return value;
}


static int64 extract_s__se3_4_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    value = sign_extend(value, 3);
    return value;
}


static uint64 extract_u_3_2_1_0__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 1;
    return value;
}



static bool ADDIU_32__cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


static bool ADDIU_RS5__cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


static bool BALRSC_cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


static bool BEQC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 < rt3 && u != 0;
}


static bool BNEC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 >= rt3 && u != 0;
}


static bool MOVE_cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


static bool P16_BR1_cond(uint64 instruction)
{
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return u != 0;
}


static bool PREF_S9__cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


static bool PREFE_cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


static bool SLTU_cond(uint64 instruction)
{
    uint64 rd = extract_rd_15_14_13_12_11(instruction);
    return rd != 0;
}



/*
 * ABS.D fd, fs - Floating Point Absolute Value
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  010001     00000          000101
 *    fmt -----
 *               fs -----
 *                    fd -----
 */
static char *ABS_D(uint64 instruction, Dis_info *info)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ABS.D %s, %s", fd, fs);
}


/*
 * ABS.S fd, fs - Floating Point Absolute Value
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  010001     00000          000101
 *    fmt -----
 *               fd -----
 *                    fs -----
 */
static char *ABS_S(uint64 instruction, Dis_info *info)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ABS.S %s, %s", fd, fs);
}


/*
 * [DSP] ABSQ_S.PH rt, rs - Find absolute value of two fractional halfwords
 *         with 16-bit saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0001000100111111
 *     rt -----
 *          rs -----
 */
static char *ABSQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.PH %s, %s", rt, rs);
}


/*
 * [DSP] ABSQ_S.QB rt, rs - Find absolute value of four fractional byte values
 *         with 8-bit saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0000000100111111
 *     rt -----
 *          rs -----
 */
static char *ABSQ_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.QB %s, %s", rt, rs);
}


/*
 * [DSP] ABSQ_S.W rt, rs - Find absolute value of fractional word with 32-bit
 *         saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ABSQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ABSQ_S.W %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ACLR(uint64 instruction, Dis_info *info)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("ACLR 0x%" PRIx64 ", %" PRId64 "(%s)",
                      bit_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADD %s, %s, %s", rd, rs, rt);
}


/*
 * ADD.D fd, fs, ft - Floating Point Add
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  010001                    000101
 *    fmt -----
 *          ft -----
 *               fs -----
 *                    fd -----
 */
static char *ADD_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ADD.D %s, %s, %s", fd, fs, ft);
}


/*
 * ADD.S fd, fs, ft - Floating Point Add
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  010001                    000101
 *    fmt -----
 *          ft -----
 *               fs -----
 *                    fd -----
 */
static char *ADD_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);
    const char *fd = FPR(fd_value, info);

    return img_format("ADD.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_15_to_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ADDIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, %" PRId64, rt, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_GP48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, %" PRId64, rt, 28, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_GP_B_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt, 28, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_GP_W_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt, 28, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_NEG_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    int64 u = neg_copy(u_value);

    return img_format("ADDIU %s, %s, %" PRId64, rt, rs, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_R1_SP_(uint64 instruction, Dis_info *info)
{
    uint64 u_value = extract_u_5_4_3_2_1_0__s2(instruction);
    uint64 rt3_value = extract_rt3_9_8_7(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("ADDIU %s, $%d, 0x%" PRIx64, rt3, 29, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0010000100111111
 *     rt -----
 *          rs -----
 */
static char *ADDIU_R2_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("ADDIU %s, %s, 0x%" PRIx64, rt3, rs3, u_value);
}


/*
 * ADDIU[RS5] rt, s5 - Add Signed Word and Set Carry Bit
 *
 *  5432109876543210
 *  100100      1
 *     rt -----
 *           s - ---
 */
static char *ADDIU_RS5_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    int64 s_value = extract_s__se3_4_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("ADDIU %s, %" PRId64, rt, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDIUPC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("ADDIUPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDIUPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("ADDIUPC %s, %s", rt, s);
}


/*
 * [DSP] ADDQ.PH rd, rt, rs - Add fractional halfword vectors
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00000001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQ_S.PH rd, rt, rs - Add fractional halfword vectors with 16-bit
 *         saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10000001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQ_S.W rd, rt, rs - Add fractional words with 32-bit saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1100000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQ_S.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQH.PH rd, rt, rs - Add fractional halfword vectors and shift
 *         right to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQH_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQH_R.PH rd, rt, rs - Add fractional halfword vectors and shift
 *         right to halve results with rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQH_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH_R.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQH_R.W rd, rt, rs - Add fractional words and shift right to halve
 *         results with rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQH_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH_R.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDQH.W rd, rt, rs - Add fractional words and shift right to halve
 *         results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDQH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDQH.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDSC rd, rt, rs - Add two signed words and set carry bit
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDSC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDSC %s, %s, %s", rd, rs, rt);
}


/*
 * ADDU[16] rd3, rs3, rt3 -
 *
 *  5432109876543210
 *  101100         0
 *    rt3 ---
 *       rs3 ---
 *          rd3 ---
 */
static char *ADDU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);

    return img_format("ADDU %s, %s, %s", rd3, rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);

    return img_format("ADDU %s, %s", rs4, rt4);
}


/*
 * [DSP] ADDU.PH rd, rt, rs - Add two pairs of unsigned halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00100001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU.PH %s, %s, %s", rd, rs, rt);
}


/*
 * ADDU.QB rd, rt, rs - Unsigned Add Quad Byte Vectors
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00011001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] ADDU_S.PH rd, rt, rs - Add two pairs of unsigned halfwords with 16-bit
 *         saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10100001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * ADDU_S.QB rd, rt, rs - Unsigned Add Quad Byte Vectors
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10011001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDU_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDU_S.QB %s, %s, %s", rd, rs, rt);
}


/*
 * ADDUH.QB rd, rt, rs - Unsigned Add Vector Quad-Bytes And Right Shift
 *                       to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00101001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDUH_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDUH.QB %s, %s, %s", rd, rs, rt);
}


/*
 * ADDUH_R.QB rd, rt, rs - Unsigned Add Vector Quad-Bytes And Right Shift
 *                         to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10101001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDUH_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDUH_R.QB %s, %s, %s", rd, rs, rt);
}

/*
 * ADDWC rd, rt, rs - Add Word with Carry Bit
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1111000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ADDWC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ADDWC %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ALUIPC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("ALUIPC %s, %%pcrel_hi(%s)", rt, s);
}


/*
 * AND[16] rt3, rs3 -
 *
 *  5432109876543210
 *  101100
 *    rt3 ---
 *       rs3 ---
 *           eu ----
 */
static char *AND_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("AND %s, %s", rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *AND_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("AND %s, %s, %s", rd, rs, rt);
}


/*
 * ANDI rt, rs, u -
 *
 *  5432109876543210
 *  101100
 *    rt3 ---
 *       rs3 ---
 *           eu ----
 */
static char *ANDI_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 eu_value = extract_eu_3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 eu = encode_eu_from_u_andi16(eu_value);

    return img_format("ANDI %s, %s, 0x%" PRIx64, rt3, rs3, eu);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ANDI_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ANDI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *APPEND(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("APPEND %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ASET(uint64 instruction, Dis_info *info)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("ASET 0x%" PRIx64 ", %" PRId64 "(%s)",
                      bit_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BALC_16_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BALC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BALC_32_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BALC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BALRSC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("BALRSC %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BBEQZC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BBEQZC %s, 0x%" PRIx64 ", %s", rt, bit_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BBNEZC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BBNEZC %s, 0x%" PRIx64 ", %s", rt, bit_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC_16_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC_32_(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC1EQZC(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *ft = FPR(ft_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC1EQZC %s, %s", ft, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC1NEZC(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *ft = FPR(ft_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC1NEZC %s, %s", ft, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC2EQZC(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC2EQZC CP%" PRIu64 ", %s", ct_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BC2NEZC(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BC2NEZC CP%" PRIu64 ", %s", ct_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BEQC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *u = ADDRESS(u_value, 2, info);

    return img_format("BEQC %s, %s, %s", rs3, rt3, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BEQC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BEQC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BEQIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BEQIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BEQZC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BEQZC %s, %s", rt3, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BGEC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BGEIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BGEIUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEIUC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BGEUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BGEUC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BLTC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BLTIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BLTIUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTIUC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BLTUC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BLTUC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BNEC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *u = ADDRESS(u_value, 2, info);

    return img_format("BNEC %s, %s, %s", rs3, rt3, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BNEC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BNEC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BNEIC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BNEIC %s, 0x%" PRIx64 ", %s", rt, u_value, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BNEZC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 2, info);

    return img_format("BNEZC %s, %s", rt3, s);
}


/*
 * [DSP] BPOSGE32C offset - Branch on greater than or equal to value 32 in
 *   DSPControl Pos field
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  100010xxxxx0010001
 *            s[13:1] -------------
 *                           s[14] -
 */
static char *BPOSGE32C(uint64 instruction, Dis_info *info)
{
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("BPOSGE32C %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BREAK_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_2_1_0(instruction);


    return img_format("BREAK 0x%" PRIx64, code_value);
}


/*
 * BREAK code - Break. Cause a Breakpoint exception
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BREAK_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("BREAK 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *BRSC(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("BRSC %s", rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CACHE(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("CACHE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      op_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CACHEE(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("CACHEE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      op_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CEIL_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CEIL_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CEIL_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CEIL_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CEIL.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CFC1 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CFC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CLASS_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CLASS.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CLASS_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CLASS.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CLO(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("CLO %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CLZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("CLZ %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_AF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.AF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_AF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.AF.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_EQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.EQ.D %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] CMP.EQ.PH rs, rt - Compare vectors of signed integer halfword values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx0000000101
 *     rt -----
 *          rs -----
 */
static char *CMP_EQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.EQ.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_EQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.EQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_LE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LE.D %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] CMP.LE.PH rs, rt - Compare vectors of signed integer halfword values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx0010000101
 *     rt -----
 *          rs -----
 */
static char *CMP_LE_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.LE.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_LE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_LT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LT.D %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] CMP.LT.PH rs, rt - Compare vectors of signed integer halfword values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx0001000101
 *     rt -----
 *          rs -----
 */
static char *CMP_LT_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMP.LT.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_LT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.LT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_NE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.NE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_NE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.NE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_OR_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.OR.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_OR_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.OR.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SAF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SAF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SAF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SAF.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SLE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SLE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SLT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SLT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SLT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SOR_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SOR.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SOR_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SOR.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SULE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SULE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SULT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SULT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SULT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_SUN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.SUN.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UEQ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UEQ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_ULE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_ULE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_ULT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_ULT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.ULT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UNE_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UNE_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMP_UN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("CMP.UN.S %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] CMPGDU.EQ.QB rd, rs, rt - Compare unsigned vector of
 *   four bytes and write result to GPR and DSPControl
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGDU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.EQ.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPGDU.LE.QB rd, rs, rt - Compare unsigned vector of
 *   four bytes and write result to GPR and DSPControl
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1000000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGDU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.LE.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPGDU.EQ.QB rd, rs, rt - Compare unsigned vector of
 *   four bytes and write result to GPR and DSPControl
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0111000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGDU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGDU.LT.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPGU.EQ.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values and write result to a GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0011000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.EQ.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPGU.LE.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values and write result to a GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0101000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.LE.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPGU.LT.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values and write result to a GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0100000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CMPGU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPGU.LT.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] CMPU.EQ.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx1001000101
 *     rt -----
 *          rs -----
 */
static char *CMPU_EQ_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.EQ.QB %s, %s", rs, rt);
}


/*
 * [DSP] CMPU.LE.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx1011000101
 *     rt -----
 *          rs -----
 */
static char *CMPU_LE_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.LE.QB %s, %s", rs, rt);
}


/*
 * [DSP] CMPU.LT.QB rd, rs, rt - Compare vectors of unsigned
 *   byte values
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          xxxxxx1010000101
 *     rt -----
 *          rs -----
 */
static char *CMPU_LT_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("CMPU.LT.QB %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *COP2_1(uint64 instruction, Dis_info *info)
{
    uint64 cofun_value = extract_cofun_25_24_23(instruction);


    return img_format("COP2_1 0x%" PRIx64, cofun_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CTC1 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("CTC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_D_L(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.L %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_D_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_D_W(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.D.W %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_S_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_S_L(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.L %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_S_PL(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.PL %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_S_PU(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.PU %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_S_W(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.S.W %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *CVT_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("CVT.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DADDIU_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DADDIU %s, %" PRId64, rt, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DADDIU_NEG_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    int64 u = neg_copy(u_value);

    return img_format("DADDIU %s, %s, %" PRId64, rt, rs, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DADDIU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DADDIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DADD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DADD %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DADDU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DADDU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DCLO(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DCLO %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DCLZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DCLZ %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DDIV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DDIV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DDIVU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DDIVU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DERET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("DERET ");
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DEXTM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXTM %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DEXT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXT %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DEXTU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("DEXTU %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DINSM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    /* !!!!!!!!!! - no conversion function */

    return img_format("DINSM %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
    /* hand edited */
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DINS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    /* !!!!!!!!!! - no conversion function */

    return img_format("DINS %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
    /* hand edited */
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DINSU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    /* !!!!!!!!!! - no conversion function */

    return img_format("DINSU %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
    /* hand edited */
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DI %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DIV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DIV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DIV_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("DIV.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DIV_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("DIV.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DIVU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DIVU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DLSA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DLSA %s, %s, %s, 0x%" PRIx64, rd, rs, rt, u2_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DLUI_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    uint64 u_value = extract_u_31_to_0__s32(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DLUI %s, 0x%" PRIx64, rt, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMFC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("DMFC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMFGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMFGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMOD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMOD %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMODU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMODU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMTC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("DMTC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMTGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMTGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DMT %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMUH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUH %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMUHU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUHU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMUL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMUL %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DMULU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DMULU %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] DPA.W.PH ac, rs, rt - Dot product with accumulate on
 *   vector integer halfword elements
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            00000010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *DPA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAQ_SA_L_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAQX_SA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAQX_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAU_H_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAU.H.QBL %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAU_H_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAU.H.QBR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPAX_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPAX.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPS_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPS.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSQ_SA_L_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSQX_SA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSQX_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSU_H_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSU.H.QBL %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSU_H_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSU.H.QBR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DPSX_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DPSX.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 * DROTR -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DROTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTR %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 * DROTR[32] -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0110
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DROTR32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTR32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DROTRV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DROTRV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DROTX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_11_10_9_8_7_6(instruction);
    uint64 shift_value = extract_shift_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DROTX %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, shift_value, shiftx_value);
}


/*
 * DSLL -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0000
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSLL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSLL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 * DSLL[32] -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0000
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSLL32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSLL32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DSLLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSLLV %s, %s, %s", rd, rs, rt);
}


/*
 * DSRA -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0100
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRA %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 * DSRA[32] -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0100
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSRA32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRA32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DSRAV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSRAV %s, %s, %s", rd, rs, rt);
}


/*
 * DSRL -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0100
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSRL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 * DSRL[32] -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  10o000          1100xxx0010
 *     rt -----
 *          rs -----
 *                       shift -----
 */
static char *DSRL32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("DSRL32 %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DSRLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSRLV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DSUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSUB %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DSUBU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("DSUBU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DVPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DVPE %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *DVP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("DVP %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EHB(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("EHB ");
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EI %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EMT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EMT %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ERET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("ERET ");
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ERETNC(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("ERETNC ");
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EVP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EVP %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EVPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("EVPE %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 msbd = encode_msbd_from_size(msbd_value);

    return img_format("EXT %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTD %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTD32(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTD32 %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTPDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTPDP %s, %s, 0x%" PRIx64, rt, ac, size_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTPDPV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTPDPV %s, %s, %s", rt, ac, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTP %s, %s, 0x%" PRIx64, rt, ac, size_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *EXTPV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTPV %s, %s, %s", rt, ac, rs);
}


/*
 * [DSP] EXTR_RS.W rt, ac, shift - Extract word value from accumulator to GPR
 *   with right shift
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            10111001111111
 *     rt -----
 *       shift -----
 *               ac --
 */
static char *EXTR_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_RS.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


/*
 * [DSP] EXTR_R.W rt, ac, shift - Extract word value from accumulator to GPR
 *   with right shift
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            01111001111111
 *     rt -----
 *       shift -----
 *               ac --
 */
static char *EXTR_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_R.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


/*
 * [DSP] EXTR_S.H rt, ac, shift - Extract halfword value from accumulator
 *   to GPR with right shift and saturate
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            11111001111111
 *     rt -----
 *       shift -----
 *               ac --
 */
static char *EXTR_S_H(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR_S.H %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


/*
 * [DSP] EXTR.W rt, ac, shift - Extract word value from accumulator to GPR
 *   with right shift
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            00111001111111
 *     rt -----
 *       shift -----
 *               ac --
 */
static char *EXTR_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("EXTR.W %s, %s, 0x%" PRIx64, rt, ac, shift_value);
}


/*
 * [DSP] EXTRV_RS.W rt, ac, rs - Extract word value with variable
 *   right shift from accumulator to GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            10111010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *EXTRV_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_RS.W %s, %s, %s", rt, ac, rs);
}


/*
 * [DSP] EXTRV_R.W rt, ac, rs - Extract word value with variable
 *   right shift from accumulator to GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            01111010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *EXTRV_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_R.W %s, %s, %s", rt, ac, rs);
}


/*
 * [DSP] EXTRV_S.H rt, ac, rs - Extract halfword value variable from
 *   accumulator to GPR with right shift and saturate
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            11111010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *EXTRV_S_H(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV_S.H %s, %s, %s", rt, ac, rs);
}


/*
 * [DSP] EXTRV.W rt, ac, rs - Extract word value with variable
 *   right shift from accumulator to GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            00111010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *EXTRV_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("EXTRV.W %s, %s, %s", rt, ac, rs);
}


/*
 * EXTW - Extract Word
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000                    011111
 *     rt -----
 *          rs -----
 *               rd -----
 *                 shift -----
 */
static char *EXTW(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("EXTW %s, %s, %s, 0x%" PRIx64, rd, rs, rt, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *FLOOR_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *FLOOR_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *FLOOR_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *FLOOR_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("FLOOR.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *FORK(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("FORK %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *HYPCALL(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_17_to_0(instruction);


    return img_format("HYPCALL 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *HYPCALL_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_1_0(instruction);


    return img_format("HYPCALL 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *INS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    /* !!!!!!!!!! - no conversion function */

    return img_format("INS %s, %s, 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, rs, lsb_value, msbd_value);
    /* hand edited */
}


/*
 * [DSP] INSV rt, rs - Insert bit field variable
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0100000100111111
 *     rt -----
 *          rs -----
 */
static char *INSV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("INSV %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *IRET(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("IRET ");
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *JALRC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("JALRC $%d, %s", 31, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *JALRC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("JALRC %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *JALRC_HB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("JALRC.HB %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *JRC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("JRC %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LB_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LB %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LB_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LB %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LB_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LB %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LB_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LB %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LBU %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LBU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBUE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LBUE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LBUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LBX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LBX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LD_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LD %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LD_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LD_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LD %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("LDC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LDC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LDC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDC1X %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDC2(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("LDC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      ct_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("LDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("LDPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LDXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LDXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LH_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LH %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LH_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LH %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LH_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LH_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LH %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LHU %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LHU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHUE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LHUE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHUXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHUXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LHX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LHX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LI_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 eu_value = extract_eu_6_5_4_3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    int64 eu = encode_eu_from_s_li16(eu_value);

    return img_format("LI %s, %" PRId64, rt3, eu);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LI_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LI %s, %" PRId64, rt, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LL %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LLD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LLDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLDP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LLE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LLWP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLWP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LLWPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LLWPE %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LSA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LSA %s, %s, %s, 0x%" PRIx64, rd, rs, rt, u2_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LUI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LUI %s, %%hi(%" PRId64 ")", rt, s_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);
    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt4, u_value, rs4);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_GP16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt3, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LW %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_SP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LW %s, 0x%" PRIx64 "($%d)", rt, u_value, 29);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LW_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LW %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("LWC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWC1X %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWC2(uint64 instruction, Dis_info *info)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("LWC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      ct_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("LWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("LWPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWU_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("LWU %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWU_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWU %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWU_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("LWU %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWUX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWUXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWUXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWXS_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 rt3 = decode_gpr_gpr3(rt3_value, info);

    return img_format("LWXS %s, %s(0x%" PRIx64 ")", rd3, rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *LWXS_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("LWXS %s, %s(%s)", rd, rs, rt);
}


/*
 * [DSP] MADD ac, rs, rt - Multiply two words and add to the specified
 *         accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MADD_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MADD %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MADDF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MADDF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MADDF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MADDF.S %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] MADDU ac, rs, rt - Multiply two unsigned words and add to the
 *         specified accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MADDU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MADDU %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MAQ_S.W.PHL ac, rs, rt - Multiply the left-most single vector
 *         fractional halfword elements with accumulation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAQ_S_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_S.W.PHL %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MAQ_S.W.PHR ac, rs, rt - Multiply the right-most single vector
 *         fractional halfword elements with accumulation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAQ_S_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_S.W.PHR %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MAQ_SA.W.PHL ac, rs, rt - Multiply the left-most single vector
 *         fractional halfword elements with saturating accumulation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAQ_SA_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_SA.W.PHL %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MAQ_SA.W.PHR ac, rs, rt - Multiply the right-most single vector
 *         fractional halfword elements with saturating accumulation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAQ_SA_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MAQ_SA.W.PHR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAX_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAX.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAX_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAX.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAXA_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAXA.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MAXA_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MAXA.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MFC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFHC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFHC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MFHC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFHC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFHGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 * [DSP] MFHI rs, ac - Move from HI register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000     xxxxx  00000001111111
 *     rt -----
 *               ac --
 */
static char *MFHI_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MFHI %s, %s", rt, ac);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFHTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFHTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


/*
 * [DSP] MFLO rs, ac - Move from HI register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000     xxxxx  01000001111111
 *     rt -----
 *               ac --
 */
static char *MFLO_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MFLO %s, %s", rt, ac);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MFTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MFTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MIN_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MIN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MIN_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MIN.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MINA_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MINA.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MINA_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MINA.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOD %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MODSUB rd, rs, rt - Modular subtraction on an index value
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MODSUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MODSUB %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1010010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MODU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MODU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOV_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MOV.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOV_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MOV.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVE_BALC(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_27_26_25_23_22_21(instruction);
    uint64 rd1_value = extract_rdl_25_24(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    const char *rd1 = GPR(decode_gpr_gpr1(rd1_value, info), info);
    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);
    g_autofree char *s = ADDRESS(s_value, 4, info);

    return img_format("MOVE.BALC %s, %s, %s", rd1, rtz4, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVEP(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rsz4_value = extract_rsz4_4_2_1_0(instruction);

    const char *rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value, info), info);
    const char *re2 = GPR(decode_gpr_gpr2_reg2(rd2_value, info), info);
    /* !!!!!!!!!! - no conversion function */
    const char *rsz4 = GPR(decode_gpr_gpr4_zero(rsz4_value, info), info);
    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);

    return img_format("MOVEP %s, %s, %s, %s", rd2, re2, rsz4, rtz4);
    /* hand edited */
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVEP_REV_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);
    const char *rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value, info), info);
    const char *rs2 = GPR(decode_gpr_gpr2_reg2(rd2_value, info), info);
    /* !!!!!!!!!! - no conversion function */

    return img_format("MOVEP %s, %s, %s, %s", rs4, rt4, rd2, rs2);
    /* hand edited */
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 rs_value = extract_rs_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("MOVE %s, %s", rt, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVN(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOVN %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MOVZ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MOVZ %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MSUB ac, rs, rt - Multiply word and subtract from accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            10101010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MSUB_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MSUB %s, %s, %s", ac, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MSUBF_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MSUBF.D %s, %s, %s", fd, fs, ft);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MSUBF_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MSUBF.S %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] MSUBU ac, rs, rt - Multiply word and add to accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            11101010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MSUBU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MSUBU %s, %s, %s", ac, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MTC1 %s, %s", rt, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTHC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTHC1(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("MTHC1 %s, %s", rt, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTHC2(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHC2 %s, CP%" PRIu64, rt, cs_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTHGC0(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHGC0 %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, c0s_value, sel_value);
}


/*
 * [DSP] MTHI rs, ac - Move to HI register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000xxxxx       10000001111111
 *          rs -----
 *               ac --
 */
static char *MTHI_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTHI %s, %s", rs, ac);
}


/*
 * [DSP] MTHLIP rs, ac - Copy LO to HI and a GPR to LO and increment pos by 32
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000xxxxx       00001001111111
 *          rs -----
 *               ac --
 */
static char *MTHLIP(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTHLIP %s, %s", rs, ac);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTHTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTHTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


/*
 * [DSP] MTLO rs, ac - Move to LO register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000xxxxx       11000001111111
 *          rs -----
 *               ac --
 */
static char *MTLO_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("MTLO %s, %s", rs, ac);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MTTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("MTTR %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                      rt, c0s_value, u_value, sel_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUH %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUHU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUHU %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);
    const char *rt4 = GPR(decode_gpr_gpr4(rt4_value, info), info);

    return img_format("MUL %s, %s", rs4, rt4);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MUL.D %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] MUL.PH rd, rs, rt - Multiply vector integer half words to same size
 *   products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00000101101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MUL_S.PH rd, rs, rt - Multiply vector integer half words to same size
 *   products (saturated)
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               10000101101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MUL_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MUL_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("MUL.S %s, %s, %s", fd, fs, ft);
}


/*
 * [DSP] MULEQ_S.W.PHL rd, rs, rt - Multiply vector fractional left halfwords
 *   to expanded width products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0000100101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULEQ_S_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEQ_S.W.PHL %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULEQ_S.W.PHR rd, rs, rt - Multiply vector fractional right halfwords
 *   to expanded width products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0001100101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULEQ_S_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEQ_S.W.PHR %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULEU_S.PH.QBL rd, rs, rt - Multiply vector fractional left bytes
 *   by halfwords to halfword products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0010010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULEU_S_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEU_S.PH.QBL %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULEU_S.PH.QBR rd, rs, rt - Multiply vector fractional right bytes
 *   by halfwords to halfword products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0011010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULEU_S_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULEU_S.PH.QBR %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULQ_RS.PH rd, rs, rt - Multiply vector fractional halfwords
 *   to fractional halfword products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0100010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULQ_RS_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_RS.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULQ_RS.W rd, rs, rt - Multiply fractional words to same size
 *   product with saturation and rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0110010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULQ_RS_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_RS.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULQ_S.PH rd, rs, rt - Multiply fractional halfwords to same size
 *   products
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0101010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULQ_S.W rd, rs, rt - Multiply fractional words to same size product
 *   with saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0111010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULQ_S.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] MULSA.W.PH ac, rs, rt - Multiply and subtract vector integer halfword
 *   elements and accumulate
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            10110010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MULSA_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULSA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MULSAQ_S.W.PH ac, rs, rt - Multiply and subtract vector fractional
 *   halfwords and accumulate
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            11110010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MULSAQ_S_W_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULSAQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MULT ac, rs, rt - Multiply word
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            00110010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MULT_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULT %s, %s, %s", ac, rs, rt);
}


/*
 * [DSP] MULTU ac, rs, rt - Multiply unsigned word
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            01110010111111
 *     rt -----
 *          rs -----
 *               ac --
 */
static char *MULTU_DSP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULTU %s, %s, %s", ac, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *MULU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("MULU %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NEG_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("NEG.D %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NEG_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("NEG.S %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NOP_16_(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("NOP ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NOP_32_(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("NOP ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NOR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("NOR %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *NOT_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("NOT %s, %s", rt3, rs3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *OR_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("OR %s, %s", rs3, rt3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *OR_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("OR %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ORI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ORI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 * [DSP] PACKRL.PH rd, rs, rt - Pack a word using the right halfword from one
 *         source register and left halfword from another source register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PACKRL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PACKRL.PH %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PAUSE(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("PAUSE ");
}


/*
 * [DSP] PICK.PH rd, rs, rt - Pick a vector of halfwords based on condition
 *         code bits
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PICK_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PICK.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PICK.QB rd, rs, rt - Pick a vector of byte values based on condition
 *         code bits
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PICK_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PICK.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PRECEQ.W.PHL rt, rs - Expand the precision of the left-most element
 *         of a paired halfword
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQ_W_PHL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQ.W.PHL %s, %s", rt, rs);
}


/*
 * [DSP] PRECEQ.W.PHR rt, rs - Expand the precision of the right-most element
 *         of a paired halfword
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQ_W_PHR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQ.W.PHR %s, %s", rt, rs);
}


/*
 * [DSP] PRECEQU.PH.QBLA rt, rs - Expand the precision of the two
 *         left-alternate elements of a quad byte vector
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQU_PH_QBLA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBLA %s, %s", rt, rs);
}


/*
 * [DSP] PRECEQU.PH.QBL rt, rs - Expand the precision of the two left-most
 *         elements of a quad byte vector
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQU_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBL %s, %s", rt, rs);
}


/*
 * [DSP] PRECEQU.PH.QBRA rt, rs - Expand the precision of the two
 *         right-alternate elements of a quad byte vector
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQU_PH_QBRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBRA %s, %s", rt, rs);
}


/*
 * [DSP] PRECEQU.PH.QBR rt, rs - Expand the precision of the two right-most
 *         elements of a quad byte vector
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEQU_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEQU.PH.QBR %s, %s", rt, rs);
}


/*
 * [DSP] PRECEU.PH.QBLA rt, rs - Expand the precision of the two
 *         left-alternate elements of a quad byte vector to four unsigned
 *         halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEU_PH_QBLA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBLA %s, %s", rt, rs);
}


/*
 * [DSP] PRECEU.PH.QBL rt, rs - Expand the precision of the two left-most
 *         elements of a quad byte vector to form unsigned halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEU_PH_QBL(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBL %s, %s", rt, rs);
}


/*
 * [DSP] PRECEU.PH.QBRA rt, rs - Expand the precision of the two
 *         right-alternate elements of a quad byte vector to form four
 *         unsigned halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEU_PH_QBRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBRA %s, %s", rt, rs);
}


/*
 * [DSP] PRECEU.PH.QBR rt, rs - Expand the precision of the two right-most
 *         elements of a quad byte vector to form unsigned halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECEU_PH_QBR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECEU.PH.QBR %s, %s", rt, rs);
}


/*
 * [DSP] PRECR.QB.PH rd, rs, rt - Reduce the precision of four integer
 *   halfwords to four bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0001101101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECR_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECR.QB.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PRECR_SRA.PH.W rt, rs, sa - Reduce the precision of two integer
 *   words to halfwords after a right shift
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECR_SRA_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECR_SRA.PH.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] PRECR_SRA_R.PH.W rt, rs, sa - Reduce the precision of two integer
 *   words to halfwords after a right shift with rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECR_SRA_R_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PRECR_SRA_R.PH.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] PRECRQ.PH.W rd, rs, rt - Reduce the precision of fractional
 *   words to fractional halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECRQ_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ.PH.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PRECRQ.QB.PH rd, rs, rt - Reduce the precision of four fractional
 *   halfwords to four bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0010101101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECRQ_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ.QB.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PRECRQ_RS.PH.W rd, rs, rt - Reduce the precision of fractional
 *   words to halfwords with rounding and saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECRQ_RS_PH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQ_RS.PH.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] PRECRQU_S.QB.PH rd, rs, rt - Reduce the precision of fractional
 *   halfwords to unsigned bytes with saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PRECRQU_S_QB_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("PRECRQU_S.QB.PH %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PREF_S9_(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREF 0x%" PRIx64 ", %" PRId64 "(%s)",
                      hint_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PREF_U12_(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREF 0x%" PRIx64 ", 0x%" PRIx64 "(%s)",
                      hint_value, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PREFE(uint64 instruction, Dis_info *info)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("PREFE 0x%" PRIx64 ", %" PRId64 "(%s)",
                      hint_value, s_value, rs);
}


/*
 * [DSP] PREPEND rt, rs, sa - Right shift and prepend bits to the MSB
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *PREPEND(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("PREPEND %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] RADDU.W.QB rt, rs - Unsigned reduction add of vector quad bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          1111000100111111
 *     rt -----
 *          rs -----
 */
static char *RADDU_W_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("RADDU.W.QB %s, %s", rt, rs);
}


/*
 * [DSP] RDDSP rt, mask - Read DSPControl register fields to a GPR
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            00011001111111
 *     rt -----
 *        mask -------
 */
static char *RDDSP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("RDDSP %s, 0x%" PRIx64, rt, mask_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RDHWR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 hs_value = extract_hs_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("RDHWR %s, CP%" PRIu64 ", 0x%" PRIx64,
                      rt, hs_value, sel_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RDPGPR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("RDPGPR %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RECIP_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RECIP.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RECIP_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RECIP.S %s, %s", ft, fs);
}


/*
 * [DSP] REPL.PH rd, s - Replicate immediate integer into all vector element
 *   positions
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x0000111101
 *     rt -----
 *           s ----------
 */
static char *REPL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se9_20_19_18_17_16_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("REPL.PH %s, %" PRId64, rt, s_value);
}


/*
 * [DSP] REPL.QB rd, u - Replicate immediate integer into all vector element
 *   positions
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000             x010111111111
 *     rt -----
 *           u --------
 */
static char *REPL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_19_18_17_16_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("REPL.QB %s, 0x%" PRIx64, rt, u_value);
}


/*
 * [DSP] REPLV.PH rt, rs - Replicate a halfword into all vector element
 *   positions
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0000001100111111
 *     rt -----
 *          rs -----
 */
static char *REPLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("REPLV.PH %s, %s", rt, rs);
}


/*
 * [DSP] REPLV.QB rt, rs - Replicate byte into all vector element positions
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          0001001100111111
 *     rt -----
 *          rs -----
 */
static char *REPLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("REPLV.QB %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RESTORE_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("RESTORE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RESTORE_JRC_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        encode_rt1_from_rt(rt1_value), count_value, 0, info);
    return img_format("RESTORE.JRC 0x%" PRIx64 "%s", u_value, save_restore_str);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RESTORE_JRC_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("RESTORE.JRC 0x%" PRIx64 "%s", u_value,
                      save_restore_str);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RESTOREF(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);


    return img_format("RESTOREF 0x%" PRIx64 ", 0x%" PRIx64,
                      u_value, count_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RINT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RINT.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RINT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RINT.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROTR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ROTR %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROTRV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("ROTRV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROTX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_10_9_8_7__s1(instruction);
    uint64 stripe_value = extract_stripe_6(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("ROTX %s, %s, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64,
                       rt, rs, shift_value, shiftx_value, stripe_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROUND_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROUND_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROUND_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *ROUND_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("ROUND.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RSQRT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RSQRT.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *RSQRT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("RSQRT.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SAVE_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        encode_rt1_from_rt(rt1_value), count_value, 0, info);
    return img_format("SAVE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SAVE_32_(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    g_autofree char *save_restore_str = save_restore_list(
        rt_value, count_value, gp_value, info);
    return img_format("SAVE 0x%" PRIx64 "%s", u_value, save_restore_str);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SAVEF(uint64 instruction, Dis_info *info)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);


    return img_format("SAVEF 0x%" PRIx64 ", 0x%" PRIx64, u_value, count_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SB_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SB %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SB_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SB %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SB_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SB %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SB_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SB %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SBE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SBE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SBX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SBX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SC(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SC %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SCD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SCDP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCDP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SCE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SCWP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCWP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SCWPE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    const char *rt = GPR(rt_value, info);
    const char *ru = GPR(ru_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SCWPE %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SD_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SD %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SD_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SD %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SD_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SD %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDBBP_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_2_1_0(instruction);


    return img_format("SDBBP 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDBBP_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("SDBBP 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("SDC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SDC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SDC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDC1X %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDC2(uint64 instruction, Dis_info *info)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SDC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      cs_value, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("SDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("SDPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SDX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SDX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SEB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEB %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SEH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEH %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SEL_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SEL.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SEL_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SEL.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SELEQZ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELEQZ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SELEQZ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELEQZ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SELNEZ_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELNEZ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SELNEZ_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SELNEZ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SEQI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SEQI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SH_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SH %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SH_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SH %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SH_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SH_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SH %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 * [DSP] SHILO ac, shift - Shift an accumulator value leaving the result in
 *   the same accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000xxxx        xxxx0000011101
 *      shift ------
 *               ac --
 */
static char *SHILO(uint64 instruction, Dis_info *info)
{
    int64 shift_value = extract_shift__se5_21_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *ac = AC(ac_value, info);

    return img_format("SHILO %s, 0x%" PRIx64, ac, shift_value);
}


/*
 * [DSP] SHILOV ac, rs - Variable shift of accumulator value leaving the result
 *   in the same accumulator
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000xxxxx       01001001111111
 *          rs -----
 *               ac --
 */
static char *SHILOV(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    const char *rs = GPR(rs_value, info);
    const char *ac = AC(ac_value, info);

    return img_format("SHILOV %s, %s", ac, rs);
}


/*
 * [DSP] SHLL.PH rt, rs, sa - Shift left logical vector pair halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000              001110110101
 *     rt -----
 *          rs -----
 *               sa ----
 */
static char *SHLL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHLL.QB rt, rs, sa - Shift left logical vector quad bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000             0100001111111
 *     rt -----
 *          rs -----
 *               sa ---
 */
static char *SHLL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHLL_S.PH rt, rs, sa - Shift left logical vector pair halfwords
 *   with saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000              001110110101
 *     rt -----
 *          rs -----
 *               sa ----
 */
static char *SHLL_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL_S.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHLL_S.PH rt, rs, sa - Shift left logical word with saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1111110101
 *     rt -----
 *          rs -----
 *               sa -----
 */
static char *SHLL_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLL_S.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHLLV.PH rd, rt, rs - Shift left logical variable vector pair
 *   halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01110001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHLLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV.PH %s, %s, %s", rd, rt, rs);
}


/*
 * [DSP] SHLLV_S.QB rd, rt, rs - Shift left logical variable vector quad bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHLLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV.QB %s, %s, %s", rd, rt, rs);
}


/*
 * [DSP] SHLLV.PH rd, rt, rs - Shift left logical variable vector pair
 *   halfwords with saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               11110001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHLLV_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV_S.PH %s, %s, %s", rd, rt, rs);
}


/*
 * [DSP] SHLLV_S.W rd, rt, rs - Shift left logical variable vector word
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1111010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHLLV_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHLLV_S.W %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRA_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRA_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRA_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRA_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRA_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRA_R.W %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRAV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV.PH %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRAV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV.QB %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRAV_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.PH %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRAV_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.QB %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRAV_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRAV_R.W %s, %s, %s", rd, rt, rs);
}


/*
 * [DSP] SHRL.PH rt, rs, sa - Shift right logical two halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000              001111111111
 *     rt -----
 *          rs -----
 *               sa ----
 */
static char *SHRL_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRL.PH %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHRL.QB rt, rs, sa - Shift right logical vector quad bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000             1100001111111
 *     rt -----
 *          rs -----
 *               sa ---
 */
static char *SHRL_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRL.QB %s, %s, 0x%" PRIx64, rt, rs, sa_value);
}


/*
 * [DSP] SHLLV.PH rd, rt, rs - Shift right logical variable vector pair of
 *   halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1100010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRLV_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRLV.PH %s, %s, %s", rd, rt, rs);
}


/*
 * [DSP] SHLLV.QB rd, rt, rs - Shift right logical variable vector quad bytes
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1101010101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHRLV_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SHRLV.QB %s, %s, %s", rd, rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SHX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SHXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SHXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SIGRIE(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_18_to_0(instruction);


    return img_format("SIGRIE 0x%" PRIx64, code_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLL_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 shift3 = encode_shift3_from_shift(shift3_value);

    return img_format("SLL %s, %s, 0x%" PRIx64, rt3, rs3, shift3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLLV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLT(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLT %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLTI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLTI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLTIU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SLTIU %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SLTU(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SLTU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SOV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SOV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SPECIAL2(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_to_3(instruction);


    return img_format("SPECIAL2 0x%" PRIx64, op_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SQRT_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("SQRT.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SQRT_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("SQRT.S %s, %s", ft, fs);
}


/*
 * SRA rd, rt, sa - Shift Word Right Arithmetic
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  00000000000               000011
 *          rt -----
 *               rd -----
 *                    sa -----
 */
static char *SRA(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SRA %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 * SRAV rd, rt, rs - Shift Word Right Arithmetic Variable
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00000000111
 *     rs -----
 *          rt -----
 *               rd -----
 */
static char *SRAV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SRAV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00000000111
 *     rs -----
 *          rt -----
 *               rd -----
 */
static char *SRL_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    uint64 shift3 = encode_shift3_from_shift(shift3_value);

    return img_format("SRL %s, %s, 0x%" PRIx64, rt3, rs3, shift3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SRL_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SRL %s, %s, 0x%" PRIx64, rt, rs, shift_value);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SRLV(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SRLV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUB %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUB_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SUB.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUB_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    const char *fd = FPR(fd_value, info);
    const char *fs = FPR(fs_value, info);
    const char *ft = FPR(ft_value, info);

    return img_format("SUB.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQ_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQ.S.PH rd, rt, rs - Subtract fractional halfword vectors and shift
 *   right to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQ_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQ.S.W rd, rt, rs - Subtract fractional halfword vectors and shift
 *   right to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQ_S_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQ_S.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQH.PH rd, rt, rs - Subtract fractional halfword vectors and shift
 *   right to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQH_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQH_R.PH rd, rt, rs - Subtract fractional halfword vectors and shift
 *   right to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQH_R_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH_R.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQH_R.W rd, rt, rs - Subtract fractional halfword vectors and shift
 *   right to halve results with rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               11001001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQH_R_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH_R.W %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBQH.W rd, rs, rt - Subtract fractional words and shift right to
 *   halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBQH_W(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBQH.W %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    const char *rd3 = GPR(decode_gpr_gpr3(rd3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("SUBU %s, %s, %s", rd3, rs3, rt3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBU.PH rd, rs, rt - Subtract unsigned unsigned halfwords
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01100001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBU.QB rd, rs, rt - Subtract unsigned quad byte vectors
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01011001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBU_S.PH rd, rs, rt - Subtract unsigned unsigned halfwords with
 *   8-bit saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               11100001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_S_PH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU_S.PH %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBU_S.QB rd, rs, rt - Subtract unsigned quad byte vectors with
 *   8-bit saturation
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               11011001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBU_S_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBU_S.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBUH.QB rd, rs, rt - Subtract unsigned bytes and right shift
 *   to halve results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               01101001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBUH_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBUH.QB %s, %s, %s", rd, rs, rt);
}


/*
 * [DSP] SUBUH_R.QB rd, rs, rt - Subtract unsigned bytes and right shift
 *   to halve results with rounding
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               11101001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SUBUH_R_QB(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SUBUH_R.QB %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_16_(uint64 instruction, Dis_info *info)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);
    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rtz3, u_value, rs3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_4X4_(uint64 instruction, Dis_info *info)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    const char *rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value, info), info);
    const char *rs4 = GPR(decode_gpr_gpr4(rs4_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rtz4, u_value, rs4);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_GP16_(uint64 instruction, Dis_info *info)
{
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);

    const char *rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value, info), info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rtz3, u_value, 28);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_GP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rt, u_value, 28);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_S9_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SW %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_SP_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("SW %s, 0x%" PRIx64 "($%d)", rt, u_value, 29);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SW_U12_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SW %s, 0x%" PRIx64 "(%s)", rt, u_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC1_GP_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    const char *ft = FPR(ft_value, info);

    return img_format("SWC1 %s, 0x%" PRIx64 "($%d)", ft, u_value, 28);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC1_S9_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWC1 %s, %" PRId64 "(%s)", ft, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC1_U12_(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWC1 %s, 0x%" PRIx64 "(%s)", ft, u_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC1X(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWC1X %s, %s(%s)", ft, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC1XS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    const char *ft = FPR(ft_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWC2(uint64 instruction, Dis_info *info)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SWC2 CP%" PRIu64 ", %" PRId64 "(%s)",
                      cs_value, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("SWE %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("SWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWPC_48_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    const char *rt = GPR(rt_value, info);
    g_autofree char *s = ADDRESS(s_value, 6, info);

    return img_format("SWPC %s, %s", rt, s);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWX(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWX %s, %s(%s)", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SWXS(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("SWXS %s, %s(%s)", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SYNC(uint64 instruction, Dis_info *info)
{
    uint64 stype_value = extract_stype_20_19_18_17_16(instruction);


    return img_format("SYNC 0x%" PRIx64, stype_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SYNCI(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SYNCI %" PRId64 "(%s)", s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SYNCIE(uint64 instruction, Dis_info *info)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rs = GPR(rs_value, info);

    return img_format("SYNCIE %" PRId64 "(%s)", s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *SYSCALL_16_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_1_0(instruction);


    return img_format("SYSCALL 0x%" PRIx64, code_value);
}


/*
 * SYSCALL code - System Call. Cause a System Call Exception
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  00000000000010
 *           code ------------------
 */
static char *SYSCALL_32_(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_17_to_0(instruction);


    return img_format("SYSCALL 0x%" PRIx64, code_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TEQ(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("TEQ %s, %s", rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGINV(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGINV ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGINVF(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGINVF ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGP(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGP ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGR ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGWI(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGWI ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBGWR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBGWR ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBINV(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBINV ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBINVF(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBINVF ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBP(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBP ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBR ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBWI(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBWI ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TLBWR(uint64 instruction, Dis_info *info)
{
    (void)instruction;

    return g_strdup("TLBWR ");
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TNE(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("TNE %s, %s", rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TRUNC_L_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.L.D %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TRUNC_L_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.L.S %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TRUNC_W_D(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.W.D %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *TRUNC_W_S(uint64 instruction, Dis_info *info)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    const char *ft = FPR(ft_value, info);
    const char *fs = FPR(fs_value, info);

    return img_format("TRUNC.W.S %s, %s", ft, fs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UALDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UALDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UALH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("UALH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UALWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UALWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UASDM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UASDM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UASH(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("UASH %s, %" PRId64 "(%s)", rt, s_value, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UASWM(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);
    uint64 count3 = encode_count3_from_count(count3_value);

    return img_format("UASWM %s, %" PRId64 "(%s), 0x%" PRIx64,
                      rt, s_value, rs, count3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *UDI(uint64 instruction, Dis_info *info)
{
    uint64 op_value = extract_op_25_to_3(instruction);


    return img_format("UDI 0x%" PRIx64, op_value);
}


/*
 * WAIT code - Enter Wait State
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          1100001101111111
 *   code ----------
 */
static char *WAIT(uint64 instruction, Dis_info *info)
{
    uint64 code_value = extract_code_25_24_23_22_21_20_19_18_17_16(instruction);


    return img_format("WAIT 0x%" PRIx64, code_value);
}


/*
 * [DSP] WRDSP rt, mask - Write selected fields from a GPR to the DSPControl
 *         register
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000            01011001111111
 *     rt -----
 *        mask -------
 */
static char *WRDSP(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    const char *rt = GPR(rt_value, info);

    return img_format("WRDSP %s, 0x%" PRIx64, rt, mask_value);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *WRPGPR(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("WRPGPR %s, %s", rt, rs);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *XOR_16_(uint64 instruction, Dis_info *info)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    const char *rs3 = GPR(decode_gpr_gpr3(rs3_value, info), info);
    const char *rt3 = GPR(decode_gpr_gpr3(rt3_value, info), info);

    return img_format("XOR %s, %s", rs3, rt3);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *XOR_32_(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    const char *rd = GPR(rd_value, info);
    const char *rs = GPR(rs_value, info);
    const char *rt = GPR(rt_value, info);

    return img_format("XOR %s, %s, %s", rd, rs, rt);
}


/*
 * ADDQH_R.W rd, rt, rs - Add Fractional Words And Shift Right to Halve Results
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 *               rd -----
 */
static char *XORI(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("XORI %s, %s, 0x%" PRIx64, rt, rs, u_value);
}


/*
 * YIELD rt, rs -
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               00010001101
 *     rt -----
 *          rs -----
 */
static char *YIELD(uint64 instruction, Dis_info *info)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    const char *rt = GPR(rt_value, info);
    const char *rs = GPR(rs_value, info);

    return img_format("YIELD %s, %s", rt, rs);
}



/*
 *                nanoMIPS instruction pool organization
 *                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *
 *                 ┌─ P.ADDIU ─── P.RI ─── P.SYSCALL
 *                 │
 *                 │                                      ┌─ P.TRAP
 *                 │                                      │
 *                 │                      ┌─ _POOL32A0_0 ─┼─ P.CMOVE
 *                 │                      │               │
 *                 │                      │               └─ P.SLTU
 *                 │        ┌─ _POOL32A0 ─┤
 *                 │        │             │
 *                 │        │             │
 *                 │        │             └─ _POOL32A0_1 ─── CRC32
 *                 │        │
 *                 ├─ P32A ─┤
 *                 │        │                           ┌─ PP.LSX
 *                 │        │             ┌─ P.LSX ─────┤
 *                 │        │             │             └─ PP.LSXS
 *                 │        └─ _POOL32A7 ─┤
 *                 │                      │             ┌─ POOL32Axf_4
 *                 │                      └─ POOL32Axf ─┤
 *                 │                                    └─ POOL32Axf_5
 *                 │
 *                 ├─ PBAL
 *                 │
 *                 ├─ P.GP.W   ┌─ PP.LSX
 *         ┌─ P32 ─┤           │
 *         │       ├─ P.GP.BH ─┴─ PP.LSXS
 *         │       │
 *         │       ├─ P.J ─────── PP.BALRSC
 *         │       │
 *         │       ├─ P48I
 *         │       │           ┌─ P.SR
 *         │       │           │
 *         │       │           ├─ P.SHIFT
 *         │       │           │
 *         │       ├─ P.U12 ───┼─ P.ROTX
 *         │       │           │
 *         │       │           ├─ P.INS
 *         │       │           │
 *         │       │           └─ P.EXT
 *         │       │
 *         │       ├─ P.LS.U12 ── P.PREF.U12
 *         │       │
 *         │       ├─ P.BR1 ───── P.BR3A
 *         │       │
 *         │       │           ┌─ P.LS.S0 ─── P16.SYSCALL
 *         │       │           │
 *         │       │           │           ┌─ P.LL
 *         │       │           ├─ P.LS.S1 ─┤
 *         │       │           │           └─ P.SC
 *         │       │           │
 *         │       │           │           ┌─ P.PREFE
 *  MAJOR ─┤       ├─ P.LS.S9 ─┤           │
 *         │       │           ├─ P.LS.E0 ─┼─ P.LLE
 *         │       │           │           │
 *         │       │           │           └─ P.SCE
 *         │       │           │
 *         │       │           ├─ P.LS.WM
 *         │       │           │
 *         │       │           └─ P.LS.UAWM
 *         │       │
 *         │       │
 *         │       ├─ P.BR2
 *         │       │
 *         │       ├─ P.BRI
 *         │       │
 *         │       └─ P.LUI
 *         │
 *         │
 *         │       ┌─ P16.MV ──── P16.RI ─── P16.SYSCALL
 *         │       │
 *         │       ├─ P16.SR
 *         │       │
 *         │       ├─ P16.SHIFT
 *         │       │
 *         │       ├─ P16.4x4
 *         │       │
 *         │       ├─ P16C ────── POOL16C_0 ── POOL16C_00
 *         │       │
 *         └─ P16 ─┼─ P16.LB
 *                 │
 *                 ├─ P16.A1
 *                 │
 *                 ├─ P16.LH
 *                 │
 *                 ├─ P16.A2 ──── P.ADDIU[RS5]
 *                 │
 *                 ├─ P16.ADDU
 *                 │
 *                 └─ P16.BR ──┬─ P16.JRC
 *                             │
 *                             └─ P16.BR1
 *
 *
 *  (FP, DPS, and some minor instruction pools are omitted from the diagram)
 *
 */

static const Pool P_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x00080000, &SYSCALL_32_      , 0,
       0x0                 },        /* SYSCALL[32] */
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x000c0000, &HYPCALL          , 0,
       CP0_ | VZ_          },        /* HYPCALL */
};


static const Pool P_RI[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00000000, &SIGRIE           , 0,
       0x0                 },        /* SIGRIE */
    { pool                , P_SYSCALL           , 2   , 32,
       0xfff80000, 0x00080000, 0                      , 0,
       0x0                 },        /* P.SYSCALL */
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00100000, &BREAK_32_        , 0,
       0x0                 },        /* BREAK[32] */
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00180000, &SDBBP_32_        , 0,
       EJTAG_              },        /* SDBBP[32] */
};


static const Pool P_ADDIU[2] = {
    { pool                , P_RI                , 4   , 32,
       0xffe00000, 0x00000000, 0                      , 0,
       0x0                 },        /* P.RI */
    { instruction         , 0                   , 0   , 32,
       0xfc000000, 0x00000000, &ADDIU_32_        , &ADDIU_32__cond   ,
       0x0                 },        /* ADDIU[32] */
};


static const Pool P_TRAP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000000, &TEQ              , 0,
       XMMS_               },        /* TEQ */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000400, &TNE              , 0,
       XMMS_               },        /* TNE */
};


static const Pool P_CMOVE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000210, &MOVZ             , 0,
       0x0                 },        /* MOVZ */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000610, &MOVN             , 0,
       0x0                 },        /* MOVN */
};


static const Pool P_D_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010ab0, &DMT              , 0,
       MT_                 },        /* DMT */
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000ab0, &DVPE             , 0,
       MT_                 },        /* DVPE */
};


static const Pool P_E_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010eb0, &EMT              , 0,
       MT_                 },        /* EMT */
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000eb0, &EVPE             , 0,
       MT_                 },        /* EVPE */
};


static const Pool _P_MT_VPE[2] = {
    { pool                , P_D_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000ab0, 0                      , 0,
       0x0                 },        /* P.D_MT_VPE */
    { pool                , P_E_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000eb0, 0                      , 0,
       0x0                 },        /* P.E_MT_VPE */
};


static const Pool P_MT_VPE[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200002b0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(0) */
    { pool                , _P_MT_VPE           , 2   , 32,
       0xfc003bff, 0x20000ab0, 0                      , 0,
       0x0                 },        /* _P.MT_VPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200012b0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20001ab0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200022b0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20002ab0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x200032b0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003bff, 0x20003ab0, 0                      , 0,
       0x0                 },        /* P.MT_VPE~*(7) */
};


static const Pool P_DVP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000390, &DVP              , 0,
       0x0                 },        /* DVP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000790, &EVP              , 0,
       0x0                 },        /* EVP */
};


static const Pool P_SLTU[2] = {
    { pool                , P_DVP               , 2   , 32,
       0xfc00fbff, 0x20000390, 0                      , 0,
       0x0                 },        /* P.DVP */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000390, &SLTU             , &SLTU_cond        ,
       0x0                 },        /* SLTU */
};


static const Pool _POOL32A0[128] = {
    { pool                , P_TRAP              , 2   , 32,
       0xfc0003ff, 0x20000000, 0                      , 0,
       0x0                 },        /* P.TRAP */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000008, &SEB              , 0,
       XMMS_               },        /* SEB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000010, &SLLV             , 0,
       0x0                 },        /* SLLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000018, &MUL_32_          , 0,
       0x0                 },        /* MUL[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000020, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000028, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(5) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000030, &MFC0             , 0,
       0x0                 },        /* MFC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000038, &MFHC0            , 0,
       CP0_ | MVH_         },        /* MFHC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000040, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(8) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000048, &SEH              , 0,
       0x0                 },        /* SEH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000050, &SRLV             , 0,
       0x0                 },        /* SRLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000058, &MUH              , 0,
       0x0                 },        /* MUH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000060, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000068, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(13) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000070, &MTC0             , 0,
       CP0_                },        /* MTC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000078, &MTHC0            , 0,
       CP0_ | MVH_         },        /* MTHC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000080, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000088, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(17) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000090, &SRAV             , 0,
       0x0                 },        /* SRAV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000098, &MULU             , 0,
       0x0                 },        /* MULU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(21) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b0, &MFGC0            , 0,
       CP0_ | VZ_          },        /* MFGC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b8, &MFHGC0           , 0,
       CP0_ | VZ_ | MVH_   },        /* MFHGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(25) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d0, &ROTRV            , 0,
       0x0                 },        /* ROTRV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d8, &MUHU             , 0,
       0x0                 },        /* MUHU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(29) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f0, &MTGC0            , 0,
       CP0_ | VZ_          },        /* MTGC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f8, &MTHGC0           , 0,
       CP0_ | VZ_ | MVH_   },        /* MTHGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000100, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(32) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000108, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(33) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000110, &ADD              , 0,
       XMMS_               },        /* ADD */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000118, &DIV              , 0,
       0x0                 },        /* DIV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000120, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(36) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000128, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(37) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000130, &DMFC0            , 0,
       CP0_ | MIPS64_      },        /* DMFC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000138, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(39) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000140, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(40) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000148, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(41) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000150, &ADDU_32_         , 0,
       0x0                 },        /* ADDU[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000158, &MOD              , 0,
       0x0                 },        /* MOD */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000160, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(44) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000168, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(45) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000170, &DMTC0            , 0,
       CP0_ | MIPS64_      },        /* DMTC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000178, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(47) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000180, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(48) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000188, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(49) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000190, &SUB              , 0,
       XMMS_               },        /* SUB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000198, &DIVU             , 0,
       0x0                 },        /* DIVU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(53) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b0, &DMFGC0           , 0,
       CP0_ | MIPS64_ | VZ_},        /* DMFGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c0, &RDHWR            , 0,
       XMMS_               },        /* RDHWR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(57) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d0, &SUBU_32_         , 0,
       0x0                 },        /* SUBU[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d8, &MODU             , 0,
       0x0                 },        /* MODU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(61) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f0, &DMTGC0           , 0,
       CP0_ | MIPS64_ | VZ_},        /* DMTGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(63) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000200, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(64) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000208, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(65) */
    { pool                , P_CMOVE             , 2   , 32,
       0xfc0003ff, 0x20000210, 0                      , 0,
       0x0                 },        /* P.CMOVE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000218, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(67) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000220, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(68) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000228, &FORK             , 0,
       MT_                 },        /* FORK */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000230, &MFTR             , 0,
       MT_                 },        /* MFTR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000238, &MFHTR            , 0,
       MT_                 },        /* MFHTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000240, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(72) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000248, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(73) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000250, &AND_32_          , 0,
       0x0                 },        /* AND[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000258, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(75) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000260, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(76) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000268, &YIELD            , 0,
       MT_                 },        /* YIELD */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000270, &MTTR             , 0,
       MT_                 },        /* MTTR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000278, &MTHTR            , 0,
       MT_                 },        /* MTHTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000280, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(80) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000288, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(81) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000290, &OR_32_           , 0,
       0x0                 },        /* OR[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000298, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(83) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(84) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(85) */
    { pool                , P_MT_VPE            , 8   , 32,
       0xfc0003ff, 0x200002b0, 0                      , 0,
       0x0                 },        /* P.MT_VPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002b8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(87) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(88) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(89) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d0, &NOR              , 0,
       0x0                 },        /* NOR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(91) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(92) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(93) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(94) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(95) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000300, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(96) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000308, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(97) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000310, &XOR_32_          , 0,
       0x0                 },        /* XOR[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000318, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(99) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000320, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(100) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000328, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(101) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000330, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(102) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000338, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(103) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000340, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(104) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000348, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(105) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000350, &SLT              , 0,
       0x0                 },        /* SLT */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000358, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(107) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000360, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(108) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000368, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(109) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000370, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(110) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000378, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(111) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000380, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(112) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000388, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(113) */
    { pool                , P_SLTU              , 2   , 32,
       0xfc0003ff, 0x20000390, 0                      , 0,
       0x0                 },        /* P.SLTU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000398, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(115) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(116) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(117) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003b0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(118) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003b8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(119) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(120) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(121) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d0, &SOV              , 0,
       0x0                 },        /* SOV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(123) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(124) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(125) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(126) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(127) */
};


static const Pool ADDQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000000d, &ADDQ_PH          , 0,
       DSP_                },        /* ADDQ.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000040d, &ADDQ_S_PH        , 0,
       DSP_                },        /* ADDQ_S.PH */
};


static const Pool MUL__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000002d, &MUL_PH           , 0,
       DSP_                },        /* MUL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000042d, &MUL_S_PH         , 0,
       DSP_                },        /* MUL_S.PH */
};


static const Pool ADDQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000004d, &ADDQH_PH         , 0,
       DSP_                },        /* ADDQH.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000044d, &ADDQH_R_PH       , 0,
       DSP_                },        /* ADDQH_R.PH */
};


static const Pool ADDQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000008d, &ADDQH_W          , 0,
       DSP_                },        /* ADDQH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000048d, &ADDQH_R_W        , 0,
       DSP_                },        /* ADDQH_R.W */
};


static const Pool ADDU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200000cd, &ADDU_QB          , 0,
       DSP_                },        /* ADDU.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004cd, &ADDU_S_QB        , 0,
       DSP_                },        /* ADDU_S.QB */
};


static const Pool ADDU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000010d, &ADDU_PH          , 0,
       DSP_                },        /* ADDU.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000050d, &ADDU_S_PH        , 0,
       DSP_                },        /* ADDU_S.PH */
};


static const Pool ADDUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000014d, &ADDUH_QB         , 0,
       DSP_                },        /* ADDUH.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000054d, &ADDUH_R_QB       , 0,
       DSP_                },        /* ADDUH_R.QB */
};


static const Pool SHRAV__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000018d, &SHRAV_PH         , 0,
       DSP_                },        /* SHRAV.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000058d, &SHRAV_R_PH       , 0,
       DSP_                },        /* SHRAV_R.PH */
};


static const Pool SHRAV__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200001cd, &SHRAV_QB         , 0,
       DSP_                },        /* SHRAV.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005cd, &SHRAV_R_QB       , 0,
       DSP_                },        /* SHRAV_R.QB */
};


static const Pool SUBQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000020d, &SUBQ_PH          , 0,
       DSP_                },        /* SUBQ.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000060d, &SUBQ_S_PH        , 0,
       DSP_                },        /* SUBQ_S.PH */
};


static const Pool SUBQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000024d, &SUBQH_PH         , 0,
       DSP_                },        /* SUBQH.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000064d, &SUBQH_R_PH       , 0,
       DSP_                },        /* SUBQH_R.PH */
};


static const Pool SUBQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000028d, &SUBQH_W          , 0,
       DSP_                },        /* SUBQH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000068d, &SUBQH_R_W        , 0,
       DSP_                },        /* SUBQH_R.W */
};


static const Pool SUBU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002cd, &SUBU_QB          , 0,
       DSP_                },        /* SUBU.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006cd, &SUBU_S_QB        , 0,
       DSP_                },        /* SUBU_S.QB */
};


static const Pool SUBU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000030d, &SUBU_PH          , 0,
       DSP_                },        /* SUBU.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000070d, &SUBU_S_PH        , 0,
       DSP_                },        /* SUBU_S.PH */
};


static const Pool SHRA__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000335, &SHRA_PH          , 0,
       DSP_                },        /* SHRA.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000735, &SHRA_R_PH        , 0,
       DSP_                },        /* SHRA_R.PH */
};


static const Pool SUBUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000034d, &SUBUH_QB         , 0,
       DSP_                },        /* SUBUH.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000074d, &SUBUH_R_QB       , 0,
       DSP_                },        /* SUBUH_R.QB */
};


static const Pool SHLLV__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000038d, &SHLLV_PH         , 0,
       DSP_                },        /* SHLLV.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000078d, &SHLLV_S_PH       , 0,
       DSP_                },        /* SHLLV_S.PH */
};


static const Pool SHLL__S__PH[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003b5, &SHLL_PH          , 0,
       DSP_                },        /* SHLL.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200007b5, 0                      , 0,
       0x0                 },        /* SHLL[_S].PH~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x20000bb5, &SHLL_S_PH        , 0,
       DSP_                },        /* SHLL_S.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000fb5, 0                      , 0,
       0x0                 },        /* SHLL[_S].PH~*(3) */
};


static const Pool PRECR_SRA__R__PH_W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003cd, &PRECR_SRA_PH_W   , 0,
       DSP_                },        /* PRECR_SRA.PH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007cd, &PRECR_SRA_R_PH_W , 0,
       DSP_                },        /* PRECR_SRA_R.PH.W */
};


static const Pool _POOL32A5[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000005, &CMP_EQ_PH        , 0,
       DSP_                },        /* CMP.EQ.PH */
    { pool                , ADDQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000000d, 0                      , 0,
       0x0                 },        /* ADDQ[_S].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000015, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(2) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000001d, &SHILO            , 0,
       DSP_                },        /* SHILO */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000025, &MULEQ_S_W_PHL    , 0,
       DSP_                },        /* MULEQ_S.W.PHL */
    { pool                , MUL__S__PH          , 2   , 32,
       0xfc0003ff, 0x2000002d, 0                      , 0,
       0x0                 },        /* MUL[_S].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000035, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(6) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000003d, &REPL_PH          , 0,
       DSP_                },        /* REPL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000045, &CMP_LT_PH        , 0,
       DSP_                },        /* CMP.LT.PH */
    { pool                , ADDQH__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000004d, 0                      , 0,
       0x0                 },        /* ADDQH[_R].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000055, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000005d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(11) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000065, &MULEQ_S_W_PHR    , 0,
       DSP_                },        /* MULEQ_S.W.PHR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000006d, &PRECR_QB_PH      , 0,
       DSP_                },        /* PRECR.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000075, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000007d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000085, &CMP_LE_PH        , 0,
       DSP_                },        /* CMP.LE.PH */
    { pool                , ADDQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000008d, 0                      , 0,
       0x0                 },        /* ADDQH[_R].W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000095, &MULEU_S_PH_QBL   , 0,
       DSP_                },        /* MULEU_S.PH.QBL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000009d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(20) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ad, &PRECRQ_QB_PH     , 0,
       DSP_                },        /* PRECRQ.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(23) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c5, &CMPGU_EQ_QB      , 0,
       DSP_                },        /* CMPGU.EQ.QB */
    { pool                , ADDU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200000cd, 0                      , 0,
       0x0                 },        /* ADDU[_S].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d5, &MULEU_S_PH_QBR   , 0,
       DSP_                },        /* MULEU_S.PH.QBR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(28) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ed, &PRECRQ_PH_W      , 0,
       DSP_                },        /* PRECRQ.PH.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(31) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000105, &CMPGU_LT_QB      , 0,
       DSP_                },        /* CMPGU.LT.QB */
    { pool                , ADDU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000010d, 0                      , 0,
       0x0                 },        /* ADDU[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000115, &MULQ_RS_PH       , 0,
       DSP_                },        /* MULQ_RS.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000011d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(35) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000125, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(36) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000012d, &PRECRQ_RS_PH_W   , 0,
       DSP_                },        /* PRECRQ_RS.PH.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000135, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(38) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000013d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(39) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000145, &CMPGU_LE_QB      , 0,
       DSP_                },        /* CMPGU.LE.QB */
    { pool                , ADDUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000014d, 0                      , 0,
       0x0                 },        /* ADDUH[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000155, &MULQ_S_PH        , 0,
       DSP_                },        /* MULQ_S.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000015d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(43) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000165, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(44) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000016d, &PRECRQU_S_QB_PH  , 0,
       DSP_                },        /* PRECRQU_S.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000175, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(46) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000017d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(47) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000185, &CMPGDU_EQ_QB     , 0,
       DSP_                },        /* CMPGDU.EQ.QB */
    { pool                , SHRAV__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000018d, 0                      , 0,
       0x0                 },        /* SHRAV[_R].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000195, &MULQ_RS_W        , 0,
       DSP_                },        /* MULQ_RS.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000019d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(51) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(52) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ad, &PACKRL_PH        , 0,
       DSP_                },        /* PACKRL.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(54) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c5, &CMPGDU_LT_QB     , 0,
       DSP_                },        /* CMPGDU.LT.QB */
    { pool                , SHRAV__R__QB        , 2   , 32,
       0xfc0003ff, 0x200001cd, 0                      , 0,
       0x0                 },        /* SHRAV[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d5, &MULQ_S_W         , 0,
       DSP_                },        /* MULQ_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(59) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(60) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ed, &PICK_QB          , 0,
       DSP_                },        /* PICK.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(62) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(63) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000205, &CMPGDU_LE_QB     , 0,
       DSP_                },        /* CMPGDU.LE.QB */
    { pool                , SUBQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000020d, 0                      , 0,
       0x0                 },        /* SUBQ[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000215, &APPEND           , 0,
       DSP_                },        /* APPEND */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000021d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(67) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000225, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(68) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000022d, &PICK_PH          , 0,
       DSP_                },        /* PICK.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000235, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(70) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000023d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(71) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000245, &CMPU_EQ_QB       , 0,
       DSP_                },        /* CMPU.EQ.QB */
    { pool                , SUBQH__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000024d, 0                      , 0,
       0x0                 },        /* SUBQH[_R].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000255, &PREPEND          , 0,
       DSP_                },        /* PREPEND */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000025d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(75) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000265, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(76) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000026d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(77) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000275, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(78) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000027d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(79) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000285, &CMPU_LT_QB       , 0,
       DSP_                },        /* CMPU.LT.QB */
    { pool                , SUBQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000028d, 0                      , 0,
       0x0                 },        /* SUBQH[_R].W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000295, &MODSUB           , 0,
       DSP_                },        /* MODSUB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000029d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(83) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(84) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002ad, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(85) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002b5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(86) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(87) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002c5, &CMPU_LE_QB       , 0,
       DSP_                },        /* CMPU.LE.QB */
    { pool                , SUBU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200002cd, 0                      , 0,
       0x0                 },        /* SUBU[_S].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d5, &SHRAV_R_W        , 0,
       DSP_                },        /* SHRAV_R.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(91) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(92) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002ed, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(93) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002f5, &SHRA_R_W         , 0,
       DSP_                },        /* SHRA_R.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(95) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000305, &ADDQ_S_W         , 0,
       DSP_                },        /* ADDQ_S.W */
    { pool                , SUBU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000030d, 0                      , 0,
       0x0                 },        /* SUBU[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000315, &SHRLV_PH         , 0,
       DSP_                },        /* SHRLV.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000031d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(99) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000325, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(100) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000032d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(101) */
    { pool                , SHRA__R__PH         , 2   , 32,
       0xfc0003ff, 0x20000335, 0                      , 0,
       0x0                 },        /* SHRA[_R].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000033d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(103) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000345, &SUBQ_S_W         , 0,
       DSP_                },        /* SUBQ_S.W */
    { pool                , SUBUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000034d, 0                      , 0,
       0x0                 },        /* SUBUH[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000355, &SHRLV_QB         , 0,
       DSP_                },        /* SHRLV.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000035d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(107) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000365, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(108) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000036d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(109) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000375, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(110) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000037d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(111) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000385, &ADDSC            , 0,
       DSP_                },        /* ADDSC */
    { pool                , SHLLV__S__PH        , 2   , 32,
       0xfc0003ff, 0x2000038d, 0                      , 0,
       0x0                 },        /* SHLLV[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000395, &SHLLV_QB         , 0,
       DSP_                },        /* SHLLV.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000039d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(115) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(116) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003ad, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(117) */
    { pool                , SHLL__S__PH         , 4   , 32,
       0xfc0003ff, 0x200003b5, 0                      , 0,
       0x0                 },        /* SHLL[_S].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(119) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003c5, &ADDWC            , 0,
       DSP_                },        /* ADDWC */
    { pool                , PRECR_SRA__R__PH_W  , 2   , 32,
       0xfc0003ff, 0x200003cd, 0                      , 0,
       0x0                 },        /* PRECR_SRA[_R].PH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d5, &SHLLV_S_W        , 0,
       DSP_                },        /* SHLLV_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(123) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(124) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003ed, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(125) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003f5, &SHLL_S_W         , 0,
       DSP_                },        /* SHLL_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(127) */
};


static const Pool PP_LSX[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000007, &LBX              , 0,
       0x0                 },        /* LBX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000087, &SBX              , 0,
       XMMS_               },        /* SBX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000107, &LBUX             , 0,
       0x0                 },        /* LBUX */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000187, 0                      , 0,
       0x0                 },        /* PP.LSX~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000207, &LHX              , 0,
       0x0                 },        /* LHX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000287, &SHX              , 0,
       XMMS_               },        /* SHX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000307, &LHUX             , 0,
       0x0                 },        /* LHUX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000387, &LWUX             , 0,
       MIPS64_             },        /* LWUX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000407, &LWX              , 0,
       0x0                 },        /* LWX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000487, &SWX              , 0,
       XMMS_               },        /* SWX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000507, &LWC1X            , 0,
       CP1_                },        /* LWC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000587, &SWC1X            , 0,
       CP1_                },        /* SWC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000607, &LDX              , 0,
       MIPS64_             },        /* LDX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000687, &SDX              , 0,
       MIPS64_             },        /* SDX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000707, &LDC1X            , 0,
       CP1_                },        /* LDC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000787, &SDC1X            , 0,
       CP1_                },        /* SDC1X */
};


static const Pool PP_LSXS[16] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000047, 0                      , 0,
       0x0                 },        /* PP.LSXS~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x200000c7, 0                      , 0,
       0x0                 },        /* PP.LSXS~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000147, 0                      , 0,
       0x0                 },        /* PP.LSXS~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x200001c7, 0                      , 0,
       0x0                 },        /* PP.LSXS~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000247, &LHXS             , 0,
       0x0                 },        /* LHXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002c7, &SHXS             , 0,
       XMMS_               },        /* SHXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000347, &LHUXS            , 0,
       0x0                 },        /* LHUXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003c7, &LWUXS            , 0,
       MIPS64_             },        /* LWUXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000447, &LWXS_32_         , 0,
       0x0                 },        /* LWXS[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004c7, &SWXS             , 0,
       XMMS_               },        /* SWXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000547, &LWC1XS           , 0,
       CP1_                },        /* LWC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005c7, &SWC1XS           , 0,
       CP1_                },        /* SWC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000647, &LDXS             , 0,
       MIPS64_             },        /* LDXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006c7, &SDXS             , 0,
       MIPS64_             },        /* SDXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000747, &LDC1XS           , 0,
       CP1_                },        /* LDC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007c7, &SDC1XS           , 0,
       CP1_                },        /* SDC1XS */
};


static const Pool P_LSX[2] = {
    { pool                , PP_LSX              , 16  , 32,
       0xfc00007f, 0x20000007, 0                      , 0,
       0x0                 },        /* PP.LSX */
    { pool                , PP_LSXS             , 16  , 32,
       0xfc00007f, 0x20000047, 0                      , 0,
       0x0                 },        /* PP.LSXS */
};


static const Pool POOL32Axf_1_0[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000007f, &MFHI_DSP_        , 0,
       DSP_                },        /* MFHI[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000107f, &MFLO_DSP_        , 0,
       DSP_                },        /* MFLO[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000207f, &MTHI_DSP_        , 0,
       DSP_                },        /* MTHI[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000307f, &MTLO_DSP_        , 0,
       DSP_                },        /* MTLO[DSP] */
};


static const Pool POOL32Axf_1_1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000027f, &MTHLIP           , 0,
       DSP_                },        /* MTHLIP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000127f, &SHILOV           , 0,
       DSP_                },        /* SHILOV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000227f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_1~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000327f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_1~*(3) */
};


static const Pool POOL32Axf_1_3[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000067f, &RDDSP            , 0,
       DSP_                },        /* RDDSP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000167f, &WRDSP            , 0,
       DSP_                },        /* WRDSP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000267f, &EXTP             , 0,
       DSP_                },        /* EXTP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000367f, &EXTPDP           , 0,
       DSP_                },        /* EXTPDP */
};


static const Pool POOL32Axf_1_4[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000087f, &SHLL_QB          , 0,
       DSP_                },        /* SHLL.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000187f, &SHRL_QB          , 0,
       DSP_                },        /* SHRL.QB */
};


static const Pool MAQ_S_A__W_PHR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000a7f, &MAQ_S_W_PHR      , 0,
       DSP_                },        /* MAQ_S.W.PHR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002a7f, &MAQ_SA_W_PHR     , 0,
       DSP_                },        /* MAQ_SA.W.PHR */
};


static const Pool MAQ_S_A__W_PHL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001a7f, &MAQ_S_W_PHL      , 0,
       DSP_                },        /* MAQ_S.W.PHL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003a7f, &MAQ_SA_W_PHL     , 0,
       DSP_                },        /* MAQ_SA.W.PHL */
};


static const Pool POOL32Axf_1_5[2] = {
    { pool                , MAQ_S_A__W_PHR      , 2   , 32,
       0xfc001fff, 0x20000a7f, 0                      , 0,
       0x0                 },        /* MAQ_S[A].W.PHR */
    { pool                , MAQ_S_A__W_PHL      , 2   , 32,
       0xfc001fff, 0x20001a7f, 0                      , 0,
       0x0                 },        /* MAQ_S[A].W.PHL */
};


static const Pool POOL32Axf_1_7[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000e7f, &EXTR_W           , 0,
       DSP_                },        /* EXTR.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001e7f, &EXTR_R_W         , 0,
       DSP_                },        /* EXTR_R.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002e7f, &EXTR_RS_W        , 0,
       DSP_                },        /* EXTR_RS.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003e7f, &EXTR_S_H         , 0,
       DSP_                },        /* EXTR_S.H */
};


static const Pool POOL32Axf_1[8] = {
    { pool                , POOL32Axf_1_0       , 4   , 32,
       0xfc000fff, 0x2000007f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_0 */
    { pool                , POOL32Axf_1_1       , 4   , 32,
       0xfc000fff, 0x2000027f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x2000047f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1~*(2) */
    { pool                , POOL32Axf_1_3       , 4   , 32,
       0xfc000fff, 0x2000067f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_3 */
    { pool                , POOL32Axf_1_4       , 2   , 32,
       0xfc000fff, 0x2000087f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_4 */
    { pool                , POOL32Axf_1_5       , 2   , 32,
       0xfc000fff, 0x20000a7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_5 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000c7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1~*(6) */
    { pool                , POOL32Axf_1_7       , 4   , 32,
       0xfc000fff, 0x20000e7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_7 */
};


static const Pool POOL32Axf_2_DSP__0_7[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200000bf, &DPA_W_PH         , 0,
       DSP_                },        /* DPA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200002bf, &DPAQ_S_W_PH      , 0,
       DSP_                },        /* DPAQ_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200004bf, &DPS_W_PH         , 0,
       DSP_                },        /* DPS.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200006bf, &DPSQ_S_W_PH      , 0,
       DSP_                },        /* DPSQ_S.W.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200008bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_0_7~*(4) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000abf, &MADD_DSP_        , 0,
       DSP_                },        /* MADD[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000cbf, &MULT_DSP_        , 0,
       DSP_                },        /* MULT[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000ebf, &EXTRV_W          , 0,
       DSP_                },        /* EXTRV.W */
};


static const Pool POOL32Axf_2_DSP__8_15[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200010bf, &DPAX_W_PH        , 0,
       DSP_                },        /* DPAX.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200012bf, &DPAQ_SA_L_W      , 0,
       DSP_                },        /* DPAQ_SA.L.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200014bf, &DPSX_W_PH        , 0,
       DSP_                },        /* DPSX.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200016bf, &DPSQ_SA_L_W      , 0,
       DSP_                },        /* DPSQ_SA.L.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200018bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_8_15~*(4) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001abf, &MADDU_DSP_       , 0,
       DSP_                },        /* MADDU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001cbf, &MULTU_DSP_       , 0,
       DSP_                },        /* MULTU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001ebf, &EXTRV_R_W        , 0,
       DSP_                },        /* EXTRV_R.W */
};


static const Pool POOL32Axf_2_DSP__16_23[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200020bf, &DPAU_H_QBL       , 0,
       DSP_                },        /* DPAU.H.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200022bf, &DPAQX_S_W_PH     , 0,
       DSP_                },        /* DPAQX_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200024bf, &DPSU_H_QBL       , 0,
       DSP_                },        /* DPSU.H.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200026bf, &DPSQX_S_W_PH     , 0,
       DSP_                },        /* DPSQX_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200028bf, &EXTPV            , 0,
       DSP_                },        /* EXTPV */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002abf, &MSUB_DSP_        , 0,
       DSP_                },        /* MSUB[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002cbf, &MULSA_W_PH       , 0,
       DSP_                },        /* MULSA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002ebf, &EXTRV_RS_W       , 0,
       DSP_                },        /* EXTRV_RS.W */
};


static const Pool POOL32Axf_2_DSP__24_31[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200030bf, &DPAU_H_QBR       , 0,
       DSP_                },        /* DPAU.H.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200032bf, &DPAQX_SA_W_PH    , 0,
       DSP_                },        /* DPAQX_SA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200034bf, &DPSU_H_QBR       , 0,
       DSP_                },        /* DPSU.H.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200036bf, &DPSQX_SA_W_PH    , 0,
       DSP_                },        /* DPSQX_SA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200038bf, &EXTPDPV          , 0,
       DSP_                },        /* EXTPDPV */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003abf, &MSUBU_DSP_       , 0,
       DSP_                },        /* MSUBU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003cbf, &MULSAQ_S_W_PH    , 0,
       DSP_                },        /* MULSAQ_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003ebf, &EXTRV_S_H        , 0,
       DSP_                },        /* EXTRV_S.H */
};


static const Pool POOL32Axf_2[4] = {
    { pool                , POOL32Axf_2_DSP__0_7, 8   , 32,
       0xfc0031ff, 0x200000bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_0_7 */
    { pool                , POOL32Axf_2_DSP__8_15, 8   , 32,
       0xfc0031ff, 0x200010bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_8_15 */
    { pool                , POOL32Axf_2_DSP__16_23, 8   , 32,
       0xfc0031ff, 0x200020bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_16_23 */
    { pool                , POOL32Axf_2_DSP__24_31, 8   , 32,
       0xfc0031ff, 0x200030bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_24_31 */
};


static const Pool POOL32Axf_4[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000013f, &ABSQ_S_QB        , 0,
       DSP_                },        /* ABSQ_S.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000033f, &REPLV_PH         , 0,
       DSP_                },        /* REPLV.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000053f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000073f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000093f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000d3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000113f, &ABSQ_S_PH        , 0,
       DSP_                },        /* ABSQ_S.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000133f, &REPLV_QB         , 0,
       DSP_                },        /* REPLV.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000153f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000173f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000193f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001d3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000213f, &ABSQ_S_W         , 0,
       DSP_                },        /* ABSQ_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000233f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000253f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000273f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000293f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002d3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000313f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000333f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000353f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000373f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000393f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003d3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(31) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000413f, &INSV             , 0,
       DSP_                },        /* INSV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000433f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(33) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000453f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(34) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000473f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(35) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000493f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(36) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20004b3f, &CLO              , 0,
       XMMS_               },        /* CLO */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20004d3f, &MFC2             , 0,
       CP2_                },        /* MFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(39) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000513f, &PRECEQ_W_PHL     , 0,
       DSP_                },        /* PRECEQ.W.PHL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000533f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(41) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000553f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(42) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000573f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(43) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000593f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(44) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20005b3f, &CLZ              , 0,
       XMMS_               },        /* CLZ */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20005d3f, &MTC2             , 0,
       CP2_                },        /* MTC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(47) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000613f, &PRECEQ_W_PHR     , 0,
       DSP_                },        /* PRECEQ.W.PHR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000633f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(49) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000653f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(50) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000673f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(51) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000693f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(53) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20006d3f, &DMFC2            , 0,
       CP2_                },        /* DMFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000713f, &PRECEQU_PH_QBL   , 0,
       DSP_                },        /* PRECEQU.PH.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000733f, &PRECEQU_PH_QBLA  , 0,
       DSP_                },        /* PRECEQU.PH.QBLA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000753f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(58) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000773f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(59) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000793f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(61) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20007d3f, &DMTC2            , 0,
       CP2_                },        /* DMTC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(63) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000813f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(64) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000833f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(65) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000853f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(66) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000873f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(67) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000893f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(68) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20008b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(69) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20008d3f, &MFHC2            , 0,
       CP2_                },        /* MFHC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20008f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(71) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000913f, &PRECEQU_PH_QBR   , 0,
       DSP_                },        /* PRECEQU.PH.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000933f, &PRECEQU_PH_QBRA  , 0,
       DSP_                },        /* PRECEQU.PH.QBRA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000953f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(74) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000973f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(75) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000993f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(76) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20009b3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(77) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20009d3f, &MTHC2            , 0,
       CP2_                },        /* MTHC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20009f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(79) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a13f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(80) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a33f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(81) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(82) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(83) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000a93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(84) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ab3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(85) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ad3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(86) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000af3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(87) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b13f, &PRECEU_PH_QBL    , 0,
       DSP_                },        /* PRECEU.PH.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b33f, &PRECEU_PH_QBLA   , 0,
       DSP_                },        /* PRECEU.PH.QBLA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(90) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(91) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(92) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bb3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(93) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bd3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(94) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000bf3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(95) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c13f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(96) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c33f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(97) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(98) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(99) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(100) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cb3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(101) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cd3f, &CFC2             , 0,
       CP2_                },        /* CFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cf3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(103) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d13f, &PRECEU_PH_QBR    , 0,
       DSP_                },        /* PRECEU.PH.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d33f, &PRECEU_PH_QBRA   , 0,
       DSP_                },        /* PRECEU.PH.QBRA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(106) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(107) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(108) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000db3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(109) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000dd3f, &CTC2             , 0,
       CP2_                },        /* CTC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000df3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(111) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e13f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(112) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e33f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(113) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(114) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(115) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(116) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000eb3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(117) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ed3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(118) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ef3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(119) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f13f, &RADDU_W_QB       , 0,
       DSP_                },        /* RADDU.W.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f33f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(121) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f53f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(122) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f73f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(123) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f93f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(124) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fb3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(125) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fd3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(126) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ff3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(127) */
};


static const Pool POOL32Axf_5_group0[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000017f, &TLBGP            , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000037f, &TLBP             , 0,
       CP0_ | TLB_         },        /* TLBP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000057f, &TLBGINV          , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},        /* TLBGINV */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000077f, &TLBINV           , 0,
       CP0_ | TLB_ | TLBINV_},        /* TLBINV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000097f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20000f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000117f, &TLBGR            , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000137f, &TLBR             , 0,
       CP0_ | TLB_         },        /* TLBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000157f, &TLBGINVF         , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},        /* TLBGINVF */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000177f, &TLBINVF          , 0,
       CP0_ | TLB_ | TLBINV_},        /* TLBINVF */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000197f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20001f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000217f, &TLBGWI           , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGWI */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000237f, &TLBWI            , 0,
       CP0_ | TLB_         },        /* TLBWI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000257f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000277f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000297f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20002f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(23) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000317f, &TLBGWR           , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGWR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000337f, &TLBWR            , 0,
       CP0_ | TLB_         },        /* TLBWR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000357f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000377f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000397f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20003f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0~*(31) */
};


static const Pool POOL32Axf_5_group1[32] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000417f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000437f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000457f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(2) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000477f, &DI               , 0,
       0x0                 },        /* DI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000497f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000517f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000537f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000557f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(10) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000577f, &EI               , 0,
       0x0                 },        /* EI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000597f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000617f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000637f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000657f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000677f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000697f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000717f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000737f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000757f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000777f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000797f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007b7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007d7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20007f7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1~*(31) */
};


static const Pool ERETx[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2000f37f, &ERET             , 0,
       0x0                 },        /* ERET */
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2001f37f, &ERETNC           , 0,
       0x0                 },        /* ERETNC */
};


static const Pool POOL32Axf_5_group3[32] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c17f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(0) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c37f, &WAIT             , 0,
       0x0                 },        /* WAIT */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c57f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c77f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c97f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cb7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cd7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cf7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d17f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(8) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d37f, &IRET             , 0,
       MCU_                },        /* IRET */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d57f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d77f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d97f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000db7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000dd7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000df7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e17f, &RDPGPR           , 0,
       CP0_                },        /* RDPGPR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e37f, &DERET            , 0,
       EJTAG_              },        /* DERET */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e57f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e77f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e97f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000eb7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ed7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ef7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(23) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f17f, &WRPGPR           , 0,
       CP0_                },        /* WRPGPR */
    { pool                , ERETx               , 2   , 32,
       0xfc00ffff, 0x2000f37f, 0                      , 0,
       0x0                 },        /* ERETx */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f57f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f77f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000f97f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fb7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000fd7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000ff7f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(31) */
};


static const Pool POOL32Axf_5[4] = {
    { pool                , POOL32Axf_5_group0  , 32  , 32,
       0xfc00c1ff, 0x2000017f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group0 */
    { pool                , POOL32Axf_5_group1  , 32  , 32,
       0xfc00c1ff, 0x2000417f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00c1ff, 0x2000817f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5~*(2) */
    { pool                , POOL32Axf_5_group3  , 32  , 32,
       0xfc00c1ff, 0x2000c17f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3 */
};


static const Pool SHRA__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200001ff, &SHRA_QB          , 0,
       DSP_                },        /* SHRA.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200011ff, &SHRA_R_QB        , 0,
       DSP_                },        /* SHRA_R.QB */
};


static const Pool POOL32Axf_7[8] = {
    { pool                , SHRA__R__QB         , 2   , 32,
       0xfc000fff, 0x200001ff, 0                      , 0,
       0x0                 },        /* SHRA[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003ff, &SHRL_PH          , 0,
       DSP_                },        /* SHRL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200005ff, &REPL_QB          , 0,
       DSP_                },        /* REPL.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200007ff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200009ff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000bff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000dff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000fff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7~*(7) */
};


static const Pool POOL32Axf[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x2000003f, 0                      , 0,
       0x0                 },        /* POOL32Axf~*(0) */
    { pool                , POOL32Axf_1         , 8   , 32,
       0xfc0001ff, 0x2000007f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1 */
    { pool                , POOL32Axf_2         , 4   , 32,
       0xfc0001ff, 0x200000bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x200000ff, 0                      , 0,
       0x0                 },        /* POOL32Axf~*(3) */
    { pool                , POOL32Axf_4         , 128 , 32,
       0xfc0001ff, 0x2000013f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4 */
    { pool                , POOL32Axf_5         , 4   , 32,
       0xfc0001ff, 0x2000017f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0x200001bf, 0                      , 0,
       0x0                 },        /* POOL32Axf~*(6) */
    { pool                , POOL32Axf_7         , 8   , 32,
       0xfc0001ff, 0x200001ff, 0                      , 0,
       0x0                 },        /* POOL32Axf_7 */
};


static const Pool _POOL32A7[8] = {
    { pool                , P_LSX               , 2   , 32,
       0xfc00003f, 0x20000007, 0                      , 0,
       0x0                 },        /* P.LSX */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000000f, &LSA              , 0,
       0x0                 },        /* LSA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000017, 0                      , 0,
       0x0                 },        /* _POOL32A7~*(2) */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000001f, &EXTW             , 0,
       0x0                 },        /* EXTW */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000027, 0                      , 0,
       0x0                 },        /* _POOL32A7~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x2000002f, 0                      , 0,
       0x0                 },        /* _POOL32A7~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000037, 0                      , 0,
       0x0                 },        /* _POOL32A7~*(6) */
    { pool                , POOL32Axf           , 8   , 32,
       0xfc00003f, 0x2000003f, 0                      , 0,
       0x0                 },        /* POOL32Axf */
};


static const Pool P32A[8] = {
    { pool                , _POOL32A0           , 128 , 32,
       0xfc000007, 0x20000000, 0                      , 0,
       0x0                 },        /* _POOL32A0 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000001, &SPECIAL2         , 0,
       UDI_                },        /* SPECIAL2 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000002, &COP2_1           , 0,
       CP2_                },        /* COP2_1 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000003, &UDI              , 0,
       UDI_                },        /* UDI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0x20000004, 0                      , 0,
       0x0                 },        /* P32A~*(4) */
    { pool                , _POOL32A5           , 128 , 32,
       0xfc000007, 0x20000005, 0                      , 0,
       0x0                 },        /* _POOL32A5 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0x20000006, 0                      , 0,
       0x0                 },        /* P32A~*(6) */
    { pool                , _POOL32A7           , 8   , 32,
       0xfc000007, 0x20000007, 0                      , 0,
       0x0                 },        /* _POOL32A7 */
};


static const Pool P_GP_D[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000001, &LD_GP_           , 0,
       MIPS64_             },        /* LD[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000005, &SD_GP_           , 0,
       MIPS64_             },        /* SD[GP] */
};


static const Pool P_GP_W[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000000, &ADDIU_GP_W_      , 0,
       0x0                 },        /* ADDIU[GP.W] */
    { pool                , P_GP_D              , 2   , 32,
       0xfc000003, 0x40000001, 0                      , 0,
       0x0                 },        /* P.GP.D */
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000002, &LW_GP_           , 0,
       0x0                 },        /* LW[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000003, &SW_GP_           , 0,
       0x0                 },        /* SW[GP] */
};


static const Pool POOL48I[32] = {
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600000000000ull, &LI_48_           , 0,
       XMMS_               },        /* LI[48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600100000000ull, &ADDIU_48_        , 0,
       XMMS_               },        /* ADDIU[48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600200000000ull, &ADDIU_GP48_      , 0,
       XMMS_               },        /* ADDIU[GP48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600300000000ull, &ADDIUPC_48_      , 0,
       XMMS_               },        /* ADDIUPC[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600400000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(4) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600500000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(5) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600600000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(6) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600700000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(7) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600800000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(8) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600900000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(9) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600a00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(10) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600b00000000ull, &LWPC_48_         , 0,
       XMMS_               },        /* LWPC[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600c00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(12) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600d00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(13) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600e00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(14) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600f00000000ull, &SWPC_48_         , 0,
       XMMS_               },        /* SWPC[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601000000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(16) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601100000000ull, &DADDIU_48_       , 0,
       MIPS64_             },        /* DADDIU[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601200000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(18) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601300000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(19) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601400000000ull, &DLUI_48_         , 0,
       MIPS64_             },        /* DLUI[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601500000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(21) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601600000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(22) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601700000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(23) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601800000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(24) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601900000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(25) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601a00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(26) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601b00000000ull, &LDPC_48_         , 0,
       MIPS64_             },        /* LDPC[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601c00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(28) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601d00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(29) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601e00000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(30) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601f00000000ull, &SDPC_48_         , 0,
       MIPS64_             },        /* SDPC[48] */
};


static const Pool PP_SR[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003000, &SAVE_32_         , 0,
       0x0                 },        /* SAVE[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f003, 0x80003001, 0                      , 0,
       0x0                 },        /* PP.SR~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003002, &RESTORE_32_      , 0,
       0x0                 },        /* RESTORE[32] */
    { return_instruction  , 0                   , 0   , 32,
       0xfc10f003, 0x80003003, &RESTORE_JRC_32_  , 0,
       0x0                 },        /* RESTORE.JRC[32] */
};


static const Pool P_SR_F[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103000, &SAVEF            , 0,
       CP1_                },        /* SAVEF */
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103001, &RESTOREF         , 0,
       CP1_                },        /* RESTOREF */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103002, 0                      , 0,
       0x0                 },        /* P.SR.F~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103003, 0                      , 0,
       0x0                 },        /* P.SR.F~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103004, 0                      , 0,
       0x0                 },        /* P.SR.F~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103005, 0                      , 0,
       0x0                 },        /* P.SR.F~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103006, 0                      , 0,
       0x0                 },        /* P.SR.F~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f007, 0x80103007, 0                      , 0,
       0x0                 },        /* P.SR.F~*(7) */
};


static const Pool P_SR[2] = {
    { pool                , PP_SR               , 4   , 32,
       0xfc10f000, 0x80003000, 0                      , 0,
       0x0                 },        /* PP.SR */
    { pool                , P_SR_F              , 8   , 32,
       0xfc10f000, 0x80103000, 0                      , 0,
       0x0                 },        /* P.SR.F */
};


static const Pool P_SLL[5] = {
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c000, &NOP_32_          , 0,
       0x0                 },        /* NOP[32] */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c003, &EHB              , 0,
       0x0                 },        /* EHB */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c005, &PAUSE            , 0,
       0x0                 },        /* PAUSE */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c006, &SYNC             , 0,
       0x0                 },        /* SYNC */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c000, &SLL_32_          , 0,
       0x0                 },        /* SLL[32] */
};


static const Pool P_SHIFT[16] = {
    { pool                , P_SLL               , 5   , 32,
       0xfc00f1e0, 0x8000c000, 0                      , 0,
       0x0                 },        /* P.SLL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c020, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c040, &SRL_32_          , 0,
       0x0                 },        /* SRL[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c060, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c080, &SRA              , 0,
       0x0                 },        /* SRA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0a0, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(5) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0c0, &ROTR             , 0,
       0x0                 },        /* ROTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0e0, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c100, &DSLL             , 0,
       MIPS64_             },        /* DSLL */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c120, &DSLL32           , 0,
       MIPS64_             },        /* DSLL32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c140, &DSRL             , 0,
       MIPS64_             },        /* DSRL */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c160, &DSRL32           , 0,
       MIPS64_             },        /* DSRL32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c180, &DSRA             , 0,
       MIPS64_             },        /* DSRA */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1a0, &DSRA32           , 0,
       MIPS64_             },        /* DSRA32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1c0, &DROTR            , 0,
       MIPS64_             },        /* DROTR */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1e0, &DROTR32          , 0,
       MIPS64_             },        /* DROTR32 */
};


static const Pool P_ROTX[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000d000, &ROTX             , 0,
       XMMS_               },        /* ROTX */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d020, 0                      , 0,
       0x0                 },        /* P.ROTX~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d800, 0                      , 0,
       0x0                 },        /* P.ROTX~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f820, 0x8000d820, 0                      , 0,
       0x0                 },        /* P.ROTX~*(3) */
};


static const Pool P_INS[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e000, &INS              , 0,
       XMMS_               },        /* INS */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e020, &DINSU            , 0,
       MIPS64_             },        /* DINSU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e800, &DINSM            , 0,
       MIPS64_             },        /* DINSM */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e820, &DINS             , 0,
       MIPS64_             },        /* DINS */
};


static const Pool P_EXT[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f000, &EXT              , 0,
       XMMS_               },        /* EXT */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f020, &DEXTU            , 0,
       MIPS64_             },        /* DEXTU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f800, &DEXTM            , 0,
       MIPS64_             },        /* DEXTM */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f820, &DEXT             , 0,
       MIPS64_             },        /* DEXT */
};


static const Pool P_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80000000, &ORI              , 0,
       0x0                 },        /* ORI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80001000, &XORI             , 0,
       0x0                 },        /* XORI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80002000, &ANDI_32_         , 0,
       0x0                 },        /* ANDI[32] */
    { pool                , P_SR                , 2   , 32,
       0xfc00f000, 0x80003000, 0                      , 0,
       0x0                 },        /* P.SR */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80004000, &SLTI             , 0,
       0x0                 },        /* SLTI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80005000, &SLTIU            , 0,
       0x0                 },        /* SLTIU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80006000, &SEQI             , 0,
       0x0                 },        /* SEQI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x80007000, 0                      , 0,
       0x0                 },        /* P.U12~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80008000, &ADDIU_NEG_       , 0,
       0x0                 },        /* ADDIU[NEG] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80009000, &DADDIU_U12_      , 0,
       MIPS64_             },        /* DADDIU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000a000, &DADDIU_NEG_      , 0,
       MIPS64_             },        /* DADDIU[NEG] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000b000, &DROTX            , 0,
       MIPS64_             },        /* DROTX */
    { pool                , P_SHIFT             , 16  , 32,
       0xfc00f000, 0x8000c000, 0                      , 0,
       0x0                 },        /* P.SHIFT */
    { pool                , P_ROTX              , 4   , 32,
       0xfc00f000, 0x8000d000, 0                      , 0,
       0x0                 },        /* P.ROTX */
    { pool                , P_INS               , 4   , 32,
       0xfc00f000, 0x8000e000, 0                      , 0,
       0x0                 },        /* P.INS */
    { pool                , P_EXT               , 4   , 32,
       0xfc00f000, 0x8000f000, 0                      , 0,
       0x0                 },        /* P.EXT */
};


static const Pool RINT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000020, &RINT_S           , 0,
       CP1_                },        /* RINT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000220, &RINT_D           , 0,
       CP1_                },        /* RINT.D */
};


static const Pool ADD_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000030, &ADD_S            , 0,
       CP1_                },        /* ADD.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000230, 0                      , 0,
       CP1_                },        /* ADD.fmt0~*(1) */
};


static const Pool SELEQZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000038, &SELEQZ_S         , 0,
       CP1_                },        /* SELEQZ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000238, &SELEQZ_D         , 0,
       CP1_                },        /* SELEQZ.D */
};


static const Pool CLASS_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000060, &CLASS_S          , 0,
       CP1_                },        /* CLASS.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000260, &CLASS_D          , 0,
       CP1_                },        /* CLASS.D */
};


static const Pool SUB_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000070, &SUB_S            , 0,
       CP1_                },        /* SUB.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000270, 0                      , 0,
       CP1_                },        /* SUB.fmt0~*(1) */
};


static const Pool SELNEZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000078, &SELNEZ_S         , 0,
       CP1_                },        /* SELNEZ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000278, &SELNEZ_D         , 0,
       CP1_                },        /* SELNEZ.D */
};


static const Pool MUL_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b0, &MUL_S            , 0,
       CP1_                },        /* MUL.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b0, 0                      , 0,
       CP1_                },        /* MUL.fmt0~*(1) */
};


static const Pool SEL_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b8, &SEL_S            , 0,
       CP1_                },        /* SEL.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b8, &SEL_D            , 0,
       CP1_                },        /* SEL.D */
};


static const Pool DIV_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000f0, &DIV_S            , 0,
       CP1_                },        /* DIV.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002f0, 0                      , 0,
       CP1_                },        /* DIV.fmt0~*(1) */
};


static const Pool ADD_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000130, &ADD_D            , 0,
       CP1_                },        /* ADD.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000330, 0                      , 0,
       CP1_                },        /* ADD.fmt1~*(1) */
};


static const Pool SUB_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000170, &SUB_D            , 0,
       CP1_                },        /* SUB.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000370, 0                      , 0,
       CP1_                },        /* SUB.fmt1~*(1) */
};


static const Pool MUL_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b0, &MUL_D            , 0,
       CP1_                },        /* MUL.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b0, 0                      , 0,
       CP1_                },        /* MUL.fmt1~*(1) */
};


static const Pool MADDF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b8, &MADDF_S          , 0,
       CP1_                },        /* MADDF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b8, &MADDF_D          , 0,
       CP1_                },        /* MADDF.D */
};


static const Pool DIV_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f0, &DIV_D            , 0,
       CP1_                },        /* DIV.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f0, 0                      , 0,
       CP1_                },        /* DIV.fmt1~*(1) */
};


static const Pool MSUBF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f8, &MSUBF_S          , 0,
       CP1_                },        /* MSUBF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f8, &MSUBF_D          , 0,
       CP1_                },        /* MSUBF.D */
};


static const Pool POOL32F_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000000, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000008, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000010, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000018, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(3) */
    { pool                , RINT_fmt            , 2   , 32,
       0xfc0001ff, 0xa0000020, 0                      , 0,
       CP1_                },        /* RINT.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000028, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(5) */
    { pool                , ADD_fmt0            , 2   , 32,
       0xfc0001ff, 0xa0000030, 0                      , 0,
       CP1_                },        /* ADD.fmt0 */
    { pool                , SELEQZ_fmt          , 2   , 32,
       0xfc0001ff, 0xa0000038, 0                      , 0,
       CP1_                },        /* SELEQZ.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000040, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000048, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000050, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000058, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(11) */
    { pool                , CLASS_fmt           , 2   , 32,
       0xfc0001ff, 0xa0000060, 0                      , 0,
       CP1_                },        /* CLASS.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000068, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(13) */
    { pool                , SUB_fmt0            , 2   , 32,
       0xfc0001ff, 0xa0000070, 0                      , 0,
       CP1_                },        /* SUB.fmt0 */
    { pool                , SELNEZ_fmt          , 2   , 32,
       0xfc0001ff, 0xa0000078, 0                      , 0,
       CP1_                },        /* SELNEZ.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000080, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000088, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000090, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000098, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000a0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000a8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(21) */
    { pool                , MUL_fmt0            , 2   , 32,
       0xfc0001ff, 0xa00000b0, 0                      , 0,
       CP1_                },        /* MUL.fmt0 */
    { pool                , SEL_fmt             , 2   , 32,
       0xfc0001ff, 0xa00000b8, 0                      , 0,
       CP1_                },        /* SEL.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000c0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000c8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000d0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000d8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000e0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000e8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(29) */
    { pool                , DIV_fmt0            , 2   , 32,
       0xfc0001ff, 0xa00000f0, 0                      , 0,
       CP1_                },        /* DIV.fmt0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00000f8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(31) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000100, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(32) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000108, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(33) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000110, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(34) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000118, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(35) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000120, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(36) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000128, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(37) */
    { pool                , ADD_fmt1            , 2   , 32,
       0xfc0001ff, 0xa0000130, 0                      , 0,
       CP1_                },        /* ADD.fmt1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000138, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(39) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000140, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(40) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000148, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(41) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000150, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(42) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000158, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(43) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000160, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(44) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000168, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(45) */
    { pool                , SUB_fmt1            , 2   , 32,
       0xfc0001ff, 0xa0000170, 0                      , 0,
       CP1_                },        /* SUB.fmt1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000178, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(47) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000180, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(48) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000188, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(49) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000190, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(50) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa0000198, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(51) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001a0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001a8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(53) */
    { pool                , MUL_fmt1            , 2   , 32,
       0xfc0001ff, 0xa00001b0, 0                      , 0,
       CP1_                },        /* MUL.fmt1 */
    { pool                , MADDF_fmt           , 2   , 32,
       0xfc0001ff, 0xa00001b8, 0                      , 0,
       CP1_                },        /* MADDF.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001c0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(56) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001c8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(57) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001d0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(58) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001d8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(59) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001e0, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xa00001e8, 0                      , 0,
       CP1_                },        /* POOL32F_0~*(61) */
    { pool                , DIV_fmt1            , 2   , 32,
       0xfc0001ff, 0xa00001f0, 0                      , 0,
       CP1_                },        /* DIV.fmt1 */
    { pool                , MSUBF_fmt           , 2   , 32,
       0xfc0001ff, 0xa00001f8, 0                      , 0,
       CP1_                },        /* MSUBF.fmt */
};


static const Pool MIN_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000003, &MIN_S            , 0,
       CP1_                },        /* MIN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000203, &MIN_D            , 0,
       CP1_                },        /* MIN.D */
};


static const Pool MAX_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000000b, &MAX_S            , 0,
       CP1_                },        /* MAX.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000020b, &MAX_D            , 0,
       CP1_                },        /* MAX.D */
};


static const Pool MINA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000023, &MINA_S           , 0,
       CP1_                },        /* MINA.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000223, &MINA_D           , 0,
       CP1_                },        /* MINA.D */
};


static const Pool MAXA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000002b, &MAXA_S           , 0,
       CP1_                },        /* MAXA.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000022b, &MAXA_D           , 0,
       CP1_                },        /* MAXA.D */
};


static const Pool CVT_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000013b, &CVT_L_S          , 0,
       CP1_                },        /* CVT.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000413b, &CVT_L_D          , 0,
       CP1_                },        /* CVT.L.D */
};


static const Pool RSQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000023b, &RSQRT_S          , 0,
       CP1_                },        /* RSQRT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000423b, &RSQRT_D          , 0,
       CP1_                },        /* RSQRT.D */
};


static const Pool FLOOR_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000033b, &FLOOR_L_S        , 0,
       CP1_                },        /* FLOOR.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000433b, &FLOOR_L_D        , 0,
       CP1_                },        /* FLOOR.L.D */
};


static const Pool CVT_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000093b, &CVT_W_S          , 0,
       CP1_                },        /* CVT.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000493b, &CVT_W_D          , 0,
       CP1_                },        /* CVT.W.D */
};


static const Pool SQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000a3b, &SQRT_S           , 0,
       CP1_                },        /* SQRT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004a3b, &SQRT_D           , 0,
       CP1_                },        /* SQRT.D */
};


static const Pool FLOOR_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b3b, &FLOOR_W_S        , 0,
       CP1_                },        /* FLOOR.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b3b, &FLOOR_W_D        , 0,
       CP1_                },        /* FLOOR.W.D */
};


static const Pool RECIP_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000123b, &RECIP_S          , 0,
       CP1_                },        /* RECIP.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000523b, &RECIP_D          , 0,
       CP1_                },        /* RECIP.D */
};


static const Pool CEIL_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000133b, &CEIL_L_S         , 0,
       CP1_                },        /* CEIL.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000533b, &CEIL_L_D         , 0,
       CP1_                },        /* CEIL.L.D */
};


static const Pool CEIL_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b3b, &CEIL_W_S         , 0,
       CP1_                },        /* CEIL.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b3b, &CEIL_W_D         , 0,
       CP1_                },        /* CEIL.W.D */
};


static const Pool TRUNC_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000233b, &TRUNC_L_S        , 0,
       CP1_                },        /* TRUNC.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000633b, &TRUNC_L_D        , 0,
       CP1_                },        /* TRUNC.L.D */
};


static const Pool TRUNC_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b3b, &TRUNC_W_S        , 0,
       CP1_                },        /* TRUNC.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b3b, &TRUNC_W_D        , 0,
       CP1_                },        /* TRUNC.W.D */
};


static const Pool ROUND_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000333b, &ROUND_L_S        , 0,
       CP1_                },        /* ROUND.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000733b, &ROUND_L_D        , 0,
       CP1_                },        /* ROUND.L.D */
};


static const Pool ROUND_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b3b, &ROUND_W_S        , 0,
       CP1_                },        /* ROUND.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b3b, &ROUND_W_D        , 0,
       CP1_                },        /* ROUND.W.D */
};


static const Pool POOL32Fxf_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000003b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(0) */
    { pool                , CVT_L_fmt           , 2   , 32,
       0xfc003fff, 0xa000013b, 0                      , 0,
       CP1_                },        /* CVT.L.fmt */
    { pool                , RSQRT_fmt           , 2   , 32,
       0xfc003fff, 0xa000023b, 0                      , 0,
       CP1_                },        /* RSQRT.fmt */
    { pool                , FLOOR_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000033b, 0                      , 0,
       CP1_                },        /* FLOOR.L.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000043b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000053b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000063b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000073b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000083b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(8) */
    { pool                , CVT_W_fmt           , 2   , 32,
       0xfc003fff, 0xa000093b, 0                      , 0,
       CP1_                },        /* CVT.W.fmt */
    { pool                , SQRT_fmt            , 2   , 32,
       0xfc003fff, 0xa0000a3b, 0                      , 0,
       CP1_                },        /* SQRT.fmt */
    { pool                , FLOOR_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0000b3b, 0                      , 0,
       CP1_                },        /* FLOOR.W.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000c3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000d3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000e3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0000f3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000103b, &CFC1             , 0,
       CP1_                },        /* CFC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000113b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(17) */
    { pool                , RECIP_fmt           , 2   , 32,
       0xfc003fff, 0xa000123b, 0                      , 0,
       CP1_                },        /* RECIP.fmt */
    { pool                , CEIL_L_fmt          , 2   , 32,
       0xfc003fff, 0xa000133b, 0                      , 0,
       CP1_                },        /* CEIL.L.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000143b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000153b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000163b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000173b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(23) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000183b, &CTC1             , 0,
       CP1_                },        /* CTC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000193b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001a3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(26) */
    { pool                , CEIL_W_fmt          , 2   , 32,
       0xfc003fff, 0xa0001b3b, 0                      , 0,
       CP1_                },        /* CEIL.W.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001c3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001d3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001e3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0001f3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(31) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000203b, &MFC1             , 0,
       CP1_                },        /* MFC1 */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000213b, &CVT_S_PL         , 0,
       CP1_                },        /* CVT.S.PL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000223b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(34) */
    { pool                , TRUNC_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000233b, 0                      , 0,
       CP1_                },        /* TRUNC.L.fmt */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000243b, &DMFC1            , 0,
       CP1_ | MIPS64_      },        /* DMFC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000253b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(37) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000263b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(38) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000273b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(39) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000283b, &MTC1             , 0,
       CP1_                },        /* MTC1 */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000293b, &CVT_S_PU         , 0,
       CP1_                },        /* CVT.S.PU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002a3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(42) */
    { pool                , TRUNC_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0002b3b, 0                      , 0,
       CP1_                },        /* TRUNC.W.fmt */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa0002c3b, &DMTC1            , 0,
       CP1_ | MIPS64_      },        /* DMTC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002d3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(45) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002e3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(46) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002f3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(47) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000303b, &MFHC1            , 0,
       CP1_                },        /* MFHC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000313b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(49) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000323b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(50) */
    { pool                , ROUND_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000333b, 0                      , 0,
       CP1_                },        /* ROUND.L.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000343b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000353b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(53) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000363b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(54) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000373b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000383b, &MTHC1            , 0,
       CP1_                },        /* MTHC1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000393b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(57) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003a3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(58) */
    { pool                , ROUND_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0003b3b, 0                      , 0,
       CP1_                },        /* ROUND.W.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003c3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003d3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(61) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003e3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(62) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0003f3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(63) */
};


static const Pool MOV_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000007b, &MOV_S            , 0,
       CP1_                },        /* MOV.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000207b, &MOV_D            , 0,
       CP1_                },        /* MOV.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000407b, 0                      , 0,
       CP1_                },        /* MOV.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000607b, 0                      , 0,
       CP1_                },        /* MOV.fmt~*(3) */
};


static const Pool ABS_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000037b, &ABS_S            , 0,
       CP1_                },        /* ABS.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000237b, &ABS_D            , 0,
       CP1_                },        /* ABS.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000437b, 0                      , 0,
       CP1_                },        /* ABS.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000637b, 0                      , 0,
       CP1_                },        /* ABS.fmt~*(3) */
};


static const Pool NEG_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b7b, &NEG_S            , 0,
       CP1_                },        /* NEG.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b7b, &NEG_D            , 0,
       CP1_                },        /* NEG.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b7b, 0                      , 0,
       CP1_                },        /* NEG.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b7b, 0                      , 0,
       CP1_                },        /* NEG.fmt~*(3) */
};


static const Pool CVT_D_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000137b, &CVT_D_S          , 0,
       CP1_                },        /* CVT.D.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000337b, &CVT_D_W          , 0,
       CP1_                },        /* CVT.D.W */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000537b, &CVT_D_L          , 0,
       CP1_                },        /* CVT.D.L */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000737b, 0                      , 0,
       CP1_                },        /* CVT.D.fmt~*(3) */
};


static const Pool CVT_S_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b7b, &CVT_S_D          , 0,
       CP1_                },        /* CVT.S.D */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b7b, &CVT_S_W          , 0,
       CP1_                },        /* CVT.S.W */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b7b, &CVT_S_L          , 0,
       CP1_                },        /* CVT.S.L */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b7b, 0                      , 0,
       CP1_                },        /* CVT.S.fmt~*(3) */
};


static const Pool POOL32Fxf_1[32] = {
    { pool                , MOV_fmt             , 4   , 32,
       0xfc001fff, 0xa000007b, 0                      , 0,
       CP1_                },        /* MOV.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000017b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000027b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(2) */
    { pool                , ABS_fmt             , 4   , 32,
       0xfc001fff, 0xa000037b, 0                      , 0,
       CP1_                },        /* ABS.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000047b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000057b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000067b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000077b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000087b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000097b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000a7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(10) */
    { pool                , NEG_fmt             , 4   , 32,
       0xfc001fff, 0xa0000b7b, 0                      , 0,
       CP1_                },        /* NEG.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000c7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000d7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000e7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0000f7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000107b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000117b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000127b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(18) */
    { pool                , CVT_D_fmt           , 4   , 32,
       0xfc001fff, 0xa000137b, 0                      , 0,
       CP1_                },        /* CVT.D.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000147b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000157b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000167b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000177b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000187b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa000197b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001a7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(26) */
    { pool                , CVT_S_fmt           , 4   , 32,
       0xfc001fff, 0xa0001b7b, 0                      , 0,
       CP1_                },        /* CVT.S.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001c7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001d7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001e7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc001fff, 0xa0001f7b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1~*(31) */
};


static const Pool POOL32Fxf[4] = {
    { pool                , POOL32Fxf_0         , 64  , 32,
       0xfc0000ff, 0xa000003b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0 */
    { pool                , POOL32Fxf_1         , 32  , 32,
       0xfc0000ff, 0xa000007b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_1 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0000ff, 0xa00000bb, 0                      , 0,
       CP1_                },        /* POOL32Fxf~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0000ff, 0xa00000fb, 0                      , 0,
       CP1_                },        /* POOL32Fxf~*(3) */
};


static const Pool POOL32F_3[8] = {
    { pool                , MIN_fmt             , 2   , 32,
       0xfc00003f, 0xa0000003, 0                      , 0,
       CP1_                },        /* MIN.fmt */
    { pool                , MAX_fmt             , 2   , 32,
       0xfc00003f, 0xa000000b, 0                      , 0,
       CP1_                },        /* MAX.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000013, 0                      , 0,
       CP1_                },        /* POOL32F_3~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000001b, 0                      , 0,
       CP1_                },        /* POOL32F_3~*(3) */
    { pool                , MINA_fmt            , 2   , 32,
       0xfc00003f, 0xa0000023, 0                      , 0,
       CP1_                },        /* MINA.fmt */
    { pool                , MAXA_fmt            , 2   , 32,
       0xfc00003f, 0xa000002b, 0                      , 0,
       CP1_                },        /* MAXA.fmt */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000033, 0                      , 0,
       CP1_                },        /* POOL32F_3~*(6) */
    { pool                , POOL32Fxf           , 4   , 32,
       0xfc00003f, 0xa000003b, 0                      , 0,
       CP1_                },        /* POOL32Fxf */
};


static const Pool CMP_condn_S[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000005, &CMP_AF_S         , 0,
       CP1_                },        /* CMP.AF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000045, &CMP_UN_S         , 0,
       CP1_                },        /* CMP.UN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000085, &CMP_EQ_S         , 0,
       CP1_                },        /* CMP.EQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000c5, &CMP_UEQ_S        , 0,
       CP1_                },        /* CMP.UEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000105, &CMP_LT_S         , 0,
       CP1_                },        /* CMP.LT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000145, &CMP_ULT_S        , 0,
       CP1_                },        /* CMP.ULT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000185, &CMP_LE_S         , 0,
       CP1_                },        /* CMP.LE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001c5, &CMP_ULE_S        , 0,
       CP1_                },        /* CMP.ULE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000205, &CMP_SAF_S        , 0,
       CP1_                },        /* CMP.SAF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000245, &CMP_SUN_S        , 0,
       CP1_                },        /* CMP.SUN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000285, &CMP_SEQ_S        , 0,
       CP1_                },        /* CMP.SEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002c5, &CMP_SUEQ_S       , 0,
       CP1_                },        /* CMP.SUEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000305, &CMP_SLT_S        , 0,
       CP1_                },        /* CMP.SLT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000345, &CMP_SULT_S       , 0,
       CP1_                },        /* CMP.SULT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000385, &CMP_SLE_S        , 0,
       CP1_                },        /* CMP.SLE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003c5, &CMP_SULE_S       , 0,
       CP1_                },        /* CMP.SULE.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000405, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(16) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000445, &CMP_OR_S         , 0,
       CP1_                },        /* CMP.OR.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000485, &CMP_UNE_S        , 0,
       CP1_                },        /* CMP.UNE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004c5, &CMP_NE_S         , 0,
       CP1_                },        /* CMP.NE.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000505, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000545, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000585, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00005c5, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000605, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(24) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000645, &CMP_SOR_S        , 0,
       CP1_                },        /* CMP.SOR.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000685, &CMP_SUNE_S       , 0,
       CP1_                },        /* CMP.SUNE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006c5, &CMP_SNE_S        , 0,
       CP1_                },        /* CMP.SNE.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000705, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000745, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000785, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00007c5, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(31) */
};


static const Pool CMP_condn_D[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000015, &CMP_AF_D         , 0,
       CP1_                },        /* CMP.AF.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000055, &CMP_UN_D         , 0,
       CP1_                },        /* CMP.UN.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000095, &CMP_EQ_D         , 0,
       CP1_                },        /* CMP.EQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000d5, &CMP_UEQ_D        , 0,
       CP1_                },        /* CMP.UEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000115, &CMP_LT_D         , 0,
       CP1_                },        /* CMP.LT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000155, &CMP_ULT_D        , 0,
       CP1_                },        /* CMP.ULT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000195, &CMP_LE_D         , 0,
       CP1_                },        /* CMP.LE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001d5, &CMP_ULE_D        , 0,
       CP1_                },        /* CMP.ULE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000215, &CMP_SAF_D        , 0,
       CP1_                },        /* CMP.SAF.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000255, &CMP_SUN_D        , 0,
       CP1_                },        /* CMP.SUN.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000295, &CMP_SEQ_D        , 0,
       CP1_                },        /* CMP.SEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002d5, &CMP_SUEQ_D       , 0,
       CP1_                },        /* CMP.SUEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000315, &CMP_SLT_D        , 0,
       CP1_                },        /* CMP.SLT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000355, &CMP_SULT_D       , 0,
       CP1_                },        /* CMP.SULT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000395, &CMP_SLE_D        , 0,
       CP1_                },        /* CMP.SLE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003d5, &CMP_SULE_D       , 0,
       CP1_                },        /* CMP.SULE.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000415, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(16) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000455, &CMP_OR_D         , 0,
       CP1_                },        /* CMP.OR.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000495, &CMP_UNE_D        , 0,
       CP1_                },        /* CMP.UNE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004d5, &CMP_NE_D         , 0,
       CP1_                },        /* CMP.NE.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000515, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000555, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000595, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00005d5, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000615, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(24) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000655, &CMP_SOR_D        , 0,
       CP1_                },        /* CMP.SOR.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000695, &CMP_SUNE_D       , 0,
       CP1_                },        /* CMP.SUNE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006d5, &CMP_SNE_D        , 0,
       CP1_                },        /* CMP.SNE.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000715, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000755, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000795, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa00007d5, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(31) */
};


static const Pool POOL32F_5[8] = {
    { pool                , CMP_condn_S         , 32  , 32,
       0xfc00003f, 0xa0000005, 0                      , 0,
       CP1_                },        /* CMP.condn.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000000d, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(1) */
    { pool                , CMP_condn_D         , 32  , 32,
       0xfc00003f, 0xa0000015, 0                      , 0,
       CP1_                },        /* CMP.condn.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000001d, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000025, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000002d, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa0000035, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xa000003d, 0                      , 0,
       CP1_                },        /* POOL32F_5~*(7) */
};


static const Pool POOL32F[8] = {
    { pool                , POOL32F_0           , 64  , 32,
       0xfc000007, 0xa0000000, 0                      , 0,
       CP1_                },        /* POOL32F_0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000001, 0                      , 0,
       CP1_                },        /* POOL32F~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000002, 0                      , 0,
       CP1_                },        /* POOL32F~*(2) */
    { pool                , POOL32F_3           , 8   , 32,
       0xfc000007, 0xa0000003, 0                      , 0,
       CP1_                },        /* POOL32F_3 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000004, 0                      , 0,
       CP1_                },        /* POOL32F~*(4) */
    { pool                , POOL32F_5           , 8   , 32,
       0xfc000007, 0xa0000005, 0                      , 0,
       CP1_                },        /* POOL32F_5 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000006, 0                      , 0,
       CP1_                },        /* POOL32F~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xa0000007, 0                      , 0,
       CP1_                },        /* POOL32F~*(7) */
};


static const Pool POOL32S_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000000, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(0) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000008, &DLSA             , 0,
       MIPS64_             },        /* DLSA */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000010, &DSLLV            , 0,
       MIPS64_             },        /* DSLLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000018, &DMUL             , 0,
       MIPS64_             },        /* DMUL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000020, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000028, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000030, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000038, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000040, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000048, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(9) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000050, &DSRLV            , 0,
       MIPS64_             },        /* DSRLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000058, &DMUH             , 0,
       MIPS64_             },        /* DMUH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000060, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000068, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000070, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000078, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000080, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000088, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(17) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000090, &DSRAV            , 0,
       MIPS64_             },        /* DSRAV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000098, &DMULU            , 0,
       MIPS64_             },        /* DMULU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000a0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000a8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000b0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000b8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000c0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000c8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(25) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000d0, &DROTRV           , 0,
       MIPS64_             },        /* DROTRV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000d8, &DMUHU            , 0,
       MIPS64_             },        /* DMUHU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000e0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000e8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000f0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000f8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(31) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000100, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(32) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000108, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(33) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000110, &DADD             , 0,
       MIPS64_             },        /* DADD */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000118, &DDIV             , 0,
       MIPS64_             },        /* DDIV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000120, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(36) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000128, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(37) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000130, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(38) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000138, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(39) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000140, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(40) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000148, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(41) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000150, &DADDU            , 0,
       MIPS64_             },        /* DADDU */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000158, &DMOD             , 0,
       MIPS64_             },        /* DMOD */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000160, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(44) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000168, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(45) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000170, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(46) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000178, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(47) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000180, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(48) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000188, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(49) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000190, &DSUB             , 0,
       MIPS64_             },        /* DSUB */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000198, &DDIVU            , 0,
       MIPS64_             },        /* DDIVU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001a0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001a8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(53) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001b0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(54) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001b8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(55) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001c0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(56) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001c8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(57) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001d0, &DSUBU            , 0,
       MIPS64_             },        /* DSUBU */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001d8, &DMODU            , 0,
       MIPS64_             },        /* DMODU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001e0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001e8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(61) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001f0, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(62) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001f8, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(63) */
};


static const Pool POOL32Sxf_4[128] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000013c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000033c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000053c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000073c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000093c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0000f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000113c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000133c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000153c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000173c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000193c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0001f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000213c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000233c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000253c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000273c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000293c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0002f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000313c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000333c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000353c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000373c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000393c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0003f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(31) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000413c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(32) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000433c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(33) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000453c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(34) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000473c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(35) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000493c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(36) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004b3c, &DCLO             , 0,
       MIPS64_             },        /* DCLO */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(38) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0004f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(39) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000513c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(40) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000533c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(41) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000553c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(42) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000573c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(43) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000593c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(44) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005b3c, &DCLZ             , 0,
       MIPS64_             },        /* DCLZ */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(46) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0005f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(47) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000613c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(48) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000633c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(49) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000653c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(50) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000673c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(51) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000693c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(53) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(54) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0006f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(55) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000713c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(56) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000733c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(57) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000753c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(58) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000773c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(59) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000793c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(61) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(62) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0007f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(63) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000813c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(64) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000833c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(65) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000853c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(66) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000873c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(67) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000893c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(68) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(69) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(70) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0008f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(71) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000913c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(72) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000933c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(73) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000953c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(74) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000973c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(75) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000993c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(76) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009b3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(77) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009d3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(78) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc0009f3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(79) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(80) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(81) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(82) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(83) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000a93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(84) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ab3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(85) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ad3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(86) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000af3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(87) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(88) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(89) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(90) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(91) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000b93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(92) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bb3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(93) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bd3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(94) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000bf3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(95) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(96) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(97) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(98) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(99) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000c93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(100) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cb3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(101) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cd3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(102) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000cf3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(103) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(104) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(105) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(106) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(107) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000d93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(108) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000db3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(109) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000dd3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(110) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000df3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(111) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(112) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(113) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(114) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(115) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000e93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(116) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000eb3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(117) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ed3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(118) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ef3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(119) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f13c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(120) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f33c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(121) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f53c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(122) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f73c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(123) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000f93c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(124) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000fb3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(125) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000fd3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(126) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0xc000ff3c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4~*(127) */
};


static const Pool POOL32Sxf[8] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000003c, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000007c, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000bc, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000fc, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(3) */
    { pool                , POOL32Sxf_4         , 128 , 32,
       0xfc0001ff, 0xc000013c, 0                      , 0,
       0x0                 },        /* POOL32Sxf_4 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc000017c, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001bc, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001fc, 0                      , 0,
       0x0                 },        /* POOL32Sxf~*(7) */
};


static const Pool POOL32S_4[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc0000004, &EXTD             , 0,
       MIPS64_             },        /* EXTD */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc000000c, &EXTD32           , 0,
       MIPS64_             },        /* EXTD32 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000014, 0                      , 0,
       0x0                 },        /* POOL32S_4~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc000001c, 0                      , 0,
       0x0                 },        /* POOL32S_4~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000024, 0                      , 0,
       0x0                 },        /* POOL32S_4~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc000002c, 0                      , 0,
       0x0                 },        /* POOL32S_4~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0xc0000034, 0                      , 0,
       0x0                 },        /* POOL32S_4~*(6) */
    { pool                , POOL32Sxf           , 8   , 32,
       0xfc00003f, 0xc000003c, 0                      , 0,
       0x0                 },        /* POOL32Sxf */
};


static const Pool POOL32S[8] = {
    { pool                , POOL32S_0           , 64  , 32,
       0xfc000007, 0xc0000000, 0                      , 0,
       0x0                 },        /* POOL32S_0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000001, 0                      , 0,
       0x0                 },        /* POOL32S~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000002, 0                      , 0,
       0x0                 },        /* POOL32S~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000003, 0                      , 0,
       0x0                 },        /* POOL32S~*(3) */
    { pool                , POOL32S_4           , 8   , 32,
       0xfc000007, 0xc0000004, 0                      , 0,
       0x0                 },        /* POOL32S_4 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000005, 0                      , 0,
       0x0                 },        /* POOL32S~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000006, 0                      , 0,
       0x0                 },        /* POOL32S~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000007, 0xc0000007, 0                      , 0,
       0x0                 },        /* POOL32S~*(7) */
};


static const Pool P_LUI[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000000, &LUI              , 0,
       0x0                 },        /* LUI */
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000002, &ALUIPC           , 0,
       0x0                 },        /* ALUIPC */
};


static const Pool P_GP_LH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100000, &LH_GP_           , 0,
       0x0                 },        /* LH[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100001, &LHU_GP_          , 0,
       0x0                 },        /* LHU[GP] */
};


static const Pool P_GP_SH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44140000, &SH_GP_           , 0,
       0x0                 },        /* SH[GP] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0001, 0x44140001, 0                      , 0,
       0x0                 },        /* P.GP.SH~*(1) */
};


static const Pool P_GP_CP1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180000, &LWC1_GP_         , 0,
       CP1_                },        /* LWC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180001, &SWC1_GP_         , 0,
       CP1_                },        /* SWC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180002, &LDC1_GP_         , 0,
       CP1_                },        /* LDC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180003, &SDC1_GP_         , 0,
       CP1_                },        /* SDC1[GP] */
};


static const Pool P_GP_M64[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0000, &LWU_GP_          , 0,
       MIPS64_             },        /* LWU[GP] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0001, 0                      , 0,
       0x0                 },        /* P.GP.M64~*(1) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0002, 0                      , 0,
       0x0                 },        /* P.GP.M64~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0003, 0                      , 0,
       0x0                 },        /* P.GP.M64~*(3) */
};


static const Pool P_GP_BH[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44000000, &LB_GP_           , 0,
       0x0                 },        /* LB[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44040000, &SB_GP_           , 0,
       0x0                 },        /* SB[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44080000, &LBU_GP_          , 0,
       0x0                 },        /* LBU[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x440c0000, &ADDIU_GP_B_      , 0,
       0x0                 },        /* ADDIU[GP.B] */
    { pool                , P_GP_LH             , 2   , 32,
       0xfc1c0000, 0x44100000, 0                      , 0,
       0x0                 },        /* P.GP.LH */
    { pool                , P_GP_SH             , 2   , 32,
       0xfc1c0000, 0x44140000, 0                      , 0,
       0x0                 },        /* P.GP.SH */
    { pool                , P_GP_CP1            , 4   , 32,
       0xfc1c0000, 0x44180000, 0                      , 0,
       0x0                 },        /* P.GP.CP1 */
    { pool                , P_GP_M64            , 4   , 32,
       0xfc1c0000, 0x441c0000, 0                      , 0,
       0x0                 },        /* P.GP.M64 */
};


static const Pool P_LS_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84000000, &LB_U12_          , 0,
       0x0                 },        /* LB[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84001000, &SB_U12_          , 0,
       0x0                 },        /* SB[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84002000, &LBU_U12_         , 0,
       0x0                 },        /* LBU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84003000, &PREF_U12_        , 0,
       0x0                 },        /* PREF[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84004000, &LH_U12_          , 0,
       0x0                 },        /* LH[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84005000, &SH_U12_          , 0,
       0x0                 },        /* SH[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84006000, &LHU_U12_         , 0,
       0x0                 },        /* LHU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84007000, &LWU_U12_         , 0,
       MIPS64_             },        /* LWU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84008000, &LW_U12_          , 0,
       0x0                 },        /* LW[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84009000, &SW_U12_          , 0,
       0x0                 },        /* SW[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400a000, &LWC1_U12_        , 0,
       CP1_                },        /* LWC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400b000, &SWC1_U12_        , 0,
       CP1_                },        /* SWC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400c000, &LD_U12_          , 0,
       MIPS64_             },        /* LD[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400d000, &SD_U12_          , 0,
       MIPS64_             },        /* SD[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400e000, &LDC1_U12_        , 0,
       CP1_                },        /* LDC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400f000, &SDC1_U12_        , 0,
       CP1_                },        /* SDC1[U12] */
};


static const Pool P_PREF_S9_[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01800, &SYNCI            , 0,
       0x0                 },        /* SYNCI */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001800, &PREF_S9_         , &PREF_S9__cond    ,
       0x0                 },        /* PREF[S9] */
};


static const Pool P_LS_S0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000000, &LB_S9_           , 0,
       0x0                 },        /* LB[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000800, &SB_S9_           , 0,
       0x0                 },        /* SB[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001000, &LBU_S9_          , 0,
       0x0                 },        /* LBU[S9] */
    { pool                , P_PREF_S9_          , 2   , 32,
       0xfc007f00, 0xa4001800, 0                      , 0,
       0x0                 },        /* P.PREF[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002000, &LH_S9_           , 0,
       0x0                 },        /* LH[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002800, &SH_S9_           , 0,
       0x0                 },        /* SH[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003000, &LHU_S9_          , 0,
       0x0                 },        /* LHU[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003800, &LWU_S9_          , 0,
       MIPS64_             },        /* LWU[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004000, &LW_S9_           , 0,
       0x0                 },        /* LW[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004800, &SW_S9_           , 0,
       0x0                 },        /* SW[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005000, &LWC1_S9_         , 0,
       CP1_                },        /* LWC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005800, &SWC1_S9_         , 0,
       CP1_                },        /* SWC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006000, &LD_S9_           , 0,
       MIPS64_             },        /* LD[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006800, &SD_S9_           , 0,
       MIPS64_             },        /* SD[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007000, &LDC1_S9_         , 0,
       CP1_                },        /* LDC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007800, &SDC1_S9_         , 0,
       CP1_                },        /* SDC1[S9] */
};


static const Pool ASET_ACLR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa4001100, &ASET             , 0,
       MCU_                },        /* ASET */
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa6001100, &ACLR             , 0,
       MCU_                },        /* ACLR */
};


static const Pool P_LL[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005100, &LL               , 0,
       0x0                 },        /* LL */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005101, &LLWP             , 0,
       XNP_                },        /* LLWP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005102, 0                      , 0,
       0x0                 },        /* P.LL~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005103, 0                      , 0,
       0x0                 },        /* P.LL~*(3) */
};


static const Pool P_SC[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005900, &SC               , 0,
       0x0                 },        /* SC */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005901, &SCWP             , 0,
       XNP_                },        /* SCWP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005902, 0                      , 0,
       0x0                 },        /* P.SC~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005903, 0                      , 0,
       0x0                 },        /* P.SC~*(3) */
};


static const Pool P_LLD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007100, &LLD              , 0,
       MIPS64_             },        /* LLD */
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007101, &LLDP             , 0,
       MIPS64_             },        /* LLDP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007102, 0                      , 0,
       0x0                 },        /* P.LLD~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007103, 0                      , 0,
       0x0                 },        /* P.LLD~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007104, 0                      , 0,
       0x0                 },        /* P.LLD~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007105, 0                      , 0,
       0x0                 },        /* P.LLD~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007106, 0                      , 0,
       0x0                 },        /* P.LLD~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007107, 0                      , 0,
       0x0                 },        /* P.LLD~*(7) */
};


static const Pool P_SCD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007900, &SCD              , 0,
       MIPS64_             },        /* SCD */
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007901, &SCDP             , 0,
       MIPS64_             },        /* SCDP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007902, 0                      , 0,
       0x0                 },        /* P.SCD~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007903, 0                      , 0,
       0x0                 },        /* P.SCD~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007904, 0                      , 0,
       0x0                 },        /* P.SCD~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007905, 0                      , 0,
       0x0                 },        /* P.SCD~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007906, 0                      , 0,
       0x0                 },        /* P.SCD~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f07, 0xa4007907, 0                      , 0,
       0x0                 },        /* P.SCD~*(7) */
};


static const Pool P_LS_S1[16] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4000100, 0                      , 0,
       0x0                 },        /* P.LS.S1~*(0) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4000900, 0                      , 0,
       0x0                 },        /* P.LS.S1~*(1) */
    { pool                , ASET_ACLR           , 2   , 32,
       0xfc007f00, 0xa4001100, 0                      , 0,
       0x0                 },        /* ASET_ACLR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4001900, 0                      , 0,
       0x0                 },        /* P.LS.S1~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002100, &UALH             , 0,
       XMMS_               },        /* UALH */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002900, &UASH             , 0,
       XMMS_               },        /* UASH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4003100, 0                      , 0,
       0x0                 },        /* P.LS.S1~*(6) */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003900, &CACHE            , 0,
       CP0_                },        /* CACHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004100, &LWC2             , 0,
       CP2_                },        /* LWC2 */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004900, &SWC2             , 0,
       CP2_                },        /* SWC2 */
    { pool                , P_LL                , 4   , 32,
       0xfc007f00, 0xa4005100, 0                      , 0,
       0x0                 },        /* P.LL */
    { pool                , P_SC                , 4   , 32,
       0xfc007f00, 0xa4005900, 0                      , 0,
       0x0                 },        /* P.SC */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006100, &LDC2             , 0,
       CP2_                },        /* LDC2 */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006900, &SDC2             , 0,
       CP2_                },        /* SDC2 */
    { pool                , P_LLD               , 8   , 32,
       0xfc007f00, 0xa4007100, 0                      , 0,
       0x0                 },        /* P.LLD */
    { pool                , P_SCD               , 8   , 32,
       0xfc007f00, 0xa4007900, 0                      , 0,
       0x0                 },        /* P.SCD */
};


static const Pool P_PREFE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01a00, &SYNCIE           , 0,
       CP0_ | EVA_         },        /* SYNCIE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001a00, &PREFE            , &PREFE_cond       ,
       CP0_ | EVA_         },        /* PREFE */
};


static const Pool P_LLE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005200, &LLE              , 0,
       CP0_ | EVA_         },        /* LLE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005201, &LLWPE            , 0,
       CP0_ | EVA_         },        /* LLWPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005202, 0                      , 0,
       0x0                 },        /* P.LLE~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005203, 0                      , 0,
       0x0                 },        /* P.LLE~*(3) */
};


static const Pool P_SCE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a00, &SCE              , 0,
       CP0_ | EVA_         },        /* SCE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a01, &SCWPE            , 0,
       CP0_ | EVA_         },        /* SCWPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a02, 0                      , 0,
       0x0                 },        /* P.SCE~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a03, 0                      , 0,
       0x0                 },        /* P.SCE~*(3) */
};


static const Pool P_LS_E0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000200, &LBE              , 0,
       CP0_ | EVA_         },        /* LBE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000a00, &SBE              , 0,
       CP0_ | EVA_         },        /* SBE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001200, &LBUE             , 0,
       CP0_ | EVA_         },        /* LBUE */
    { pool                , P_PREFE             , 2   , 32,
       0xfc007f00, 0xa4001a00, 0                      , 0,
       0x0                 },        /* P.PREFE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002200, &LHE              , 0,
       CP0_ | EVA_         },        /* LHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002a00, &SHE              , 0,
       CP0_ | EVA_         },        /* SHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003200, &LHUE             , 0,
       CP0_ | EVA_         },        /* LHUE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003a00, &CACHEE           , 0,
       CP0_ | EVA_         },        /* CACHEE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004200, &LWE              , 0,
       CP0_ | EVA_         },        /* LWE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004a00, &SWE              , 0,
       CP0_ | EVA_         },        /* SWE */
    { pool                , P_LLE               , 4   , 32,
       0xfc007f00, 0xa4005200, 0                      , 0,
       0x0                 },        /* P.LLE */
    { pool                , P_SCE               , 4   , 32,
       0xfc007f00, 0xa4005a00, 0                      , 0,
       0x0                 },        /* P.SCE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4006200, 0                      , 0,
       0x0                 },        /* P.LS.E0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4006a00, 0                      , 0,
       0x0                 },        /* P.LS.E0~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4007200, 0                      , 0,
       0x0                 },        /* P.LS.E0~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4007a00, 0                      , 0,
       0x0                 },        /* P.LS.E0~*(15) */
};


static const Pool P_LS_WM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000400, &LWM              , 0,
       XMMS_               },        /* LWM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000c00, &SWM              , 0,
       XMMS_               },        /* SWM */
};


static const Pool P_LS_UAWM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000500, &UALWM            , 0,
       XMMS_               },        /* UALWM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000d00, &UASWM            , 0,
       XMMS_               },        /* UASWM */
};


static const Pool P_LS_DM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000600, &LDM              , 0,
       MIPS64_             },        /* LDM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000e00, &SDM              , 0,
       MIPS64_             },        /* SDM */
};


static const Pool P_LS_UADM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000700, &UALDM            , 0,
       MIPS64_             },        /* UALDM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000f00, &UASDM            , 0,
       MIPS64_             },        /* UASDM */
};


static const Pool P_LS_S9[8] = {
    { pool                , P_LS_S0             , 16  , 32,
       0xfc000700, 0xa4000000, 0                      , 0,
       0x0                 },        /* P.LS.S0 */
    { pool                , P_LS_S1             , 16  , 32,
       0xfc000700, 0xa4000100, 0                      , 0,
       0x0                 },        /* P.LS.S1 */
    { pool                , P_LS_E0             , 16  , 32,
       0xfc000700, 0xa4000200, 0                      , 0,
       0x0                 },        /* P.LS.E0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000700, 0xa4000300, 0                      , 0,
       0x0                 },        /* P.LS.S9~*(3) */
    { pool                , P_LS_WM             , 2   , 32,
       0xfc000700, 0xa4000400, 0                      , 0,
       0x0                 },        /* P.LS.WM */
    { pool                , P_LS_UAWM           , 2   , 32,
       0xfc000700, 0xa4000500, 0                      , 0,
       0x0                 },        /* P.LS.UAWM */
    { pool                , P_LS_DM             , 2   , 32,
       0xfc000700, 0xa4000600, 0                      , 0,
       0x0                 },        /* P.LS.DM */
    { pool                , P_LS_UADM           , 2   , 32,
       0xfc000700, 0xa4000700, 0                      , 0,
       0x0                 },        /* P.LS.UADM */
};


static const Pool P_BAL[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfe000000, 0x28000000, &BC_32_           , 0,
       0x0                 },        /* BC[32] */
    { call_instruction    , 0                   , 0   , 32,
       0xfe000000, 0x2a000000, &BALC_32_         , 0,
       0x0                 },        /* BALC[32] */
};


static const Pool P_BALRSC[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xffe0f000, 0x48008000, &BRSC             , 0,
       0x0                 },        /* BRSC */
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48008000, &BALRSC           , &BALRSC_cond      ,
       0x0                 },        /* BALRSC */
};


static const Pool P_J[16] = {
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48000000, &JALRC_32_        , 0,
       0x0                 },        /* JALRC[32] */
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48001000, &JALRC_HB         , 0,
       0x0                 },        /* JALRC.HB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48002000, 0                      , 0,
       0x0                 },        /* P.J~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48003000, 0                      , 0,
       0x0                 },        /* P.J~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48004000, 0                      , 0,
       0x0                 },        /* P.J~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48005000, 0                      , 0,
       0x0                 },        /* P.J~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48006000, 0                      , 0,
       0x0                 },        /* P.J~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48007000, 0                      , 0,
       0x0                 },        /* P.J~*(7) */
    { pool                , P_BALRSC            , 2   , 32,
       0xfc00f000, 0x48008000, 0                      , 0,
       0x0                 },        /* P.BALRSC */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x48009000, 0                      , 0,
       0x0                 },        /* P.J~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800a000, 0                      , 0,
       0x0                 },        /* P.J~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800b000, 0                      , 0,
       0x0                 },        /* P.J~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800c000, 0                      , 0,
       0x0                 },        /* P.J~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800d000, 0                      , 0,
       0x0                 },        /* P.J~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800e000, 0                      , 0,
       0x0                 },        /* P.J~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x4800f000, 0                      , 0,
       0x0                 },        /* P.J~*(15) */
};


static const Pool P_BR3A[32] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88004000, &BC1EQZC          , 0,
       CP1_                },        /* BC1EQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88014000, &BC1NEZC          , 0,
       CP1_                },        /* BC1NEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88024000, &BC2EQZC          , 0,
       CP2_                },        /* BC2EQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88034000, &BC2NEZC          , 0,
       CP2_                },        /* BC2NEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88044000, &BPOSGE32C        , 0,
       DSP_                },        /* BPOSGE32C */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88054000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(5) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88064000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(6) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88074000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88084000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(8) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88094000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(9) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880a4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(10) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880b4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880c4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880d4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(13) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880e4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x880f4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88104000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88114000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(17) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88124000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(18) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88134000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88144000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88154000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(21) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88164000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88174000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88184000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x88194000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881a4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(26) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881b4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881c4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881d4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(29) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881e4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1fc000, 0x881f4000, 0                      , 0,
       0x0                 },        /* P.BR3A~*(31) */
};


static const Pool P_BR1[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88000000, &BEQC_32_         , 0,
       0x0                 },        /* BEQC[32] */
    { pool                , P_BR3A              , 32  , 32,
       0xfc00c000, 0x88004000, 0                      , 0,
       0x0                 },        /* P.BR3A */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88008000, &BGEC             , 0,
       0x0                 },        /* BGEC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x8800c000, &BGEUC            , 0,
       0x0                 },        /* BGEUC */
};


static const Pool P_BR2[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8000000, &BNEC_32_         , 0,
       0x0                 },        /* BNEC[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00c000, 0xa8004000, 0                      , 0,
       0x0                 },        /* P.BR2~*(1) */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8008000, &BLTC             , 0,
       0x0                 },        /* BLTC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa800c000, &BLTUC            , 0,
       0x0                 },        /* BLTUC */
};


static const Pool P_BRI[8] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8000000, &BEQIC            , 0,
       0x0                 },        /* BEQIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8040000, &BBEQZC           , 0,
       XMMS_               },        /* BBEQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8080000, &BGEIC            , 0,
       0x0                 },        /* BGEIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc80c0000, &BGEIUC           , 0,
       0x0                 },        /* BGEIUC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8100000, &BNEIC            , 0,
       0x0                 },        /* BNEIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8140000, &BBNEZC           , 0,
       XMMS_               },        /* BBNEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8180000, &BLTIC            , 0,
       0x0                 },        /* BLTIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc81c0000, &BLTIUC           , 0,
       0x0                 },        /* BLTIUC */
};


static const Pool P32[32] = {
    { pool                , P_ADDIU             , 2   , 32,
       0xfc000000, 0x00000000, 0                      , 0,
       0x0                 },        /* P.ADDIU */
    { pool                , P32A                , 8   , 32,
       0xfc000000, 0x20000000, 0                      , 0,
       0x0                 },        /* P32A */
    { pool                , P_GP_W              , 4   , 32,
       0xfc000000, 0x40000000, 0                      , 0,
       0x0                 },        /* P.GP.W */
    { pool                , POOL48I             , 32  , 48,
       0xfc0000000000ull, 0x600000000000ull, 0                      , 0,
       0x0                 },        /* POOL48I */
    { pool                , P_U12               , 16  , 32,
       0xfc000000, 0x80000000, 0                      , 0,
       0x0                 },        /* P.U12 */
    { pool                , POOL32F             , 8   , 32,
       0xfc000000, 0xa0000000, 0                      , 0,
       CP1_                },        /* POOL32F */
    { pool                , POOL32S             , 8   , 32,
       0xfc000000, 0xc0000000, 0                      , 0,
       0x0                 },        /* POOL32S */
    { pool                , P_LUI               , 2   , 32,
       0xfc000000, 0xe0000000, 0                      , 0,
       0x0                 },        /* P.LUI */
    { instruction         , 0                   , 0   , 32,
       0xfc000000, 0x04000000, &ADDIUPC_32_      , 0,
       0x0                 },        /* ADDIUPC[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x24000000, 0                      , 0,
       0x0                 },        /* P32~*(5) */
    { pool                , P_GP_BH             , 8   , 32,
       0xfc000000, 0x44000000, 0                      , 0,
       0x0                 },        /* P.GP.BH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x64000000, 0                      , 0,
       0x0                 },        /* P32~*(13) */
    { pool                , P_LS_U12            , 16  , 32,
       0xfc000000, 0x84000000, 0                      , 0,
       0x0                 },        /* P.LS.U12 */
    { pool                , P_LS_S9             , 8   , 32,
       0xfc000000, 0xa4000000, 0                      , 0,
       0x0                 },        /* P.LS.S9 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xc4000000, 0                      , 0,
       0x0                 },        /* P32~*(25) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xe4000000, 0                      , 0,
       0x0                 },        /* P32~*(29) */
    { call_instruction    , 0                   , 0   , 32,
       0xfc000000, 0x08000000, &MOVE_BALC        , 0,
       XMMS_               },        /* MOVE.BALC */
    { pool                , P_BAL               , 2   , 32,
       0xfc000000, 0x28000000, 0                      , 0,
       0x0                 },        /* P.BAL */
    { pool                , P_J                 , 16  , 32,
       0xfc000000, 0x48000000, 0                      , 0,
       0x0                 },        /* P.J */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x68000000, 0                      , 0,
       0x0                 },        /* P32~*(14) */
    { pool                , P_BR1               , 4   , 32,
       0xfc000000, 0x88000000, 0                      , 0,
       0x0                 },        /* P.BR1 */
    { pool                , P_BR2               , 4   , 32,
       0xfc000000, 0xa8000000, 0                      , 0,
       0x0                 },        /* P.BR2 */
    { pool                , P_BRI               , 8   , 32,
       0xfc000000, 0xc8000000, 0                      , 0,
       0x0                 },        /* P.BRI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xe8000000, 0                      , 0,
       0x0                 },        /* P32~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x0c000000, 0                      , 0,
       0x0                 },        /* P32~*(3) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x2c000000, 0                      , 0,
       0x0                 },        /* P32~*(7) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x4c000000, 0                      , 0,
       0x0                 },        /* P32~*(11) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x6c000000, 0                      , 0,
       0x0                 },        /* P32~*(15) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0x8c000000, 0                      , 0,
       0x0                 },        /* P32~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xac000000, 0                      , 0,
       0x0                 },        /* P32~*(23) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xcc000000, 0                      , 0,
       0x0                 },        /* P32~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000000, 0xec000000, 0                      , 0,
       0x0                 },        /* P32~*(31) */
};


static const Pool P16_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x1008    , &SYSCALL_16_      , 0,
       0x0                 },        /* SYSCALL[16] */
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x100c    , &HYPCALL_16_      , 0,
       CP0_ | VZ_          },        /* HYPCALL[16] */
};


static const Pool P16_RI[4] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfff8    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16.RI~*(0) */
    { pool                , P16_SYSCALL         , 2   , 16,
       0xfff8    , 0x1008    , 0                      , 0,
       0x0                 },        /* P16.SYSCALL */
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1010    , &BREAK_16_        , 0,
       0x0                 },        /* BREAK[16] */
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1018    , &SDBBP_16_        , 0,
       EJTAG_              },        /* SDBBP[16] */
};


static const Pool P16_MV[2] = {
    { pool                , P16_RI              , 4   , 16,
       0xffe0    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16.RI */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1000    , &MOVE             , &MOVE_cond        ,
       0x0                 },        /* MOVE */
};


static const Pool P16_SHIFT[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3000    , &SLL_16_          , 0,
       0x0                 },        /* SLL[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3008    , &SRL_16_          , 0,
       0x0                 },        /* SRL[16] */
};


static const Pool POOL16C_00[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5000    , &NOT_16_          , 0,
       0x0                 },        /* NOT[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5004    , &XOR_16_          , 0,
       0x0                 },        /* XOR[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5008    , &AND_16_          , 0,
       0x0                 },        /* AND[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x500c    , &OR_16_           , 0,
       0x0                 },        /* OR[16] */
};


static const Pool POOL16C_0[2] = {
    { pool                , POOL16C_00          , 4   , 16,
       0xfc03    , 0x5000    , 0                      , 0,
       0x0                 },        /* POOL16C_00 */
    { reserved_block      , 0                   , 0   , 16,
       0xfc03    , 0x5002    , 0                      , 0,
       0x0                 },        /* POOL16C_0~*(1) */
};


static const Pool P16C[2] = {
    { pool                , POOL16C_0           , 2   , 16,
       0xfc01    , 0x5000    , 0                      , 0,
       0x0                 },        /* POOL16C_0 */
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0x5001    , &LWXS_16_         , 0,
       0x0                 },        /* LWXS[16] */
};


static const Pool P16_A1[2] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfc40    , 0x7000    , 0                      , 0,
       0x0                 },        /* P16.A1~*(0) */
    { instruction         , 0                   , 0   , 16,
       0xfc40    , 0x7040    , &ADDIU_R1_SP_     , 0,
       0x0                 },        /* ADDIU[R1.SP] */
};


static const Pool P_ADDIU_RS5_[2] = {
    { instruction         , 0                   , 0   , 16,
       0xffe8    , 0x9008    , &NOP_16_          , 0,
       0x0                 },        /* NOP[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9008    , &ADDIU_RS5_       , &ADDIU_RS5__cond  ,
       0x0                 },        /* ADDIU[RS5] */
};


static const Pool P16_A2[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9000    , &ADDIU_R2_        , 0,
       0x0                 },        /* ADDIU[R2] */
    { pool                , P_ADDIU_RS5_        , 2   , 16,
       0xfc08    , 0x9008    , 0                      , 0,
       0x0                 },        /* P.ADDIU[RS5] */
};


static const Pool P16_ADDU[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb000    , &ADDU_16_         , 0,
       0x0                 },        /* ADDU[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb001    , &SUBU_16_         , 0,
       0x0                 },        /* SUBU[16] */
};


static const Pool P16_JRC[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc1f    , 0xd800    , &JRC              , 0,
       0x0                 },        /* JRC */
    { call_instruction    , 0                   , 0   , 16,
       0xfc1f    , 0xd810    , &JALRC_16_        , 0,
       0x0                 },        /* JALRC[16] */
};


static const Pool P16_BR1[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &BEQC_16_         , &BEQC_16__cond    ,
       XMMS_               },        /* BEQC[16] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &BNEC_16_         , &BNEC_16__cond    ,
       XMMS_               },        /* BNEC[16] */
};


static const Pool P16_BR[2] = {
    { pool                , P16_JRC             , 2   , 16,
       0xfc0f    , 0xd800    , 0                      , 0,
       0x0                 },        /* P16.JRC */
    { pool                , P16_BR1             , 2   , 16,
       0xfc00    , 0xd800    , 0                      , &P16_BR1_cond     ,
       0x0                 },        /* P16.BR1 */
};


static const Pool P16_SR[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfd00    , 0x1c00    , &SAVE_16_         , 0,
       0x0                 },        /* SAVE[16] */
    { return_instruction  , 0                   , 0   , 16,
       0xfd00    , 0x1d00    , &RESTORE_JRC_16_  , 0,
       0x0                 },        /* RESTORE.JRC[16] */
};


static const Pool P16_4X4[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c00    , &ADDU_4X4_        , 0,
       XMMS_               },        /* ADDU[4X4] */
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c08    , &MUL_4X4_         , 0,
       XMMS_               },        /* MUL[4X4] */
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d00    , 0                      , 0,
       0x0                 },        /* P16.4X4~*(2) */
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d08    , 0                      , 0,
       0x0                 },        /* P16.4X4~*(3) */
};


static const Pool P16_LB[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c00    , &LB_16_           , 0,
       0x0                 },        /* LB[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c04    , &SB_16_           , 0,
       0x0                 },        /* SB[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c08    , &LBU_16_          , 0,
       0x0                 },        /* LBU[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc0c    , 0x5c0c    , 0                      , 0,
       0x0                 },        /* P16.LB~*(3) */
};


static const Pool P16_LH[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c00    , &LH_16_           , 0,
       0x0                 },        /* LH[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c01    , &SH_16_           , 0,
       0x0                 },        /* SH[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c08    , &LHU_16_          , 0,
       0x0                 },        /* LHU[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc09    , 0x7c09    , 0                      , 0,
       0x0                 },        /* P16.LH~*(3) */
};


static const Pool P16[32] = {
    { pool                , P16_MV              , 2   , 16,
       0xfc00    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16.MV */
    { pool                , P16_SHIFT           , 2   , 16,
       0xfc00    , 0x3000    , 0                      , 0,
       0x0                 },        /* P16.SHIFT */
    { pool                , P16C                , 2   , 16,
       0xfc00    , 0x5000    , 0                      , 0,
       0x0                 },        /* P16C */
    { pool                , P16_A1              , 2   , 16,
       0xfc00    , 0x7000    , 0                      , 0,
       0x0                 },        /* P16.A1 */
    { pool                , P16_A2              , 2   , 16,
       0xfc00    , 0x9000    , 0                      , 0,
       0x0                 },        /* P16.A2 */
    { pool                , P16_ADDU            , 2   , 16,
       0xfc00    , 0xb000    , 0                      , 0,
       0x0                 },        /* P16.ADDU */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xd000    , &LI_16_           , 0,
       0x0                 },        /* LI[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf000    , &ANDI_16_         , 0,
       0x0                 },        /* ANDI[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1400    , &LW_16_           , 0,
       0x0                 },        /* LW[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x3400    , &LW_SP_           , 0,
       0x0                 },        /* LW[SP] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x5400    , &LW_GP16_         , 0,
       0x0                 },        /* LW[GP16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x7400    , &LW_4X4_          , 0,
       XMMS_               },        /* LW[4X4] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x9400    , &SW_16_           , 0,
       0x0                 },        /* SW[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xb400    , &SW_SP_           , 0,
       0x0                 },        /* SW[SP] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xd400    , &SW_GP16_         , 0,
       0x0                 },        /* SW[GP16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf400    , &SW_4X4_          , 0,
       XMMS_               },        /* SW[4X4] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x1800    , &BC_16_           , 0,
       0x0                 },        /* BC[16] */
    { call_instruction    , 0                   , 0   , 16,
       0xfc00    , 0x3800    , &BALC_16_         , 0,
       0x0                 },        /* BALC[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x5800    , 0                      , 0,
       0x0                 },        /* P16~*(10) */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x7800    , 0                      , 0,
       0x0                 },        /* P16~*(14) */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x9800    , &BEQZC_16_        , 0,
       0x0                 },        /* BEQZC[16] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xb800    , &BNEZC_16_        , 0,
       0x0                 },        /* BNEZC[16] */
    { pool                , P16_BR              , 2   , 16,
       0xfc00    , 0xd800    , 0                      , 0,
       0x0                 },        /* P16.BR */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0xf800    , 0                      , 0,
       0x0                 },        /* P16~*(30) */
    { pool                , P16_SR              , 2   , 16,
       0xfc00    , 0x1c00    , 0                      , 0,
       0x0                 },        /* P16.SR */
    { pool                , P16_4X4             , 4   , 16,
       0xfc00    , 0x3c00    , 0                      , 0,
       0x0                 },        /* P16.4X4 */
    { pool                , P16_LB              , 4   , 16,
       0xfc00    , 0x5c00    , 0                      , 0,
       0x0                 },        /* P16.LB */
    { pool                , P16_LH              , 4   , 16,
       0xfc00    , 0x7c00    , 0                      , 0,
       0x0                 },        /* P16.LH */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x9c00    , 0                      , 0,
       0x0                 },        /* P16~*(19) */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xbc00    , &MOVEP            , 0,
       XMMS_               },        /* MOVEP */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0xdc00    , 0                      , 0,
       0x0                 },        /* P16~*(27) */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xfc00    , &MOVEP_REV_       , 0,
       XMMS_               },        /* MOVEP[REV] */
};


static const Pool MAJOR[2] = {
    { pool                , P32                 , 32  , 32,
       0x10000000, 0x00000000, 0                      , 0,
       0x0                 },        /* P32 */
    { pool                , P16                 , 32  , 16,
       0x1000    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16 */
};

static bool nanomips_dis(const uint16_t *data, char **buf, Dis_info *info)
{
    TABLE_ENTRY_TYPE type;

    /* Handle runtime errors. */
    if (unlikely(sigsetjmp(info->buf, 0) != 0)) {
        return false;
    }
    return Disassemble(data, buf, &type, MAJOR, ARRAY_SIZE(MAJOR), info) >= 0;
}

static bool read_u16(uint16_t *ret, bfd_vma memaddr,
                     struct disassemble_info *info)
{
    int status = (*info->read_memory_func)(memaddr, (bfd_byte *)ret, 2, info);
    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return false;
    }

    if ((info->endian == BFD_ENDIAN_BIG) != HOST_BIG_ENDIAN) {
        bswap16s(ret);
    }
    return true;
}

int print_insn_nanomips(bfd_vma memaddr, struct disassemble_info *info)
{
    int length;
    uint16_t words[3] = { };
    g_autofree char *buf = NULL;

    info->bytes_per_chunk = 2;
    info->display_endian = info->endian;
    info->insn_info_valid = 1;
    info->branch_delay_insns = 0;
    info->data_size = 0;
    info->insn_type = dis_nonbranch;
    info->target = 0;
    info->target2 = 0;

    Dis_info disassm_info;
    disassm_info.m_pc = memaddr;
    disassm_info.fprintf_func = info->fprintf_func;
    disassm_info.stream = info->stream;

    if (!read_u16(&words[0], memaddr, info)) {
        return -1;
    }
    length = 2;

    /* Handle 32-bit opcodes.  */
    if ((words[0] & 0x1000) == 0) {
        if (!read_u16(&words[1], memaddr + 2, info)) {
            return -1;
        }
        length = 4;

        /* Handle 48-bit opcodes.  */
        if ((words[0] >> 10) == 0x18) {
            if (!read_u16(&words[1], memaddr + 4, info)) {
                return -1;
            }
            length = 6;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(words); i++) {
        if (i * 2 < length) {
            (*info->fprintf_func)(info->stream, "%04x ", words[i]);
        } else {
            (*info->fprintf_func)(info->stream, "     ");
        }
    }

    if (nanomips_dis(words, &buf, &disassm_info)) {
        (*info->fprintf_func) (info->stream, "%s", buf);
    }

    return length;
}
