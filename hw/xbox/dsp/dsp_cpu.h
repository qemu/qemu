/*
    DSP56300 emulator

    Adapted from Hatari DSP M56001 emulation
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

#ifndef DSP_CPU_H
#define DSP_CPU_H

#include <stdio.h>
#include <stdint.h>

/* Functions */
void dsp56k_init_cpu(void);		/* Set dsp_core to use */
void dsp56k_execute_instruction(void);	/* Execute 1 instruction */
uint16_t dsp56k_execute_one_disasm_instruction(FILE *out, uint32_t pc);	/* Execute 1 instruction in disasm mode */

uint32_t dsp56k_read_memory(int space, uint32_t address);
void dsp56k_write_memory(int space, uint32_t address, uint32_t value);

/* Interrupt relative functions */
void dsp56k_add_interrupt(uint16_t inter);

#endif	/* DSP_CPU_H */
