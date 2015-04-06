/*
    DSP56300 Disassembler

    Based on Hatari DSP M56001 Disassembler
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

typedef enum {
	DSP_TRACE_MODE,
	DSP_DISASM_MODE
} dsp_trace_disasm_t;

/* Functions */
void dsp56k_disasm_init(void);
uint16_t dsp56k_disasm(dsp_trace_disasm_t value);
const char* dsp56k_get_instruction_text(void);

/* Registers change */
void dsp56k_disasm_reg_save(void);
void dsp56k_disasm_reg_compare(void);

#endif /* DSP_DISASM_H */
