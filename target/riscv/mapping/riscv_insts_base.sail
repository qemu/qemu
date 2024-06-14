/*=======================================================================================*/
/*  RISCV Sail Model                                                                     */
/*                                                                                       */
/*  This Sail RISC-V architecture model, comprising all files and                        */
/*  directories except for the snapshots of the Lem and Sail libraries                   */
/*  in the prover_snapshots directory (which include copies of their                     */
/*  licences), is subject to the BSD two-clause licence below.                           */
/*                                                                                       */
/*  Copyright (c) 2017-2023                                                              */
/*    Prashanth Mundkur                                                                  */
/*    Rishiyur S. Nikhil and Bluespec, Inc.                                              */
/*    Jon French                                                                         */
/*    Brian Campbell                                                                     */
/*    Robert Norton-Wright                                                               */
/*    Alasdair Armstrong                                                                 */
/*    Thomas Bauereiss                                                                   */
/*    Shaked Flur                                                                        */
/*    Christopher Pulte                                                                  */
/*    Peter Sewell                                                                       */
/*    Alexander Richardson                                                               */
/*    Hesham Almatary                                                                    */
/*    Jessica Clarke                                                                     */
/*    Microsoft, for contributions by Robert Norton-Wright and Nathaniel Wesley Filardo  */
/*    Peter Rugg                                                                         */
/*    Aril Computer Corp., for contributions by Scott Johnson                            */
/*    Philipp Tomsich                                                                    */
/*    VRULL GmbH, for contributions by its employees                                     */
/*                                                                                       */
/*  All rights reserved.                                                                 */
/*                                                                                       */
/*  This software was developed by the above within the Rigorous                         */
/*  Engineering of Mainstream Systems (REMS) project, partly funded by                   */
/*  EPSRC grant EP/K008528/1, at the Universities of Cambridge and                       */
/*  Edinburgh.                                                                           */
/*                                                                                       */
/*  This software was developed by SRI International and the University of               */
/*  Cambridge Computer Laboratory (Department of Computer Science and                    */
/*  Technology) under DARPA/AFRL contract FA8650-18-C-7809 ("CIFV"), and                 */
/*  under DARPA contract HR0011-18-C-0016 ("ECATS") as part of the DARPA                 */
/*  SSITH research programme.                                                            */
/*                                                                                       */
/*  This project has received funding from the European Research Council                 */
/*  (ERC) under the European Union’s Horizon 2020 research and innovation                */
/*  programme (grant agreement 789108, ELVER).                                           */
/*                                                                                       */
/*                                                                                       */
/*  Redistribution and use in source and binary forms, with or without                   */
/*  modification, are permitted provided that the following conditions                   */
/*  are met:                                                                             */
/*  1. Redistributions of source code must retain the above copyright                    */
/*     notice, this list of conditions and the following disclaimer.                     */
/*  2. Redistributions in binary form must reproduce the above copyright                 */
/*     notice, this list of conditions and the following disclaimer in                   */
/*     the documentation and/or other materials provided with the                        */
/*     distribution.                                                                     */
/*                                                                                       */
/*  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''                   */
/*  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED                    */
/*  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A                      */
/*  PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR                  */
/*  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,                         */
/*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT                     */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF                     */
/*  USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND                  */
/*  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,                   */
/*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT                   */
/*  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF                   */
/*  SUCH DAMAGE.                                                                         */
/*=======================================================================================*/

/* ****************************************************************** */
/* This file specifies the instructions in the base integer set.      */


/* ****************************************************************** */
union clause ast = UTYPE : (bits(20), regidx, uop)

mapping encdec_uop : uop <-> bits(7) = {
  RISCV_LUI   <-> 0b0110111,
  RISCV_AUIPC <-> 0b0010111
}

mapping clause encdec = UTYPE(imm, rd, op)
  <-> imm @ rd @ encdec_uop(op)

function clause execute UTYPE(imm, rd, op) = {
  let off : xlenbits = sign_extend(imm @ 0x000);
  let ret : xlenbits = match op {
    RISCV_LUI   => off,
    RISCV_AUIPC => get_arch_pc() + off
  };
  X(rd) = ret;
  RETIRE_SUCCESS
}

mapping utype_mnemonic : uop <-> string = {
  RISCV_LUI   <-> "lui",
  RISCV_AUIPC <-> "auipc"
}

mapping clause assembly = UTYPE(imm, rd, op)
  <-> utype_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ hex_bits_20(imm)

/* ****************************************************************** */
union clause ast = RISCV_JAL : (bits(21), regidx)

mapping clause encdec = RISCV_JAL(imm_19 @ imm_7_0 @ imm_8 @ imm_18_13 @ imm_12_9 @ 0b0, rd)
  <-> imm_19 : bits(1) @ imm_18_13 : bits(6) @ imm_12_9 : bits(4) @ imm_8 : bits(1) @ imm_7_0 : bits(8) @ rd @ 0b1101111

/*
ideally we want some syntax like

mapping clause encdec = RISCV_JAL(imm @ 0b0, rd) <-> imm[19] @ imm[9..0] @ imm[10] @ imm[18..11] @ rd @ 0b1101111

match bv {
  imm[19] @ imm[9..0] @ imm[10] @ imm[18..11] -> imm @ 0b0
}

but this is difficult
*/

function clause execute (RISCV_JAL(imm, rd)) = {
  let t : xlenbits = PC + sign_extend(imm);
  /* Extensions get the first checks on the prospective target address. */
  match ext_control_check_pc(t) {
    Ext_ControlAddr_Error(e) => {
      ext_handle_control_check_error(e);
      RETIRE_FAIL
    },
    Ext_ControlAddr_OK(target) => {
      /* Perform standard alignment check */
      if bit_to_bool(target[1]) & not(extension("C"))
      then {
        handle_mem_exception(target, E_Fetch_Addr_Align());
        RETIRE_FAIL
      } else {
        X(rd) = get_next_pc();
        set_next_pc(target);
        RETIRE_SUCCESS
      }
    }
  }
}

/* TODO: handle 2-byte-alignment in mappings */

mapping clause assembly = RISCV_JAL(imm, rd)
  <-> "jal" ^ spc() ^ reg_name(rd) ^ sep() ^ hex_bits_21(imm)

/* ****************************************************************** */
$[name jump and link register]
/*!
 * The target address is obtained by adding the sign-extended 12-bit
 * I-immediate to the register rs1, then setting the
 * least-significant bit of the result to zero. The address of the
 * instruction following the jump (pc+4) is written to register rd.
 * Register x0 can be used as the destination if the result is not
 * required.
 */
union clause ast = RISCV_JALR : (bits(12), regidx, regidx)

$[format I]
mapping clause encdec = RISCV_JALR(imm, rs1, rd)
  <-> imm @ rs1 @ 0b000 @ rd @ 0b1100111

mapping clause assembly = RISCV_JALR(imm, rs1, rd)
  <-> "jalr" ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ hex_bits_12(imm)

/* see riscv_jalr_seq.sail or riscv_jalr_rmem.sail for the execute clause. */

/* ****************************************************************** */
$[name conditional branch]
/*!
 * The target address for this branch instruction is determined by combining
 * the sign-extended 13-bit immediate value with the contents of register rs1.
 * Additionally, the least-significant bit of the result is set to zero.
 * The condition for the branch is based on the specified operation (bop),
 * which can be one of the following mnemonic codes:
 *   - "beq"   : Branch if equal
 *   - "bne"   : Branch if not equal
 *   - "blt"   : Branch if less than (signed)
 *   - "bge"   : Branch if greater than or equal to (signed)
 *   - "bltu"  : Branch if less than (unsigned)
 *   - "bgeu"  : Branch if greater than or equal to (unsigned)
 *
 * The branch is taken if the specified condition is true, leading to a jump
 * to the target address. If the branch is not taken, the execution proceeds
 * to the next instruction.
 */
union clause ast = BTYPE : (bits(13), regidx, regidx, bop)

mapping encdec_bop : bop <-> bits(3) = {
  RISCV_BEQ  <-> 0b000,
  RISCV_BNE  <-> 0b001,
  RISCV_BLT  <-> 0b100,
  RISCV_BGE  <-> 0b101,
  RISCV_BLTU <-> 0b110,
  RISCV_BGEU <-> 0b111
}

mapping clause encdec = BTYPE(imm7_6 @ imm5_0 @ imm7_5_0 @ imm5_4_1 @ 0b0, rs2, rs1, op)
  <-> imm7_6 : bits(1) @ imm7_5_0 : bits(6) @ rs2 @ rs1 @ encdec_bop(op) @ imm5_4_1 : bits(4) @ imm5_0 : bits(1) @ 0b1100011

function clause execute (BTYPE(imm, rs2, rs1, op)) = {
  let rs1_val = X(rs1);
  let rs2_val = X(rs2);
  let taken : bool = match op {
    RISCV_BEQ  => rs1_val == rs2_val,
    RISCV_BNE  => rs1_val != rs2_val,
    RISCV_BLT  => rs1_val <_s rs2_val,
    RISCV_BGE  => rs1_val >=_s rs2_val,
    RISCV_BLTU => rs1_val <_u rs2_val,
    RISCV_BGEU => rs1_val >=_u rs2_val
  };
  let t : xlenbits = PC + sign_extend(imm);
  if taken then {
    /* Extensions get the first checks on the prospective target address. */
    match ext_control_check_pc(t) {
      Ext_ControlAddr_Error(e) => {
        ext_handle_control_check_error(e);
        RETIRE_FAIL
      },
      Ext_ControlAddr_OK(target) => {
        if bit_to_bool(target[1]) & not(extension("C")) then {
          handle_mem_exception(target, E_Fetch_Addr_Align());
          RETIRE_FAIL;
        } else {
          set_next_pc(target);
          RETIRE_SUCCESS
        }
      }
    }
  } else RETIRE_SUCCESS
}

mapping btype_mnemonic : bop <-> string = {
  RISCV_BEQ  <-> "beq",
  RISCV_BNE  <-> "bne",
  RISCV_BLT  <-> "blt",
  RISCV_BGE  <-> "bge",
  RISCV_BLTU <-> "bltu",
  RISCV_BGEU <-> "bgeu"
}

mapping clause assembly = BTYPE(imm, rs2, rs1, op)
  <-> btype_mnemonic(op) ^ spc() ^ reg_name(rs1) ^ sep() ^ reg_name(rs2) ^ sep() ^ hex_bits_13(imm)

/* ****************************************************************** */

/*!
 * The ITYPE instruction operates on an immediate value, adding, comparing, or
 * performing bitwise operations with the contents of register rs1.
 * The immediate value, rs1, and the operation code (iop) determine the operation.
 * The result is stored in register rd.
 * The supported immediate operations (iop) include:
 *   - "addi"  : Add immediate
 *   - "slti"  : Set less than immediate (signed)
 *   - "sltiu" : Set less than immediate (unsigned)
 *   - "andi"  : AND immediate
 *   - "ori"   : OR immediate
 *   - "xori"  : XOR immediate
 *
 * Note: The immediate value is sign-extended before performing the operation.
 */
union clause ast = ITYPE : (bits(12), regidx, regidx, iop)

mapping encdec_iop : iop <-> bits(3) = {
  RISCV_ADDI  <-> 0b000,
  RISCV_SLTI  <-> 0b010,
  RISCV_SLTIU <-> 0b011,
  RISCV_ANDI  <-> 0b111,
  RISCV_ORI   <-> 0b110,
  RISCV_XORI  <-> 0b100
}

mapping clause encdec = ITYPE(imm, rs1, rd, op)
  <-> imm @ rs1 @ encdec_iop(op) @ rd @ 0b0010011

function clause execute (ITYPE (imm, rs1, rd, op)) = {
  let rs1_val = X(rs1);
  let immext : xlenbits = sign_extend(imm);
  let result : xlenbits = match op {
    RISCV_ADDI  => rs1_val + immext,
    RISCV_SLTI  => zero_extend(bool_to_bits(rs1_val <_s immext)),
    RISCV_SLTIU => zero_extend(bool_to_bits(rs1_val <_u immext)),
    RISCV_ANDI  => rs1_val & immext,
    RISCV_ORI   => rs1_val | immext,
    RISCV_XORI  => rs1_val ^ immext
  };
  X(rd) = result;
  RETIRE_SUCCESS
}

mapping itype_mnemonic : iop <-> string = {
  $[name add immediate]
    RISCV_ADDI  <-> "addi",
  $[name set less than immediate]
    RISCV_SLTI  <-> "slti",
  $[name set less than immediate unsigned]
    RISCV_SLTIU <-> "sltiu",
  $[name XOR immediate]
    RISCV_XORI  <-> "xori",
  $[name OR immediate]
    RISCV_ORI   <-> "ori",
  $[name AND immediate]
    RISCV_ANDI  <-> "andi"
}

mapping clause assembly = ITYPE(imm, rs1, rd, op)
  <-> itype_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ hex_bits_12(imm)

/* ****************************************************************** */
$[name Shift Immediate]
/*!
 * The SHIFTIOP (Shift Immediate Operation) instruction format is used for
 * operations that involve shifting the bits of a register by an immediate
 * value. The specific operation is determined by the opcode field, and the
 * shift amount is specified by the immediate value (shamt). The result is
 * written to the destination register (rd), and the source operand is the
 * register specified by rs1. The format is common for shift-left logical
 * immediate (SLLI), shift-right logical immediate (SRLI), and shift-right
 * arithmetic immediate (SRAI) operations.
 *
 * Note: For RV32, the decoder ensures that shamt[5] = 0.
 */
union clause ast = SHIFTIOP : (bits(6), regidx, regidx, sop)

mapping encdec_sop : sop <-> bits(3) = {
  RISCV_SLLI <-> 0b001,
  RISCV_SRLI <-> 0b101,
  RISCV_SRAI <-> 0b101
}

mapping clause encdec = SHIFTIOP(shamt, rs1, rd, RISCV_SLLI) <-> 0b000000 @ shamt @ rs1 @ 0b001 @ rd @ 0b0010011 if sizeof(xlen) == 64 | shamt[5] == bitzero
mapping clause encdec = SHIFTIOP(shamt, rs1, rd, RISCV_SRLI) <-> 0b000000 @ shamt @ rs1 @ 0b101 @ rd @ 0b0010011 if sizeof(xlen) == 64 | shamt[5] == bitzero
mapping clause encdec = SHIFTIOP(shamt, rs1, rd, RISCV_SRAI) <-> 0b010000 @ shamt @ rs1 @ 0b101 @ rd @ 0b0010011 if sizeof(xlen) == 64 | shamt[5] == bitzero

function clause execute (SHIFTIOP(shamt, rs1, rd, op)) = {
  let rs1_val = X(rs1);
  /* the decoder guard should ensure that shamt[5] = 0 for RV32 */
  let result : xlenbits = match op {
    RISCV_SLLI => if   sizeof(xlen) == 32
                  then rs1_val << shamt[4..0]
                  else rs1_val << shamt,
    RISCV_SRLI => if   sizeof(xlen) == 32
                  then rs1_val >> shamt[4..0]
                  else rs1_val >> shamt,
    RISCV_SRAI => if   sizeof(xlen) == 32
                  then shift_right_arith32(rs1_val, shamt[4..0])
                  else shift_right_arith64(rs1_val, shamt)
  };
  X(rd) = result;
  RETIRE_SUCCESS
}

mapping shiftiop_mnemonic : sop <-> string = {
  RISCV_SLLI <-> "slli",
  RISCV_SRLI <-> "srli",
  RISCV_SRAI <-> "srai"
}

mapping clause assembly = SHIFTIOP(shamt, rs1, rd, op)
  <-> shiftiop_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ hex_bits_6(shamt)

/* ****************************************************************** */

/*!
 * The R-type (Register-type) instruction format is used for operations
 * that involve three registers. The specific operation is determined
 * by the opcode and funct7 fields. The result is written to the
 * destination register (rd), and the source operands are specified
 * by the source registers (rs1 and rs2). The format is common for
 * arithmetic, logical, and shift operations.
 */
union clause ast = RTYPE : (regidx, regidx, regidx, rop)

mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_ADD)  <-> 0b0000000 @ rs2 @ rs1 @ 0b000 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SLT)  <-> 0b0000000 @ rs2 @ rs1 @ 0b010 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SLTU) <-> 0b0000000 @ rs2 @ rs1 @ 0b011 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_AND)  <-> 0b0000000 @ rs2 @ rs1 @ 0b111 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_OR)   <-> 0b0000000 @ rs2 @ rs1 @ 0b110 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_XOR)  <-> 0b0000000 @ rs2 @ rs1 @ 0b100 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SLL)  <-> 0b0000000 @ rs2 @ rs1 @ 0b001 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SRL)  <-> 0b0000000 @ rs2 @ rs1 @ 0b101 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SUB)  <-> 0b0100000 @ rs2 @ rs1 @ 0b000 @ rd @ 0b0110011
mapping clause encdec = RTYPE(rs2, rs1, rd, RISCV_SRA)  <-> 0b0100000 @ rs2 @ rs1 @ 0b101 @ rd @ 0b0110011

function clause execute (RTYPE(rs2, rs1, rd, op)) = {
  let rs1_val = X(rs1);
  let rs2_val = X(rs2);
  let result : xlenbits = match op {
    RISCV_ADD  => rs1_val + rs2_val,
    RISCV_SLT  => zero_extend(bool_to_bits(rs1_val <_s rs2_val)),
    RISCV_SLTU => zero_extend(bool_to_bits(rs1_val <_u rs2_val)),
    RISCV_AND  => rs1_val & rs2_val,
    RISCV_OR   => rs1_val | rs2_val,
    RISCV_XOR  => rs1_val ^ rs2_val,
    RISCV_SLL  => if   sizeof(xlen) == 32
                  then rs1_val << (rs2_val[4..0])
                  else rs1_val << (rs2_val[5..0]),
    RISCV_SRL  => if   sizeof(xlen) == 32
                  then rs1_val >> (rs2_val[4..0])
                  else rs1_val >> (rs2_val[5..0]),
    RISCV_SUB  => rs1_val - rs2_val,
    RISCV_SRA  => if   sizeof(xlen) == 32
                  then shift_right_arith32(rs1_val, rs2_val[4..0])
                  else shift_right_arith64(rs1_val, rs2_val[5..0])
  };
  X(rd) = result;
  RETIRE_SUCCESS
}

mapping rtype_mnemonic : rop <-> string = {
  RISCV_ADD  <-> "add",
  RISCV_SLT  <-> "slt",
  RISCV_SLTU <-> "sltu",
  RISCV_AND  <-> "and",
  RISCV_OR   <-> "or",
  RISCV_XOR  <-> "xor",
  RISCV_SLL  <-> "sll",
  RISCV_SRL  <-> "srl",
  RISCV_SUB  <-> "sub",
  RISCV_SRA  <-> "sra"
}

mapping clause assembly = RTYPE(rs2, rs1, rd, op)
  <-> rtype_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ reg_name(rs2)

/* ****************************************************************** */
$[name Load]
/*!
 * The LOAD instruction format is used for loading data from memory into a
 * register. The specific operation is determined by the word width (size),
 * whether the load is signed or unsigned (is_unsigned), and memory ordering
 * semantics (acquire, release). The result is written to the destination
 * register (rd), and the memory address is computed by adding the immediate
 * offset (imm) to the value in register rs1.
 */
union clause ast = LOAD : (bits(12), regidx, regidx, bool, word_width, bool, bool)

/* unsigned loads are only present for widths strictly less than xlen,
   signed loads also present for widths equal to xlen */
mapping clause encdec = LOAD(imm, rs1, rd, is_unsigned, size, false, false) if (word_width_bytes(size) < sizeof(xlen_bytes)) | (not(is_unsigned) & word_width_bytes(size) <= sizeof(xlen_bytes))
  <-> imm @ rs1 @ bool_bits(is_unsigned) @ size_bits(size) @ rd @ 0b0000011 if (word_width_bytes(size) < sizeof(xlen_bytes)) | (not(is_unsigned) & word_width_bytes(size) <= sizeof(xlen_bytes))

val extend_value : forall 'n, 0 < 'n <= xlen_bytes. (bool, MemoryOpResult(bits(8 * 'n))) -> MemoryOpResult(xlenbits)
function extend_value(is_unsigned, value) = match (value) {
  MemValue(v)     => MemValue(if is_unsigned then zero_extend(v) else sign_extend(v) : xlenbits),
  MemException(e) => MemException(e)
}

val process_load : forall 'n, 0 < 'n <= xlen_bytes. (regidx, xlenbits, MemoryOpResult(bits(8 * 'n)), bool) -> Retired
function process_load(rd, vaddr, value, is_unsigned) =
  match extend_value(is_unsigned, value) {
    MemValue(result) => { X(rd) = result; RETIRE_SUCCESS },
    MemException(e)  => { handle_mem_exception(vaddr, e); RETIRE_FAIL }
  }

function check_misaligned(vaddr : xlenbits, width : word_width) -> bool =
  if   plat_enable_misaligned_access() then false
  else match width {
         BYTE   => false,
         HALF   => vaddr[0] == bitone,
         WORD   => vaddr[0] == bitone | vaddr[1] == bitone,
         DOUBLE => vaddr[0] == bitone | vaddr[1] == bitone | vaddr[2] == bitone
       }

function clause execute(LOAD(imm, rs1, rd, is_unsigned, width, aq, rl)) = {
  let offset : xlenbits = sign_extend(imm);
  /* Get the address, X(rs1) + offset.
     Some extensions perform additional checks on address validity. */
  match ext_data_get_addr(rs1, offset, Read(Data), width) {
    Ext_DataAddr_Error(e)  => { ext_handle_data_check_error(e); RETIRE_FAIL },
    Ext_DataAddr_OK(vaddr) =>
      if   check_misaligned(vaddr, width)
      then { handle_mem_exception(vaddr, E_Load_Addr_Align()); RETIRE_FAIL }
      else match translateAddr(vaddr, Read(Data)) {
        TR_Failure(e, _) => { handle_mem_exception(vaddr, e); RETIRE_FAIL },
        TR_Address(paddr, _) =>
          match (width) {
            BYTE =>
              process_load(rd, vaddr, mem_read(Read(Data), paddr, 1, aq, rl, false), is_unsigned),
            HALF =>
              process_load(rd, vaddr, mem_read(Read(Data), paddr, 2, aq, rl, false), is_unsigned),
            WORD =>
              process_load(rd, vaddr, mem_read(Read(Data), paddr, 4, aq, rl, false), is_unsigned),
            DOUBLE if sizeof(xlen) >= 64 =>
              process_load(rd, vaddr, mem_read(Read(Data), paddr, 8, aq, rl, false), is_unsigned),
            _ => report_invalid_width(__FILE__, __LINE__, width, "load")
          }
      }
  }
}

val maybe_aq : bool <-> string
mapping maybe_aq = {
  true  <-> ".aq",
  false <-> ""
}

val maybe_rl : bool <-> string
mapping maybe_rl = {
  true  <-> ".rl",
  false <-> ""
}

val maybe_u : bool <-> string
mapping maybe_u = {
  true  <-> "u",
  false <-> ""
}

mapping clause assembly = LOAD(imm, rs1, rd, is_unsigned, size, aq, rl)
  <-> "l" ^ size_mnemonic(size) ^ maybe_u(is_unsigned) ^ maybe_aq(aq) ^ maybe_rl(rl) ^ spc() ^ reg_name(rd) ^ sep() ^ hex_bits_12(imm) ^ opt_spc() ^ "(" ^ opt_spc() ^ reg_name(rs1) ^ opt_spc() ^ ")"

/* ****************************************************************** */
$[name Store]
/*!
 * The STORE instruction format is used for storing data from a register into
 * memory. The specific operation is determined by the word width (size) and
 * memory ordering semantics (acquire, release). The memory address is computed
 * by adding the immediate offset (imm) to the value in register rs1, and the
 * data is taken from register rs2.
 */
union clause ast = STORE : (bits(12), regidx, regidx, word_width, bool, bool)

mapping clause encdec = STORE(imm7 @ imm5, rs2, rs1, size, false, false)              if word_width_bytes(size) <= sizeof(xlen_bytes)
  <-> imm7 : bits(7) @ rs2 @ rs1 @ 0b0 @ size_bits(size) @ imm5 : bits(5) @ 0b0100011 if word_width_bytes(size) <= sizeof(xlen_bytes)

/* NOTE: Currently, we only EA if address translation is successful.
   This may need revisiting. */
function clause execute (STORE(imm, rs2, rs1, width, aq, rl)) = {
  let offset : xlenbits = sign_extend(imm);
  /* Get the address, X(rs1) + offset.
     Some extensions perform additional checks on address validity. */
  match ext_data_get_addr(rs1, offset, Write(Data), width) {
    Ext_DataAddr_Error(e)  => { ext_handle_data_check_error(e); RETIRE_FAIL },
    Ext_DataAddr_OK(vaddr) =>
      if   check_misaligned(vaddr, width)
      then { handle_mem_exception(vaddr, E_SAMO_Addr_Align()); RETIRE_FAIL }
      else match translateAddr(vaddr, Write(Data)) {
        TR_Failure(e, _)    => { handle_mem_exception(vaddr, e); RETIRE_FAIL },
        TR_Address(paddr, _) => {
          let eares : MemoryOpResult(unit) = match width {
            BYTE   => mem_write_ea(paddr, 1, aq, rl, false),
            HALF   => mem_write_ea(paddr, 2, aq, rl, false),
            WORD   => mem_write_ea(paddr, 4, aq, rl, false),
            DOUBLE => mem_write_ea(paddr, 8, aq, rl, false)
          };
          match (eares) {
            MemException(e) => { handle_mem_exception(vaddr, e); RETIRE_FAIL },
            MemValue(_) => {
              let rs2_val = X(rs2);
              let res : MemoryOpResult(bool) = match (width) {
                BYTE => mem_write_value(paddr, 1, rs2_val[7..0],  aq, rl, false),
                HALF => mem_write_value(paddr, 2, rs2_val[15..0], aq, rl, false),
                WORD => mem_write_value(paddr, 4, rs2_val[31..0], aq, rl, false),
                DOUBLE if sizeof(xlen) >= 64
                     => mem_write_value(paddr, 8, rs2_val,        aq, rl, false),
                _    => report_invalid_width(__FILE__, __LINE__, width, "store"),
              };
              match (res) {
                MemValue(true)  => RETIRE_SUCCESS,
                MemValue(false) => internal_error(__FILE__, __LINE__, "store got false from mem_write_value"),
                MemException(e) => { handle_mem_exception(vaddr, e); RETIRE_FAIL }
              }
            }
          }
        }
      }
  }
}

mapping clause assembly = STORE(imm, rs2, rs1, size, aq, rl)
  <-> "s" ^ size_mnemonic(size) ^ maybe_aq(aq) ^ maybe_rl(rl) ^ spc() ^ reg_name(rs2) ^ sep() ^ hex_bits_12(imm) ^ opt_spc() ^ "(" ^ opt_spc() ^ reg_name(rs1) ^ opt_spc() ^ ")"

/* ****************************************************************** */
$[name Add Immediate Word]
/*!
 * The ADDIW instruction involves adding a sign-extended
 * 12-bit immediate value to the content of register rs1. The result is a 32-bit
 * value, and overflow is disregarded. The final outcome is the lower 32 bits of
 * the result, sign-extended to 64 bits. If the immediate value is set to zero
 * in the ADDIW rd, rs1, 0 operation, the sign-extension of the lower 32 bits
 * of register rs1 is written to register rd.
 */

union clause ast = ADDIW : (bits(12), regidx, regidx)

$[format I]
mapping clause encdec = ADDIW(imm, rs1, rd)
      if sizeof(xlen) == 64
  <-> imm @ rs1 @ 0b000 @ rd @ 0b0011011
      if sizeof(xlen) == 64

function clause execute (ADDIW(imm, rs1, rd)) = {
  let result : xlenbits = sign_extend(imm) + X(rs1);
  X(rd) = sign_extend(result[31..0]);
  RETIRE_SUCCESS
}

mapping clause assembly = ADDIW(imm, rs1, rd)
      if sizeof(xlen) == 64
  <-> "addiw" ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ hex_bits_12(imm)
      if sizeof(xlen) == 64

/* ****************************************************************** */

/*!
 * The RTYPEW instruction set operates on 32-bit values,
 * and the result is sign-extended to 64 bits. The available operations are
 * ADDW (addition), SUBW (subtraction), SLLW (logical left shift),
 * SRLW (logical right shift), and SRAW (arithmetic right shift).
 * These operations are only applicable when the width of the target
 * architecture is 64 bits.
 */

union clause ast = RTYPEW : (regidx, regidx, regidx, ropw)

mapping clause encdec = RTYPEW(rs2, rs1, rd, RISCV_ADDW)
      if sizeof(xlen) == 64
  <-> 0b0000000 @ rs2 @ rs1 @ 0b000 @ rd @ 0b0111011
      if sizeof(xlen) == 64
mapping clause encdec = RTYPEW(rs2, rs1, rd, RISCV_SUBW)
      if sizeof(xlen) == 64
  <-> 0b0100000 @ rs2 @ rs1 @ 0b000 @ rd @ 0b0111011
      if sizeof(xlen) == 64
mapping clause encdec = RTYPEW(rs2, rs1, rd, RISCV_SLLW)
      if sizeof(xlen) == 64
  <-> 0b0000000 @ rs2 @ rs1 @ 0b001 @ rd @ 0b0111011
      if sizeof(xlen) == 64
mapping clause encdec = RTYPEW(rs2, rs1, rd, RISCV_SRLW)
      if sizeof(xlen) == 64
  <-> 0b0000000 @ rs2 @ rs1 @ 0b101 @ rd @ 0b0111011
      if sizeof(xlen) == 64
mapping clause encdec = RTYPEW(rs2, rs1, rd, RISCV_SRAW)
      if sizeof(xlen) == 64
  <-> 0b0100000 @ rs2 @ rs1 @ 0b101 @ rd @ 0b0111011
      if sizeof(xlen) == 64

function clause execute (RTYPEW(rs2, rs1, rd, op)) = {
  let rs1_val = (X(rs1))[31..0];
  let rs2_val = (X(rs2))[31..0];
  let result : bits(32) = match op {
    RISCV_ADDW => rs1_val + rs2_val,
    RISCV_SUBW => rs1_val - rs2_val,
    RISCV_SLLW => rs1_val << (rs2_val[4..0]),
    RISCV_SRLW => rs1_val >> (rs2_val[4..0]),
    RISCV_SRAW => shift_right_arith32(rs1_val, rs2_val[4..0])
  };
  X(rd) = sign_extend(result);
  RETIRE_SUCCESS
}

mapping rtypew_mnemonic : ropw <-> string = {
  RISCV_ADDW <-> "addw",
  RISCV_SUBW <-> "subw",
  RISCV_SLLW <-> "sllw",
  RISCV_SRLW <-> "srlw",
  RISCV_SRAW <-> "sraw"
}

mapping clause assembly = RTYPEW(rs2, rs1, rd, op)
      if sizeof(xlen) == 64
  <-> rtypew_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ reg_name(rs2)
      if sizeof(xlen) == 64

/* ****************************************************************** */
$[name Shift Immediate Word]
/*!
 * The SHIFTIWOP instruction set deals with
 * immediate shift operations on 32-bit values, with the result sign-extended
 * to 64 bits. The available operations include SLLIW (left shift logical
 * immediate word), SRLIW (right shift logical immediate word), and SRAIW
 * (right shift arithmetic immediate word). These operations are applicable
 * when the target architecture has a width of 64 bits.
 */

union clause ast = SHIFTIWOP : (bits(5), regidx, regidx, sopw)

mapping clause encdec = SHIFTIWOP(shamt, rs1, rd, RISCV_SLLIW)
      if sizeof(xlen) == 64
  <-> 0b0000000 @ shamt @ rs1 @ 0b001 @ rd @ 0b0011011
      if sizeof(xlen) == 64
mapping clause encdec = SHIFTIWOP(shamt, rs1, rd, RISCV_SRLIW)
      if sizeof(xlen) == 64
  <-> 0b0000000 @ shamt @ rs1 @ 0b101 @ rd @ 0b0011011
      if sizeof(xlen) == 64
mapping clause encdec = SHIFTIWOP(shamt, rs1, rd, RISCV_SRAIW)
      if sizeof(xlen) == 64
  <-> 0b0100000 @ shamt @ rs1 @ 0b101 @ rd @ 0b0011011
      if sizeof(xlen) == 64

function clause execute (SHIFTIWOP(shamt, rs1, rd, op)) = {
  let rs1_val = (X(rs1))[31..0];
  let result : bits(32) = match op {
    RISCV_SLLIW => rs1_val << shamt,
    RISCV_SRLIW => rs1_val >> shamt,
    RISCV_SRAIW => shift_right_arith32(rs1_val, shamt)
  };
  X(rd) = sign_extend(result);
  RETIRE_SUCCESS
}

mapping shiftiwop_mnemonic : sopw <-> string = {
  RISCV_SLLIW <-> "slliw",
  RISCV_SRLIW <-> "srliw",
  RISCV_SRAIW <-> "sraiw"
}

mapping clause assembly = SHIFTIWOP(shamt, rs1, rd, op)
      if sizeof(xlen) == 64
  <-> shiftiwop_mnemonic(op) ^ spc() ^ reg_name(rd) ^ sep() ^ reg_name(rs1) ^ sep() ^ hex_bits_5(shamt)
      if sizeof(xlen) == 64

/* ****************************************************************** */
$[name Fence (Memory)]
/*!
 * The FENCE instruction is used to provide memory ordering guarantees.
 * It specifies ordering constraints on memory operations that precede
 * and follow it in program order. The FENCE instruction includes two
 * 4-bit fields, 'pred' and 'succ', which represent the memory ordering
 * requirements before and after the FENCE instruction, respectively.
 *
 * The bits in 'pred' and 'succ' represent the following ordering constraints:
 * - 'i': instruction stream order
 * - 'o': outstanding loads
 * - 'r': read operations
 * - 'w': write operations
 *
 * The FENCE instruction is used to control the visibility of memory
 * operations, and its behavior is influenced by the 'pred' and 'succ'
 * fields. The precise semantics of the FENCE instruction depend on the
 * specific bits set in these fields.
 */
union clause ast = FENCE : (bits(4), bits(4))

$[format I]
mapping clause encdec = FENCE(pred, succ)
  <-> 0b0000 @ pred @ succ @ 0b00000 @ 0b000 @ 0b00000 @ 0b0001111

function effective_fence_set(set : bits(4), fiom : bool) -> bits(4) = {
  // The bits are IORW. If FIOM is set then I implies R and O implies W.
  if fiom then {
    set[3 .. 2] @ (set[1 .. 0] | set[3 .. 2])
  } else set
}

/* For future versions of Sail where barriers can be parameterised */
$ifdef FEATURE_UNION_BARRIER

function clause execute (FENCE(pred, succ)) = {
  // If the FIOM bit in menvcfg/senvcfg is set then the I/O bits can imply R/W.
  let fiom = is_fiom_active();
  let pred = effective_fence_set(pred, fiom);
  let succ = effective_fence_set(succ, fiom);

  match (pred, succ) {
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_rw_rw()),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_r_rw()),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_r_r()),
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_rw_w()),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_w_w()),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_w_rw()),
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_rw_r()),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_r_w()),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_w_r()),

    (_ : bits(4)       , _ : bits(2) @ 0b00) => (),
    (_ : bits(2) @ 0b00, _ : bits(4)       ) => (),

    _ => { print("FIXME: unsupported fence");
           () }
  };
  RETIRE_SUCCESS
}

$else

function clause execute (FENCE(pred, succ)) = {
  // If the FIOM bit in menvcfg/senvcfg is set then the I/O bits can imply R/W.
  let fiom = is_fiom_active();
  let pred = effective_fence_set(pred, fiom);
  let succ = effective_fence_set(succ, fiom);

  match (pred, succ) {
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_rw_rw),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_r_rw),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_r_r),
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_rw_w),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_w_w),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_w_rw),
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_rw_r),
    (_ : bits(2) @ 0b10, _ : bits(2) @ 0b01) => __barrier(Barrier_RISCV_r_w),
    (_ : bits(2) @ 0b01, _ : bits(2) @ 0b10) => __barrier(Barrier_RISCV_w_r),

    (_ : bits(4)       , _ : bits(2) @ 0b00) => (),
    (_ : bits(2) @ 0b00, _ : bits(4)       ) => (),

    _ => { print("FIXME: unsupported fence");
           () }
  };
  RETIRE_SUCCESS
}

$endif

mapping bit_maybe_r : bits(1) <-> string = {
  0b1 <-> "r",
  0b0 <-> ""
}

mapping bit_maybe_w : bits(1) <-> string = {
  0b1 <-> "w",
  0b0 <-> ""
}

mapping bit_maybe_i : bits(1) <-> string = {
  0b1 <-> "i",
  0b0 <-> ""
}

mapping bit_maybe_o : bits(1) <-> string = {
  0b1 <-> "o",
  0b0 <-> ""
}

mapping fence_bits : bits(4) <-> string = {
  i : bits(1) @ o : bits(1) @ r : bits(1) @ w : bits(1) <-> bit_maybe_i(i) ^ bit_maybe_o(o) ^ bit_maybe_r(r) ^ bit_maybe_w(w)
}

mapping clause assembly = FENCE(pred, succ)
  <-> "fence" ^ spc() ^ fence_bits(pred) ^ sep() ^ fence_bits(succ)

/* ****************************************************************** */
$[name Fence (Total Store Order)]
/*!
 * The FENCE_TSO instruction is a memory
 * ordering instruction that provides a stronger memory consistency model
 * compared to the standard FENCE instruction. It ensures that all memory
 * operations preceding and following the FENCE_TSO instruction are globally
 * ordered. The FENCE_TSO instruction includes two 4-bit fields, 'pred' and
 * 'succ', which represent the memory ordering requirements before and after
 * the FENCE_TSO instruction, respectively.
 */
union clause ast = FENCE_TSO : (bits(4), bits(4))

$[format I]
mapping clause encdec = FENCE_TSO(pred, succ)
  <-> 0b1000 @ pred @ succ @ 0b00000 @ 0b000 @ 0b00000 @ 0b0001111

$ifdef FEATURE_UNION_BARRIER

function clause execute (FENCE_TSO(pred, succ)) = {
  match (pred, succ) {
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_tso()),
    (_ : bits(2) @ 0b00, _ : bits(2) @ 0b00) => (),

    _ => { print("FIXME: unsupported fence");
           () }
  };
  RETIRE_SUCCESS
}

$else

function clause execute (FENCE_TSO(pred, succ)) = {
  match (pred, succ) {
    (_ : bits(2) @ 0b11, _ : bits(2) @ 0b11) => __barrier(Barrier_RISCV_tso),
    (_ : bits(2) @ 0b00, _ : bits(2) @ 0b00) => (),

    _ => { print("FIXME: unsupported fence");
           () }
  };
  RETIRE_SUCCESS
}

$endif

mapping clause assembly = FENCE_TSO(pred, succ)
  <-> "fence.tso" ^ spc() ^ fence_bits(pred) ^ sep() ^ fence_bits(succ)

/* ****************************************************************** */
$[name Fence (Instruction)]
/*!
 * The FENCEI instruction is a memory ordering instruction that
 * provides a barrier to the instruction stream.
 * It ensures that all instructions preceding the FENCEI instruction are
 * globally ordered. The FENCEI instruction has no 'pred' or 'succ' fields,
 * and it is typically used to control instruction stream visibility.
 */
union clause ast = FENCEI : unit

mapping clause encdec = FENCEI()
  <-> 0b000000000000 @ 0b00000 @ 0b001 @ 0b00000 @ 0b0001111

/* fence.i is a nop for the memory model */
function clause execute FENCEI() = { /* __barrier(Barrier_RISCV_i); */ RETIRE_SUCCESS }

mapping clause assembly = FENCEI() <-> "fence.i"

/* ****************************************************************** */
$[name Environment Call]
/*!
 * The ECALL instruction, previously called SCALL is used to make a
 * request to the supporting execution environment, typically an
 * operating system. The ABI for the system defines how parameters
 * for the environment request are passed, often in defined locations
 * in the integer register file.
 */
union clause ast = ECALL : unit

mapping clause encdec = ECALL()
  <-> 0b000000000000 @ 0b00000 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute ECALL() = {
  let t : sync_exception =
    struct { trap = match (cur_privilege) {
                      User       => E_U_EnvCall(),
                      Supervisor => E_S_EnvCall(),
                      Machine    => E_M_EnvCall()
                    },
             excinfo = (None() : option(xlenbits)),
             ext     = None() };
  set_next_pc(exception_handler(cur_privilege, CTL_TRAP(t), PC));
  RETIRE_FAIL
}

mapping clause assembly = ECALL() <-> "ecall"

/* ****************************************************************** */
$[name Machine-level Return]
/*!
 * The MRET instruction is used to return from a machine-level exception,
 * transferring control back to the instruction following the one that
 * caused the exception. It is only valid when executed in Machine mode.
 */
union clause ast = MRET : unit

mapping clause encdec = MRET()
  <-> 0b0011000 @ 0b00010 @ 0b00000 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute MRET() = {
  if   cur_privilege != Machine
  then { handle_illegal(); RETIRE_FAIL }
  else if not(ext_check_xret_priv (Machine))
  then { ext_fail_xret_priv(); RETIRE_FAIL }
  else {
    set_next_pc(exception_handler(cur_privilege, CTL_MRET(), PC));
    RETIRE_SUCCESS
  }
}

mapping clause assembly = MRET() <-> "mret"

/* ****************************************************************** */
$[name Supervisor-level Return]
/*!
 * The SRET instruction is used to return from a supervisor-level exception,
 * transferring control back to the instruction following the one that
 * caused the exception. It is only valid when executed in Supervisor mode.
 */
union clause ast = SRET : unit

mapping clause encdec = SRET()
  <-> 0b0001000 @ 0b00010 @ 0b00000 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute SRET() = {
  let sret_illegal : bool = match cur_privilege {
    User       => true,
    Supervisor => not(extension("S")) | mstatus.TSR() == 0b1,
    Machine    => not(extension("S"))
  };
  if   sret_illegal
  then { handle_illegal(); RETIRE_FAIL }
  else if not(ext_check_xret_priv (Supervisor))
  then { ext_fail_xret_priv(); RETIRE_FAIL }
  else {
    set_next_pc(exception_handler(cur_privilege, CTL_SRET(), PC));
    RETIRE_SUCCESS
  }
}

mapping clause assembly = SRET() <-> "sret"

/* ****************************************************************** */
$[name Environment Breakpoint]
/*!
 * The EBREAK instruction, previously called SBREAK is utilized by
 * debuggers to trigger a breakpoint exception, leading to a transfer
 * of control back to a debugging environment.
 * This instruction is commonly employed for debugging purposes.
 */
union clause ast = EBREAK : unit

mapping clause encdec = EBREAK()
  <-> 0b000000000001 @ 0b00000 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute EBREAK() = {
  handle_mem_exception(PC, E_Breakpoint());
  RETIRE_FAIL
}

mapping clause assembly = EBREAK() <-> "ebreak"

/* ****************************************************************** */
$[name Wait For Interrupt]
/*!
 * The WFI (Wait For Interrupt) instruction is used to suspend the execution
 * pipeline until an interrupt or an event occurs. Its behavior depends on the
 * current privilege level:
 * - In Machine mode, it invokes the platform-specific WFI handler.
 * - In Supervisor mode, it checks the TW (Timer Wait) bit in mstatus. If the
 *   bit is set, it handles the illegal instruction and returns with RETIRE_FAIL.
 *   Otherwise, it invokes the platform-specific WFI handler.
 * - In User mode, it handles the illegal instruction and returns with RETIRE_FAIL.
 */
union clause ast = WFI : unit

mapping clause encdec = WFI()
  <-> 0b000100000101 @ 0b00000 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute WFI() =
  match cur_privilege {
    Machine    => { platform_wfi(); RETIRE_SUCCESS },
    Supervisor => if   mstatus.TW() == 0b1
                  then { handle_illegal(); RETIRE_FAIL }
                  else { platform_wfi(); RETIRE_SUCCESS },
    User       => { handle_illegal(); RETIRE_FAIL }
  }

mapping clause assembly = WFI() <-> "wfi"

/* ****************************************************************** */
$[name Store Fence (Virtual Memory Address)]
/*!
 * The SFENCE.VMA instruction is used to synchronize the store queue and flush
 * TLB entries based on virtual memory address and optional ASID values.
 * Its behavior depends on the current privilege level:
 * - In User mode, it handles the illegal instruction and returns with RETIRE_FAIL.
 * - In Supervisor mode, it checks for illegal instructions and performs TLB
 *   flushing based on the provided virtual memory address and ASID values.
 * - In Machine mode, it performs TLB flushing based on the provided virtual
 *   memory address and ASID values.
 */

union clause ast = SFENCE_VMA : (regidx, regidx)

$[format R]
mapping clause encdec = SFENCE_VMA(rs1, rs2)
  <-> 0b0001001 @ rs2 @ rs1 @ 0b000 @ 0b00000 @ 0b1110011

function clause execute SFENCE_VMA(rs1, rs2) = {
  let addr : option(xlenbits) = if rs1 == 0b00000 then None() else Some(X(rs1));
  let asid : option(xlenbits) = if rs2 == 0b00000 then None() else Some(X(rs2));
  match cur_privilege {
    User       => { handle_illegal(); RETIRE_FAIL },
    Supervisor => match (architecture(get_mstatus_SXL(mstatus)), mstatus.TVM()) {
                    (Some(_), 0b1)  => { handle_illegal(); RETIRE_FAIL },
                    (Some(_), 0b0) => { flush_TLB(asid, addr); RETIRE_SUCCESS },
                    (_, _)           => internal_error(__FILE__, __LINE__, "unimplemented sfence architecture")
                  },
    Machine    => { flush_TLB(asid, addr); RETIRE_SUCCESS }
  }
}

mapping clause assembly = SFENCE_VMA(rs1, rs2)
  <-> "sfence.vma" ^ spc() ^ reg_name(rs1) ^ sep() ^ reg_name(rs2)
