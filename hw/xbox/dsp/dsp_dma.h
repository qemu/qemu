#ifndef DSP_DMA_H
#define DSP_DMA_H

#include <stdint.h>

typedef enum DSPDMARegister {
    DMA_CONFIGURATION,
    DMA_CONTROL,
    DMA_START_BLOCK,
    DMA_NEXT_BLOCK,
} DSPDMARegister;

typedef struct DSPDMAState {
    uint32_t configuration;
    uint32_t control;
    uint32_t start_block;
    uint32_t next_block;
} DSPDMAState;

uint32_t dsp_dma_read(DSPDMAState *s, DSPDMARegister reg);
void dsp_dma_write(DSPDMAState *s, DSPDMARegister reg, uint32_t v);

#endif