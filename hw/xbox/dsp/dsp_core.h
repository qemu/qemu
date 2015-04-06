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

#ifndef DSP_CORE_H
#define DSP_CORE_H

#include <stdint.h>

#include "dsp_cpu.h"

#define DSP_INTERRUPT_NONE      0x0
#define DSP_INTERRUPT_DISABLED  0x1
#define DSP_INTERRUPT_LONG      0x2

#define DSP_INTER_RESET         0x0
#define DSP_INTER_ILLEGAL       0x1
#define DSP_INTER_STACK_ERROR       0x2
#define DSP_INTER_TRACE         0x3
#define DSP_INTER_SWI           0x4
#define DSP_INTER_HOST_COMMAND      0x5
#define DSP_INTER_HOST_RCV_DATA     0x6
#define DSP_INTER_HOST_TRX_DATA     0x7
#define DSP_INTER_SSI_RCV_DATA_E    0x8
#define DSP_INTER_SSI_RCV_DATA      0x9
#define DSP_INTER_SSI_TRX_DATA_E    0xa
#define DSP_INTER_SSI_TRX_DATA      0xb


typedef struct dsp_core_s dsp_core_t;
typedef struct dsp_interrupt_s dsp_interrupt_t;

struct dsp_interrupt_s {
    const uint16_t inter;
    const uint16_t vectorAddr;
    const uint16_t periph;
    const char *name;
};

struct dsp_core_s {

    /* DSP executing instructions ? */
    int running;
    
    /* DSP instruction Cycle counter */
    uint16_t    instr_cycle;

    /* Registers */
    uint32_t    pc;
    uint32_t    registers[DSP_REG_MAX];

    /* stack[0=ssh], stack[1=ssl] */
    uint32_t    stack[2][16];

    uint32_t xram[DSP_XRAM_SIZE];
    uint32_t yram[DSP_YRAM_SIZE];
    uint32_t pram[DSP_PRAM_SIZE];

    /* peripheral space, x:0xffff80-0xffffff */
    uint32_t    periph[DSP_PERIPH_SIZE];

    /* Misc */
    uint32_t loop_rep;      /* executing rep ? */
    uint32_t pc_on_rep;     /* True if PC is on REP instruction */

    /* Interruptions */
    uint16_t    interrupt_state;        /* NONE, FAST or LONG interrupt */
    uint16_t  interrupt_instr_fetch;        /* vector of the current interrupt */
    uint16_t  interrupt_save_pc;        /* save next pc value before interrupt */
    uint16_t  interrupt_counter;        /* count number of pending interrupts */
    uint16_t  interrupt_IplToRaise;     /* save the IPL level to save in the SR register */
    uint16_t  interrupt_pipeline_count; /* used to prefetch correctly the 2 inter instructions */
    int16_t  interrupt_ipl[12];     /* store the current IPL for each interrupt */
    uint16_t  interrupt_isPending[12];  /* store if interrupt is pending for each interrupt */
};


/* DSP */
extern dsp_core_t dsp_core;

/* Emulator call these to init/stop/reset DSP emulation */
void dsp_core_init(void (*host_interrupt)(void));
void dsp_core_shutdown(void);
void dsp_core_reset(void);

#endif /* DSP_CORE_H */
