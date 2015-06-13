#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "dsp_dma.h"

#define DMA_CONFIGURATION_AUTOSTART (1 << 0)
#define DMA_CONFIGURATION_AUTOREADY (1 << 1)
#define DMA_CONFIGURATION_IOC_CLEAR (1 << 2)
#define DMA_CONFIGURATION_EOL_CLEAR (1 << 3)
#define DMA_CONFIGURATION_ERR_CLEAR (1 << 4)

#define DMA_CONTROL_ACTION 0x7
#define DMA_CONTROL_ACTION_NOP 0
#define DMA_CONTROL_ACTION_START 1
#define DMA_CONTROL_ACTION_STOP 2
#define DMA_CONTROL_ACTION_FREEZE 3
#define DMA_CONTROL_ACTION_UNFREEZE 4
#define DMA_CONTROL_ACTION_ABORT 5
#define DMA_CONTROL_FROZEN (1 << 3)
#define DMA_CONTROL_RUNNING (1 << 4)
#define DMA_CONTROL_STOPPED (1 << 5)

#define NODE_POINTER_VAL 0x3fff
#define NODE_POINTER_EOL (1 << 14)

#define NODE_CONTROL_DIRECTION (1 << 1)

static void dsp_dma_run(DSPDMAState *s)
{
    if (!(s->control & DMA_CONTROL_RUNNING)
        || (s->control & DMA_CONTROL_FROZEN)) {
        return;
    }
    while (!(s->next_block & NODE_POINTER_EOL)) {
        uint32_t addr = s->next_block & NODE_POINTER_VAL;
        assert((addr+6) < sizeof(s->core->xram));

        uint32_t next_block = s->core->xram[addr];
        uint32_t control = s->core->xram[addr+1];
        uint32_t count = s->core->xram[addr+2];
        uint32_t dsp_offset = s->core->xram[addr+3];
        uint32_t scratch_offset = s->core->xram[addr+4];
        uint32_t scratch_base = s->core->xram[addr+5];
        uint32_t scratch_size = s->core->xram[addr+6]+1;

        printf("\n\n\nQQQ DMA addr %x, control %x, count %x, dsp_offset %x, scratch_offset %x, base %x, size %x\n\n\n",
            addr, control, count, dsp_offset, scratch_offset, scratch_base, scratch_size);

        uint32_t format = (control >> 10) & 7;
        unsigned int item_size;
        uint32_t item_mask;
        switch(format) {
        case 2: //big-endian?
        case 6:
            item_size = 4;
            item_mask = 0x00FFFFFF;
            break;
        default:
            assert(false);
            break;
        }

        uint32_t buf_id = (control >> 5) & 0xf;

        size_t scratch_addr = scratch_offset;
        if (buf_id == 0xe) { // 'circular'?
            assert(scratch_addr == 0);
            assert(scratch_offset + count * item_size < scratch_size);
            scratch_addr += scratch_base; //??
        } else if (buf_id != 0xf) { // 'offset'?
            assert(false);
        }

        uint32_t* dsp_ptr;
        if (dsp_offset < 0x1800) {
            assert(dsp_offset+count < sizeof(s->core->xram));
            dsp_ptr = s->core->xram + dsp_offset;
        } else if (dsp_offset >= 0x1800 && dsp_offset < 0x2000) { //?
            assert(dsp_offset-0x1800 + count < sizeof(s->core->yram));
            dsp_ptr = s->core->yram + dsp_offset - 0x1800;
        } else if (dsp_offset >= 0x2800 && dsp_offset < 0x3800) { //?
            assert(dsp_offset-0x2800 + count < sizeof(s->core->pram));
            dsp_ptr = s->core->pram + dsp_offset - 0x2800;
        } else {
            assert(false);
        }

        uint8_t* scratch_buf = calloc(count, item_size);

        assert(s->scratch_rw);

        if (control & NODE_CONTROL_DIRECTION) {
            int i;
            for (i=0; i<count; i++) {
                switch(item_size) {
                case 4:
                    *(uint32_t*)(scratch_buf + i*4) = dsp_ptr[i];
                    break;
                default:
                    assert(false);
                    break;
                }
            }

            // write to scratch memory
            s->scratch_rw(scratch_buf, scratch_addr, count*item_size, 1);
        } else {
            // read from scratch memory
            s->scratch_rw(scratch_buf, scratch_addr, count*item_size, 0);

            int i;
            for (i=0; i<count; i++) {
                switch(item_size) {
                case 4:
                    dsp_ptr[i] = (*(uint32_t*)(scratch_buf + i*4)) & item_mask;
                    break;
                default:
                    assert(false);
                    break;
                }
            }
        }

        free(scratch_buf);

        s->next_block = next_block;

        if (s->next_block & NODE_POINTER_EOL) {
            s->eol = true;
        }
    }
}

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
        switch(v & DMA_CONTROL_ACTION) {
        case DMA_CONTROL_ACTION_START:
            s->control |= DMA_CONTROL_RUNNING;
            s->control &= ~DMA_CONTROL_STOPPED;
            break;
        case DMA_CONTROL_ACTION_STOP:
            s->control |= DMA_CONTROL_STOPPED;
            s->control &= ~DMA_CONTROL_RUNNING;
            break;
        case DMA_CONTROL_ACTION_FREEZE:
            s->control |= DMA_CONTROL_FROZEN;
            break;
        case DMA_CONTROL_ACTION_UNFREEZE:
            s->control &= ~DMA_CONTROL_FROZEN;
            break;
        default:
            assert(false);
            break;
        }
        dsp_dma_run(s);
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

