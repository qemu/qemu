#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "dsp_dma.h"

uint32_t dsp_dma_read(DSPDMAState *s, DSPDMARegister reg)
{
    switch (reg) {
    case DMA_CONFIGURATION:
        return s->configuration;
    case DMA_CONTROL:
        return s->control;
    case DMA_START_BLOCK:
        return s->start_block;
    case DMA_NEXT_BLOCK:
        return s->next_block;
    default:
        assert(false);
    }
    return 0;
}
void dsp_dma_write(DSPDMAState *s, DSPDMARegister reg, uint32_t v)
{
    switch (reg) {
    case DMA_CONFIGURATION:
        s->configuration = v;
        break;
    case DMA_CONTROL:
        s->control = v;
        break;
    case DMA_START_BLOCK:
        s->start_block = v;
        break;
    case DMA_NEXT_BLOCK:
        s->next_block = v;
        break;
    default:
        assert(false);
    }
}

