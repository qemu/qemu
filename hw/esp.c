/*
 * QEMU ESP emulation
 * 
 * Copyright (c) 2005 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "vl.h"

/* debug ESP card */
#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DPRINTF(fmt, args...) \
do { printf("ESP: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define ESPDMA_REGS 4
#define ESPDMA_MAXADDR (ESPDMA_REGS * 4 - 1)
#define ESP_MAXREG 0x3f

typedef struct ESPState {
    BlockDriverState **bd;
    uint8_t regs[ESP_MAXREG];
    int irq;
    uint32_t espdmaregs[ESPDMA_REGS];
} ESPState;

static void esp_reset(void *opaque)
{
    ESPState *s = opaque;
    memset(s->regs, 0, ESP_MAXREG);
    s->regs[0x0e] = 0x4; // Indicate fas100a
    memset(s->espdmaregs, 0, ESPDMA_REGS * 4);
}

static uint32_t esp_mem_readb(void *opaque, target_phys_addr_t addr)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESP_MAXREG) >> 2;
    switch (saddr) {
    default:
	break;
    }
    DPRINTF("esp: read reg[%d]: 0x%2.2x\n", saddr, s->regs[saddr]);
    return s->regs[saddr];
}

static void esp_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESP_MAXREG) >> 2;
    DPRINTF("esp: write reg[%d]: 0x%2.2x -> 0x%2.2x\n", saddr, s->regs[saddr], val);
    switch (saddr) {
    case 3:
	// Command
	switch(val & 0x7f) {
	case 0:
	    DPRINTF("esp: NOP (%2.2x)\n", val);
	    break;
	case 2:
	    DPRINTF("esp: Chip reset (%2.2x)\n", val);
	    esp_reset(s);
	    break;
	case 3:
	    DPRINTF("esp: Bus reset (%2.2x)\n", val);
	    break;
	case 0x1a:
	    DPRINTF("esp: Set ATN (%2.2x)\n", val);
	    break;
	case 0x42:
	    DPRINTF("esp: Select with ATN (%2.2x)\n", val);
	    s->regs[4] = 0x1a; // Status: TCNT | TDONE | CMD
	    s->regs[5] = 0x20; // Intr: Disconnect, nobody there
	    s->regs[6] = 0x4;  // Seq: Cmd done
	    pic_set_irq(s->irq, 1);
	    break;
	}
	break;
    case 4 ... 7:
    case 9 ... 0xf:
	break;
    default:
	s->regs[saddr] = val;
	break;
    }
}

static CPUReadMemoryFunc *esp_mem_read[3] = {
    esp_mem_readb,
    esp_mem_readb,
    esp_mem_readb,
};

static CPUWriteMemoryFunc *esp_mem_write[3] = {
    esp_mem_writeb,
    esp_mem_writeb,
    esp_mem_writeb,
};

static uint32_t espdma_mem_readl(void *opaque, target_phys_addr_t addr)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESPDMA_MAXADDR) >> 2;
    return s->espdmaregs[saddr];
}

static void espdma_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESPDMA_MAXADDR) >> 2;
    s->espdmaregs[saddr] = val;
}

static CPUReadMemoryFunc *espdma_mem_read[3] = {
    espdma_mem_readl,
    espdma_mem_readl,
    espdma_mem_readl,
};

static CPUWriteMemoryFunc *espdma_mem_write[3] = {
    espdma_mem_writel,
    espdma_mem_writel,
    espdma_mem_writel,
};

static void esp_save(QEMUFile *f, void *opaque)
{
    ESPState *s = opaque;
    
}

static int esp_load(QEMUFile *f, void *opaque, int version_id)
{
    ESPState *s = opaque;
    
    if (version_id != 1)
        return -EINVAL;

    return 0;
}

void esp_init(BlockDriverState **bd, int irq, uint32_t espaddr, uint32_t espdaddr)
{
    ESPState *s;
    int esp_io_memory, espdma_io_memory;

    s = qemu_mallocz(sizeof(ESPState));
    if (!s)
        return;

    s->bd = bd;
    s->irq = irq;

    esp_io_memory = cpu_register_io_memory(0, esp_mem_read, esp_mem_write, s);
    cpu_register_physical_memory(espaddr, ESP_MAXREG*4, esp_io_memory);

    espdma_io_memory = cpu_register_io_memory(0, espdma_mem_read, espdma_mem_write, s);
    cpu_register_physical_memory(espdaddr, 16, espdma_io_memory);

    esp_reset(s);

    register_savevm("esp", espaddr, 1, esp_save, esp_load, s);
    qemu_register_reset(esp_reset, s);
}

