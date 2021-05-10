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

#include <cstring>
#include <stdexcept>
#include <sstream>
#include <stdio.h>
#include <stdarg.h>

#include "nanomips.h"

#define IMGASSERTONCE(test)


int nanomips_dis(char *buf,
                 unsigned address,
                 unsigned short one,
                 unsigned short two,
                 unsigned short three)
{
    std::string disasm;
    uint16 bits[3] = {one, two, three};

    NMD::TABLE_ENTRY_TYPE type;
    NMD d(address, NMD::ALL_ATTRIBUTES);
    int size = d.Disassemble(bits, disasm, type);

    strcpy(buf, disasm.c_str());
    return size;
}

int print_insn_nanomips(bfd_vma memaddr, struct disassemble_info *info)
{
    int status;
    bfd_byte buffer[2];
    uint16_t insn1 = 0, insn2 = 0, insn3 = 0;
    char buf[200];

    info->bytes_per_chunk = 2;
    info->display_endian = info->endian;
    info->insn_info_valid = 1;
    info->branch_delay_insns = 0;
    info->data_size = 0;
    info->insn_type = dis_nonbranch;
    info->target = 0;
    info->target2 = 0;

    status = (*info->read_memory_func)(memaddr, buffer, 2, info);
    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return -1;
    }

    if (info->endian == BFD_ENDIAN_BIG) {
        insn1 = bfd_getb16(buffer);
    } else {
        insn1 = bfd_getl16(buffer);
    }
    (*info->fprintf_func)(info->stream, "%04x ", insn1);

    /* Handle 32-bit opcodes.  */
    if ((insn1 & 0x1000) == 0) {
        status = (*info->read_memory_func)(memaddr + 2, buffer, 2, info);
        if (status != 0) {
            (*info->memory_error_func)(status, memaddr + 2, info);
            return -1;
        }

        if (info->endian == BFD_ENDIAN_BIG) {
            insn2 = bfd_getb16(buffer);
        } else {
            insn2 = bfd_getl16(buffer);
        }
        (*info->fprintf_func)(info->stream, "%04x ", insn2);
    } else {
        (*info->fprintf_func)(info->stream, "     ");
    }
    /* Handle 48-bit opcodes.  */
    if ((insn1 >> 10) == 0x18) {
        status = (*info->read_memory_func)(memaddr + 4, buffer, 2, info);
        if (status != 0) {
            (*info->memory_error_func)(status, memaddr + 4, info);
            return -1;
        }

        if (info->endian == BFD_ENDIAN_BIG) {
            insn3 = bfd_getb16(buffer);
        } else {
            insn3 = bfd_getl16(buffer);
        }
        (*info->fprintf_func)(info->stream, "%04x ", insn3);
    } else {
        (*info->fprintf_func)(info->stream, "     ");
    }

    int length = nanomips_dis(buf, memaddr, insn1, insn2, insn3);

    /* FIXME: Should probably use a hash table on the major opcode here.  */

    (*info->fprintf_func) (info->stream, "%s", buf);
    if (length > 0) {
        return length / 8;
    }

    info->insn_type = dis_noninsn;

    return insn3 ? 6 : insn2 ? 4 : 2;
}


namespace img
{
    address addr32(address a)
    {
        return a;
    }

    std::string format(const char *format, ...)
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        int err = vsprintf(buffer, format, args);
        if (err < 0) {
            perror(buffer);
        }
        va_end(args);
        return buffer;
    }

    std::string format(const char *format,
                       std::string s)
    {
        char buffer[256];

        sprintf(buffer, format, s.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       std::string s2)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), s2.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       std::string s2,
                       std::string s3)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), s2.c_str(), s3.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       std::string s2,
                       std::string s3,
                       std::string s4)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), s2.c_str(), s3.c_str(),
                                s4.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       std::string s2,
                       std::string s3,
                       std::string s4,
                       std::string s5)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), s2.c_str(), s3.c_str(),
                                s4.c_str(), s5.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       uint64 d,
                       std::string s2)
    {
        char buffer[256];

        sprintf(buffer, format, d, s2.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       uint64 d,
                       std::string s2)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), d, s2.c_str());

        return buffer;
    }

    std::string format(const char *format,
                       std::string s1,
                       std::string s2,
                       uint64 d)
    {
        char buffer[256];

        sprintf(buffer, format, s1.c_str(), s2.c_str(), d);

        return buffer;
    }

    char as_char(int c)
    {
        return static_cast<char>(c);
    }
};


std::string to_string(img::address a)
{
    char buffer[256];
    sprintf(buffer, "0x%" PRIx64, a);
    return buffer;
}


uint64 extract_bits(uint64 data, uint32 bit_offset, uint32 bit_size)
{
    return (data << (64 - (bit_size + bit_offset))) >> (64 - bit_size);
}


int64 sign_extend(int64 data, int msb)
{
    uint64 shift = 63 - msb;
    return (data << shift) >> shift;
}


uint64 NMD::renumber_registers(uint64 index, uint64 *register_list,
                               size_t register_list_size)
{
    if (index < register_list_size) {
        return register_list[index];
    }

    throw std::runtime_error(img::format(
                   "Invalid register mapping index %" PRIu64
                   ", size of list = %zu",
                   index, register_list_size));
}


/*
 * NMD::decode_gpr_gpr4() - decoder for 'gpr4' gpr encoding type
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
uint64 NMD::decode_gpr_gpr4(uint64 d)
{
    static uint64 register_list[] = {  8,  9, 10, 11,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr4_zero() - decoder for 'gpr4.zero' gpr encoding type
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
uint64 NMD::decode_gpr_gpr4_zero(uint64 d)
{
    static uint64 register_list[] = {  8,  9, 10,  0,  4,  5,  6,  7,
                                      16, 17, 18, 19, 20, 21, 22, 23 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr3() - decoder for 'gpr3' gpr encoding type
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
uint64 NMD::decode_gpr_gpr3(uint64 d)
{
    static uint64 register_list[] = { 16, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr3_src_store() - decoder for 'gpr3.src.store' gpr encoding
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
uint64 NMD::decode_gpr_gpr3_src_store(uint64 d)
{
    static uint64 register_list[] = {  0, 17, 18, 19,  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr2_reg1() - decoder for 'gpr2.reg1' gpr encoding type
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
uint64 NMD::decode_gpr_gpr2_reg1(uint64 d)
{
    static uint64 register_list[] = {  4,  5,  6,  7 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr2_reg2() - decoder for 'gpr2.reg2' gpr encoding type
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
uint64 NMD::decode_gpr_gpr2_reg2(uint64 d)
{
    static uint64 register_list[] = {  5,  6,  7,  8 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


/*
 * NMD::decode_gpr_gpr1() - decoder for 'gpr1' gpr encoding type
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
uint64 NMD::decode_gpr_gpr1(uint64 d)
{
    static uint64 register_list[] = {  4,  5 };
    return renumber_registers(d, register_list,
               sizeof(register_list) / sizeof(register_list[0]));
}


uint64 NMD::copy(uint64 d)
{
    return d;
}


int64 NMD::copy(int64 d)
{
    return d;
}


int64 NMD::neg_copy(uint64 d)
{
    return 0ll - d;
}


int64 NMD::neg_copy(int64 d)
{
    return -d;
}


/* strange wrapper around  gpr3 */
uint64 NMD::encode_rs3_and_check_rs3_ge_rt3(uint64 d)
{
return decode_gpr_gpr3(d);
}


/* strange wrapper around  gpr3 */
uint64 NMD::encode_rs3_and_check_rs3_lt_rt3(uint64 d)
{
    return decode_gpr_gpr3(d);
}


/* nop - done by extraction function */
uint64 NMD::encode_s_from_address(uint64 d)
{
    return d;
}


/* nop - done by extraction function */
uint64 NMD::encode_u_from_address(uint64 d)
{
    return d;
}


/* nop - done by extraction function */
uint64 NMD::encode_s_from_s_hi(uint64 d)
{
    return d;
}


uint64 NMD::encode_count3_from_count(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


uint64 NMD::encode_shift3_from_shift(uint64 d)
{
    IMGASSERTONCE(d < 8);
    return d == 0ull ? 8ull : d;
}


/* special value for load literal */
int64 NMD::encode_eu_from_s_li16(uint64 d)
{
    IMGASSERTONCE(d < 128);
    return d == 127 ? -1 : (int64)d;
}


uint64 NMD::encode_msbd_from_size(uint64 d)
{
    IMGASSERTONCE(d < 32);
    return d + 1;
}


uint64 NMD::encode_eu_from_u_andi16(uint64 d)
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


uint64 NMD::encode_msbd_from_pos_and_size(uint64 d)
{
    IMGASSERTONCE(0);
    return d;
}


/* save16 / restore16   ???? */
uint64 NMD::encode_rt1_from_rt(uint64 d)
{
    return d ? 31 : 30;
}


/* ? */
uint64 NMD::encode_lsb_from_pos_and_size(uint64 d)
{
    return d;
}


std::string NMD::save_restore_list(uint64 rt, uint64 count, uint64 gp)
{
    std::string str;

    for (uint64 counter = 0; counter != count; counter++) {
        bool use_gp = gp && (counter == count - 1);
        uint64 this_rt = use_gp ? 28 : ((rt & 0x10) | (rt + counter)) & 0x1f;
        str += img::format(",%s", GPR(this_rt));
    }

    return str;
}


std::string NMD::GPR(uint64 reg)
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

    throw std::runtime_error(img::format("Invalid GPR register index %" PRIu64,
                                         reg));
}


std::string NMD::FPR(uint64 reg)
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

    throw std::runtime_error(img::format("Invalid FPR register index %" PRIu64,
                                         reg));
}


std::string NMD::AC(uint64 reg)
{
    static const char *ac_reg[4] = {
        "ac0",  "ac1",  "ac2",  "ac3"
    };

    if (reg < 4) {
        return ac_reg[reg];
    }

    throw std::runtime_error(img::format("Invalid AC register index %" PRIu64,
                                         reg));
}


std::string NMD::IMMEDIATE(uint64 value)
{
    return img::format("0x%" PRIx64, value);
}


std::string NMD::IMMEDIATE(int64 value)
{
    return img::format("%" PRId64, value);
}


std::string NMD::CPR(uint64 reg)
{
    /* needs more work */
    return img::format("CP%" PRIu64, reg);
}


std::string NMD::ADDRESS(uint64 value, int instruction_size)
{
    /* token for string replace */
    /* const char TOKEN_REPLACE = (char)0xa2; */
    img::address address = m_pc + value + instruction_size;
    /* symbol replacement */
    /* return img::as_char(TOKEN_REPLACE) + to_string(address); */
    return to_string(address);
}


uint64 NMD::extract_op_code_value(const uint16 * data, int size)
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


int NMD::Disassemble(const uint16 * data, std::string & dis,
                     NMD::TABLE_ENTRY_TYPE & type)
{
    return Disassemble(data, dis, type, MAJOR, 2);
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
int NMD::Disassemble(const uint16 * data, std::string & dis,
                     NMD::TABLE_ENTRY_TYPE & type, const Pool *table,
                     int table_size)
{
    try
    {
        for (int i = 0; i < table_size; i++) {
            uint64 op_code = extract_op_code_value(data,
                                 table[i].instructions_size);
            if ((op_code & table[i].mask) == table[i].value) {
                /* possible match */
                conditional_function cond = table[i].condition;
                if ((cond == 0) || (this->*cond)(op_code)) {
                    try
                    {
                        if (table[i].type == pool) {
                            return Disassemble(data, dis, type,
                                               table[i].next_table,
                                               table[i].next_table_size);
                        } else if ((table[i].type == instruction) ||
                                   (table[i].type == call_instruction) ||
                                   (table[i].type == branch_instruction) ||
                                   (table[i].type == return_instruction)) {
                            if ((table[i].attributes != 0) &&
                                (m_requested_instruction_categories &
                                 table[i].attributes) == 0) {
                                /*
                                 * failed due to instruction having
                                 * an ASE attribute and the requested version
                                 * not having that attribute
                                 */
                                dis = "ASE attribute mismatch";
                                return -5;
                            }
                            disassembly_function dis_fn = table[i].disassembly;
                            if (dis_fn == 0) {
                                dis = "disassembler failure - bad table entry";
                                return -6;
                            }
                            type = table[i].type;
                            dis = (this->*dis_fn)(op_code);
                            return table[i].instructions_size;
                        } else {
                            dis = "reserved instruction";
                            return -2;
                        }
                    }
                    catch (std::runtime_error & e)
                    {
                        dis = e.what();
                        return -3;          /* runtime error */
                    }
                }
            }
        }
    }
    catch (std::exception & e)
    {
        dis = e.what();
        return -4;          /* runtime error */
    }

    dis = "failed to disassemble";
    return -1;      /* failed to disassemble        */
}


uint64 NMD::extract_code_18_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 19);
    return value;
}


uint64 NMD::extract_shift3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


uint64 NMD::extract_u_11_10_9_8_7_6_5_4_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 9) << 3;
    return value;
}


uint64 NMD::extract_count_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


uint64 NMD::extract_rtz3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


uint64 NMD::extract_u_17_to_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 17) << 1;
    return value;
}


int64 NMD::extract_s__se9_20_19_18_17_16_15_14_13_12_11(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 11, 10);
    value = sign_extend(value, 9);
    return value;
}


int64 NMD::extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 11;
    value |= extract_bits(instruction, 1, 10) << 1;
    value = sign_extend(value, 11);
    return value;
}


uint64 NMD::extract_u_10(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 10, 1);
    return value;
}


uint64 NMD::extract_rtz4_27_26_25_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    value |= extract_bits(instruction, 25, 1) << 3;
    return value;
}


uint64 NMD::extract_sa_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


uint64 NMD::extract_shift_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


uint64 NMD::extract_shiftx_10_9_8_7__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 4) << 1;
    return value;
}


uint64 NMD::extract_hint_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_count3_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 3);
    return value;
}


int64 NMD::extract_s__se31_0_11_to_2_20_to_12_s12(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 31;
    value |= extract_bits(instruction, 2, 10) << 21;
    value |= extract_bits(instruction, 12, 9) << 12;
    value = sign_extend(value, 31);
    return value;
}


int64 NMD::extract_s__se7_0_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 7;
    value |= extract_bits(instruction, 1, 6) << 1;
    value = sign_extend(value, 7);
    return value;
}


uint64 NMD::extract_u2_10_9(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 2);
    return value;
}


uint64 NMD::extract_code_25_24_23_22_21_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 10);
    return value;
}


uint64 NMD::extract_rs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_u_2_1__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 2) << 1;
    return value;
}


uint64 NMD::extract_stripe_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 1);
    return value;
}


uint64 NMD::extract_ac_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 2);
    return value;
}


uint64 NMD::extract_shift_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_rdl_25_24(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 24, 1);
    return value;
}


int64 NMD::extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 10;
    value |= extract_bits(instruction, 1, 9) << 1;
    value = sign_extend(value, 10);
    return value;
}


uint64 NMD::extract_eu_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7);
    return value;
}


uint64 NMD::extract_shift_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6);
    return value;
}


uint64 NMD::extract_count_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 4);
    return value;
}


uint64 NMD::extract_code_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    return value;
}


uint64 NMD::extract_u_11_10_9_8_7_6_5_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 12);
    return value;
}


uint64 NMD::extract_rs_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


uint64 NMD::extract_u_20_to_3__s3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 18) << 3;
    return value;
}


uint64 NMD::extract_u_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 2;
    return value;
}


uint64 NMD::extract_cofun_25_24_23(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


uint64 NMD::extract_u_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3) << 2;
    return value;
}


uint64 NMD::extract_rd3_3_2_1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 1, 3);
    return value;
}


uint64 NMD::extract_sa_15_14_13_12(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 12, 4);
    return value;
}


uint64 NMD::extract_rt_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_ru_7_6_5_4_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 5);
    return value;
}


uint64 NMD::extract_u_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


uint64 NMD::extract_rsz4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


int64 NMD::extract_s__se21_0_20_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 21;
    value |= extract_bits(instruction, 1, 20) << 1;
    value = sign_extend(value, 21);
    return value;
}


uint64 NMD::extract_op_25_to_3(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 23);
    return value;
}


uint64 NMD::extract_rs4_4_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    return value;
}


uint64 NMD::extract_bit_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 3);
    return value;
}


uint64 NMD::extract_rt_41_40_39_38_37(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 37, 5);
    return value;
}


int64 NMD::extract_shift__se5_21_20_19_18_17_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 16, 6);
    value = sign_extend(value, 5);
    return value;
}


uint64 NMD::extract_rd2_3_8(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 1;
    value |= extract_bits(instruction, 8, 1);
    return value;
}


uint64 NMD::extract_code_17_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 18);
    return value;
}


uint64 NMD::extract_size_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


int64 NMD::extract_s__se8_15_7_6_5_4_3_2_s2(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 2, 6) << 2;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


uint64 NMD::extract_u_15_to_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 16);
    return value;
}


uint64 NMD::extract_fs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


int64 NMD::extract_s__se8_15_7_6_5_4_3_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 8);
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


uint64 NMD::extract_stype_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_rtl_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 9, 1);
    return value;
}


uint64 NMD::extract_hs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_sel_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 3);
    return value;
}


uint64 NMD::extract_lsb_4_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5);
    return value;
}


uint64 NMD::extract_gp_2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 1);
    return value;
}


uint64 NMD::extract_rt3_9_8_7(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 7, 3);
    return value;
}


uint64 NMD::extract_ft_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_u_17_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 7);
    return value;
}


uint64 NMD::extract_cs_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_rt4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


uint64 NMD::extract_msbt_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


uint64 NMD::extract_u_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 6) << 2;
    return value;
}


uint64 NMD::extract_sa_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 3);
    return value;
}


int64 NMD::extract_s__se14_0_13_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 14;
    value |= extract_bits(instruction, 1, 13) << 1;
    value = sign_extend(value, 14);
    return value;
}


uint64 NMD::extract_rs3_6_5_4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 3);
    return value;
}


uint64 NMD::extract_u_31_to_0__s32(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 32) << 32;
    return value;
}


uint64 NMD::extract_shift_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 5);
    return value;
}


uint64 NMD::extract_cs_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_shiftx_11_10_9_8_7_6(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 6, 6);
    return value;
}


uint64 NMD::extract_rt_9_8_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 5);
    return value;
}


uint64 NMD::extract_op_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_u_6_5_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 7) << 2;
    return value;
}


uint64 NMD::extract_bit_16_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 6);
    return value;
}


uint64 NMD::extract_mask_20_19_18_17_16_15_14(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 14, 7);
    return value;
}


uint64 NMD::extract_eu_3_2_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4);
    return value;
}


uint64 NMD::extract_u_7_6_5_4__s4(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 4, 4) << 4;
    return value;
}


int64 NMD::extract_s__se8_15_7_6_5_4_3_s3(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 3, 5) << 3;
    value |= extract_bits(instruction, 15, 1) << 8;
    value = sign_extend(value, 8);
    return value;
}


uint64 NMD::extract_ft_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


int64 NMD::extract_s__se31_15_to_0_31_to_16(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 16) << 16;
    value |= extract_bits(instruction, 16, 16);
    value = sign_extend(value, 31);
    return value;
}


uint64 NMD::extract_u_20_19_18_17_16_15_14_13(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 13, 8);
    return value;
}


uint64 NMD::extract_u_17_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 16) << 2;
    return value;
}


uint64 NMD::extract_rd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


uint64 NMD::extract_c0s_20_19_18_17_16(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 16, 5);
    return value;
}


uint64 NMD::extract_code_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


int64 NMD::extract_s__se25_0_24_to_1_s1(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 1) << 25;
    value |= extract_bits(instruction, 1, 24) << 1;
    value = sign_extend(value, 25);
    return value;
}


uint64 NMD::extract_u_1_0(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 2);
    return value;
}


uint64 NMD::extract_u_3_8__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 3, 1) << 3;
    value |= extract_bits(instruction, 8, 1) << 2;
    return value;
}


uint64 NMD::extract_fd_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


uint64 NMD::extract_u_4_3_2_1_0__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 5) << 2;
    return value;
}


uint64 NMD::extract_rtz4_9_7_6_5(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 5, 3);
    value |= extract_bits(instruction, 9, 1) << 3;
    return value;
}


uint64 NMD::extract_sel_15_14_13_12_11(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 11, 5);
    return value;
}


uint64 NMD::extract_ct_25_24_23_22_21(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 21, 5);
    return value;
}


uint64 NMD::extract_u_20_to_2__s2(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 2, 19) << 2;
    return value;
}


int64 NMD::extract_s__se3_4_2_1_0(uint64 instruction)
{
    int64 value = 0;
    value |= extract_bits(instruction, 0, 3);
    value |= extract_bits(instruction, 4, 1) << 3;
    value = sign_extend(value, 3);
    return value;
}


uint64 NMD::extract_u_3_2_1_0__s1(uint64 instruction)
{
    uint64 value = 0;
    value |= extract_bits(instruction, 0, 4) << 1;
    return value;
}



bool NMD::ADDIU_32__cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


bool NMD::ADDIU_RS5__cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


bool NMD::BALRSC_cond(uint64 instruction)
{
    uint64 rt = extract_rt_25_24_23_22_21(instruction);
    return rt != 0;
}


bool NMD::BEQC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 < rt3 && u != 0;
}


bool NMD::BNEC_16__cond(uint64 instruction)
{
    uint64 rs3 = extract_rs3_6_5_4(instruction);
    uint64 rt3 = extract_rt3_9_8_7(instruction);
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return rs3 >= rt3 && u != 0;
}


bool NMD::MOVE_cond(uint64 instruction)
{
    uint64 rt = extract_rt_9_8_7_6_5(instruction);
    return rt != 0;
}


bool NMD::P16_BR1_cond(uint64 instruction)
{
    uint64 u = extract_u_3_2_1_0__s1(instruction);
    return u != 0;
}


bool NMD::PREF_S9__cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


bool NMD::PREFE_cond(uint64 instruction)
{
    uint64 hint = extract_hint_25_24_23_22_21(instruction);
    return hint != 31;
}


bool NMD::SLTU_cond(uint64 instruction)
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
std::string NMD::ABS_D(uint64 instruction)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string fs = FPR(copy(fs_value));
    std::string fd = FPR(copy(fd_value));

    return img::format("ABS.D %s, %s", fd, fs);
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
std::string NMD::ABS_S(uint64 instruction)
{
    uint64 fd_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string fs = FPR(copy(fs_value));
    std::string fd = FPR(copy(fd_value));

    return img::format("ABS.S %s, %s", fd, fs);
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
std::string NMD::ABSQ_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("ABSQ_S.PH %s, %s", rt, rs);
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
std::string NMD::ABSQ_S_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("ABSQ_S.QB %s, %s", rt, rs);
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
std::string NMD::ABSQ_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("ABSQ_S.W %s, %s", rt, rs);
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
std::string NMD::ACLR(uint64 instruction)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string bit = IMMEDIATE(copy(bit_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("ACLR %s, %s(%s)", bit, s, rs);
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
std::string NMD::ADD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADD %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADD_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));
    std::string fd = FPR(copy(fd_value));

    return img::format("ADD.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::ADD_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));
    std::string fd = FPR(copy(fd_value));

    return img::format("ADD.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::ADDIU_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_15_to_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ADDIU %s, %s, %s", rt, rs, u);
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
std::string NMD::ADDIU_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("ADDIU %s, %s", rt, s);
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
std::string NMD::ADDIU_GP48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("ADDIU %s, $%d, %s", rt, 28, s);
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
std::string NMD::ADDIU_GP_B_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ADDIU %s, $%d, %s", rt, 28, u);
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
std::string NMD::ADDIU_GP_W_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ADDIU %s, $%d, %s", rt, 28, u);
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
std::string NMD::ADDIU_NEG_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(neg_copy(u_value));

    return img::format("ADDIU %s, %s, %s", rt, rs, u);
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
std::string NMD::ADDIU_R1_SP_(uint64 instruction)
{
    uint64 u_value = extract_u_5_4_3_2_1_0__s2(instruction);
    uint64 rt3_value = extract_rt3_9_8_7(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ADDIU %s, $%d, %s", rt3, 29, u);
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
std::string NMD::ADDIU_R2_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1_0__s2(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ADDIU %s, %s, %s", rt3, rs3, u);
}


/*
 * ADDIU[RS5] rt, s5 - Add Signed Word and Set Carry Bit
 *
 *  5432109876543210
 *  100100      1
 *     rt -----
 *           s - ---
 */
std::string NMD::ADDIU_RS5_(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    int64 s_value = extract_s__se3_4_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("ADDIU %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ADDIUPC_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("ADDIUPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ADDIUPC_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 6);

    return img::format("ADDIUPC %s, %s", rt, s);
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
std::string NMD::ADDQ_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQ.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQ_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQ_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQ_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQ_S.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQH_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQH.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQH_R_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQH_R.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQH_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQH_R.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDQH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDQH.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDSC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDSC %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDU_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string rd3 = GPR(decode_gpr_gpr3(rd3_value));

    return img::format("ADDU %s, %s, %s", rd3, rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ADDU_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ADDU_4X4_(uint64 instruction)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    std::string rs4 = GPR(decode_gpr_gpr4(rs4_value));
    std::string rt4 = GPR(decode_gpr_gpr4(rt4_value));

    return img::format("ADDU %s, %s", rs4, rt4);
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
std::string NMD::ADDU_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDU.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDU_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDU.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDU_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDU_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDU_S_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDU_S.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDUH_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDUH.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDUH_R_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDUH_R.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::ADDWC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ADDWC %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ALUIPC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("ALUIPC %s, %%pcrel_hi(%s)", rt, s);
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
std::string NMD::AND_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("AND %s, %s", rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::AND_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("AND %s, %s, %s", rd, rs, rt);
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
std::string NMD::ANDI_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 eu_value = extract_eu_3_2_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string eu = IMMEDIATE(encode_eu_from_u_andi16(eu_value));

    return img::format("ANDI %s, %s, %s", rt3, rs3, eu);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ANDI_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ANDI %s, %s, %s", rt, rs, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::APPEND(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("APPEND %s, %s, %s", rt, rs, sa);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ASET(uint64 instruction)
{
    uint64 bit_value = extract_bit_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string bit = IMMEDIATE(copy(bit_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("ASET %s, %s(%s)", bit, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BALC_16_(uint64 instruction)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    std::string s = ADDRESS(encode_s_from_address(s_value), 2);

    return img::format("BALC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BALC_32_(uint64 instruction)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BALC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BALRSC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("BALRSC %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BBEQZC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string bit = IMMEDIATE(copy(bit_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BBEQZC %s, %s, %s", rt, bit, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BBNEZC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 bit_value = extract_bit_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string bit = IMMEDIATE(copy(bit_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BBNEZC %s, %s, %s", rt, bit, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC_16_(uint64 instruction)
{
    int64 s_value = extract_s__se10_0_9_8_7_6_5_4_3_2_1_s1(instruction);

    std::string s = ADDRESS(encode_s_from_address(s_value), 2);

    return img::format("BC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC_32_(uint64 instruction)
{
    int64 s_value = extract_s__se25_0_24_to_1_s1(instruction);

    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BC %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC1EQZC(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BC1EQZC %s, %s", ft, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC1NEZC(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BC1NEZC %s, %s", ft, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC2EQZC(uint64 instruction)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string ct = CPR(copy(ct_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BC2EQZC %s, %s", ct, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BC2NEZC(uint64 instruction)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string ct = CPR(copy(ct_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BC2NEZC %s, %s", ct, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BEQC_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    std::string rs3 = GPR(encode_rs3_and_check_rs3_lt_rt3(rs3_value));
    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = ADDRESS(encode_u_from_address(u_value), 2);

    return img::format("BEQC %s, %s, %s", rs3, rt3, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BEQC_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BEQC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BEQIC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BEQIC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BEQZC_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 2);

    return img::format("BEQZC %s, %s", rt3, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BGEC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BGEC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BGEIC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BGEIC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BGEIUC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BGEIUC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BGEUC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BGEUC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BLTC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BLTC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BLTIC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BLTIC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BLTIUC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BLTIUC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BLTUC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BLTUC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BNEC_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s1(instruction);

    std::string rs3 = GPR(encode_rs3_and_check_rs3_ge_rt3(rs3_value));
    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = ADDRESS(encode_u_from_address(u_value), 2);

    return img::format("BNEC %s, %s, %s", rs3, rt3, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BNEC_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BNEC %s, %s, %s", rs, rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BNEIC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_16_15_14_13_12_11(instruction);
    int64 s_value = extract_s__se11_0_10_9_8_7_6_5_4_3_2_1_0_s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BNEIC %s, %s, %s", rt, u, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BNEZC_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    int64 s_value = extract_s__se7_0_6_5_4_3_2_1_s1(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 2);

    return img::format("BNEZC %s, %s", rt3, s);
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
std::string NMD::BPOSGE32C(uint64 instruction)
{
    int64 s_value = extract_s__se14_0_13_to_1_s1(instruction);

    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("BPOSGE32C %s", s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BREAK_16_(uint64 instruction)
{
    uint64 code_value = extract_code_2_1_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("BREAK %s", code);
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
std::string NMD::BREAK_32_(uint64 instruction)
{
    uint64 code_value = extract_code_18_to_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("BREAK %s", code);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::BRSC(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));

    return img::format("BRSC %s", rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CACHE(uint64 instruction)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string op = IMMEDIATE(copy(op_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("CACHE %s, %s(%s)", op, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CACHEE(uint64 instruction)
{
    uint64 op_value = extract_op_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string op = IMMEDIATE(copy(op_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("CACHEE %s, %s(%s)", op, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CEIL_L_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CEIL.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CEIL_L_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CEIL.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CEIL_W_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CEIL.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CEIL_W_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CEIL.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CFC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("CFC1 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CFC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("CFC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CLASS_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CLASS.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CLASS_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CLASS.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CLO(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("CLO %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CLZ(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("CLZ %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_AF_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.AF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_AF_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.AF.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_EQ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.EQ.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::CMP_EQ_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMP.EQ.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_EQ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.EQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_LE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.LE.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::CMP_LE_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMP.LE.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_LE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.LE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_LT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.LT.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::CMP_LT_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMP.LT.PH %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_LT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.LT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_NE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.NE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_NE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.NE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_OR_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.OR.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_OR_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.OR.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SAF_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SAF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SAF_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SAF.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SEQ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SEQ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SLE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SLE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SLE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SLE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SLT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SLT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SLT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SLT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SNE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SNE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SOR_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SOR.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SOR_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SOR.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUEQ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUEQ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SULE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SULE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SULE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SULE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SULT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SULT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SULT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SULT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUN_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUNE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUNE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_SUN_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.SUN.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UEQ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UEQ.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UEQ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UEQ.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_ULE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.ULE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_ULE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.ULE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_ULT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.ULT.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_ULT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.ULT.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UN_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UNE_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UNE.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UNE_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UNE.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CMP_UN_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("CMP.UN.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::CMPGDU_EQ_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGDU.EQ.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPGDU_LE_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGDU.LE.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPGDU_LT_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGDU.LT.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPGU_EQ_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGU.EQ.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPGU_LE_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGU.LE.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPGU_LT_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPGU.LT.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::CMPU_EQ_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPU.EQ.QB %s, %s", rs, rt);
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
std::string NMD::CMPU_LE_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPU.LE.QB %s, %s", rs, rt);
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
std::string NMD::CMPU_LT_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("CMPU.LT.QB %s, %s", rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::COP2_1(uint64 instruction)
{
    uint64 cofun_value = extract_cofun_25_24_23(instruction);

    std::string cofun = IMMEDIATE(copy(cofun_value));

    return img::format("COP2_1 %s", cofun);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CTC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("CTC1 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CTC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("CTC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_D_L(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.D.L %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_D_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.D.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_D_W(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.D.W %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_L_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_L_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_S_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.S.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_S_L(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.S.L %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_S_PL(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.S.PL %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_S_PU(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.S.PU %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_S_W(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.S.W %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_W_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::CVT_W_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("CVT.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DADDIU_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("DADDIU %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DADDIU_NEG_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(neg_copy(u_value));

    return img::format("DADDIU %s, %s, %s", rt, rs, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DADDIU_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("DADDIU %s, %s, %s", rt, rs, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DADD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DADD %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DADDU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DADDU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DCLO(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("DCLO %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DCLZ(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("DCLZ %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DDIV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DDIV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DDIVU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DDIVU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DERET(uint64 instruction)
{
    (void)instruction;

    return "DERET ";
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DEXTM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string lsb = IMMEDIATE(copy(lsb_value));
    std::string msbd = IMMEDIATE(encode_msbd_from_size(msbd_value));

    return img::format("DEXTM %s, %s, %s, %s", rt, rs, lsb, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DEXT(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string lsb = IMMEDIATE(copy(lsb_value));
    std::string msbd = IMMEDIATE(encode_msbd_from_size(msbd_value));

    return img::format("DEXT %s, %s, %s, %s", rt, rs, lsb, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DEXTU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string lsb = IMMEDIATE(copy(lsb_value));
    std::string msbd = IMMEDIATE(encode_msbd_from_size(msbd_value));

    return img::format("DEXTU %s, %s, %s, %s", rt, rs, lsb, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DINSM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string pos = IMMEDIATE(encode_lsb_from_pos_and_size(lsb_value));
    std::string size = IMMEDIATE(encode_lsb_from_pos_and_size(msbd_value));
    /* !!!!!!!!!! - no conversion function */

    return img::format("DINSM %s, %s, %s, %s", rt, rs, pos, size);
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
std::string NMD::DINS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string pos = IMMEDIATE(encode_lsb_from_pos_and_size(lsb_value));
    std::string size = IMMEDIATE(encode_lsb_from_pos_and_size(msbd_value));
    /* !!!!!!!!!! - no conversion function */

    return img::format("DINS %s, %s, %s, %s", rt, rs, pos, size);
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
std::string NMD::DINSU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string pos = IMMEDIATE(encode_lsb_from_pos_and_size(lsb_value));
    std::string size = IMMEDIATE(encode_lsb_from_pos_and_size(msbd_value));
    /* !!!!!!!!!! - no conversion function */

    return img::format("DINSU %s, %s, %s, %s", rt, rs, pos, size);
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
std::string NMD::DI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("DI %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DIV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DIV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DIV_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("DIV.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DIV_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("DIV.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DIVU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DIVU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DLSA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string u2 = IMMEDIATE(copy(u2_value));

    return img::format("DLSA %s, %s, %s, %s", rd, rs, rt, u2);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DLUI_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    uint64 u_value = extract_u_31_to_0__s32(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("DLUI %s, %s", rt, u);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMFC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("DMFC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMFC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("DMFC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMFC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("DMFC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMFGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("DMFGC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMOD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMOD %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMODU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMODU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMTC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("DMTC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMTC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("DMTC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMTC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("DMTC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMTGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("DMTGC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMT(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("DMT %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMUH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMUH %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMUHU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMUHU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMUL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMUL %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DMULU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DMULU %s, %s, %s", rd, rs, rt);
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
std::string NMD::DPA_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAQ_SA_L_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAQ_S_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAQX_SA_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAQX_S_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAU_H_QBL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAU.H.QBL %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAU_H_QBR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAU.H.QBR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPAX_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPAX.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPS_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPS.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSQ_SA_L_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSQ_SA.L.W %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSQ_S_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSQ_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSQX_SA_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSQX_SA.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSQX_S_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSQX_S.W.PH %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSU_H_QBL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSU.H.QBL %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSU_H_QBR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSU.H.QBR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DPSX_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DPSX.W.PH %s, %s, %s", ac, rs, rt);
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
std::string NMD::DROTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DROTR %s, %s, %s", rt, rs, shift);
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
std::string NMD::DROTR32(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DROTR32 %s, %s, %s", rt, rs, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DROTRV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DROTRV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DROTX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_11_10_9_8_7_6(instruction);
    uint64 shift_value = extract_shift_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));
    std::string shiftx = IMMEDIATE(copy(shiftx_value));

    return img::format("DROTX %s, %s, %s, %s", rt, rs, shift, shiftx);
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
std::string NMD::DSLL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSLL %s, %s, %s", rt, rs, shift);
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
std::string NMD::DSLL32(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSLL32 %s, %s, %s", rt, rs, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DSLLV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DSLLV %s, %s, %s", rd, rs, rt);
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
std::string NMD::DSRA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSRA %s, %s, %s", rt, rs, shift);
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
std::string NMD::DSRA32(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSRA32 %s, %s, %s", rt, rs, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DSRAV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DSRAV %s, %s, %s", rd, rs, rt);
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
std::string NMD::DSRL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSRL %s, %s, %s", rt, rs, shift);
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
std::string NMD::DSRL32(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("DSRL32 %s, %s, %s", rt, rs, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DSRLV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DSRLV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DSUB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DSUB %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DSUBU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("DSUBU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DVPE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("DVPE %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::DVP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("DVP %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EHB(uint64 instruction)
{
    (void)instruction;

    return "EHB ";
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("EI %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EMT(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("EMT %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ERET(uint64 instruction)
{
    (void)instruction;

    return "ERET ";
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ERETNC(uint64 instruction)
{
    (void)instruction;

    return "ERETNC ";
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EVP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("EVP %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EVPE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("EVPE %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXT(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string lsb = IMMEDIATE(copy(lsb_value));
    std::string msbd = IMMEDIATE(encode_msbd_from_size(msbd_value));

    return img::format("EXT %s, %s, %s, %s", rt, rs, lsb, msbd);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTD %s, %s, %s, %s", rd, rs, rt, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTD32(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTD32 %s, %s, %s, %s", rd, rs, rt, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTPDP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string size = IMMEDIATE(copy(size_value));

    return img::format("EXTPDP %s, %s, %s", rt, ac, size);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTPDPV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTPDPV %s, %s, %s", rt, ac, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 size_value = extract_size_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string size = IMMEDIATE(copy(size_value));

    return img::format("EXTP %s, %s, %s", rt, ac, size);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::EXTPV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTPV %s, %s, %s", rt, ac, rs);
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
std::string NMD::EXTR_RS_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTR_RS.W %s, %s, %s", rt, ac, shift);
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
std::string NMD::EXTR_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTR_R.W %s, %s, %s", rt, ac, shift);
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
std::string NMD::EXTR_S_H(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTR_S.H %s, %s, %s", rt, ac, shift);
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
std::string NMD::EXTR_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 shift_value = extract_shift_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTR.W %s, %s, %s", rt, ac, shift);
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
std::string NMD::EXTRV_RS_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTRV_RS.W %s, %s, %s", rt, ac, rs);
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
std::string NMD::EXTRV_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTRV_R.W %s, %s, %s", rt, ac, rs);
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
std::string NMD::EXTRV_S_H(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTRV_S.H %s, %s, %s", rt, ac, rs);
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
std::string NMD::EXTRV_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("EXTRV.W %s, %s, %s", rt, ac, rs);
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
std::string NMD::EXTW(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 shift_value = extract_shift_10_9_8_7_6(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("EXTW %s, %s, %s, %s", rd, rs, rt, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::FLOOR_L_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("FLOOR.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::FLOOR_L_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("FLOOR.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::FLOOR_W_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("FLOOR.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::FLOOR_W_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("FLOOR.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::FORK(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("FORK %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::HYPCALL(uint64 instruction)
{
    uint64 code_value = extract_code_17_to_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("HYPCALL %s", code);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::HYPCALL_16_(uint64 instruction)
{
    uint64 code_value = extract_code_1_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("HYPCALL %s", code);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::INS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 msbd_value = extract_msbt_10_9_8_7_6(instruction);
    uint64 lsb_value = extract_lsb_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string pos = IMMEDIATE(encode_lsb_from_pos_and_size(lsb_value));
    std::string size = IMMEDIATE(encode_lsb_from_pos_and_size(msbd_value));
    /* !!!!!!!!!! - no conversion function */

    return img::format("INS %s, %s, %s, %s", rt, rs, pos, size);
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
std::string NMD::INSV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("INSV %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::IRET(uint64 instruction)
{
    (void)instruction;

    return "IRET ";
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::JALRC_16_(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("JALRC $%d, %s", 31, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::JALRC_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("JALRC %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::JALRC_HB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("JALRC.HB %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::JRC(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);

    std::string rt = GPR(copy(rt_value));

    return img::format("JRC %s", rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LB_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("LB %s, %s(%s)", rt3, u, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LB_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LB %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LB_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LB %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LB_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LB %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LBE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBU_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("LBU %s, %s(%s)", rt3, u, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBU_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LBU %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBU_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LBU %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBU_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LBU %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBUE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LBUE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBUX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LBUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LBX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LBX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LD_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LD %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LD_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LD %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LD_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LD %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC1_GP_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LDC1 %s, %s($%d)", ft, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC1_S9_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LDC1 %s, %s(%s)", ft, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC1_U12_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LDC1 %s, %s(%s)", ft, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC1XS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LDC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC1X(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LDC1X %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDC2(uint64 instruction)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ct = CPR(copy(ct_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LDC2 %s, %s(%s)", ct, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("LDM %s, %s(%s), %s", rt, s, rs, count3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDPC_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 6);

    return img::format("LDPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LDX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LDXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LDXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LH_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("LH %s, %s(%s)", rt3, u, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LH_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LH %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LH_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LH %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LH_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LH %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LHE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHU_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("LHU %s, %s(%s)", rt3, u, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHU_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LHU %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHU_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LHU %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHU_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LHU %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHUE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LHUE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHUX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LHUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHUXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LHUXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LHXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LHX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LHX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LI_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 eu_value = extract_eu_6_5_4_3_2_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string eu = IMMEDIATE(encode_eu_from_s_li16(eu_value));

    return img::format("LI %s, %s", rt3, eu);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LI_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("LI %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LL %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LLD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LLD %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LLDP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LLDP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LLE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LLE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LLWP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LLWP %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LLWPE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LLWPE %s, %s, (%s)", rt, ru, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LSA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);
    uint64 u2_value = extract_u2_10_9(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));
    std::string u2 = IMMEDIATE(copy(u2_value));

    return img::format("LSA %s, %s, %s, %s", rd, rs, rt, u2);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LUI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se31_0_11_to_2_20_to_12_s12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("LUI %s, %%hi(%s)", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("LW %s, %s(%s)", rt3, u, rs3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_4X4_(uint64 instruction)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    std::string rt4 = GPR(decode_gpr_gpr4(rt4_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs4 = GPR(decode_gpr_gpr4(rs4_value));

    return img::format("LW %s, %s(%s)", rt4, u, rs4);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LW %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_GP16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LW %s, %s($%d)", rt3, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LW %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_SP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LW %s, %s($%d)", rt, u, 29);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LW_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LW %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC1_GP_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LWC1 %s, %s($%d)", ft, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC1_S9_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWC1 %s, %s(%s)", ft, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC1_U12_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWC1 %s, %s(%s)", ft, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC1X(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWC1X %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC1XS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWC1XS %s, %s(%s)", ft, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWC2(uint64 instruction)
{
    uint64 ct_value = extract_ct_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ct = CPR(copy(ct_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWC2 %s, %s(%s)", ct, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWE %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("LWM %s, %s(%s), %s", rt, s, rs, count3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWPC_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 6);

    return img::format("LWPC %s, %s", rt, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWU_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("LWU %s, %s($%d)", rt, u, 28);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWU_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWU %s, %s(%s)", rt, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWU_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("LWU %s, %s(%s)", rt, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWUX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWUX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWUXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWUXS %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWX %s, %s(%s)", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWXS_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    std::string rd3 = GPR(decode_gpr_gpr3(rd3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string rt3 = IMMEDIATE(decode_gpr_gpr3(rt3_value));

    return img::format("LWXS %s, %s(%s)", rd3, rs3, rt3);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::LWXS_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("LWXS %s, %s(%s)", rd, rs, rt);
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
std::string NMD::MADD_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MADD %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MADDF_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MADDF.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MADDF_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MADDF.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::MADDU_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MADDU %s, %s, %s", ac, rs, rt);
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
std::string NMD::MAQ_S_W_PHL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MAQ_S.W.PHL %s, %s, %s", ac, rs, rt);
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
std::string NMD::MAQ_S_W_PHR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MAQ_S.W.PHR %s, %s, %s", ac, rs, rt);
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
std::string NMD::MAQ_SA_W_PHL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MAQ_SA.W.PHL %s, %s, %s", ac, rs, rt);
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
std::string NMD::MAQ_SA_W_PHR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MAQ_SA.W.PHR %s, %s, %s", ac, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MAX_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MAX.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MAX_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MAX.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MAXA_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MAXA.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MAXA_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MAXA.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MFC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("MFC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFGC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFHC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFHC0 %s, %s, %s", rt, c0s, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFHC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MFHC1 %s, %s", rt, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFHC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("MFHC2 %s, %s", rt, cs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFHGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFHGC0 %s, %s, %s", rt, c0s, sel);
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
std::string NMD::MFHI_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));

    return img::format("MFHI %s, %s", rt, ac);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFHTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = IMMEDIATE(copy(c0s_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFHTR %s, %s, %s, %s", rt, c0s, u, sel);
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
std::string NMD::MFLO_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ac = AC(copy(ac_value));

    return img::format("MFLO %s, %s", rt, ac);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MFTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = IMMEDIATE(copy(c0s_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MFTR %s, %s, %s, %s", rt, c0s, u, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MIN_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MIN.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MIN_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MIN.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MINA_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MINA.D %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MINA_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MINA.S %s, %s, %s", fd, fs, ft);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MOD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MOD %s, %s, %s", rd, rs, rt);
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
std::string NMD::MODSUB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MODSUB %s, %s, %s", rd, rs, rt);
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
std::string NMD::MODU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MODU %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MOV_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MOV.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MOV_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MOV.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MOVE_BALC(uint64 instruction)
{
    uint64 rtz4_value = extract_rtz4_27_26_25_23_22_21(instruction);
    uint64 rd1_value = extract_rdl_25_24(instruction);
    int64 s_value = extract_s__se21_0_20_to_1_s1(instruction);

    std::string rd1 = GPR(decode_gpr_gpr1(rd1_value));
    std::string rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 4);

    return img::format("MOVE.BALC %s, %s, %s", rd1, rtz4, s);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::MOVEP(uint64 instruction)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rsz4_value = extract_rsz4_4_2_1_0(instruction);

    std::string rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value));
    std::string re2 = GPR(decode_gpr_gpr2_reg2(rd2_value));
    /* !!!!!!!!!! - no conversion function */
    std::string rsz4 = GPR(decode_gpr_gpr4_zero(rsz4_value));
    std::string rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value));

    return img::format("MOVEP %s, %s, %s, %s", rd2, re2, rsz4, rtz4);
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
std::string NMD::MOVEP_REV_(uint64 instruction)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rd2_value = extract_rd2_3_8(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    std::string rs4 = GPR(decode_gpr_gpr4(rs4_value));
    std::string rt4 = GPR(decode_gpr_gpr4(rt4_value));
    std::string rd2 = GPR(decode_gpr_gpr2_reg1(rd2_value));
    std::string rs2 = GPR(decode_gpr_gpr2_reg2(rd2_value));
    /* !!!!!!!!!! - no conversion function */

    return img::format("MOVEP %s, %s, %s, %s", rs4, rt4, rd2, rs2);
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
std::string NMD::MOVE(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 rs_value = extract_rs_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("MOVE %s, %s", rt, rs);
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
std::string NMD::MOVN(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MOVN %s, %s, %s", rd, rs, rt);
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
std::string NMD::MOVZ(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MOVZ %s, %s, %s", rd, rs, rt);
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
std::string NMD::MSUB_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MSUB %s, %s, %s", ac, rs, rt);
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
std::string NMD::MSUBF_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MSUBF.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::MSUBF_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MSUBF.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::MSUBU_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MSUBU %s, %s, %s", ac, rs, rt);
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
std::string NMD::MTC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTC0 %s, %s, %s", rt, c0s, sel);
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
std::string NMD::MTC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MTC1 %s, %s", rt, fs);
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
std::string NMD::MTC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("MTC2 %s, %s", rt, cs);
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
std::string NMD::MTGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTGC0 %s, %s, %s", rt, c0s, sel);
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
std::string NMD::MTHC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTHC0 %s, %s, %s", rt, c0s, sel);
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
std::string NMD::MTHC1(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("MTHC1 %s, %s", rt, fs);
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
std::string NMD::MTHC2(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 cs_value = extract_cs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string cs = CPR(copy(cs_value));

    return img::format("MTHC2 %s, %s", rt, cs);
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
std::string NMD::MTHGC0(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = CPR(copy(c0s_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTHGC0 %s, %s, %s", rt, c0s, sel);
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
std::string NMD::MTHI_DSP_(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string ac = AC(copy(ac_value));

    return img::format("MTHI %s, %s", rs, ac);
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
std::string NMD::MTHLIP(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string ac = AC(copy(ac_value));

    return img::format("MTHLIP %s, %s", rs, ac);
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
std::string NMD::MTHTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = IMMEDIATE(copy(c0s_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTHTR %s, %s, %s, %s", rt, c0s, u, sel);
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
std::string NMD::MTLO_DSP_(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string ac = AC(copy(ac_value));

    return img::format("MTLO %s, %s", rs, ac);
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
std::string NMD::MTTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 c0s_value = extract_c0s_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_15_14_13_12_11(instruction);
    uint64 u_value = extract_u_10(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string c0s = IMMEDIATE(copy(c0s_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("MTTR %s, %s, %s, %s", rt, c0s, u, sel);
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
std::string NMD::MUH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MUH %s, %s, %s", rd, rs, rt);
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
std::string NMD::MUHU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MUHU %s, %s, %s", rd, rs, rt);
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
std::string NMD::MUL_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MUL %s, %s, %s", rd, rs, rt);
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
std::string NMD::MUL_4X4_(uint64 instruction)
{
    uint64 rt4_value = extract_rt4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);

    std::string rs4 = GPR(decode_gpr_gpr4(rs4_value));
    std::string rt4 = GPR(decode_gpr_gpr4(rt4_value));

    return img::format("MUL %s, %s", rs4, rt4);
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
std::string NMD::MUL_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MUL.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::MUL_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MUL.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::MUL_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MUL_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::MUL_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("MUL.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::MULEQ_S_W_PHL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULEQ_S.W.PHL %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULEQ_S_W_PHR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULEQ_S.W.PHR %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULEU_S_PH_QBL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULEU_S.PH.QBL %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULEU_S_PH_QBR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULEU_S.PH.QBR %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULQ_RS_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULQ_RS.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULQ_RS_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULQ_RS.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULQ_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULQ_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULQ_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULQ_S.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::MULSA_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULSA.W.PH %s, %s, %s", ac, rs, rt);
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
std::string NMD::MULSAQ_S_W_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULSAQ_S.W.PH %s, %s, %s", ac, rs, rt);
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
std::string NMD::MULT_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULT %s, %s, %s", ac, rs, rt);
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
std::string NMD::MULTU_DSP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string ac = AC(copy(ac_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULTU %s, %s, %s", ac, rs, rt);
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
std::string NMD::MULU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("MULU %s, %s, %s", rd, rs, rt);
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
std::string NMD::NEG_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("NEG.D %s, %s", ft, fs);
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
std::string NMD::NEG_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("NEG.S %s, %s", ft, fs);
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
std::string NMD::NOP_16_(uint64 instruction)
{
    (void)instruction;

    return "NOP ";
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
std::string NMD::NOP_32_(uint64 instruction)
{
    (void)instruction;

    return "NOP ";
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
std::string NMD::NOR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("NOR %s, %s, %s", rd, rs, rt);
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
std::string NMD::NOT_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("NOT %s, %s", rt3, rs3);
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
std::string NMD::OR_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));

    return img::format("OR %s, %s", rs3, rt3);
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
std::string NMD::OR_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("OR %s, %s, %s", rd, rs, rt);
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
std::string NMD::ORI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("ORI %s, %s, %s", rt, rs, u);
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
std::string NMD::PACKRL_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PACKRL.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::PAUSE(uint64 instruction)
{
    (void)instruction;

    return "PAUSE ";
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
std::string NMD::PICK_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PICK.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::PICK_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PICK.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::PRECEQ_W_PHL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQ.W.PHL %s, %s", rt, rs);
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
std::string NMD::PRECEQ_W_PHR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQ.W.PHR %s, %s", rt, rs);
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
std::string NMD::PRECEQU_PH_QBLA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQU.PH.QBLA %s, %s", rt, rs);
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
std::string NMD::PRECEQU_PH_QBL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQU.PH.QBL %s, %s", rt, rs);
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
std::string NMD::PRECEQU_PH_QBRA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQU.PH.QBRA %s, %s", rt, rs);
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
std::string NMD::PRECEQU_PH_QBR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEQU.PH.QBR %s, %s", rt, rs);
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
std::string NMD::PRECEU_PH_QBLA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEU.PH.QBLA %s, %s", rt, rs);
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
std::string NMD::PRECEU_PH_QBL(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEU.PH.QBL %s, %s", rt, rs);
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
std::string NMD::PRECEU_PH_QBRA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEU.PH.QBRA %s, %s", rt, rs);
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
std::string NMD::PRECEU_PH_QBR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PRECEU.PH.QBR %s, %s", rt, rs);
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
std::string NMD::PRECR_QB_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PRECR.QB.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::PRECR_SRA_PH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("PRECR_SRA.PH.W %s, %s, %s", rt, rs, sa);
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
std::string NMD::PRECR_SRA_R_PH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("PRECR_SRA_R.PH.W %s, %s, %s", rt, rs, sa);
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
std::string NMD::PRECRQ_PH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PRECRQ.PH.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::PRECRQ_QB_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PRECRQ.QB.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::PRECRQ_RS_PH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PRECRQ_RS.PH.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::PRECRQU_S_QB_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("PRECRQU_S.QB.PH %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::PREF_S9_(uint64 instruction)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string hint = IMMEDIATE(copy(hint_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PREF %s, %s(%s)", hint, s, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::PREF_U12_(uint64 instruction)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string hint = IMMEDIATE(copy(hint_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PREF %s, %s(%s)", hint, u, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::PREFE(uint64 instruction)
{
    uint64 hint_value = extract_hint_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string hint = IMMEDIATE(copy(hint_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("PREFE %s, %s(%s)", hint, s, rs);
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
std::string NMD::PREPEND(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("PREPEND %s, %s, %s", rt, rs, sa);
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
std::string NMD::RADDU_W_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("RADDU.W.QB %s, %s", rt, rs);
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
std::string NMD::RDDSP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string mask = IMMEDIATE(copy(mask_value));

    return img::format("RDDSP %s, %s", rt, mask);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RDHWR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 hs_value = extract_hs_20_19_18_17_16(instruction);
    uint64 sel_value = extract_sel_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string hs = CPR(copy(hs_value));
    std::string sel = IMMEDIATE(copy(sel_value));

    return img::format("RDHWR %s, %s, %s", rt, hs, sel);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RDPGPR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("RDPGPR %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RECIP_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RECIP.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RECIP_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RECIP.S %s, %s", ft, fs);
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
std::string NMD::REPL_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se9_20_19_18_17_16_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));

    return img::format("REPL.PH %s, %s", rt, s);
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
std::string NMD::REPL_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_19_18_17_16_15_14_13(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("REPL.QB %s, %s", rt, u);
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
std::string NMD::REPLV_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("REPLV.PH %s, %s", rt, rs);
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
std::string NMD::REPLV_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("REPLV.QB %s, %s", rt, rs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RESTORE_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    return img::format("RESTORE %s%s", u,
               save_restore_list(rt_value, count_value, gp_value));
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RESTORE_JRC_16_(uint64 instruction)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    return img::format("RESTORE.JRC %s%s", u,
        save_restore_list(encode_rt1_from_rt(rt1_value), count_value, 0));
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RESTORE_JRC_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    return img::format("RESTORE.JRC %s%s", u,
               save_restore_list(rt_value, count_value, gp_value));
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RESTOREF(uint64 instruction)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    std::string count = IMMEDIATE(copy(count_value));

    return img::format("RESTOREF %s, %s", u, count);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RINT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RINT.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RINT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RINT.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROTR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("ROTR %s, %s, %s", rt, rs, shift);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROTRV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("ROTRV %s, %s, %s", rd, rs, rt);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROTX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shiftx_value = extract_shiftx_10_9_8_7__s1(instruction);
    uint64 stripe_value = extract_stripe_6(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));
    std::string shiftx = IMMEDIATE(copy(shiftx_value));
    std::string stripe = IMMEDIATE(copy(stripe_value));

    return img::format("ROTX %s, %s, %s, %s, %s",
                       rt, rs, shift, shiftx, stripe);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROUND_L_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("ROUND.L.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROUND_L_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("ROUND.L.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROUND_W_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("ROUND.W.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::ROUND_W_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("ROUND.W.S %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RSQRT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RSQRT.D %s, %s", ft, fs);
}


/*
 *
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000               x1110000101
 *     rt -----
 *          rs -----
 *               rd -----
 */
std::string NMD::RSQRT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("RSQRT.S %s, %s", ft, fs);
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
std::string NMD::SAVE_16_(uint64 instruction)
{
    uint64 rt1_value = extract_rtl_11(instruction);
    uint64 u_value = extract_u_7_6_5_4__s4(instruction);
    uint64 count_value = extract_count_3_2_1_0(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    return img::format("SAVE %s%s", u,
        save_restore_list(encode_rt1_from_rt(rt1_value), count_value, 0));
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
std::string NMD::SAVE_32_(uint64 instruction)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);
    uint64 gp_value = extract_gp_2(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    return img::format("SAVE %s%s", u,
               save_restore_list(rt_value, count_value, gp_value));
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
std::string NMD::SAVEF(uint64 instruction)
{
    uint64 count_value = extract_count_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3__s3(instruction);

    std::string u = IMMEDIATE(copy(u_value));
    std::string count = IMMEDIATE(copy(count_value));

    return img::format("SAVEF %s, %s", u, count);
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
std::string NMD::SB_16_(uint64 instruction)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_1_0(instruction);

    std::string rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("SB %s, %s(%s)", rtz3, u, rs3);
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
std::string NMD::SB_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SB %s, %s($%d)", rt, u, 28);
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
std::string NMD::SB_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SB %s, %s(%s)", rt, s, rs);
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
std::string NMD::SB_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SB %s, %s(%s)", rt, u, rs);
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
std::string NMD::SBE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SBE %s, %s(%s)", rt, s, rs);
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
std::string NMD::SBX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SBX %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SC(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SC %s, %s(%s)", rt, s, rs);
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
std::string NMD::SCD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_s3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SCD %s, %s(%s)", rt, s, rs);
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
std::string NMD::SCDP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SCDP %s, %s, (%s)", rt, ru, rs);
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
std::string NMD::SCE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SCE %s, %s(%s)", rt, s, rs);
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
std::string NMD::SCWP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SCWP %s, %s, (%s)", rt, ru, rs);
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
std::string NMD::SCWPE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ru_value = extract_ru_7_6_5_4_3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string ru = GPR(copy(ru_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SCWPE %s, %s, (%s)", rt, ru, rs);
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
std::string NMD::SD_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_3__s3(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SD %s, %s($%d)", rt, u, 28);
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
std::string NMD::SD_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SD %s, %s(%s)", rt, s, rs);
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
std::string NMD::SD_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SD %s, %s(%s)", rt, u, rs);
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
std::string NMD::SDBBP_16_(uint64 instruction)
{
    uint64 code_value = extract_code_2_1_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("SDBBP %s", code);
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
std::string NMD::SDBBP_32_(uint64 instruction)
{
    uint64 code_value = extract_code_18_to_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("SDBBP %s", code);
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
std::string NMD::SDC1_GP_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SDC1 %s, %s($%d)", ft, u, 28);
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
std::string NMD::SDC1_S9_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SDC1 %s, %s(%s)", ft, s, rs);
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
std::string NMD::SDC1_U12_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SDC1 %s, %s(%s)", ft, u, rs);
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
std::string NMD::SDC1X(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SDC1X %s, %s(%s)", ft, rs, rt);
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
std::string NMD::SDC1XS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SDC1XS %s, %s(%s)", ft, rs, rt);
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
std::string NMD::SDC2(uint64 instruction)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string cs = CPR(copy(cs_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SDC2 %s, %s(%s)", cs, s, rs);
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
std::string NMD::SDM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("SDM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::SDPC_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 6);

    return img::format("SDPC %s, %s", rt, s);
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
std::string NMD::SDXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SDXS %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SDX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SDX %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SEB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SEB %s, %s", rt, rs);
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
std::string NMD::SEH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SEH %s, %s", rt, rs);
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
std::string NMD::SEL_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SEL.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::SEL_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SEL.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::SELEQZ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SELEQZ.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::SELEQZ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SELEQZ.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::SELNEZ_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SELNEZ.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::SELNEZ_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SELNEZ.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::SEQI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SEQI %s, %s, %s", rt, rs, u);
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
std::string NMD::SH_16_(uint64 instruction)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_2_1__s1(instruction);

    std::string rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("SH %s, %s(%s)", rtz3, u, rs3);
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
std::string NMD::SH_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_1__s1(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SH %s, %s($%d)", rt, u, 28);
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
std::string NMD::SH_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SH %s, %s(%s)", rt, s, rs);
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
std::string NMD::SH_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SH %s, %s(%s)", rt, u, rs);
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
std::string NMD::SHE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHE %s, %s(%s)", rt, s, rs);
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
std::string NMD::SHILO(uint64 instruction)
{
    int64 shift_value = extract_shift__se5_21_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string shift = IMMEDIATE(copy(shift_value));
    std::string ac = AC(copy(ac_value));

    return img::format("SHILO %s, %s", ac, shift);
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
std::string NMD::SHILOV(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ac_value = extract_ac_15_14(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string ac = AC(copy(ac_value));

    return img::format("SHILOV %s, %s", ac, rs);
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
std::string NMD::SHLL_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHLL.PH %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHLL_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHLL.QB %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHLL_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHLL_S.PH %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHLL_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHLL_S.W %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHLLV_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHLLV.PH %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHLLV_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHLLV.QB %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHLLV_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHLLV_S.PH %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHLLV_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHLLV_S.W %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRA_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRA.PH %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRA_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRA.QB %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRA_R_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRA_R.PH %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRA_R_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRA_R.QB %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRA_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12_11(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRA_R.W %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRAV_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRAV.PH %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRAV_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRAV.QB %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRAV_R_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRAV_R.PH %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRAV_R_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRAV_R.QB %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRAV_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRAV_R.W %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRL_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRL.PH %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRL_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 sa_value = extract_sa_15_14_13(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string sa = IMMEDIATE(copy(sa_value));

    return img::format("SHRL.QB %s, %s, %s", rt, rs, sa);
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
std::string NMD::SHRLV_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRLV.PH %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHRLV_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SHRLV.QB %s, %s, %s", rd, rt, rs);
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
std::string NMD::SHX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SHX %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SHXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SHXS %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SIGRIE(uint64 instruction)
{
    uint64 code_value = extract_code_18_to_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("SIGRIE %s", code);
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
std::string NMD::SLL_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string shift3 = IMMEDIATE(encode_shift3_from_shift(shift3_value));

    return img::format("SLL %s, %s, %s", rt3, rs3, shift3);
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
std::string NMD::SLL_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("SLL %s, %s, %s", rt, rs, shift);
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
std::string NMD::SLLV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SLLV %s, %s, %s", rd, rs, rt);
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
std::string NMD::SLT(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SLT %s, %s, %s", rd, rs, rt);
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
std::string NMD::SLTI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SLTI %s, %s, %s", rt, rs, u);
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
std::string NMD::SLTIU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SLTIU %s, %s, %s", rt, rs, u);
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
std::string NMD::SLTU(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SLTU %s, %s, %s", rd, rs, rt);
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
std::string NMD::SOV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SOV %s, %s, %s", rd, rs, rt);
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
std::string NMD::SPECIAL2(uint64 instruction)
{
    uint64 op_value = extract_op_25_to_3(instruction);

    std::string op = IMMEDIATE(copy(op_value));

    return img::format("SPECIAL2 %s", op);
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
std::string NMD::SQRT_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("SQRT.D %s, %s", ft, fs);
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
std::string NMD::SQRT_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("SQRT.S %s, %s", ft, fs);
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
std::string NMD::SRA(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("SRA %s, %s, %s", rt, rs, shift);
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
std::string NMD::SRAV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SRAV %s, %s, %s", rd, rs, rt);
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
std::string NMD::SRL_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 shift3_value = extract_shift3_2_1_0(instruction);

    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string shift3 = IMMEDIATE(encode_shift3_from_shift(shift3_value));

    return img::format("SRL %s, %s, %s", rt3, rs3, shift3);
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
std::string NMD::SRL_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 shift_value = extract_shift_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string shift = IMMEDIATE(copy(shift_value));

    return img::format("SRL %s, %s, %s", rt, rs, shift);
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
std::string NMD::SRLV(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SRLV %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUB %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUB_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SUB.D %s, %s, %s", fd, fs, ft);
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
std::string NMD::SUB_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);
    uint64 fd_value = extract_fd_15_14_13_12_11(instruction);

    std::string fd = FPR(copy(fd_value));
    std::string fs = FPR(copy(fs_value));
    std::string ft = FPR(copy(ft_value));

    return img::format("SUB.S %s, %s, %s", fd, fs, ft);
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
std::string NMD::SUBQ_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQ.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQ_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQ_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQ_S_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQ_S.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQH_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQH.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQH_R_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQH_R.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQH_R_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQH_R.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBQH_W(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBQH.W %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBU_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 rd3_value = extract_rd3_3_2_1(instruction);

    std::string rd3 = GPR(decode_gpr_gpr3(rd3_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));

    return img::format("SUBU %s, %s, %s", rd3, rs3, rt3);
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
std::string NMD::SUBU_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBU %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBU_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBU.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBU_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBU.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBU_S_PH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBU_S.PH %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBU_S_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBU_S.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBUH_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBUH.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::SUBUH_R_QB(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SUBUH_R.QB %s, %s, %s", rd, rs, rt);
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
std::string NMD::SW_16_(uint64 instruction)
{
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);
    uint64 u_value = extract_u_3_2_1_0__s2(instruction);

    std::string rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));

    return img::format("SW %s, %s(%s)", rtz3, u, rs3);
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
std::string NMD::SW_4X4_(uint64 instruction)
{
    uint64 rtz4_value = extract_rtz4_9_7_6_5(instruction);
    uint64 rs4_value = extract_rs4_4_2_1_0(instruction);
    uint64 u_value = extract_u_3_8__s2(instruction);

    std::string rtz4 = GPR(decode_gpr_gpr4_zero(rtz4_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs4 = GPR(decode_gpr_gpr4(rs4_value));

    return img::format("SW %s, %s(%s)", rtz4, u, rs4);
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
std::string NMD::SW_GP16_(uint64 instruction)
{
    uint64 u_value = extract_u_6_5_4_3_2_1_0__s2(instruction);
    uint64 rtz3_value = extract_rtz3_9_8_7(instruction);

    std::string rtz3 = GPR(decode_gpr_gpr3_src_store(rtz3_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SW %s, %s($%d)", rtz3, u, 28);
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
std::string NMD::SW_GP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_20_to_2__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SW %s, %s($%d)", rt, u, 28);
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
std::string NMD::SW_S9_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SW %s, %s(%s)", rt, s, rs);
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
std::string NMD::SW_SP_(uint64 instruction)
{
    uint64 rt_value = extract_rt_9_8_7_6_5(instruction);
    uint64 u_value = extract_u_4_3_2_1_0__s2(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SW %s, %s($%d)", rt, u, 29);
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
std::string NMD::SW_U12_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SW %s, %s(%s)", rt, u, rs);
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
std::string NMD::SWC1_GP_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 u_value = extract_u_17_to_2__s2(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("SWC1 %s, %s($%d)", ft, u, 28);
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
std::string NMD::SWC1_S9_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SWC1 %s, %s(%s)", ft, s, rs);
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
std::string NMD::SWC1_U12_(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string u = IMMEDIATE(copy(u_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SWC1 %s, %s(%s)", ft, u, rs);
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
std::string NMD::SWC1X(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SWC1X %s, %s(%s)", ft, rs, rt);
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
std::string NMD::SWC1XS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 ft_value = extract_ft_15_14_13_12_11(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SWC1XS %s, %s(%s)", ft, rs, rt);
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
std::string NMD::SWC2(uint64 instruction)
{
    uint64 cs_value = extract_cs_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string cs = CPR(copy(cs_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SWC2 %s, %s(%s)", cs, s, rs);
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
std::string NMD::SWE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SWE %s, %s(%s)", rt, s, rs);
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
std::string NMD::SWM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("SWM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::SWPC_48_(uint64 instruction)
{
    uint64 rt_value = extract_rt_41_40_39_38_37(instruction);
    int64 s_value = extract_s__se31_15_to_0_31_to_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = ADDRESS(encode_s_from_address(s_value), 6);

    return img::format("SWPC %s, %s", rt, s);
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
std::string NMD::SWX(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SWX %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SWXS(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("SWXS %s, %s(%s)", rd, rs, rt);
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
std::string NMD::SYNC(uint64 instruction)
{
    uint64 stype_value = extract_stype_20_19_18_17_16(instruction);

    std::string stype = IMMEDIATE(copy(stype_value));

    return img::format("SYNC %s", stype);
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
std::string NMD::SYNCI(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SYNCI %s(%s)", s, rs);
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
std::string NMD::SYNCIE(uint64 instruction)
{
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("SYNCIE %s(%s)", s, rs);
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
std::string NMD::SYSCALL_16_(uint64 instruction)
{
    uint64 code_value = extract_code_1_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("SYSCALL %s", code);
}


/*
 * SYSCALL code - System Call. Cause a System Call Exception
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  00000000000010
 *           code ------------------
 */
std::string NMD::SYSCALL_32_(uint64 instruction)
{
    uint64 code_value = extract_code_17_to_0(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("SYSCALL %s", code);
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
std::string NMD::TEQ(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("TEQ %s, %s", rs, rt);
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
std::string NMD::TLBGINV(uint64 instruction)
{
    (void)instruction;

    return "TLBGINV ";
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
std::string NMD::TLBGINVF(uint64 instruction)
{
    (void)instruction;

    return "TLBGINVF ";
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
std::string NMD::TLBGP(uint64 instruction)
{
    (void)instruction;

    return "TLBGP ";
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
std::string NMD::TLBGR(uint64 instruction)
{
    (void)instruction;

    return "TLBGR ";
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
std::string NMD::TLBGWI(uint64 instruction)
{
    (void)instruction;

    return "TLBGWI ";
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
std::string NMD::TLBGWR(uint64 instruction)
{
    (void)instruction;

    return "TLBGWR ";
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
std::string NMD::TLBINV(uint64 instruction)
{
    (void)instruction;

    return "TLBINV ";
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
std::string NMD::TLBINVF(uint64 instruction)
{
    (void)instruction;

    return "TLBINVF ";
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
std::string NMD::TLBP(uint64 instruction)
{
    (void)instruction;

    return "TLBP ";
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
std::string NMD::TLBR(uint64 instruction)
{
    (void)instruction;

    return "TLBR ";
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
std::string NMD::TLBWI(uint64 instruction)
{
    (void)instruction;

    return "TLBWI ";
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
std::string NMD::TLBWR(uint64 instruction)
{
    (void)instruction;

    return "TLBWR ";
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
std::string NMD::TNE(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("TNE %s, %s", rs, rt);
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
std::string NMD::TRUNC_L_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("TRUNC.L.D %s, %s", ft, fs);
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
std::string NMD::TRUNC_L_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("TRUNC.L.S %s, %s", ft, fs);
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
std::string NMD::TRUNC_W_D(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("TRUNC.W.D %s, %s", ft, fs);
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
std::string NMD::TRUNC_W_S(uint64 instruction)
{
    uint64 ft_value = extract_ft_25_24_23_22_21(instruction);
    uint64 fs_value = extract_fs_20_19_18_17_16(instruction);

    std::string ft = FPR(copy(ft_value));
    std::string fs = FPR(copy(fs_value));

    return img::format("TRUNC.W.S %s, %s", ft, fs);
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
std::string NMD::UALDM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("UALDM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::UALH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("UALH %s, %s(%s)", rt, s, rs);
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
std::string NMD::UALWM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("UALWM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::UASDM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("UASDM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::UASH(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("UASH %s, %s(%s)", rt, s, rs);
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
std::string NMD::UASWM(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    int64 s_value = extract_s__se8_15_7_6_5_4_3_2_1_0(instruction);
    uint64 count3_value = extract_count3_14_13_12(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string s = IMMEDIATE(copy(s_value));
    std::string rs = GPR(copy(rs_value));
    std::string count3 = IMMEDIATE(encode_count3_from_count(count3_value));

    return img::format("UASWM %s, %s(%s), %s", rt, s, rs, count3);
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
std::string NMD::UDI(uint64 instruction)
{
    uint64 op_value = extract_op_25_to_3(instruction);

    std::string op = IMMEDIATE(copy(op_value));

    return img::format("UDI %s", op);
}


/*
 * WAIT code - Enter Wait State
 *
 *   3         2         1
 *  10987654321098765432109876543210
 *  001000          1100001101111111
 *   code ----------
 */
std::string NMD::WAIT(uint64 instruction)
{
    uint64 code_value = extract_code_25_24_23_22_21_20_19_18_17_16(instruction);

    std::string code = IMMEDIATE(copy(code_value));

    return img::format("WAIT %s", code);
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
std::string NMD::WRDSP(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 mask_value = extract_mask_20_19_18_17_16_15_14(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string mask = IMMEDIATE(copy(mask_value));

    return img::format("WRDSP %s, %s", rt, mask);
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
std::string NMD::WRPGPR(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("WRPGPR %s, %s", rt, rs);
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
std::string NMD::XOR_16_(uint64 instruction)
{
    uint64 rt3_value = extract_rt3_9_8_7(instruction);
    uint64 rs3_value = extract_rs3_6_5_4(instruction);

    std::string rs3 = GPR(decode_gpr_gpr3(rs3_value));
    std::string rt3 = GPR(decode_gpr_gpr3(rt3_value));

    return img::format("XOR %s, %s", rs3, rt3);
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
std::string NMD::XOR_32_(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 rd_value = extract_rd_15_14_13_12_11(instruction);

    std::string rd = GPR(copy(rd_value));
    std::string rs = GPR(copy(rs_value));
    std::string rt = GPR(copy(rt_value));

    return img::format("XOR %s, %s, %s", rd, rs, rt);
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
std::string NMD::XORI(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);
    uint64 u_value = extract_u_11_10_9_8_7_6_5_4_3_2_1_0(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));
    std::string u = IMMEDIATE(copy(u_value));

    return img::format("XORI %s, %s, %s", rt, rs, u);
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
std::string NMD::YIELD(uint64 instruction)
{
    uint64 rt_value = extract_rt_25_24_23_22_21(instruction);
    uint64 rs_value = extract_rs_20_19_18_17_16(instruction);

    std::string rt = GPR(copy(rt_value));
    std::string rs = GPR(copy(rs_value));

    return img::format("YIELD %s, %s", rt, rs);
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

NMD::Pool NMD::P_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x00080000, &NMD::SYSCALL_32_      , 0,
       0x0                 },        /* SYSCALL[32] */
    { instruction         , 0                   , 0   , 32,
       0xfffc0000, 0x000c0000, &NMD::HYPCALL          , 0,
       CP0_ | VZ_          },        /* HYPCALL */
};


NMD::Pool NMD::P_RI[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00000000, &NMD::SIGRIE           , 0,
       0x0                 },        /* SIGRIE */
    { pool                , P_SYSCALL           , 2   , 32,
       0xfff80000, 0x00080000, 0                      , 0,
       0x0                 },        /* P.SYSCALL */
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00100000, &NMD::BREAK_32_        , 0,
       0x0                 },        /* BREAK[32] */
    { instruction         , 0                   , 0   , 32,
       0xfff80000, 0x00180000, &NMD::SDBBP_32_        , 0,
       EJTAG_              },        /* SDBBP[32] */
};


NMD::Pool NMD::P_ADDIU[2] = {
    { pool                , P_RI                , 4   , 32,
       0xffe00000, 0x00000000, 0                      , 0,
       0x0                 },        /* P.RI */
    { instruction         , 0                   , 0   , 32,
       0xfc000000, 0x00000000, &NMD::ADDIU_32_        , &NMD::ADDIU_32__cond   ,
       0x0                 },        /* ADDIU[32] */
};


NMD::Pool NMD::P_TRAP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000000, &NMD::TEQ              , 0,
       XMMS_               },        /* TEQ */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000400, &NMD::TNE              , 0,
       XMMS_               },        /* TNE */
};


NMD::Pool NMD::P_CMOVE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000210, &NMD::MOVZ             , 0,
       0x0                 },        /* MOVZ */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000610, &NMD::MOVN             , 0,
       0x0                 },        /* MOVN */
};


NMD::Pool NMD::P_D_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010ab0, &NMD::DMT              , 0,
       MT_                 },        /* DMT */
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000ab0, &NMD::DVPE             , 0,
       MT_                 },        /* DVPE */
};


NMD::Pool NMD::P_E_MT_VPE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20010eb0, &NMD::EMT              , 0,
       MT_                 },        /* EMT */
    { instruction         , 0                   , 0   , 32,
       0xfc1f3fff, 0x20000eb0, &NMD::EVPE             , 0,
       MT_                 },        /* EVPE */
};


NMD::Pool NMD::_P_MT_VPE[2] = {
    { pool                , P_D_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000ab0, 0                      , 0,
       0x0                 },        /* P.D_MT_VPE */
    { pool                , P_E_MT_VPE          , 2   , 32,
       0xfc003fff, 0x20000eb0, 0                      , 0,
       0x0                 },        /* P.E_MT_VPE */
};


NMD::Pool NMD::P_MT_VPE[8] = {
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


NMD::Pool NMD::P_DVP[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000390, &NMD::DVP              , 0,
       0x0                 },        /* DVP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20000790, &NMD::EVP              , 0,
       0x0                 },        /* EVP */
};


NMD::Pool NMD::P_SLTU[2] = {
    { pool                , P_DVP               , 2   , 32,
       0xfc00fbff, 0x20000390, 0                      , 0,
       0x0                 },        /* P.DVP */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000390, &NMD::SLTU             , &NMD::SLTU_cond        ,
       0x0                 },        /* SLTU */
};


NMD::Pool NMD::_POOL32A0[128] = {
    { pool                , P_TRAP              , 2   , 32,
       0xfc0003ff, 0x20000000, 0                      , 0,
       0x0                 },        /* P.TRAP */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000008, &NMD::SEB              , 0,
       XMMS_               },        /* SEB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000010, &NMD::SLLV             , 0,
       0x0                 },        /* SLLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000018, &NMD::MUL_32_          , 0,
       0x0                 },        /* MUL[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000020, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(4) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000028, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(5) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000030, &NMD::MFC0             , 0,
       0x0                 },        /* MFC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000038, &NMD::MFHC0            , 0,
       CP0_ | MVH_         },        /* MFHC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000040, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(8) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000048, &NMD::SEH              , 0,
       0x0                 },        /* SEH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000050, &NMD::SRLV             , 0,
       0x0                 },        /* SRLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000058, &NMD::MUH              , 0,
       0x0                 },        /* MUH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000060, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(12) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000068, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(13) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000070, &NMD::MTC0             , 0,
       CP0_                },        /* MTC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000078, &NMD::MTHC0            , 0,
       CP0_ | MVH_         },        /* MTHC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000080, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(16) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000088, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(17) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000090, &NMD::SRAV             , 0,
       0x0                 },        /* SRAV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000098, &NMD::MULU             , 0,
       0x0                 },        /* MULU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(20) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(21) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b0, &NMD::MFGC0            , 0,
       CP0_ | VZ_          },        /* MFGC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b8, &NMD::MFHGC0           , 0,
       CP0_ | VZ_ | MVH_   },        /* MFHGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(24) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(25) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d0, &NMD::ROTRV            , 0,
       0x0                 },        /* ROTRV */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d8, &NMD::MUHU             , 0,
       0x0                 },        /* MUHU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(28) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(29) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f0, &NMD::MTGC0            , 0,
       CP0_ | VZ_          },        /* MTGC0 */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f8, &NMD::MTHGC0           , 0,
       CP0_ | VZ_ | MVH_   },        /* MTHGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000100, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(32) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000108, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(33) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000110, &NMD::ADD              , 0,
       XMMS_               },        /* ADD */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000118, &NMD::DIV              , 0,
       0x0                 },        /* DIV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000120, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(36) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000128, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(37) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000130, &NMD::DMFC0            , 0,
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
       0xfc0003ff, 0x20000150, &NMD::ADDU_32_         , 0,
       0x0                 },        /* ADDU[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000158, &NMD::MOD              , 0,
       0x0                 },        /* MOD */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000160, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(44) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000168, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(45) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000170, &NMD::DMTC0            , 0,
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
       0xfc0003ff, 0x20000190, &NMD::SUB              , 0,
       XMMS_               },        /* SUB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000198, &NMD::DIVU             , 0,
       0x0                 },        /* DIVU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(52) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(53) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b0, &NMD::DMFGC0           , 0,
       CP0_ | MIPS64_ | VZ_},        /* DMFGC0 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c0, &NMD::RDHWR            , 0,
       XMMS_               },        /* RDHWR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(57) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d0, &NMD::SUBU_32_         , 0,
       0x0                 },        /* SUBU[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d8, &NMD::MODU             , 0,
       0x0                 },        /* MODU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e0, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(60) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e8, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(61) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f0, &NMD::DMTGC0           , 0,
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
       0xfc0003ff, 0x20000228, &NMD::FORK             , 0,
       MT_                 },        /* FORK */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000230, &NMD::MFTR             , 0,
       MT_                 },        /* MFTR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000238, &NMD::MFHTR            , 0,
       MT_                 },        /* MFHTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000240, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(72) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000248, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(73) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000250, &NMD::AND_32_          , 0,
       0x0                 },        /* AND[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000258, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(75) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000260, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(76) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000268, &NMD::YIELD            , 0,
       MT_                 },        /* YIELD */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000270, &NMD::MTTR             , 0,
       MT_                 },        /* MTTR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000278, &NMD::MTHTR            , 0,
       MT_                 },        /* MTHTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000280, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(80) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000288, 0                      , 0,
       0x0                 },        /* _POOL32A0~*(81) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000290, &NMD::OR_32_           , 0,
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
       0xfc0003ff, 0x200002d0, &NMD::NOR              , 0,
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
       0xfc0003ff, 0x20000310, &NMD::XOR_32_          , 0,
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
       0xfc0003ff, 0x20000350, &NMD::SLT              , 0,
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
       0xfc0003ff, 0x200003d0, &NMD::SOV              , 0,
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


NMD::Pool NMD::ADDQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000000d, &NMD::ADDQ_PH          , 0,
       DSP_                },        /* ADDQ.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000040d, &NMD::ADDQ_S_PH        , 0,
       DSP_                },        /* ADDQ_S.PH */
};


NMD::Pool NMD::MUL__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000002d, &NMD::MUL_PH           , 0,
       DSP_                },        /* MUL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000042d, &NMD::MUL_S_PH         , 0,
       DSP_                },        /* MUL_S.PH */
};


NMD::Pool NMD::ADDQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000004d, &NMD::ADDQH_PH         , 0,
       DSP_                },        /* ADDQH.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000044d, &NMD::ADDQH_R_PH       , 0,
       DSP_                },        /* ADDQH_R.PH */
};


NMD::Pool NMD::ADDQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000008d, &NMD::ADDQH_W          , 0,
       DSP_                },        /* ADDQH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000048d, &NMD::ADDQH_R_W        , 0,
       DSP_                },        /* ADDQH_R.W */
};


NMD::Pool NMD::ADDU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200000cd, &NMD::ADDU_QB          , 0,
       DSP_                },        /* ADDU.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004cd, &NMD::ADDU_S_QB        , 0,
       DSP_                },        /* ADDU_S.QB */
};


NMD::Pool NMD::ADDU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000010d, &NMD::ADDU_PH          , 0,
       DSP_                },        /* ADDU.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000050d, &NMD::ADDU_S_PH        , 0,
       DSP_                },        /* ADDU_S.PH */
};


NMD::Pool NMD::ADDUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000014d, &NMD::ADDUH_QB         , 0,
       DSP_                },        /* ADDUH.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000054d, &NMD::ADDUH_R_QB       , 0,
       DSP_                },        /* ADDUH_R.QB */
};


NMD::Pool NMD::SHRAV__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000018d, &NMD::SHRAV_PH         , 0,
       DSP_                },        /* SHRAV.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000058d, &NMD::SHRAV_R_PH       , 0,
       DSP_                },        /* SHRAV_R.PH */
};


NMD::Pool NMD::SHRAV__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200001cd, &NMD::SHRAV_QB         , 0,
       DSP_                },        /* SHRAV.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005cd, &NMD::SHRAV_R_QB       , 0,
       DSP_                },        /* SHRAV_R.QB */
};


NMD::Pool NMD::SUBQ__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000020d, &NMD::SUBQ_PH          , 0,
       DSP_                },        /* SUBQ.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000060d, &NMD::SUBQ_S_PH        , 0,
       DSP_                },        /* SUBQ_S.PH */
};


NMD::Pool NMD::SUBQH__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000024d, &NMD::SUBQH_PH         , 0,
       DSP_                },        /* SUBQH.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000064d, &NMD::SUBQH_R_PH       , 0,
       DSP_                },        /* SUBQH_R.PH */
};


NMD::Pool NMD::SUBQH__R__W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000028d, &NMD::SUBQH_W          , 0,
       DSP_                },        /* SUBQH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000068d, &NMD::SUBQH_R_W        , 0,
       DSP_                },        /* SUBQH_R.W */
};


NMD::Pool NMD::SUBU__S__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002cd, &NMD::SUBU_QB          , 0,
       DSP_                },        /* SUBU.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006cd, &NMD::SUBU_S_QB        , 0,
       DSP_                },        /* SUBU_S.QB */
};


NMD::Pool NMD::SUBU__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000030d, &NMD::SUBU_PH          , 0,
       DSP_                },        /* SUBU.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000070d, &NMD::SUBU_S_PH        , 0,
       DSP_                },        /* SUBU_S.PH */
};


NMD::Pool NMD::SHRA__R__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000335, &NMD::SHRA_PH          , 0,
       DSP_                },        /* SHRA.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000735, &NMD::SHRA_R_PH        , 0,
       DSP_                },        /* SHRA_R.PH */
};


NMD::Pool NMD::SUBUH__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000034d, &NMD::SUBUH_QB         , 0,
       DSP_                },        /* SUBUH.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000074d, &NMD::SUBUH_R_QB       , 0,
       DSP_                },        /* SUBUH_R.QB */
};


NMD::Pool NMD::SHLLV__S__PH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000038d, &NMD::SHLLV_PH         , 0,
       DSP_                },        /* SHLLV.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x2000078d, &NMD::SHLLV_S_PH       , 0,
       DSP_                },        /* SHLLV_S.PH */
};


NMD::Pool NMD::SHLL__S__PH[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003b5, &NMD::SHLL_PH          , 0,
       DSP_                },        /* SHLL.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x200007b5, 0                      , 0,
       0x0                 },        /* SHLL[_S].PH~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x20000bb5, &NMD::SHLL_S_PH        , 0,
       DSP_                },        /* SHLL_S.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc000fff, 0x20000fb5, 0                      , 0,
       0x0                 },        /* SHLL[_S].PH~*(3) */
};


NMD::Pool NMD::PRECR_SRA__R__PH_W[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003cd, &NMD::PRECR_SRA_PH_W   , 0,
       DSP_                },        /* PRECR_SRA.PH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007cd, &NMD::PRECR_SRA_R_PH_W , 0,
       DSP_                },        /* PRECR_SRA_R.PH.W */
};


NMD::Pool NMD::_POOL32A5[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000005, &NMD::CMP_EQ_PH        , 0,
       DSP_                },        /* CMP.EQ.PH */
    { pool                , ADDQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000000d, 0                      , 0,
       0x0                 },        /* ADDQ[_S].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000015, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(2) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000001d, &NMD::SHILO            , 0,
       DSP_                },        /* SHILO */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000025, &NMD::MULEQ_S_W_PHL    , 0,
       DSP_                },        /* MULEQ_S.W.PHL */
    { pool                , MUL__S__PH          , 2   , 32,
       0xfc0003ff, 0x2000002d, 0                      , 0,
       0x0                 },        /* MUL[_S].PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000035, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(6) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000003d, &NMD::REPL_PH          , 0,
       DSP_                },        /* REPL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000045, &NMD::CMP_LT_PH        , 0,
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
       0xfc0003ff, 0x20000065, &NMD::MULEQ_S_W_PHR    , 0,
       DSP_                },        /* MULEQ_S.W.PHR */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000006d, &NMD::PRECR_QB_PH      , 0,
       DSP_                },        /* PRECR.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000075, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(14) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000007d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(15) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000085, &NMD::CMP_LE_PH        , 0,
       DSP_                },        /* CMP.LE.PH */
    { pool                , ADDQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000008d, 0                      , 0,
       0x0                 },        /* ADDQH[_R].W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000095, &NMD::MULEU_S_PH_QBL   , 0,
       DSP_                },        /* MULEU_S.PH.QBL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000009d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(19) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(20) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ad, &NMD::PRECRQ_QB_PH     , 0,
       DSP_                },        /* PRECRQ.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000b5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(22) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(23) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000c5, &NMD::CMPGU_EQ_QB      , 0,
       DSP_                },        /* CMPGU.EQ.QB */
    { pool                , ADDU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200000cd, 0                      , 0,
       0x0                 },        /* ADDU[_S].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000d5, &NMD::MULEU_S_PH_QBR   , 0,
       DSP_                },        /* MULEU_S.PH.QBR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(27) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(28) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200000ed, &NMD::PRECRQ_PH_W      , 0,
       DSP_                },        /* PRECRQ.PH.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000f5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(30) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200000fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(31) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000105, &NMD::CMPGU_LT_QB      , 0,
       DSP_                },        /* CMPGU.LT.QB */
    { pool                , ADDU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000010d, 0                      , 0,
       0x0                 },        /* ADDU[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000115, &NMD::MULQ_RS_PH       , 0,
       DSP_                },        /* MULQ_RS.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000011d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(35) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000125, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(36) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000012d, &NMD::PRECRQ_RS_PH_W   , 0,
       DSP_                },        /* PRECRQ_RS.PH.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000135, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(38) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000013d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(39) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000145, &NMD::CMPGU_LE_QB      , 0,
       DSP_                },        /* CMPGU.LE.QB */
    { pool                , ADDUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000014d, 0                      , 0,
       0x0                 },        /* ADDUH[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000155, &NMD::MULQ_S_PH        , 0,
       DSP_                },        /* MULQ_S.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000015d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(43) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000165, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(44) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000016d, &NMD::PRECRQU_S_QB_PH  , 0,
       DSP_                },        /* PRECRQU_S.QB.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000175, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(46) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000017d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(47) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000185, &NMD::CMPGDU_EQ_QB     , 0,
       DSP_                },        /* CMPGDU.EQ.QB */
    { pool                , SHRAV__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000018d, 0                      , 0,
       0x0                 },        /* SHRAV[_R].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000195, &NMD::MULQ_RS_W        , 0,
       DSP_                },        /* MULQ_RS.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000019d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(51) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001a5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(52) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ad, &NMD::PACKRL_PH        , 0,
       DSP_                },        /* PACKRL.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001b5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(54) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001bd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001c5, &NMD::CMPGDU_LT_QB     , 0,
       DSP_                },        /* CMPGDU.LT.QB */
    { pool                , SHRAV__R__QB        , 2   , 32,
       0xfc0003ff, 0x200001cd, 0                      , 0,
       0x0                 },        /* SHRAV[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001d5, &NMD::MULQ_S_W         , 0,
       DSP_                },        /* MULQ_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001dd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(59) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001e5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(60) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200001ed, &NMD::PICK_QB          , 0,
       DSP_                },        /* PICK.QB */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001f5, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(62) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200001fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(63) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000205, &NMD::CMPGDU_LE_QB     , 0,
       DSP_                },        /* CMPGDU.LE.QB */
    { pool                , SUBQ__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000020d, 0                      , 0,
       0x0                 },        /* SUBQ[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000215, &NMD::APPEND           , 0,
       DSP_                },        /* APPEND */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000021d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(67) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000225, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(68) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x2000022d, &NMD::PICK_PH          , 0,
       DSP_                },        /* PICK.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x20000235, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(70) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x2000023d, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(71) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000245, &NMD::CMPU_EQ_QB       , 0,
       DSP_                },        /* CMPU.EQ.QB */
    { pool                , SUBQH__R__PH        , 2   , 32,
       0xfc0003ff, 0x2000024d, 0                      , 0,
       0x0                 },        /* SUBQH[_R].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000255, &NMD::PREPEND          , 0,
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
       0xfc0003ff, 0x20000285, &NMD::CMPU_LT_QB       , 0,
       DSP_                },        /* CMPU.LT.QB */
    { pool                , SUBQH__R__W         , 2   , 32,
       0xfc0003ff, 0x2000028d, 0                      , 0,
       0x0                 },        /* SUBQH[_R].W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000295, &NMD::MODSUB           , 0,
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
       0xfc0003ff, 0x200002c5, &NMD::CMPU_LE_QB       , 0,
       DSP_                },        /* CMPU.LE.QB */
    { pool                , SUBU__S__QB         , 2   , 32,
       0xfc0003ff, 0x200002cd, 0                      , 0,
       0x0                 },        /* SUBU[_S].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200002d5, &NMD::SHRAV_R_W        , 0,
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
       0xfc0003ff, 0x200002f5, &NMD::SHRA_R_W         , 0,
       DSP_                },        /* SHRA_R.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200002fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(95) */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000305, &NMD::ADDQ_S_W         , 0,
       DSP_                },        /* ADDQ_S.W */
    { pool                , SUBU__S__PH         , 2   , 32,
       0xfc0003ff, 0x2000030d, 0                      , 0,
       0x0                 },        /* SUBU[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000315, &NMD::SHRLV_PH         , 0,
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
       0xfc0003ff, 0x20000345, &NMD::SUBQ_S_W         , 0,
       DSP_                },        /* SUBQ_S.W */
    { pool                , SUBUH__R__QB        , 2   , 32,
       0xfc0003ff, 0x2000034d, 0                      , 0,
       0x0                 },        /* SUBUH[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000355, &NMD::SHRLV_QB         , 0,
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
       0xfc0003ff, 0x20000385, &NMD::ADDSC            , 0,
       DSP_                },        /* ADDSC */
    { pool                , SHLLV__S__PH        , 2   , 32,
       0xfc0003ff, 0x2000038d, 0                      , 0,
       0x0                 },        /* SHLLV[_S].PH */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x20000395, &NMD::SHLLV_QB         , 0,
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
       0xfc0003ff, 0x200003c5, &NMD::ADDWC            , 0,
       DSP_                },        /* ADDWC */
    { pool                , PRECR_SRA__R__PH_W  , 2   , 32,
       0xfc0003ff, 0x200003cd, 0                      , 0,
       0x0                 },        /* PRECR_SRA[_R].PH.W */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0x200003d5, &NMD::SHLLV_S_W        , 0,
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
       0xfc0003ff, 0x200003f5, &NMD::SHLL_S_W         , 0,
       DSP_                },        /* SHLL_S.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0x200003fd, 0                      , 0,
       0x0                 },        /* _POOL32A5~*(127) */
};


NMD::Pool NMD::PP_LSX[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000007, &NMD::LBX              , 0,
       0x0                 },        /* LBX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000087, &NMD::SBX              , 0,
       XMMS_               },        /* SBX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000107, &NMD::LBUX             , 0,
       0x0                 },        /* LBUX */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0x20000187, 0                      , 0,
       0x0                 },        /* PP.LSX~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000207, &NMD::LHX              , 0,
       0x0                 },        /* LHX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000287, &NMD::SHX              , 0,
       XMMS_               },        /* SHX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000307, &NMD::LHUX             , 0,
       0x0                 },        /* LHUX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000387, &NMD::LWUX             , 0,
       MIPS64_             },        /* LWUX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000407, &NMD::LWX              , 0,
       0x0                 },        /* LWX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000487, &NMD::SWX              , 0,
       XMMS_               },        /* SWX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000507, &NMD::LWC1X            , 0,
       CP1_                },        /* LWC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000587, &NMD::SWC1X            , 0,
       CP1_                },        /* SWC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000607, &NMD::LDX              , 0,
       MIPS64_             },        /* LDX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000687, &NMD::SDX              , 0,
       MIPS64_             },        /* SDX */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000707, &NMD::LDC1X            , 0,
       CP1_                },        /* LDC1X */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000787, &NMD::SDC1X            , 0,
       CP1_                },        /* SDC1X */
};


NMD::Pool NMD::PP_LSXS[16] = {
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
       0xfc0007ff, 0x20000247, &NMD::LHXS             , 0,
       0x0                 },        /* LHXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200002c7, &NMD::SHXS             , 0,
       XMMS_               },        /* SHXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000347, &NMD::LHUXS            , 0,
       0x0                 },        /* LHUXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200003c7, &NMD::LWUXS            , 0,
       MIPS64_             },        /* LWUXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000447, &NMD::LWXS_32_         , 0,
       0x0                 },        /* LWXS[32] */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200004c7, &NMD::SWXS             , 0,
       XMMS_               },        /* SWXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000547, &NMD::LWC1XS           , 0,
       CP1_                },        /* LWC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200005c7, &NMD::SWC1XS           , 0,
       CP1_                },        /* SWC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000647, &NMD::LDXS             , 0,
       MIPS64_             },        /* LDXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200006c7, &NMD::SDXS             , 0,
       MIPS64_             },        /* SDXS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x20000747, &NMD::LDC1XS           , 0,
       CP1_                },        /* LDC1XS */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0x200007c7, &NMD::SDC1XS           , 0,
       CP1_                },        /* SDC1XS */
};


NMD::Pool NMD::P_LSX[2] = {
    { pool                , PP_LSX              , 16  , 32,
       0xfc00007f, 0x20000007, 0                      , 0,
       0x0                 },        /* PP.LSX */
    { pool                , PP_LSXS             , 16  , 32,
       0xfc00007f, 0x20000047, 0                      , 0,
       0x0                 },        /* PP.LSXS */
};


NMD::Pool NMD::POOL32Axf_1_0[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000007f, &NMD::MFHI_DSP_        , 0,
       DSP_                },        /* MFHI[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000107f, &NMD::MFLO_DSP_        , 0,
       DSP_                },        /* MFLO[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000207f, &NMD::MTHI_DSP_        , 0,
       DSP_                },        /* MTHI[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000307f, &NMD::MTLO_DSP_        , 0,
       DSP_                },        /* MTLO[DSP] */
};


NMD::Pool NMD::POOL32Axf_1_1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000027f, &NMD::MTHLIP           , 0,
       DSP_                },        /* MTHLIP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000127f, &NMD::SHILOV           , 0,
       DSP_                },        /* SHILOV */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000227f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_1~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x2000327f, 0                      , 0,
       0x0                 },        /* POOL32Axf_1_1~*(3) */
};


NMD::Pool NMD::POOL32Axf_1_3[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000067f, &NMD::RDDSP            , 0,
       DSP_                },        /* RDDSP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000167f, &NMD::WRDSP            , 0,
       DSP_                },        /* WRDSP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000267f, &NMD::EXTP             , 0,
       DSP_                },        /* EXTP */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x2000367f, &NMD::EXTPDP           , 0,
       DSP_                },        /* EXTPDP */
};


NMD::Pool NMD::POOL32Axf_1_4[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000087f, &NMD::SHLL_QB          , 0,
       DSP_                },        /* SHLL.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x2000187f, &NMD::SHRL_QB          , 0,
       DSP_                },        /* SHRL.QB */
};


NMD::Pool NMD::MAQ_S_A__W_PHR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000a7f, &NMD::MAQ_S_W_PHR      , 0,
       DSP_                },        /* MAQ_S.W.PHR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002a7f, &NMD::MAQ_SA_W_PHR     , 0,
       DSP_                },        /* MAQ_SA.W.PHR */
};


NMD::Pool NMD::MAQ_S_A__W_PHL[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001a7f, &NMD::MAQ_S_W_PHL      , 0,
       DSP_                },        /* MAQ_S.W.PHL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003a7f, &NMD::MAQ_SA_W_PHL     , 0,
       DSP_                },        /* MAQ_SA.W.PHL */
};


NMD::Pool NMD::POOL32Axf_1_5[2] = {
    { pool                , MAQ_S_A__W_PHR      , 2   , 32,
       0xfc001fff, 0x20000a7f, 0                      , 0,
       0x0                 },        /* MAQ_S[A].W.PHR */
    { pool                , MAQ_S_A__W_PHL      , 2   , 32,
       0xfc001fff, 0x20001a7f, 0                      , 0,
       0x0                 },        /* MAQ_S[A].W.PHL */
};


NMD::Pool NMD::POOL32Axf_1_7[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000e7f, &NMD::EXTR_W           , 0,
       DSP_                },        /* EXTR.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001e7f, &NMD::EXTR_R_W         , 0,
       DSP_                },        /* EXTR_R.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002e7f, &NMD::EXTR_RS_W        , 0,
       DSP_                },        /* EXTR_RS.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003e7f, &NMD::EXTR_S_H         , 0,
       DSP_                },        /* EXTR_S.H */
};


NMD::Pool NMD::POOL32Axf_1[8] = {
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


NMD::Pool NMD::POOL32Axf_2_DSP__0_7[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200000bf, &NMD::DPA_W_PH         , 0,
       DSP_                },        /* DPA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200002bf, &NMD::DPAQ_S_W_PH      , 0,
       DSP_                },        /* DPAQ_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200004bf, &NMD::DPS_W_PH         , 0,
       DSP_                },        /* DPS.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200006bf, &NMD::DPSQ_S_W_PH      , 0,
       DSP_                },        /* DPSQ_S.W.PH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200008bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_0_7~*(4) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000abf, &NMD::MADD_DSP_        , 0,
       DSP_                },        /* MADD[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000cbf, &NMD::MULT_DSP_        , 0,
       DSP_                },        /* MULT[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20000ebf, &NMD::EXTRV_W          , 0,
       DSP_                },        /* EXTRV.W */
};


NMD::Pool NMD::POOL32Axf_2_DSP__8_15[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200010bf, &NMD::DPAX_W_PH        , 0,
       DSP_                },        /* DPAX.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200012bf, &NMD::DPAQ_SA_L_W      , 0,
       DSP_                },        /* DPAQ_SA.L.W */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200014bf, &NMD::DPSX_W_PH        , 0,
       DSP_                },        /* DPSX.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200016bf, &NMD::DPSQ_SA_L_W      , 0,
       DSP_                },        /* DPSQ_SA.L.W */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0x200018bf, 0                      , 0,
       0x0                 },        /* POOL32Axf_2(DSP)_8_15~*(4) */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001abf, &NMD::MADDU_DSP_       , 0,
       DSP_                },        /* MADDU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001cbf, &NMD::MULTU_DSP_       , 0,
       DSP_                },        /* MULTU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20001ebf, &NMD::EXTRV_R_W        , 0,
       DSP_                },        /* EXTRV_R.W */
};


NMD::Pool NMD::POOL32Axf_2_DSP__16_23[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200020bf, &NMD::DPAU_H_QBL       , 0,
       DSP_                },        /* DPAU.H.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200022bf, &NMD::DPAQX_S_W_PH     , 0,
       DSP_                },        /* DPAQX_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200024bf, &NMD::DPSU_H_QBL       , 0,
       DSP_                },        /* DPSU.H.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200026bf, &NMD::DPSQX_S_W_PH     , 0,
       DSP_                },        /* DPSQX_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200028bf, &NMD::EXTPV            , 0,
       DSP_                },        /* EXTPV */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002abf, &NMD::MSUB_DSP_        , 0,
       DSP_                },        /* MSUB[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002cbf, &NMD::MULSA_W_PH       , 0,
       DSP_                },        /* MULSA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20002ebf, &NMD::EXTRV_RS_W       , 0,
       DSP_                },        /* EXTRV_RS.W */
};


NMD::Pool NMD::POOL32Axf_2_DSP__24_31[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200030bf, &NMD::DPAU_H_QBR       , 0,
       DSP_                },        /* DPAU.H.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200032bf, &NMD::DPAQX_SA_W_PH    , 0,
       DSP_                },        /* DPAQX_SA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200034bf, &NMD::DPSU_H_QBR       , 0,
       DSP_                },        /* DPSU.H.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200036bf, &NMD::DPSQX_SA_W_PH    , 0,
       DSP_                },        /* DPSQX_SA.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x200038bf, &NMD::EXTPDPV          , 0,
       DSP_                },        /* EXTPDPV */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003abf, &NMD::MSUBU_DSP_       , 0,
       DSP_                },        /* MSUBU[DSP] */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003cbf, &NMD::MULSAQ_S_W_PH    , 0,
       DSP_                },        /* MULSAQ_S.W.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0x20003ebf, &NMD::EXTRV_S_H        , 0,
       DSP_                },        /* EXTRV_S.H */
};


NMD::Pool NMD::POOL32Axf_2[4] = {
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


NMD::Pool NMD::POOL32Axf_4[128] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000013f, &NMD::ABSQ_S_QB        , 0,
       DSP_                },        /* ABSQ_S.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000033f, &NMD::REPLV_PH         , 0,
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
       0xfc00ffff, 0x2000113f, &NMD::ABSQ_S_PH        , 0,
       DSP_                },        /* ABSQ_S.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000133f, &NMD::REPLV_QB         , 0,
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
       0xfc00ffff, 0x2000213f, &NMD::ABSQ_S_W         , 0,
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
       0xfc00ffff, 0x2000413f, &NMD::INSV             , 0,
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
       0xfc00ffff, 0x20004b3f, &NMD::CLO              , 0,
       XMMS_               },        /* CLO */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20004d3f, &NMD::MFC2             , 0,
       CP2_                },        /* MFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20004f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(39) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000513f, &NMD::PRECEQ_W_PHL     , 0,
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
       0xfc00ffff, 0x20005b3f, &NMD::CLZ              , 0,
       XMMS_               },        /* CLZ */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x20005d3f, &NMD::MTC2             , 0,
       CP2_                },        /* MTC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20005f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(47) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000613f, &NMD::PRECEQ_W_PHR     , 0,
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
       0xfc00ffff, 0x20006d3f, &NMD::DMFC2            , 0,
       CP2_                },        /* DMFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20006f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(55) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000713f, &NMD::PRECEQU_PH_QBL   , 0,
       DSP_                },        /* PRECEQU.PH.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000733f, &NMD::PRECEQU_PH_QBLA  , 0,
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
       0xfc00ffff, 0x20007d3f, &NMD::DMTC2            , 0,
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
       0xfc00ffff, 0x20008d3f, &NMD::MFHC2            , 0,
       CP2_                },        /* MFHC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x20008f3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(71) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000913f, &NMD::PRECEQU_PH_QBR   , 0,
       DSP_                },        /* PRECEQU.PH.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000933f, &NMD::PRECEQU_PH_QBRA  , 0,
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
       0xfc00ffff, 0x20009d3f, &NMD::MTHC2            , 0,
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
       0xfc00ffff, 0x2000b13f, &NMD::PRECEU_PH_QBL    , 0,
       DSP_                },        /* PRECEU.PH.QBL */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000b33f, &NMD::PRECEU_PH_QBLA   , 0,
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
       0xfc00ffff, 0x2000cd3f, &NMD::CFC2             , 0,
       CP2_                },        /* CFC2 */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000cf3f, 0                      , 0,
       0x0                 },        /* POOL32Axf_4~*(103) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d13f, &NMD::PRECEU_PH_QBR    , 0,
       DSP_                },        /* PRECEU.PH.QBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000d33f, &NMD::PRECEU_PH_QBRA   , 0,
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
       0xfc00ffff, 0x2000dd3f, &NMD::CTC2             , 0,
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
       0xfc00ffff, 0x2000f13f, &NMD::RADDU_W_QB       , 0,
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


NMD::Pool NMD::POOL32Axf_5_group0[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000017f, &NMD::TLBGP            , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000037f, &NMD::TLBP             , 0,
       CP0_ | TLB_         },        /* TLBP */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000057f, &NMD::TLBGINV          , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},        /* TLBGINV */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000077f, &NMD::TLBINV           , 0,
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
       0xfc00ffff, 0x2000117f, &NMD::TLBGR            , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000137f, &NMD::TLBR             , 0,
       CP0_ | TLB_         },        /* TLBR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000157f, &NMD::TLBGINVF         , 0,
       CP0_ | VZ_ | TLB_ | TLBINV_},        /* TLBGINVF */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000177f, &NMD::TLBINVF          , 0,
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
       0xfc00ffff, 0x2000217f, &NMD::TLBGWI           , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGWI */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000237f, &NMD::TLBWI            , 0,
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
       0xfc00ffff, 0x2000317f, &NMD::TLBGWR           , 0,
       CP0_ | VZ_ | TLB_   },        /* TLBGWR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000337f, &NMD::TLBWR            , 0,
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


NMD::Pool NMD::POOL32Axf_5_group1[32] = {
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
       0xfc00ffff, 0x2000477f, &NMD::DI               , 0,
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
       0xfc00ffff, 0x2000577f, &NMD::EI               , 0,
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


NMD::Pool NMD::ERETx[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2000f37f, &NMD::ERET             , 0,
       0x0                 },        /* ERET */
    { instruction         , 0                   , 0   , 32,
       0xfc01ffff, 0x2001f37f, &NMD::ERETNC           , 0,
       0x0                 },        /* ERETNC */
};


NMD::Pool NMD::POOL32Axf_5_group3[32] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c17f, 0                      , 0,
       0x0                 },        /* POOL32Axf_5_group3~*(0) */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000c37f, &NMD::WAIT             , 0,
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
       0xfc00ffff, 0x2000d37f, &NMD::IRET             , 0,
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
       0xfc00ffff, 0x2000e17f, &NMD::RDPGPR           , 0,
       CP0_                },        /* RDPGPR */
    { instruction         , 0                   , 0   , 32,
       0xfc00ffff, 0x2000e37f, &NMD::DERET            , 0,
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
       0xfc00ffff, 0x2000f17f, &NMD::WRPGPR           , 0,
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


NMD::Pool NMD::POOL32Axf_5[4] = {
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


NMD::Pool NMD::SHRA__R__QB[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200001ff, &NMD::SHRA_QB          , 0,
       DSP_                },        /* SHRA.QB */
    { instruction         , 0                   , 0   , 32,
       0xfc001fff, 0x200011ff, &NMD::SHRA_R_QB        , 0,
       DSP_                },        /* SHRA_R.QB */
};


NMD::Pool NMD::POOL32Axf_7[8] = {
    { pool                , SHRA__R__QB         , 2   , 32,
       0xfc000fff, 0x200001ff, 0                      , 0,
       0x0                 },        /* SHRA[_R].QB */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200003ff, &NMD::SHRL_PH          , 0,
       DSP_                },        /* SHRL.PH */
    { instruction         , 0                   , 0   , 32,
       0xfc000fff, 0x200005ff, &NMD::REPL_QB          , 0,
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


NMD::Pool NMD::POOL32Axf[8] = {
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


NMD::Pool NMD::_POOL32A7[8] = {
    { pool                , P_LSX               , 2   , 32,
       0xfc00003f, 0x20000007, 0                      , 0,
       0x0                 },        /* P.LSX */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000000f, &NMD::LSA              , 0,
       0x0                 },        /* LSA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00003f, 0x20000017, 0                      , 0,
       0x0                 },        /* _POOL32A7~*(2) */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0x2000001f, &NMD::EXTW             , 0,
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


NMD::Pool NMD::P32A[8] = {
    { pool                , _POOL32A0           , 128 , 32,
       0xfc000007, 0x20000000, 0                      , 0,
       0x0                 },        /* _POOL32A0 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000001, &NMD::SPECIAL2         , 0,
       UDI_                },        /* SPECIAL2 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000002, &NMD::COP2_1           , 0,
       CP2_                },        /* COP2_1 */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x20000003, &NMD::UDI              , 0,
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


NMD::Pool NMD::P_GP_D[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000001, &NMD::LD_GP_           , 0,
       MIPS64_             },        /* LD[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc000007, 0x40000005, &NMD::SD_GP_           , 0,
       MIPS64_             },        /* SD[GP] */
};


NMD::Pool NMD::P_GP_W[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000000, &NMD::ADDIU_GP_W_      , 0,
       0x0                 },        /* ADDIU[GP.W] */
    { pool                , P_GP_D              , 2   , 32,
       0xfc000003, 0x40000001, 0                      , 0,
       0x0                 },        /* P.GP.D */
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000002, &NMD::LW_GP_           , 0,
       0x0                 },        /* LW[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc000003, 0x40000003, &NMD::SW_GP_           , 0,
       0x0                 },        /* SW[GP] */
};


NMD::Pool NMD::POOL48I[32] = {
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600000000000ull, &NMD::LI_48_           , 0,
       XMMS_               },        /* LI[48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600100000000ull, &NMD::ADDIU_48_        , 0,
       XMMS_               },        /* ADDIU[48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600200000000ull, &NMD::ADDIU_GP48_      , 0,
       XMMS_               },        /* ADDIU[GP48] */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x600300000000ull, &NMD::ADDIUPC_48_      , 0,
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
       0xfc1f00000000ull, 0x600b00000000ull, &NMD::LWPC_48_         , 0,
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
       0xfc1f00000000ull, 0x600f00000000ull, &NMD::SWPC_48_         , 0,
       XMMS_               },        /* SWPC[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601000000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(16) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601100000000ull, &NMD::DADDIU_48_       , 0,
       MIPS64_             },        /* DADDIU[48] */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601200000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(18) */
    { reserved_block      , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601300000000ull, 0                      , 0,
       0x0                 },        /* POOL48I~*(19) */
    { instruction         , 0                   , 0   , 48,
       0xfc1f00000000ull, 0x601400000000ull, &NMD::DLUI_48_         , 0,
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
       0xfc1f00000000ull, 0x601b00000000ull, &NMD::LDPC_48_         , 0,
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
       0xfc1f00000000ull, 0x601f00000000ull, &NMD::SDPC_48_         , 0,
       MIPS64_             },        /* SDPC[48] */
};


NMD::Pool NMD::PP_SR[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003000, &NMD::SAVE_32_         , 0,
       0x0                 },        /* SAVE[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc10f003, 0x80003001, 0                      , 0,
       0x0                 },        /* PP.SR~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc10f003, 0x80003002, &NMD::RESTORE_32_      , 0,
       0x0                 },        /* RESTORE[32] */
    { return_instruction  , 0                   , 0   , 32,
       0xfc10f003, 0x80003003, &NMD::RESTORE_JRC_32_  , 0,
       0x0                 },        /* RESTORE.JRC[32] */
};


NMD::Pool NMD::P_SR_F[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103000, &NMD::SAVEF            , 0,
       CP1_                },        /* SAVEF */
    { instruction         , 0                   , 0   , 32,
       0xfc10f007, 0x80103001, &NMD::RESTOREF         , 0,
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


NMD::Pool NMD::P_SR[2] = {
    { pool                , PP_SR               , 4   , 32,
       0xfc10f000, 0x80003000, 0                      , 0,
       0x0                 },        /* PP.SR */
    { pool                , P_SR_F              , 8   , 32,
       0xfc10f000, 0x80103000, 0                      , 0,
       0x0                 },        /* P.SR.F */
};


NMD::Pool NMD::P_SLL[5] = {
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c000, &NMD::NOP_32_          , 0,
       0x0                 },        /* NOP[32] */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c003, &NMD::EHB              , 0,
       0x0                 },        /* EHB */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c005, &NMD::PAUSE            , 0,
       0x0                 },        /* PAUSE */
    { instruction         , 0                   , 0   , 32,
       0xffe0f1ff, 0x8000c006, &NMD::SYNC             , 0,
       0x0                 },        /* SYNC */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c000, &NMD::SLL_32_          , 0,
       0x0                 },        /* SLL[32] */
};


NMD::Pool NMD::P_SHIFT[16] = {
    { pool                , P_SLL               , 5   , 32,
       0xfc00f1e0, 0x8000c000, 0                      , 0,
       0x0                 },        /* P.SLL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c020, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(1) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c040, &NMD::SRL_32_          , 0,
       0x0                 },        /* SRL[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c060, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(3) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c080, &NMD::SRA              , 0,
       0x0                 },        /* SRA */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0a0, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(5) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0c0, &NMD::ROTR             , 0,
       0x0                 },        /* ROTR */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c0e0, 0                      , 0,
       0x0                 },        /* P.SHIFT~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c100, &NMD::DSLL             , 0,
       MIPS64_             },        /* DSLL */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c120, &NMD::DSLL32           , 0,
       MIPS64_             },        /* DSLL32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c140, &NMD::DSRL             , 0,
       MIPS64_             },        /* DSRL */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c160, &NMD::DSRL32           , 0,
       MIPS64_             },        /* DSRL32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c180, &NMD::DSRA             , 0,
       MIPS64_             },        /* DSRA */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1a0, &NMD::DSRA32           , 0,
       MIPS64_             },        /* DSRA32 */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1c0, &NMD::DROTR            , 0,
       MIPS64_             },        /* DROTR */
    { instruction         , 0                   , 0   , 32,
       0xfc00f1e0, 0x8000c1e0, &NMD::DROTR32          , 0,
       MIPS64_             },        /* DROTR32 */
};


NMD::Pool NMD::P_ROTX[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000d000, &NMD::ROTX             , 0,
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


NMD::Pool NMD::P_INS[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e000, &NMD::INS              , 0,
       XMMS_               },        /* INS */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e020, &NMD::DINSU            , 0,
       MIPS64_             },        /* DINSU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e800, &NMD::DINSM            , 0,
       MIPS64_             },        /* DINSM */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000e820, &NMD::DINS             , 0,
       MIPS64_             },        /* DINS */
};


NMD::Pool NMD::P_EXT[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f000, &NMD::EXT              , 0,
       XMMS_               },        /* EXT */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f020, &NMD::DEXTU            , 0,
       MIPS64_             },        /* DEXTU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f800, &NMD::DEXTM            , 0,
       MIPS64_             },        /* DEXTM */
    { instruction         , 0                   , 0   , 32,
       0xfc00f820, 0x8000f820, &NMD::DEXT             , 0,
       MIPS64_             },        /* DEXT */
};


NMD::Pool NMD::P_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80000000, &NMD::ORI              , 0,
       0x0                 },        /* ORI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80001000, &NMD::XORI             , 0,
       0x0                 },        /* XORI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80002000, &NMD::ANDI_32_         , 0,
       0x0                 },        /* ANDI[32] */
    { pool                , P_SR                , 2   , 32,
       0xfc00f000, 0x80003000, 0                      , 0,
       0x0                 },        /* P.SR */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80004000, &NMD::SLTI             , 0,
       0x0                 },        /* SLTI */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80005000, &NMD::SLTIU            , 0,
       0x0                 },        /* SLTIU */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80006000, &NMD::SEQI             , 0,
       0x0                 },        /* SEQI */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00f000, 0x80007000, 0                      , 0,
       0x0                 },        /* P.U12~*(7) */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80008000, &NMD::ADDIU_NEG_       , 0,
       0x0                 },        /* ADDIU[NEG] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x80009000, &NMD::DADDIU_U12_      , 0,
       MIPS64_             },        /* DADDIU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000a000, &NMD::DADDIU_NEG_      , 0,
       MIPS64_             },        /* DADDIU[NEG] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8000b000, &NMD::DROTX            , 0,
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


NMD::Pool NMD::RINT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000020, &NMD::RINT_S           , 0,
       CP1_                },        /* RINT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000220, &NMD::RINT_D           , 0,
       CP1_                },        /* RINT.D */
};


NMD::Pool NMD::ADD_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000030, &NMD::ADD_S            , 0,
       CP1_                },        /* ADD.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000230, 0                      , 0,
       CP1_                },        /* ADD.fmt0~*(1) */
};


NMD::Pool NMD::SELEQZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000038, &NMD::SELEQZ_S         , 0,
       CP1_                },        /* SELEQZ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000238, &NMD::SELEQZ_D         , 0,
       CP1_                },        /* SELEQZ.D */
};


NMD::Pool NMD::CLASS_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000060, &NMD::CLASS_S          , 0,
       CP1_                },        /* CLASS.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000260, &NMD::CLASS_D          , 0,
       CP1_                },        /* CLASS.D */
};


NMD::Pool NMD::SUB_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000070, &NMD::SUB_S            , 0,
       CP1_                },        /* SUB.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000270, 0                      , 0,
       CP1_                },        /* SUB.fmt0~*(1) */
};


NMD::Pool NMD::SELNEZ_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000078, &NMD::SELNEZ_S         , 0,
       CP1_                },        /* SELNEZ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000278, &NMD::SELNEZ_D         , 0,
       CP1_                },        /* SELNEZ.D */
};


NMD::Pool NMD::MUL_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b0, &NMD::MUL_S            , 0,
       CP1_                },        /* MUL.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b0, 0                      , 0,
       CP1_                },        /* MUL.fmt0~*(1) */
};


NMD::Pool NMD::SEL_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000b8, &NMD::SEL_S            , 0,
       CP1_                },        /* SEL.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002b8, &NMD::SEL_D            , 0,
       CP1_                },        /* SEL.D */
};


NMD::Pool NMD::DIV_fmt0[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00000f0, &NMD::DIV_S            , 0,
       CP1_                },        /* DIV.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00002f0, 0                      , 0,
       CP1_                },        /* DIV.fmt0~*(1) */
};


NMD::Pool NMD::ADD_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000130, &NMD::ADD_D            , 0,
       CP1_                },        /* ADD.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000330, 0                      , 0,
       CP1_                },        /* ADD.fmt1~*(1) */
};


NMD::Pool NMD::SUB_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000170, &NMD::SUB_D            , 0,
       CP1_                },        /* SUB.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa0000370, 0                      , 0,
       CP1_                },        /* SUB.fmt1~*(1) */
};


NMD::Pool NMD::MUL_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b0, &NMD::MUL_D            , 0,
       CP1_                },        /* MUL.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b0, 0                      , 0,
       CP1_                },        /* MUL.fmt1~*(1) */
};


NMD::Pool NMD::MADDF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001b8, &NMD::MADDF_S          , 0,
       CP1_                },        /* MADDF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003b8, &NMD::MADDF_D          , 0,
       CP1_                },        /* MADDF.D */
};


NMD::Pool NMD::DIV_fmt1[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f0, &NMD::DIV_D            , 0,
       CP1_                },        /* DIV.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f0, 0                      , 0,
       CP1_                },        /* DIV.fmt1~*(1) */
};


NMD::Pool NMD::MSUBF_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00001f8, &NMD::MSUBF_S          , 0,
       CP1_                },        /* MSUBF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0003ff, 0xa00003f8, &NMD::MSUBF_D          , 0,
       CP1_                },        /* MSUBF.D */
};


NMD::Pool NMD::POOL32F_0[64] = {
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


NMD::Pool NMD::MIN_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000003, &NMD::MIN_S            , 0,
       CP1_                },        /* MIN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000203, &NMD::MIN_D            , 0,
       CP1_                },        /* MIN.D */
};


NMD::Pool NMD::MAX_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000000b, &NMD::MAX_S            , 0,
       CP1_                },        /* MAX.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000020b, &NMD::MAX_D            , 0,
       CP1_                },        /* MAX.D */
};


NMD::Pool NMD::MINA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000023, &NMD::MINA_S           , 0,
       CP1_                },        /* MINA.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa0000223, &NMD::MINA_D           , 0,
       CP1_                },        /* MINA.D */
};


NMD::Pool NMD::MAXA_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000002b, &NMD::MAXA_S           , 0,
       CP1_                },        /* MAXA.S */
    { instruction         , 0                   , 0   , 32,
       0xfc00023f, 0xa000022b, &NMD::MAXA_D           , 0,
       CP1_                },        /* MAXA.D */
};


NMD::Pool NMD::CVT_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000013b, &NMD::CVT_L_S          , 0,
       CP1_                },        /* CVT.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000413b, &NMD::CVT_L_D          , 0,
       CP1_                },        /* CVT.L.D */
};


NMD::Pool NMD::RSQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000023b, &NMD::RSQRT_S          , 0,
       CP1_                },        /* RSQRT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000423b, &NMD::RSQRT_D          , 0,
       CP1_                },        /* RSQRT.D */
};


NMD::Pool NMD::FLOOR_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000033b, &NMD::FLOOR_L_S        , 0,
       CP1_                },        /* FLOOR.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000433b, &NMD::FLOOR_L_D        , 0,
       CP1_                },        /* FLOOR.L.D */
};


NMD::Pool NMD::CVT_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000093b, &NMD::CVT_W_S          , 0,
       CP1_                },        /* CVT.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000493b, &NMD::CVT_W_D          , 0,
       CP1_                },        /* CVT.W.D */
};


NMD::Pool NMD::SQRT_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000a3b, &NMD::SQRT_S           , 0,
       CP1_                },        /* SQRT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004a3b, &NMD::SQRT_D           , 0,
       CP1_                },        /* SQRT.D */
};


NMD::Pool NMD::FLOOR_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b3b, &NMD::FLOOR_W_S        , 0,
       CP1_                },        /* FLOOR.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b3b, &NMD::FLOOR_W_D        , 0,
       CP1_                },        /* FLOOR.W.D */
};


NMD::Pool NMD::RECIP_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000123b, &NMD::RECIP_S          , 0,
       CP1_                },        /* RECIP.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000523b, &NMD::RECIP_D          , 0,
       CP1_                },        /* RECIP.D */
};


NMD::Pool NMD::CEIL_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000133b, &NMD::CEIL_L_S         , 0,
       CP1_                },        /* CEIL.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000533b, &NMD::CEIL_L_D         , 0,
       CP1_                },        /* CEIL.L.D */
};


NMD::Pool NMD::CEIL_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b3b, &NMD::CEIL_W_S         , 0,
       CP1_                },        /* CEIL.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b3b, &NMD::CEIL_W_D         , 0,
       CP1_                },        /* CEIL.W.D */
};


NMD::Pool NMD::TRUNC_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000233b, &NMD::TRUNC_L_S        , 0,
       CP1_                },        /* TRUNC.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000633b, &NMD::TRUNC_L_D        , 0,
       CP1_                },        /* TRUNC.L.D */
};


NMD::Pool NMD::TRUNC_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b3b, &NMD::TRUNC_W_S        , 0,
       CP1_                },        /* TRUNC.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b3b, &NMD::TRUNC_W_D        , 0,
       CP1_                },        /* TRUNC.W.D */
};


NMD::Pool NMD::ROUND_L_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000333b, &NMD::ROUND_L_S        , 0,
       CP1_                },        /* ROUND.L.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000733b, &NMD::ROUND_L_D        , 0,
       CP1_                },        /* ROUND.L.D */
};


NMD::Pool NMD::ROUND_W_fmt[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b3b, &NMD::ROUND_W_S        , 0,
       CP1_                },        /* ROUND.W.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b3b, &NMD::ROUND_W_D        , 0,
       CP1_                },        /* ROUND.W.D */
};


NMD::Pool NMD::POOL32Fxf_0[64] = {
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
       0xfc003fff, 0xa000103b, &NMD::CFC1             , 0,
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
       0xfc003fff, 0xa000183b, &NMD::CTC1             , 0,
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
       0xfc003fff, 0xa000203b, &NMD::MFC1             , 0,
       CP1_                },        /* MFC1 */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000213b, &NMD::CVT_S_PL         , 0,
       CP1_                },        /* CVT.S.PL */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa000223b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(34) */
    { pool                , TRUNC_L_fmt         , 2   , 32,
       0xfc003fff, 0xa000233b, 0                      , 0,
       CP1_                },        /* TRUNC.L.fmt */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000243b, &NMD::DMFC1            , 0,
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
       0xfc003fff, 0xa000283b, &NMD::MTC1             , 0,
       CP1_                },        /* MTC1 */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa000293b, &NMD::CVT_S_PU         , 0,
       CP1_                },        /* CVT.S.PU */
    { reserved_block      , 0                   , 0   , 32,
       0xfc003fff, 0xa0002a3b, 0                      , 0,
       CP1_                },        /* POOL32Fxf_0~*(42) */
    { pool                , TRUNC_W_fmt         , 2   , 32,
       0xfc003fff, 0xa0002b3b, 0                      , 0,
       CP1_                },        /* TRUNC.W.fmt */
    { instruction         , 0                   , 0   , 32,
       0xfc003fff, 0xa0002c3b, &NMD::DMTC1            , 0,
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
       0xfc003fff, 0xa000303b, &NMD::MFHC1            , 0,
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
       0xfc003fff, 0xa000383b, &NMD::MTHC1            , 0,
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


NMD::Pool NMD::MOV_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000007b, &NMD::MOV_S            , 0,
       CP1_                },        /* MOV.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000207b, &NMD::MOV_D            , 0,
       CP1_                },        /* MOV.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000407b, 0                      , 0,
       CP1_                },        /* MOV.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000607b, 0                      , 0,
       CP1_                },        /* MOV.fmt~*(3) */
};


NMD::Pool NMD::ABS_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000037b, &NMD::ABS_S            , 0,
       CP1_                },        /* ABS.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000237b, &NMD::ABS_D            , 0,
       CP1_                },        /* ABS.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000437b, 0                      , 0,
       CP1_                },        /* ABS.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000637b, 0                      , 0,
       CP1_                },        /* ABS.fmt~*(3) */
};


NMD::Pool NMD::NEG_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0000b7b, &NMD::NEG_S            , 0,
       CP1_                },        /* NEG.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0002b7b, &NMD::NEG_D            , 0,
       CP1_                },        /* NEG.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0004b7b, 0                      , 0,
       CP1_                },        /* NEG.fmt~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0006b7b, 0                      , 0,
       CP1_                },        /* NEG.fmt~*(3) */
};


NMD::Pool NMD::CVT_D_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000137b, &NMD::CVT_D_S          , 0,
       CP1_                },        /* CVT.D.S */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000337b, &NMD::CVT_D_W          , 0,
       CP1_                },        /* CVT.D.W */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa000537b, &NMD::CVT_D_L          , 0,
       CP1_                },        /* CVT.D.L */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa000737b, 0                      , 0,
       CP1_                },        /* CVT.D.fmt~*(3) */
};


NMD::Pool NMD::CVT_S_fmt[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0001b7b, &NMD::CVT_S_D          , 0,
       CP1_                },        /* CVT.S.D */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0003b7b, &NMD::CVT_S_W          , 0,
       CP1_                },        /* CVT.S.W */
    { instruction         , 0                   , 0   , 32,
       0xfc007fff, 0xa0005b7b, &NMD::CVT_S_L          , 0,
       CP1_                },        /* CVT.S.L */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007fff, 0xa0007b7b, 0                      , 0,
       CP1_                },        /* CVT.S.fmt~*(3) */
};


NMD::Pool NMD::POOL32Fxf_1[32] = {
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


NMD::Pool NMD::POOL32Fxf[4] = {
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


NMD::Pool NMD::POOL32F_3[8] = {
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


NMD::Pool NMD::CMP_condn_S[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000005, &NMD::CMP_AF_S         , 0,
       CP1_                },        /* CMP.AF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000045, &NMD::CMP_UN_S         , 0,
       CP1_                },        /* CMP.UN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000085, &NMD::CMP_EQ_S         , 0,
       CP1_                },        /* CMP.EQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000c5, &NMD::CMP_UEQ_S        , 0,
       CP1_                },        /* CMP.UEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000105, &NMD::CMP_LT_S         , 0,
       CP1_                },        /* CMP.LT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000145, &NMD::CMP_ULT_S        , 0,
       CP1_                },        /* CMP.ULT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000185, &NMD::CMP_LE_S         , 0,
       CP1_                },        /* CMP.LE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001c5, &NMD::CMP_ULE_S        , 0,
       CP1_                },        /* CMP.ULE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000205, &NMD::CMP_SAF_S        , 0,
       CP1_                },        /* CMP.SAF.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000245, &NMD::CMP_SUN_S        , 0,
       CP1_                },        /* CMP.SUN.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000285, &NMD::CMP_SEQ_S        , 0,
       CP1_                },        /* CMP.SEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002c5, &NMD::CMP_SUEQ_S       , 0,
       CP1_                },        /* CMP.SUEQ.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000305, &NMD::CMP_SLT_S        , 0,
       CP1_                },        /* CMP.SLT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000345, &NMD::CMP_SULT_S       , 0,
       CP1_                },        /* CMP.SULT.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000385, &NMD::CMP_SLE_S        , 0,
       CP1_                },        /* CMP.SLE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003c5, &NMD::CMP_SULE_S       , 0,
       CP1_                },        /* CMP.SULE.S */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000405, 0                      , 0,
       CP1_                },        /* CMP.condn.S~*(16) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000445, &NMD::CMP_OR_S         , 0,
       CP1_                },        /* CMP.OR.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000485, &NMD::CMP_UNE_S        , 0,
       CP1_                },        /* CMP.UNE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004c5, &NMD::CMP_NE_S         , 0,
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
       0xfc0007ff, 0xa0000645, &NMD::CMP_SOR_S        , 0,
       CP1_                },        /* CMP.SOR.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000685, &NMD::CMP_SUNE_S       , 0,
       CP1_                },        /* CMP.SUNE.S */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006c5, &NMD::CMP_SNE_S        , 0,
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


NMD::Pool NMD::CMP_condn_D[32] = {
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000015, &NMD::CMP_AF_D         , 0,
       CP1_                },        /* CMP.AF.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000055, &NMD::CMP_UN_D         , 0,
       CP1_                },        /* CMP.UN.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000095, &NMD::CMP_EQ_D         , 0,
       CP1_                },        /* CMP.EQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00000d5, &NMD::CMP_UEQ_D        , 0,
       CP1_                },        /* CMP.UEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000115, &NMD::CMP_LT_D         , 0,
       CP1_                },        /* CMP.LT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000155, &NMD::CMP_ULT_D        , 0,
       CP1_                },        /* CMP.ULT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000195, &NMD::CMP_LE_D         , 0,
       CP1_                },        /* CMP.LE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00001d5, &NMD::CMP_ULE_D        , 0,
       CP1_                },        /* CMP.ULE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000215, &NMD::CMP_SAF_D        , 0,
       CP1_                },        /* CMP.SAF.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000255, &NMD::CMP_SUN_D        , 0,
       CP1_                },        /* CMP.SUN.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000295, &NMD::CMP_SEQ_D        , 0,
       CP1_                },        /* CMP.SEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00002d5, &NMD::CMP_SUEQ_D       , 0,
       CP1_                },        /* CMP.SUEQ.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000315, &NMD::CMP_SLT_D        , 0,
       CP1_                },        /* CMP.SLT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000355, &NMD::CMP_SULT_D       , 0,
       CP1_                },        /* CMP.SULT.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000395, &NMD::CMP_SLE_D        , 0,
       CP1_                },        /* CMP.SLE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00003d5, &NMD::CMP_SULE_D       , 0,
       CP1_                },        /* CMP.SULE.D */
    { reserved_block      , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000415, 0                      , 0,
       CP1_                },        /* CMP.condn.D~*(16) */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000455, &NMD::CMP_OR_D         , 0,
       CP1_                },        /* CMP.OR.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000495, &NMD::CMP_UNE_D        , 0,
       CP1_                },        /* CMP.UNE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00004d5, &NMD::CMP_NE_D         , 0,
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
       0xfc0007ff, 0xa0000655, &NMD::CMP_SOR_D        , 0,
       CP1_                },        /* CMP.SOR.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa0000695, &NMD::CMP_SUNE_D       , 0,
       CP1_                },        /* CMP.SUNE.D */
    { instruction         , 0                   , 0   , 32,
       0xfc0007ff, 0xa00006d5, &NMD::CMP_SNE_D        , 0,
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


NMD::Pool NMD::POOL32F_5[8] = {
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


NMD::Pool NMD::POOL32F[8] = {
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


NMD::Pool NMD::POOL32S_0[64] = {
    { reserved_block      , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000000, 0                      , 0,
       0x0                 },        /* POOL32S_0~*(0) */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000008, &NMD::DLSA             , 0,
       MIPS64_             },        /* DLSA */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000010, &NMD::DSLLV            , 0,
       MIPS64_             },        /* DSLLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000018, &NMD::DMUL             , 0,
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
       0xfc0001ff, 0xc0000050, &NMD::DSRLV            , 0,
       MIPS64_             },        /* DSRLV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000058, &NMD::DMUH             , 0,
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
       0xfc0001ff, 0xc0000090, &NMD::DSRAV            , 0,
       MIPS64_             },        /* DSRAV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000098, &NMD::DMULU            , 0,
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
       0xfc0001ff, 0xc00000d0, &NMD::DROTRV           , 0,
       MIPS64_             },        /* DROTRV */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00000d8, &NMD::DMUHU            , 0,
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
       0xfc0001ff, 0xc0000110, &NMD::DADD             , 0,
       MIPS64_             },        /* DADD */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000118, &NMD::DDIV             , 0,
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
       0xfc0001ff, 0xc0000150, &NMD::DADDU            , 0,
       MIPS64_             },        /* DADDU */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000158, &NMD::DMOD             , 0,
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
       0xfc0001ff, 0xc0000190, &NMD::DSUB             , 0,
       MIPS64_             },        /* DSUB */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc0000198, &NMD::DDIVU            , 0,
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
       0xfc0001ff, 0xc00001d0, &NMD::DSUBU            , 0,
       MIPS64_             },        /* DSUBU */
    { instruction         , 0                   , 0   , 32,
       0xfc0001ff, 0xc00001d8, &NMD::DMODU            , 0,
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


NMD::Pool NMD::POOL32Sxf_4[128] = {
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
       0xfc00ffff, 0xc0004b3c, &NMD::DCLO             , 0,
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
       0xfc00ffff, 0xc0005b3c, &NMD::DCLZ             , 0,
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


NMD::Pool NMD::POOL32Sxf[8] = {
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


NMD::Pool NMD::POOL32S_4[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc0000004, &NMD::EXTD             , 0,
       MIPS64_             },        /* EXTD */
    { instruction         , 0                   , 0   , 32,
       0xfc00003f, 0xc000000c, &NMD::EXTD32           , 0,
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


NMD::Pool NMD::POOL32S[8] = {
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


NMD::Pool NMD::P_LUI[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000000, &NMD::LUI              , 0,
       0x0                 },        /* LUI */
    { instruction         , 0                   , 0   , 32,
       0xfc000002, 0xe0000002, &NMD::ALUIPC           , 0,
       0x0                 },        /* ALUIPC */
};


NMD::Pool NMD::P_GP_LH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100000, &NMD::LH_GP_           , 0,
       0x0                 },        /* LH[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44100001, &NMD::LHU_GP_          , 0,
       0x0                 },        /* LHU[GP] */
};


NMD::Pool NMD::P_GP_SH[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0001, 0x44140000, &NMD::SH_GP_           , 0,
       0x0                 },        /* SH[GP] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc1c0001, 0x44140001, 0                      , 0,
       0x0                 },        /* P.GP.SH~*(1) */
};


NMD::Pool NMD::P_GP_CP1[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180000, &NMD::LWC1_GP_         , 0,
       CP1_                },        /* LWC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180001, &NMD::SWC1_GP_         , 0,
       CP1_                },        /* SWC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180002, &NMD::LDC1_GP_         , 0,
       CP1_                },        /* LDC1[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x44180003, &NMD::SDC1_GP_         , 0,
       CP1_                },        /* SDC1[GP] */
};


NMD::Pool NMD::P_GP_M64[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0003, 0x441c0000, &NMD::LWU_GP_          , 0,
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


NMD::Pool NMD::P_GP_BH[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44000000, &NMD::LB_GP_           , 0,
       0x0                 },        /* LB[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44040000, &NMD::SB_GP_           , 0,
       0x0                 },        /* SB[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x44080000, &NMD::LBU_GP_          , 0,
       0x0                 },        /* LBU[GP] */
    { instruction         , 0                   , 0   , 32,
       0xfc1c0000, 0x440c0000, &NMD::ADDIU_GP_B_      , 0,
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


NMD::Pool NMD::P_LS_U12[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84000000, &NMD::LB_U12_          , 0,
       0x0                 },        /* LB[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84001000, &NMD::SB_U12_          , 0,
       0x0                 },        /* SB[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84002000, &NMD::LBU_U12_         , 0,
       0x0                 },        /* LBU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84003000, &NMD::PREF_U12_        , 0,
       0x0                 },        /* PREF[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84004000, &NMD::LH_U12_          , 0,
       0x0                 },        /* LH[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84005000, &NMD::SH_U12_          , 0,
       0x0                 },        /* SH[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84006000, &NMD::LHU_U12_         , 0,
       0x0                 },        /* LHU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84007000, &NMD::LWU_U12_         , 0,
       MIPS64_             },        /* LWU[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84008000, &NMD::LW_U12_          , 0,
       0x0                 },        /* LW[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x84009000, &NMD::SW_U12_          , 0,
       0x0                 },        /* SW[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400a000, &NMD::LWC1_U12_        , 0,
       CP1_                },        /* LWC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400b000, &NMD::SWC1_U12_        , 0,
       CP1_                },        /* SWC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400c000, &NMD::LD_U12_          , 0,
       MIPS64_             },        /* LD[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400d000, &NMD::SD_U12_          , 0,
       MIPS64_             },        /* SD[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400e000, &NMD::LDC1_U12_        , 0,
       CP1_                },        /* LDC1[U12] */
    { instruction         , 0                   , 0   , 32,
       0xfc00f000, 0x8400f000, &NMD::SDC1_U12_        , 0,
       CP1_                },        /* SDC1[U12] */
};


NMD::Pool NMD::P_PREF_S9_[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01800, &NMD::SYNCI            , 0,
       0x0                 },        /* SYNCI */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001800, &NMD::PREF_S9_         , &NMD::PREF_S9__cond    ,
       0x0                 },        /* PREF[S9] */
};


NMD::Pool NMD::P_LS_S0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000000, &NMD::LB_S9_           , 0,
       0x0                 },        /* LB[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000800, &NMD::SB_S9_           , 0,
       0x0                 },        /* SB[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001000, &NMD::LBU_S9_          , 0,
       0x0                 },        /* LBU[S9] */
    { pool                , P_PREF_S9_          , 2   , 32,
       0xfc007f00, 0xa4001800, 0                      , 0,
       0x0                 },        /* P.PREF[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002000, &NMD::LH_S9_           , 0,
       0x0                 },        /* LH[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002800, &NMD::SH_S9_           , 0,
       0x0                 },        /* SH[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003000, &NMD::LHU_S9_          , 0,
       0x0                 },        /* LHU[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003800, &NMD::LWU_S9_          , 0,
       MIPS64_             },        /* LWU[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004000, &NMD::LW_S9_           , 0,
       0x0                 },        /* LW[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004800, &NMD::SW_S9_           , 0,
       0x0                 },        /* SW[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005000, &NMD::LWC1_S9_         , 0,
       CP1_                },        /* LWC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4005800, &NMD::SWC1_S9_         , 0,
       CP1_                },        /* SWC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006000, &NMD::LD_S9_           , 0,
       MIPS64_             },        /* LD[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006800, &NMD::SD_S9_           , 0,
       MIPS64_             },        /* SD[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007000, &NMD::LDC1_S9_         , 0,
       CP1_                },        /* LDC1[S9] */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4007800, &NMD::SDC1_S9_         , 0,
       CP1_                },        /* SDC1[S9] */
};


NMD::Pool NMD::ASET_ACLR[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa4001100, &NMD::ASET             , 0,
       MCU_                },        /* ASET */
    { instruction         , 0                   , 0   , 32,
       0xfe007f00, 0xa6001100, &NMD::ACLR             , 0,
       MCU_                },        /* ACLR */
};


NMD::Pool NMD::P_LL[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005100, &NMD::LL               , 0,
       0x0                 },        /* LL */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005101, &NMD::LLWP             , 0,
       XNP_                },        /* LLWP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005102, 0                      , 0,
       0x0                 },        /* P.LL~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005103, 0                      , 0,
       0x0                 },        /* P.LL~*(3) */
};


NMD::Pool NMD::P_SC[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005900, &NMD::SC               , 0,
       0x0                 },        /* SC */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005901, &NMD::SCWP             , 0,
       XNP_                },        /* SCWP */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005902, 0                      , 0,
       0x0                 },        /* P.SC~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005903, 0                      , 0,
       0x0                 },        /* P.SC~*(3) */
};


NMD::Pool NMD::P_LLD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007100, &NMD::LLD              , 0,
       MIPS64_             },        /* LLD */
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007101, &NMD::LLDP             , 0,
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


NMD::Pool NMD::P_SCD[8] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007900, &NMD::SCD              , 0,
       MIPS64_             },        /* SCD */
    { instruction         , 0                   , 0   , 32,
       0xfc007f07, 0xa4007901, &NMD::SCDP             , 0,
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


NMD::Pool NMD::P_LS_S1[16] = {
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
       0xfc007f00, 0xa4002100, &NMD::UALH             , 0,
       XMMS_               },        /* UALH */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002900, &NMD::UASH             , 0,
       XMMS_               },        /* UASH */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f00, 0xa4003100, 0                      , 0,
       0x0                 },        /* P.LS.S1~*(6) */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003900, &NMD::CACHE            , 0,
       CP0_                },        /* CACHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004100, &NMD::LWC2             , 0,
       CP2_                },        /* LWC2 */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004900, &NMD::SWC2             , 0,
       CP2_                },        /* SWC2 */
    { pool                , P_LL                , 4   , 32,
       0xfc007f00, 0xa4005100, 0                      , 0,
       0x0                 },        /* P.LL */
    { pool                , P_SC                , 4   , 32,
       0xfc007f00, 0xa4005900, 0                      , 0,
       0x0                 },        /* P.SC */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006100, &NMD::LDC2             , 0,
       CP2_                },        /* LDC2 */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4006900, &NMD::SDC2             , 0,
       CP2_                },        /* SDC2 */
    { pool                , P_LLD               , 8   , 32,
       0xfc007f00, 0xa4007100, 0                      , 0,
       0x0                 },        /* P.LLD */
    { pool                , P_SCD               , 8   , 32,
       0xfc007f00, 0xa4007900, 0                      , 0,
       0x0                 },        /* P.SCD */
};


NMD::Pool NMD::P_PREFE[2] = {
    { instruction         , 0                   , 0   , 32,
       0xffe07f00, 0xa7e01a00, &NMD::SYNCIE           , 0,
       CP0_ | EVA_         },        /* SYNCIE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001a00, &NMD::PREFE            , &NMD::PREFE_cond       ,
       CP0_ | EVA_         },        /* PREFE */
};


NMD::Pool NMD::P_LLE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005200, &NMD::LLE              , 0,
       CP0_ | EVA_         },        /* LLE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005201, &NMD::LLWPE            , 0,
       CP0_ | EVA_         },        /* LLWPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005202, 0                      , 0,
       0x0                 },        /* P.LLE~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005203, 0                      , 0,
       0x0                 },        /* P.LLE~*(3) */
};


NMD::Pool NMD::P_SCE[4] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a00, &NMD::SCE              , 0,
       CP0_ | EVA_         },        /* SCE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a01, &NMD::SCWPE            , 0,
       CP0_ | EVA_         },        /* SCWPE */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a02, 0                      , 0,
       0x0                 },        /* P.SCE~*(2) */
    { reserved_block      , 0                   , 0   , 32,
       0xfc007f03, 0xa4005a03, 0                      , 0,
       0x0                 },        /* P.SCE~*(3) */
};


NMD::Pool NMD::P_LS_E0[16] = {
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000200, &NMD::LBE              , 0,
       CP0_ | EVA_         },        /* LBE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4000a00, &NMD::SBE              , 0,
       CP0_ | EVA_         },        /* SBE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4001200, &NMD::LBUE             , 0,
       CP0_ | EVA_         },        /* LBUE */
    { pool                , P_PREFE             , 2   , 32,
       0xfc007f00, 0xa4001a00, 0                      , 0,
       0x0                 },        /* P.PREFE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002200, &NMD::LHE              , 0,
       CP0_ | EVA_         },        /* LHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4002a00, &NMD::SHE              , 0,
       CP0_ | EVA_         },        /* SHE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003200, &NMD::LHUE             , 0,
       CP0_ | EVA_         },        /* LHUE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4003a00, &NMD::CACHEE           , 0,
       CP0_ | EVA_         },        /* CACHEE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004200, &NMD::LWE              , 0,
       CP0_ | EVA_         },        /* LWE */
    { instruction         , 0                   , 0   , 32,
       0xfc007f00, 0xa4004a00, &NMD::SWE              , 0,
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


NMD::Pool NMD::P_LS_WM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000400, &NMD::LWM              , 0,
       XMMS_               },        /* LWM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000c00, &NMD::SWM              , 0,
       XMMS_               },        /* SWM */
};


NMD::Pool NMD::P_LS_UAWM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000500, &NMD::UALWM            , 0,
       XMMS_               },        /* UALWM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000d00, &NMD::UASWM            , 0,
       XMMS_               },        /* UASWM */
};


NMD::Pool NMD::P_LS_DM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000600, &NMD::LDM              , 0,
       MIPS64_             },        /* LDM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000e00, &NMD::SDM              , 0,
       MIPS64_             },        /* SDM */
};


NMD::Pool NMD::P_LS_UADM[2] = {
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000700, &NMD::UALDM            , 0,
       MIPS64_             },        /* UALDM */
    { instruction         , 0                   , 0   , 32,
       0xfc000f00, 0xa4000f00, &NMD::UASDM            , 0,
       MIPS64_             },        /* UASDM */
};


NMD::Pool NMD::P_LS_S9[8] = {
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


NMD::Pool NMD::P_BAL[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfe000000, 0x28000000, &NMD::BC_32_           , 0,
       0x0                 },        /* BC[32] */
    { call_instruction    , 0                   , 0   , 32,
       0xfe000000, 0x2a000000, &NMD::BALC_32_         , 0,
       0x0                 },        /* BALC[32] */
};


NMD::Pool NMD::P_BALRSC[2] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xffe0f000, 0x48008000, &NMD::BRSC             , 0,
       0x0                 },        /* BRSC */
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48008000, &NMD::BALRSC           , &NMD::BALRSC_cond      ,
       0x0                 },        /* BALRSC */
};


NMD::Pool NMD::P_J[16] = {
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48000000, &NMD::JALRC_32_        , 0,
       0x0                 },        /* JALRC[32] */
    { call_instruction    , 0                   , 0   , 32,
       0xfc00f000, 0x48001000, &NMD::JALRC_HB         , 0,
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


NMD::Pool NMD::P_BR3A[32] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88004000, &NMD::BC1EQZC          , 0,
       CP1_                },        /* BC1EQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88014000, &NMD::BC1NEZC          , 0,
       CP1_                },        /* BC1NEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88024000, &NMD::BC2EQZC          , 0,
       CP2_                },        /* BC2EQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88034000, &NMD::BC2NEZC          , 0,
       CP2_                },        /* BC2NEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1fc000, 0x88044000, &NMD::BPOSGE32C        , 0,
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


NMD::Pool NMD::P_BR1[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88000000, &NMD::BEQC_32_         , 0,
       0x0                 },        /* BEQC[32] */
    { pool                , P_BR3A              , 32  , 32,
       0xfc00c000, 0x88004000, 0                      , 0,
       0x0                 },        /* P.BR3A */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x88008000, &NMD::BGEC             , 0,
       0x0                 },        /* BGEC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0x8800c000, &NMD::BGEUC            , 0,
       0x0                 },        /* BGEUC */
};


NMD::Pool NMD::P_BR2[4] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8000000, &NMD::BNEC_32_         , 0,
       0x0                 },        /* BNEC[32] */
    { reserved_block      , 0                   , 0   , 32,
       0xfc00c000, 0xa8004000, 0                      , 0,
       0x0                 },        /* P.BR2~*(1) */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa8008000, &NMD::BLTC             , 0,
       0x0                 },        /* BLTC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc00c000, 0xa800c000, &NMD::BLTUC            , 0,
       0x0                 },        /* BLTUC */
};


NMD::Pool NMD::P_BRI[8] = {
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8000000, &NMD::BEQIC            , 0,
       0x0                 },        /* BEQIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8040000, &NMD::BBEQZC           , 0,
       XMMS_               },        /* BBEQZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8080000, &NMD::BGEIC            , 0,
       0x0                 },        /* BGEIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc80c0000, &NMD::BGEIUC           , 0,
       0x0                 },        /* BGEIUC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8100000, &NMD::BNEIC            , 0,
       0x0                 },        /* BNEIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8140000, &NMD::BBNEZC           , 0,
       XMMS_               },        /* BBNEZC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc8180000, &NMD::BLTIC            , 0,
       0x0                 },        /* BLTIC */
    { branch_instruction  , 0                   , 0   , 32,
       0xfc1c0000, 0xc81c0000, &NMD::BLTIUC           , 0,
       0x0                 },        /* BLTIUC */
};


NMD::Pool NMD::P32[32] = {
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
       0xfc000000, 0x04000000, &NMD::ADDIUPC_32_      , 0,
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
       0xfc000000, 0x08000000, &NMD::MOVE_BALC        , 0,
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


NMD::Pool NMD::P16_SYSCALL[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x1008    , &NMD::SYSCALL_16_      , 0,
       0x0                 },        /* SYSCALL[16] */
    { instruction         , 0                   , 0   , 16,
       0xfffc    , 0x100c    , &NMD::HYPCALL_16_      , 0,
       CP0_ | VZ_          },        /* HYPCALL[16] */
};


NMD::Pool NMD::P16_RI[4] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfff8    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16.RI~*(0) */
    { pool                , P16_SYSCALL         , 2   , 16,
       0xfff8    , 0x1008    , 0                      , 0,
       0x0                 },        /* P16.SYSCALL */
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1010    , &NMD::BREAK_16_        , 0,
       0x0                 },        /* BREAK[16] */
    { instruction         , 0                   , 0   , 16,
       0xfff8    , 0x1018    , &NMD::SDBBP_16_        , 0,
       EJTAG_              },        /* SDBBP[16] */
};


NMD::Pool NMD::P16_MV[2] = {
    { pool                , P16_RI              , 4   , 16,
       0xffe0    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16.RI */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1000    , &NMD::MOVE             , &NMD::MOVE_cond        ,
       0x0                 },        /* MOVE */
};


NMD::Pool NMD::P16_SHIFT[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3000    , &NMD::SLL_16_          , 0,
       0x0                 },        /* SLL[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x3008    , &NMD::SRL_16_          , 0,
       0x0                 },        /* SRL[16] */
};


NMD::Pool NMD::POOL16C_00[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5000    , &NMD::NOT_16_          , 0,
       0x0                 },        /* NOT[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5004    , &NMD::XOR_16_          , 0,
       0x0                 },        /* XOR[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x5008    , &NMD::AND_16_          , 0,
       0x0                 },        /* AND[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0f    , 0x500c    , &NMD::OR_16_           , 0,
       0x0                 },        /* OR[16] */
};


NMD::Pool NMD::POOL16C_0[2] = {
    { pool                , POOL16C_00          , 4   , 16,
       0xfc03    , 0x5000    , 0                      , 0,
       0x0                 },        /* POOL16C_00 */
    { reserved_block      , 0                   , 0   , 16,
       0xfc03    , 0x5002    , 0                      , 0,
       0x0                 },        /* POOL16C_0~*(1) */
};


NMD::Pool NMD::P16C[2] = {
    { pool                , POOL16C_0           , 2   , 16,
       0xfc01    , 0x5000    , 0                      , 0,
       0x0                 },        /* POOL16C_0 */
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0x5001    , &NMD::LWXS_16_         , 0,
       0x0                 },        /* LWXS[16] */
};


NMD::Pool NMD::P16_A1[2] = {
    { reserved_block      , 0                   , 0   , 16,
       0xfc40    , 0x7000    , 0                      , 0,
       0x0                 },        /* P16.A1~*(0) */
    { instruction         , 0                   , 0   , 16,
       0xfc40    , 0x7040    , &NMD::ADDIU_R1_SP_     , 0,
       0x0                 },        /* ADDIU[R1.SP] */
};


NMD::Pool NMD::P_ADDIU_RS5_[2] = {
    { instruction         , 0                   , 0   , 16,
       0xffe8    , 0x9008    , &NMD::NOP_16_          , 0,
       0x0                 },        /* NOP[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9008    , &NMD::ADDIU_RS5_       , &NMD::ADDIU_RS5__cond  ,
       0x0                 },        /* ADDIU[RS5] */
};


NMD::Pool NMD::P16_A2[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc08    , 0x9000    , &NMD::ADDIU_R2_        , 0,
       0x0                 },        /* ADDIU[R2] */
    { pool                , P_ADDIU_RS5_        , 2   , 16,
       0xfc08    , 0x9008    , 0                      , 0,
       0x0                 },        /* P.ADDIU[RS5] */
};


NMD::Pool NMD::P16_ADDU[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb000    , &NMD::ADDU_16_         , 0,
       0x0                 },        /* ADDU[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc01    , 0xb001    , &NMD::SUBU_16_         , 0,
       0x0                 },        /* SUBU[16] */
};


NMD::Pool NMD::P16_JRC[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc1f    , 0xd800    , &NMD::JRC              , 0,
       0x0                 },        /* JRC */
    { call_instruction    , 0                   , 0   , 16,
       0xfc1f    , 0xd810    , &NMD::JALRC_16_        , 0,
       0x0                 },        /* JALRC[16] */
};


NMD::Pool NMD::P16_BR1[2] = {
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &NMD::BEQC_16_         , &NMD::BEQC_16__cond    ,
       XMMS_               },        /* BEQC[16] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xd800    , &NMD::BNEC_16_         , &NMD::BNEC_16__cond    ,
       XMMS_               },        /* BNEC[16] */
};


NMD::Pool NMD::P16_BR[2] = {
    { pool                , P16_JRC             , 2   , 16,
       0xfc0f    , 0xd800    , 0                      , 0,
       0x0                 },        /* P16.JRC */
    { pool                , P16_BR1             , 2   , 16,
       0xfc00    , 0xd800    , 0                      , &NMD::P16_BR1_cond     ,
       0x0                 },        /* P16.BR1 */
};


NMD::Pool NMD::P16_SR[2] = {
    { instruction         , 0                   , 0   , 16,
       0xfd00    , 0x1c00    , &NMD::SAVE_16_         , 0,
       0x0                 },        /* SAVE[16] */
    { return_instruction  , 0                   , 0   , 16,
       0xfd00    , 0x1d00    , &NMD::RESTORE_JRC_16_  , 0,
       0x0                 },        /* RESTORE.JRC[16] */
};


NMD::Pool NMD::P16_4X4[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c00    , &NMD::ADDU_4X4_        , 0,
       XMMS_               },        /* ADDU[4X4] */
    { instruction         , 0                   , 0   , 16,
       0xfd08    , 0x3c08    , &NMD::MUL_4X4_         , 0,
       XMMS_               },        /* MUL[4X4] */
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d00    , 0                      , 0,
       0x0                 },        /* P16.4X4~*(2) */
    { reserved_block      , 0                   , 0   , 16,
       0xfd08    , 0x3d08    , 0                      , 0,
       0x0                 },        /* P16.4X4~*(3) */
};


NMD::Pool NMD::P16_LB[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c00    , &NMD::LB_16_           , 0,
       0x0                 },        /* LB[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c04    , &NMD::SB_16_           , 0,
       0x0                 },        /* SB[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc0c    , 0x5c08    , &NMD::LBU_16_          , 0,
       0x0                 },        /* LBU[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc0c    , 0x5c0c    , 0                      , 0,
       0x0                 },        /* P16.LB~*(3) */
};


NMD::Pool NMD::P16_LH[4] = {
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c00    , &NMD::LH_16_           , 0,
       0x0                 },        /* LH[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c01    , &NMD::SH_16_           , 0,
       0x0                 },        /* SH[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc09    , 0x7c08    , &NMD::LHU_16_          , 0,
       0x0                 },        /* LHU[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc09    , 0x7c09    , 0                      , 0,
       0x0                 },        /* P16.LH~*(3) */
};


NMD::Pool NMD::P16[32] = {
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
       0xfc00    , 0xd000    , &NMD::LI_16_           , 0,
       0x0                 },        /* LI[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf000    , &NMD::ANDI_16_         , 0,
       0x0                 },        /* ANDI[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x1400    , &NMD::LW_16_           , 0,
       0x0                 },        /* LW[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x3400    , &NMD::LW_SP_           , 0,
       0x0                 },        /* LW[SP] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x5400    , &NMD::LW_GP16_         , 0,
       0x0                 },        /* LW[GP16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x7400    , &NMD::LW_4X4_          , 0,
       XMMS_               },        /* LW[4X4] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0x9400    , &NMD::SW_16_           , 0,
       0x0                 },        /* SW[16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xb400    , &NMD::SW_SP_           , 0,
       0x0                 },        /* SW[SP] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xd400    , &NMD::SW_GP16_         , 0,
       0x0                 },        /* SW[GP16] */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xf400    , &NMD::SW_4X4_          , 0,
       XMMS_               },        /* SW[4X4] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x1800    , &NMD::BC_16_           , 0,
       0x0                 },        /* BC[16] */
    { call_instruction    , 0                   , 0   , 16,
       0xfc00    , 0x3800    , &NMD::BALC_16_         , 0,
       0x0                 },        /* BALC[16] */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x5800    , 0                      , 0,
       0x0                 },        /* P16~*(10) */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0x7800    , 0                      , 0,
       0x0                 },        /* P16~*(14) */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0x9800    , &NMD::BEQZC_16_        , 0,
       0x0                 },        /* BEQZC[16] */
    { branch_instruction  , 0                   , 0   , 16,
       0xfc00    , 0xb800    , &NMD::BNEZC_16_        , 0,
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
       0xfc00    , 0xbc00    , &NMD::MOVEP            , 0,
       XMMS_               },        /* MOVEP */
    { reserved_block      , 0                   , 0   , 16,
       0xfc00    , 0xdc00    , 0                      , 0,
       0x0                 },        /* P16~*(27) */
    { instruction         , 0                   , 0   , 16,
       0xfc00    , 0xfc00    , &NMD::MOVEP_REV_       , 0,
       XMMS_               },        /* MOVEP[REV] */
};


NMD::Pool NMD::MAJOR[2] = {
    { pool                , P32                 , 32  , 32,
       0x10000000, 0x00000000, 0                      , 0,
       0x0                 },        /* P32 */
    { pool                , P16                 , 32  , 16,
       0x1000    , 0x1000    , 0                      , 0,
       0x0                 },        /* P16 */
};
