/*
    DSP56300 emulation

    Based on Hatari DSP M56001 emulation
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


#include <stdio.h>
#include <string.h>
#include <math.h>

#include "dsp.h"
#include "dsp_cpu.h"

#include "dsp_core.h"

/*--- the DSP core itself ---*/
dsp_core_t dsp_core;

static void (*dsp_host_interrupt)(void);   /* Function to trigger host interrupt */

#define DPRINTF(s, ...) fprintf(stderr, s, ## __VA_ARGS__)

/* Init DSP emulation */
void dsp_core_init(void (*host_interrupt)(void))
{
    DPRINTF("Dsp: core init\n");

    dsp_host_interrupt = host_interrupt;
    memset(&dsp_core, 0, sizeof(dsp_core_t));
}

/* Shutdown DSP emulation */
void dsp_core_shutdown(void)
{
    dsp_core.running = 0;
    DPRINTF("Dsp: core shutdown\n");
}

/* Reset */
void dsp_core_reset(void)
{
    int i;

    DPRINTF("Dsp: core reset\n");
    dsp_core_shutdown();

    /* Memory */
    memset(dsp_core.periph, 0, sizeof(dsp_core.periph));
    memset(dsp_core.stack, 0, sizeof(dsp_core.stack));
    memset(dsp_core.registers, 0, sizeof(dsp_core.registers));
    // dsp_core.dsp_host_rtx = 0;
    // dsp_core.dsp_host_htx = 0;
    
    /* Registers */
    dsp_core.pc = 0x0000;
    dsp_core.registers[DSP_REG_OMR]=0x02;
    for (i=0;i<8;i++) {
        dsp_core.registers[DSP_REG_M0+i]=0x00ffff;
    }

    /* Interruptions */
    memset((void*)dsp_core.interrupt_isPending, 0, sizeof(dsp_core.interrupt_isPending));
    dsp_core.interrupt_state = DSP_INTERRUPT_NONE;
    dsp_core.interrupt_instr_fetch = -1;
    dsp_core.interrupt_save_pc = -1;
    dsp_core.interrupt_counter = 0;
    dsp_core.interrupt_pipeline_count = 0;
    for (i=0;i<5;i++) {
        dsp_core.interrupt_ipl[i] = 3;
    }
    for (i=5;i<12;i++) {
        dsp_core.interrupt_ipl[i] = -1;
    }

    /* Other hardware registers */
    // dsp_core.periph[DSP_SPACE_X][DSP_IPR]=0;
    // dsp_core.periph[DSP_SPACE_X][DSP_BCR]=0xffff;

    /* Misc */
    dsp_core.loop_rep = 0;

    DPRINTF("Dsp: reset done\n");
    dsp56k_init_cpu();
}

