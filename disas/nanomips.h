/*
 *  Header file for nanoMIPS disassembler component of QEMU
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

#ifndef DISAS_NANOMIPS_H
#define DISAS_NANOMIPS_H

#include <string>

typedef int64_t int64;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint64_t img_address;

enum TABLE_ENTRY_TYPE {
    instruction,
    call_instruction,
    branch_instruction,
    return_instruction,
    reserved_block,
    pool,
};

enum TABLE_ATTRIBUTE_TYPE {
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
};

typedef struct Dis_info {
  img_address m_pc;
} Dis_info;

typedef bool (*conditional_function)(uint64 instruction);
typedef std::string (*disassembly_function)(uint64 instruction,
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

#endif
