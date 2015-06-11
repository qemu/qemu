/*
    DSP56300 Disassembler

    Copyright (c) 2015 espes

    Adapted from Hatari DSP M56001 Disassembler
    (C) 2003-2008 ARAnyM developer team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef DSP_DISASM_H
#define DSP_DISASM_H

#include <stdint.h>
#include <stdbool.h>

extern uint32_t prev_inst_pc;
extern bool isLooping;

/* Used to display dc instead of unknown instruction for illegal opcodes */
extern bool isInDisasmMode;

extern uint32_t disasm_cur_inst;
extern uint16_t disasm_cur_inst_len;
extern char str_instr[50];
extern char parallelmove_name[64];


/* Functions */
const char* dsp56k_get_instruction_text(void);

/* Registers change */
void dsp56k_disasm_reg_save(void);
void dsp56k_disasm_reg_compare(void);


typedef void (*dis_func_t)(void);

void dis_undefined(void);

/* Instructions without parallel moves */
void dis_add_long(void);
void dis_andi(void);
void dis_bcc_long(void);
void dis_bcc_imm(void);
void dis_bchg_aa(void);
void dis_bchg_ea(void);
void dis_bchg_pp(void);
void dis_bchg_reg(void);
void dis_bclr_aa(void);
void dis_bclr_ea(void);
void dis_bclr_pp(void);
void dis_bclr_reg(void);
void dis_bra_imm(void);
void dis_bset_aa(void);
void dis_bset_ea(void);
void dis_bset_pp(void);
void dis_bset_reg(void);
void dis_btst_aa(void);
void dis_btst_ea(void);
void dis_btst_pp(void);
void dis_btst_reg(void);
void dis_cmpu(void);
void dis_div(void);
void dis_enddo(void);
void dis_illegal(void);
void dis_jcc_imm(void);
void dis_jcc_ea(void);
void dis_jclr_aa(void);
void dis_jclr_ea(void);
void dis_jclr_pp(void);
void dis_jclr_reg(void);
void dis_jmp_ea(void);
void dis_jmp_imm(void);
void dis_jscc_ea(void);
void dis_jscc_imm(void);
void dis_jsclr_aa(void);
void dis_jsclr_ea(void);
void dis_jsclr_pp(void);
void dis_jsclr_reg(void);
void dis_jset_aa(void);
void dis_jset_ea(void);
void dis_jset_pp(void);
void dis_jset_reg(void);
void dis_jsr_ea(void);
void dis_jsr_imm(void);
void dis_jsset_aa(void);
void dis_jsset_ea(void);
void dis_jsset_pp(void);
void dis_jsset_reg(void);
void dis_lua(void);
void dis_movem_ea(void);
void dis_movem_aa(void);
void dis_norm(void);
void dis_ori(void);
void dis_reset(void);
void dis_rti(void);
void dis_rts(void);
void dis_stop(void);
void dis_swi(void);
void dis_tcc(void);
void dis_wait(void);
void dis_do_ea(void);
void dis_do_aa(void);
void dis_do_imm(void);
void dis_do_reg(void);
void dis_dor_imm(void);
void dis_rep_aa(void);
void dis_rep_ea(void);
void dis_rep_imm(void);
void dis_rep_reg(void);
void dis_movec_aa(void);
void dis_movec_ea(void);
void dis_movec_imm(void);
void dis_movec_reg(void);
void dis_movep_0(void);
void dis_movep_1(void);
void dis_movep_23(void);

void dis_movep_x_low(void);
void dis_move_x_aa(void);

/* Parallel moves */
void dis_pm_class2(void);
void dis_pm(void);
void dis_pm_0(void);
void dis_pm_1(void);
void dis_pm_2(void);
void dis_pm_4(void);
void dis_pm_8(void);

#endif /* DSP_DISASM_H */
