/*
 * MCPX DSP DMA
 *
 * Copyright (c) 2015 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DSP_DMA_H
#define DSP_DMA_H

#include <stdint.h>
#include <stdbool.h>

#include "dsp.h"
#include "dsp_cpu.h"

typedef enum DSPDMARegister {
    DMA_CONFIGURATION,
    DMA_CONTROL,
    DMA_START_BLOCK,
    DMA_NEXT_BLOCK,
} DSPDMARegister;

typedef struct DSPDMAState {
    dsp_core_t* core;

    void* scratch_rw_opaque;
    dsp_scratch_rw_func scratch_rw;

    uint32_t configuration;
    uint32_t control;
    uint32_t start_block;
    uint32_t next_block;

    bool error;
    bool eol;
} DSPDMAState;

uint32_t dsp_dma_read(DSPDMAState *s, DSPDMARegister reg);
void dsp_dma_write(DSPDMAState *s, DSPDMARegister reg, uint32_t v);

#endif