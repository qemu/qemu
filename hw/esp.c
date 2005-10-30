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
//#define DEBUG_ESP

#ifdef DEBUG_ESP
#define DPRINTF(fmt, args...) \
do { printf("ESP: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define ESPDMA_REGS 4
#define ESPDMA_MAXADDR (ESPDMA_REGS * 4 - 1)
#define ESP_MAXREG 0x3f
#define TI_BUFSZ 65536
#define DMA_VER 0xa0000000
#define DMA_LOADED 0x04000000

typedef struct ESPState {
    BlockDriverState **bd;
    uint8_t rregs[ESP_MAXREG];
    uint8_t wregs[ESP_MAXREG];
    int irq;
    uint32_t espdmaregs[ESPDMA_REGS];
    uint32_t ti_size;
    uint32_t ti_rptr, ti_wptr;
    int ti_dir;
    uint8_t ti_buf[TI_BUFSZ];
    int dma;
} ESPState;

#define STAT_DO 0x00
#define STAT_DI 0x01
#define STAT_CD 0x02
#define STAT_ST 0x03
#define STAT_MI 0x06
#define STAT_MO 0x07

#define STAT_TC 0x10
#define STAT_IN 0x80

#define INTR_FC 0x08
#define INTR_BS 0x10
#define INTR_DC 0x20

#define SEQ_0 0x0
#define SEQ_CD 0x4

static void handle_satn(ESPState *s)
{
    uint8_t buf[32];
    uint32_t dmaptr, dmalen;
    unsigned int i;
    int64_t nb_sectors;
    int target;

    dmalen = s->wregs[0] | (s->wregs[1] << 8);
    target = s->wregs[4] & 7;
    DPRINTF("Select with ATN len %d target %d\n", dmalen, target);
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
	DPRINTF("DMA Direction: %c, addr 0x%8.8x\n", s->espdmaregs[0] & 0x100? 'w': 'r', dmaptr);
	cpu_physical_memory_read(dmaptr, buf, dmalen);
    } else {
	buf[0] = 0;
	memcpy(&buf[1], s->ti_buf, dmalen);
	dmalen++;
    }
    for (i = 0; i < dmalen; i++) {
	DPRINTF("Command %2.2x\n", buf[i]);
    }
    s->ti_dir = 0;
    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;

    if (target > 4 || !s->bd[target]) { // No such drive
	s->rregs[4] = STAT_IN;
	s->rregs[5] = INTR_DC;
	s->rregs[6] = SEQ_0;
	s->espdmaregs[0] |= 1;
	pic_set_irq(s->irq, 1);
	return;
    }
    switch (buf[1]) {
    case 0x0:
	DPRINTF("Test Unit Ready (len %d)\n", buf[5]);
	break;
    case 0x12:
	DPRINTF("Inquiry (len %d)\n", buf[5]);
	memset(s->ti_buf, 0, 36);
	if (bdrv_get_type_hint(s->bd[target]) == BDRV_TYPE_CDROM) {
	    s->ti_buf[0] = 5;
	    memcpy(&s->ti_buf[16], "QEMU CDROM     ", 16);
	} else {
	    s->ti_buf[0] = 0;
	    memcpy(&s->ti_buf[16], "QEMU HARDDISK  ", 16);
	}
	memcpy(&s->ti_buf[8], "QEMU   ", 8);
	s->ti_buf[2] = 1;
	s->ti_buf[3] = 2;
	s->ti_dir = 1;
	s->ti_size = 36;
	break;
    case 0x1a:
	DPRINTF("Mode Sense(6) (page %d, len %d)\n", buf[3], buf[5]);
	break;
    case 0x25:
	DPRINTF("Read Capacity (len %d)\n", buf[5]);
	memset(s->ti_buf, 0, 8);
	bdrv_get_geometry(s->bd[target], &nb_sectors);
	s->ti_buf[0] = (nb_sectors >> 24) & 0xff;
	s->ti_buf[1] = (nb_sectors >> 16) & 0xff;
	s->ti_buf[2] = (nb_sectors >> 8) & 0xff;
	s->ti_buf[3] = nb_sectors & 0xff;
	s->ti_buf[4] = 0;
	s->ti_buf[5] = 0;
	if (bdrv_get_type_hint(s->bd[target]) == BDRV_TYPE_CDROM)
	    s->ti_buf[6] = 8; // sector size 2048
	else
	    s->ti_buf[6] = 2; // sector size 512
	s->ti_buf[7] = 0;
	s->ti_dir = 1;
	s->ti_size = 8;
	break;
    case 0x28:
	{
	    int64_t offset, len;

	    if (bdrv_get_type_hint(s->bd[target]) == BDRV_TYPE_CDROM) {
		offset = ((buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6]) * 4;
		len = ((buf[8] << 8) | buf[9]) * 4;
		s->ti_size = len * 2048;
	    } else {
		offset = (buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];
		len = (buf[8] << 8) | buf[9];
		s->ti_size = len * 512;
	    }
	    DPRINTF("Read (10) (offset %lld len %lld)\n", offset, len);
	    bdrv_read(s->bd[target], offset, s->ti_buf, len);
	    // XXX error handling
	    s->ti_dir = 1;
	    break;
	}
    case 0x2a:
	{
	    int64_t offset, len;

	    if (bdrv_get_type_hint(s->bd[target]) == BDRV_TYPE_CDROM) {
		offset = ((buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6]) * 4;
		len = ((buf[8] << 8) | buf[9]) * 4;
		s->ti_size = len * 2048;
	    } else {
		offset = (buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];
		len = (buf[8] << 8) | buf[9];
		s->ti_size = len * 512;
	    }
	    DPRINTF("Write (10) (offset %lld len %lld)\n", offset, len);
	    bdrv_write(s->bd[target], offset, s->ti_buf, len);
	    // XXX error handling
	    s->ti_dir = 0;
	    break;
	}
    default:
	DPRINTF("Unknown SCSI command (%2.2x)\n", buf[1]);
	break;
    }
    s->rregs[4] = STAT_IN | STAT_TC | STAT_DI;
    s->rregs[5] = INTR_BS | INTR_FC;
    s->rregs[6] = SEQ_CD;
    s->espdmaregs[0] |= 1;
    pic_set_irq(s->irq, 1);
}

static void dma_write(ESPState *s, const uint8_t *buf, uint32_t len)
{
    uint32_t dmaptr, dmalen;

    dmalen = s->wregs[0] | (s->wregs[1] << 8);
    DPRINTF("Transfer status len %d\n", dmalen);
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
	DPRINTF("DMA Direction: %c\n", s->espdmaregs[0] & 0x100? 'w': 'r');
	cpu_physical_memory_write(dmaptr, buf, len);
	s->rregs[4] = STAT_IN | STAT_TC | STAT_ST;
	s->rregs[5] = INTR_BS | INTR_FC;
	s->rregs[6] = SEQ_CD;
	s->espdmaregs[0] |= 1;
    } else {
	memcpy(s->ti_buf, buf, len);
	s->ti_size = dmalen;
	s->ti_rptr = 0;
	s->ti_wptr = 0;
    }
    pic_set_irq(s->irq, 1);

}

static const uint8_t okbuf[] = {0, 0};

static void handle_ti(ESPState *s)
{
    uint32_t dmaptr, dmalen;
    unsigned int i;

    dmalen = s->wregs[0] | (s->wregs[1] << 8);
    DPRINTF("Transfer Information len %d\n", dmalen);
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
	DPRINTF("DMA Direction: %c, addr 0x%8.8x\n", s->espdmaregs[0] & 0x100? 'w': 'r', dmaptr);
	for (i = 0; i < s->ti_size; i++) {
	    dmaptr = iommu_translate(s->espdmaregs[1] + i);
	    if (s->ti_dir)
		cpu_physical_memory_write(dmaptr, &s->ti_buf[i], 1);
	    else
		cpu_physical_memory_read(dmaptr, &s->ti_buf[i], 1);
	}
	s->rregs[4] = STAT_IN | STAT_TC | STAT_ST;
	s->rregs[5] = INTR_BS;
	s->rregs[6] = 0;
	s->espdmaregs[0] |= 1;
    } else {
	s->ti_size = dmalen;
	s->ti_rptr = 0;
	s->ti_wptr = 0;
    }	
    pic_set_irq(s->irq, 1);
}

static void esp_reset(void *opaque)
{
    ESPState *s = opaque;
    memset(s->rregs, 0, ESP_MAXREG);
    s->rregs[0x0e] = 0x4; // Indicate fas100a
    memset(s->espdmaregs, 0, ESPDMA_REGS * 4);
}

static uint32_t esp_mem_readb(void *opaque, target_phys_addr_t addr)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESP_MAXREG) >> 2;
    switch (saddr) {
    case 2:
	// FIFO
	if (s->ti_size > 0) {
	    s->ti_size--;
	    s->rregs[saddr] = s->ti_buf[s->ti_rptr++];
	    pic_set_irq(s->irq, 1);
	}
	if (s->ti_size == 0) {
            s->ti_rptr = 0;
            s->ti_wptr = 0;
        }
	break;
    default:
	break;
    }
    DPRINTF("read reg[%d]: 0x%2.2x\n", saddr, s->rregs[saddr]);

    return s->rregs[saddr];
}

static void esp_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESP_MAXREG) >> 2;
    DPRINTF("write reg[%d]: 0x%2.2x -> 0x%2.2x\n", saddr, s->wregs[saddr], val);
    switch (saddr) {
    case 0:
    case 1:
        s->rregs[saddr] = val;
        break;
    case 2:
	// FIFO
	s->ti_size++;
	s->ti_buf[s->ti_wptr++] = val & 0xff;
	break;
    case 3:
        s->rregs[saddr] = val;
	// Command
	if (val & 0x80) {
	    s->dma = 1;
	} else {
	    s->dma = 0;
	}
	switch(val & 0x7f) {
	case 0:
	    DPRINTF("NOP (%2.2x)\n", val);
	    break;
	case 1:
	    DPRINTF("Flush FIFO (%2.2x)\n", val);
	    s->rregs[6] = 0;
	    s->rregs[5] = INTR_FC;
	    break;
	case 2:
	    DPRINTF("Chip reset (%2.2x)\n", val);
	    esp_reset(s);
	    break;
	case 3:
	    DPRINTF("Bus reset (%2.2x)\n", val);
	    break;
	case 0x10:
	    handle_ti(s);
	    break;
	case 0x11:
	    DPRINTF("Initiator Command Complete Sequence (%2.2x)\n", val);
	    dma_write(s, okbuf, 2);
	    break;
	case 0x12:
	    DPRINTF("Message Accepted (%2.2x)\n", val);
	    dma_write(s, okbuf, 2);
	    s->rregs[5] = INTR_DC;
	    s->rregs[6] = 0;
	    break;
	case 0x1a:
	    DPRINTF("Set ATN (%2.2x)\n", val);
	    break;
	case 0x42:
	    handle_satn(s);
	    break;
	case 0x43:
	    DPRINTF("Set ATN & stop (%2.2x)\n", val);
	    handle_satn(s);
	    break;
	default:
	    DPRINTF("Unhandled ESP command (%2.2x)\n", val);
	    break;
	}
	break;
    case 4 ... 7:
	break;
    case 8:
        s->rregs[saddr] = val;
        break;
    case 9 ... 10:
        break;
    case 11 ... 15:
        s->rregs[saddr] = val;
        break;
    default:
	break;
    }
    s->wregs[saddr] = val;
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
    DPRINTF("read dmareg[%d]: 0x%8.8x\n", saddr, s->espdmaregs[saddr]);

    return s->espdmaregs[saddr];
}

static void espdma_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESPDMA_MAXADDR) >> 2;
    DPRINTF("write dmareg[%d]: 0x%8.8x -> 0x%8.8x\n", saddr, s->espdmaregs[saddr], val);
    switch (saddr) {
    case 0:
	if (!(val & 0x10))
	    pic_set_irq(s->irq, 0);
	if (val & 0x80) {
            esp_reset(s);
        } else if (val & 0x40) {
            val &= ~0x40;
        } else if (val == 0)
            val = 0x40;
        val &= 0x0fffffff;
        val |= DMA_VER;
	break;
    case 1:
        s->espdmaregs[0] = DMA_LOADED;
        break;
    default:
	break;
    }
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
    unsigned int i;

    qemu_put_buffer(f, s->rregs, ESP_MAXREG);
    qemu_put_buffer(f, s->wregs, ESP_MAXREG);
    qemu_put_be32s(f, &s->irq);
    for (i = 0; i < ESPDMA_REGS; i++)
	qemu_put_be32s(f, &s->espdmaregs[i]);
    qemu_put_be32s(f, &s->ti_size);
    qemu_put_be32s(f, &s->ti_rptr);
    qemu_put_be32s(f, &s->ti_wptr);
    qemu_put_be32s(f, &s->ti_dir);
    qemu_put_buffer(f, s->ti_buf, TI_BUFSZ);
    qemu_put_be32s(f, &s->dma);
}

static int esp_load(QEMUFile *f, void *opaque, int version_id)
{
    ESPState *s = opaque;
    unsigned int i;
    
    if (version_id != 1)
        return -EINVAL;

    qemu_get_buffer(f, s->rregs, ESP_MAXREG);
    qemu_get_buffer(f, s->wregs, ESP_MAXREG);
    qemu_get_be32s(f, &s->irq);
    for (i = 0; i < ESPDMA_REGS; i++)
	qemu_get_be32s(f, &s->espdmaregs[i]);
    qemu_get_be32s(f, &s->ti_size);
    qemu_get_be32s(f, &s->ti_rptr);
    qemu_get_be32s(f, &s->ti_wptr);
    qemu_get_be32s(f, &s->ti_dir);
    qemu_get_buffer(f, s->ti_buf, TI_BUFSZ);
    qemu_get_be32s(f, &s->dma);

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

