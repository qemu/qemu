/*
 * MCPX DSP emulator

 * Copyright (c) 2015 espes

 * Adapted from Hatari DSP M56001 emulation
 * (C) 2001-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct DSPState DSPState;

typedef void (*dsp_scratch_rw_func)(
    void* opaque, uint8_t* ptr, uint32_t addr, size_t len, bool dir);

/* Dsp commands */
DSPState* dsp_init(void* scratch_rw_opaque, dsp_scratch_rw_func scratch_rw);
void dsp_destroy(DSPState* dsp);
void dsp_reset(DSPState* dsp);

void dsp_step(DSPState* dsp);
void dsp_run(DSPState* dsp, int cycles);

void dsp_bootstrap(DSPState* dsp);
void dsp_start_frame(DSPState* dsp);


/* Dsp Debugger commands */
uint32_t dsp_read_memory(DSPState* dsp, char space, uint32_t addr);
uint32_t dsp_disasm_memory(DSPState* dsp, uint32_t dsp_memdump_addr, uint32_t dsp_memdump_upper, char space);
uint32_t dsp_disasm_address(DSPState* dsp, FILE *out, uint32_t lowerAdr, uint32_t UpperAdr);
void dsp_info(DSPState* dsp);
void dsp_print_registers(DSPState* dsp);
int dsp_get_register_address(DSPState* dsp, const char *arg, uint32_t **addr, uint32_t *mask);
bool dsp_disasm_set_register(DSPState* dsp, const char *arg, uint32_t value);


#endif /* DSP_H */
