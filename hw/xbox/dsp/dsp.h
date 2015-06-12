/*
    DSP56300 emulator

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
#include <stdio.h>

typedef struct dsp_core_s dsp_core_t;

/* Dsp commands */
dsp_core_t* dsp_init(void);
void dsp_destroy(dsp_core_t* dsp);
void dsp_run(dsp_core_t* dsp, int nHostCycles);


/* Dsp Debugger commands */
uint32_t dsp_read_memory(dsp_core_t* dsp, uint32_t addr, char space, const char **mem_str);
uint32_t dsp_disasm_memory(dsp_core_t* dsp, uint32_t dsp_memdump_addr, uint32_t dsp_memdump_upper, char space);
uint32_t dsp_disasm_address(dsp_core_t* dsp, FILE *out, uint32_t lowerAdr, uint32_t UpperAdr);
void dsp_info(dsp_core_t* dsp);
void dsp_print_registers(dsp_core_t* dsp);
int dsp_get_register_address(dsp_core_t* dsp, const char *arg, uint32_t **addr, uint32_t *mask);
bool dsp_disasm_set_register(dsp_core_t* dsp, const char *arg, uint32_t value);


#endif /* DSP_H */
