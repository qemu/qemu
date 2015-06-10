/*
    DSP56300 emulation

    Copyright (c) 2015 espes

    Adapted from Hatari DSP M56001 emulation
    (C) 2001-2008 ARAnyM developer team
    Adaption to Hatari (C) 2008 by Thomas Huth

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

#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>

/* Dsp commands */
bool dsp_process_irq(void);
void dsp_init(void);
void dsp_uninit(void);
void dsp_reset(void);
void dsp_run(int nHostCycles);

void dsp_core_reset(void);


/* Dsp Debugger commands */
uint32_t dsp_get_pc(void);
uint32_t dsp_get_next_pc(uint32_t pc);
uint16_t dsp_get_instr_cycles(void);
uint32_t dsp_read_memory(uint32_t addr, char space, const char **mem_str);
uint32_t dsp_disasm_memory(uint32_t dsp_memdump_addr, uint32_t dsp_memdump_upper, char space);
uint32_t dsp_disasm_address(FILE *out, uint32_t lowerAdr, uint32_t UpperAdr);
void dsp_info(uint32_t dummy);
void dsp_print_registers(void);
int dsp_get_register_address(const char *arg, uint32_t **addr, uint32_t *mask);
bool dsp_disasm_set_register(const char *arg, uint32_t value);


#endif /* DSP_H */
