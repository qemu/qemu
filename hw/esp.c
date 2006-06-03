/*
 * QEMU ESP emulation
 * 
 * Copyright (c) 2005-2006 Fabrice Bellard
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
#define pic_set_irq(irq, level) \
do { printf("ESP: set_irq(%d): %d\n", (irq), (level)); pic_set_irq((irq),(level));} while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define ESPDMA_REGS 4
#define ESPDMA_MAXADDR (ESPDMA_REGS * 4 - 1)
#define ESP_MAXREG 0x3f
#define TI_BUFSZ 32
#define DMA_VER 0xa0000000
#define DMA_INTR 1
#define DMA_INTREN 0x10
#define DMA_WRITE_MEM 0x100
#define DMA_LOADED 0x04000000
typedef struct ESPState ESPState;

struct ESPState {
    BlockDriverState **bd;
    uint8_t rregs[ESP_MAXREG];
    uint8_t wregs[ESP_MAXREG];
    int irq;
    uint32_t espdmaregs[ESPDMA_REGS];
    uint32_t ti_size;
    uint32_t ti_rptr, ti_wptr;
    uint8_t ti_buf[TI_BUFSZ];
    int sense;
    int dma;
    SCSIDevice *scsi_dev[MAX_DISKS];
    SCSIDevice *current_dev;
    uint8_t cmdbuf[TI_BUFSZ];
    int cmdlen;
    int do_cmd;
};

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
#define INTR_RST 0x80

#define SEQ_0 0x0
#define SEQ_CD 0x4

static int get_cmd(ESPState *s, uint8_t *buf)
{
    uint32_t dmaptr, dmalen;
    int target;

    dmalen = s->wregs[0] | (s->wregs[1] << 8);
    target = s->wregs[4] & 7;
    DPRINTF("get_cmd: len %d target %d\n", dmalen, target);
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
	DPRINTF("DMA Direction: %c, addr 0x%8.8x\n",
                s->espdmaregs[0] & DMA_WRITE_MEM ? 'w': 'r', dmaptr);
	cpu_physical_memory_read(dmaptr, buf, dmalen);
    } else {
	buf[0] = 0;
	memcpy(&buf[1], s->ti_buf, dmalen);
	dmalen++;
    }

    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;

    if (target >= 4 || !s->scsi_dev[target]) {
        // No such drive
	s->rregs[4] = STAT_IN;
	s->rregs[5] = INTR_DC;
	s->rregs[6] = SEQ_0;
	s->espdmaregs[0] |= DMA_INTR;
	pic_set_irq(s->irq, 1);
	return 0;
    }
    s->current_dev = s->scsi_dev[target];
    return dmalen;
}

static void do_cmd(ESPState *s, uint8_t *buf)
{
    int32_t datalen;
    int lun;

    DPRINTF("do_cmd: busid 0x%x\n", buf[0]);
    lun = buf[0] & 7;
    datalen = scsi_send_command(s->current_dev, 0, &buf[1], lun);
    if (datalen == 0) {
        s->ti_size = 0;
    } else {
        s->rregs[4] = STAT_IN | STAT_TC;
        if (datalen > 0) {
            s->rregs[4] |= STAT_DI;
            s->ti_size = datalen;
        } else {
            s->rregs[4] |= STAT_DO;
            s->ti_size = -datalen;
        }
    }
    s->rregs[5] = INTR_BS | INTR_FC;
    s->rregs[6] = SEQ_CD;
    s->espdmaregs[0] |= DMA_INTR;
    pic_set_irq(s->irq, 1);
}

static void handle_satn(ESPState *s)
{
    uint8_t buf[32];
    int len;

    len = get_cmd(s, buf);
    if (len)
        do_cmd(s, buf);
}

static void handle_satn_stop(ESPState *s)
{
    s->cmdlen = get_cmd(s, s->cmdbuf);
    if (s->cmdlen) {
        DPRINTF("Set ATN & Stop: cmdlen %d\n", s->cmdlen);
        s->do_cmd = 1;
        s->espdmaregs[1] += s->cmdlen;
        s->rregs[4] = STAT_IN | STAT_TC | STAT_CD;
        s->rregs[5] = INTR_BS | INTR_FC;
        s->rregs[6] = SEQ_CD;
        s->espdmaregs[0] |= DMA_INTR;
        pic_set_irq(s->irq, 1);
    }
}

static void write_response(ESPState *s)
{
    uint32_t dmaptr;

    DPRINTF("Transfer status (sense=%d)\n", s->sense);
    s->ti_buf[0] = s->sense;
    s->ti_buf[1] = 0;
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
	DPRINTF("DMA Direction: %c\n",
                s->espdmaregs[0] & DMA_WRITE_MEM ? 'w': 'r');
	cpu_physical_memory_write(dmaptr, s->ti_buf, 2);
	s->rregs[4] = STAT_IN | STAT_TC | STAT_ST;
	s->rregs[5] = INTR_BS | INTR_FC;
	s->rregs[6] = SEQ_CD;
    } else {
	s->ti_size = 2;
	s->ti_rptr = 0;
	s->ti_wptr = 0;
	s->rregs[7] = 2;
    }
    s->espdmaregs[0] |= DMA_INTR;
    pic_set_irq(s->irq, 1);

}

static void esp_command_complete(void *opaque, uint32_t tag, int sense)
{
    ESPState *s = (ESPState *)opaque;

    DPRINTF("SCSI Command complete\n");
    if (s->ti_size != 0)
        DPRINTF("SCSI command completed unexpectedly\n");
    s->ti_size = 0;
    if (sense)
        DPRINTF("Command failed\n");
    s->sense = sense;
    s->rregs[4] = STAT_IN | STAT_TC | STAT_ST;
}

static void handle_ti(ESPState *s)
{
    uint32_t dmaptr, dmalen, minlen, len, from, to;
    unsigned int i;
    int to_device;
    uint8_t buf[TARGET_PAGE_SIZE];

    dmalen = s->wregs[0] | (s->wregs[1] << 8);
    if (dmalen==0) {
      dmalen=0x10000;
    }

    if (s->do_cmd)
        minlen = (dmalen < 32) ? dmalen : 32;
    else
        minlen = (dmalen < s->ti_size) ? dmalen : s->ti_size;
    DPRINTF("Transfer Information len %d\n", minlen);
    if (s->dma) {
	dmaptr = iommu_translate(s->espdmaregs[1]);
        /* Check if the transfer writes to to reads from the device.  */
        to_device = (s->espdmaregs[0] & DMA_WRITE_MEM) == 0;
	DPRINTF("DMA Direction: %c, addr 0x%8.8x %08x\n",
                to_device ? 'r': 'w', dmaptr, s->ti_size);
	from = s->espdmaregs[1];
	to = from + minlen;
	for (i = 0; i < minlen; i += len, from += len) {
	    dmaptr = iommu_translate(s->espdmaregs[1] + i);
	    if ((from & TARGET_PAGE_MASK) != (to & TARGET_PAGE_MASK)) {
               len = TARGET_PAGE_SIZE - (from & ~TARGET_PAGE_MASK);
            } else {
	       len = to - from;
            }
            DPRINTF("DMA address p %08x v %08x len %08x, from %08x, to %08x\n", dmaptr, s->espdmaregs[1] + i, len, from, to);
            s->ti_size -= len;
            if (s->do_cmd) {
                DPRINTF("command len %d + %d\n", s->cmdlen, len);
                cpu_physical_memory_read(dmaptr, &s->cmdbuf[s->cmdlen], len);
                s->ti_size = 0;
                s->cmdlen = 0;
                s->do_cmd = 0;
                do_cmd(s, s->cmdbuf);
                return;
            } else {
                if (to_device) {
                    cpu_physical_memory_read(dmaptr, buf, len);
                    scsi_write_data(s->current_dev, buf, len);
                } else {
                    scsi_read_data(s->current_dev, buf, len);
                    cpu_physical_memory_write(dmaptr, buf, len);
                }
            }
        }
        if (s->ti_size) {
	    s->rregs[4] = STAT_IN | STAT_TC | (to_device ? STAT_DO : STAT_DI);
        }
        s->rregs[5] = INTR_BS;
	s->rregs[6] = 0;
	s->rregs[7] = 0;
	s->espdmaregs[0] |= DMA_INTR;
    } else if (s->do_cmd) {
        DPRINTF("command len %d\n", s->cmdlen);
        s->ti_size = 0;
        s->cmdlen = 0;
        s->do_cmd = 0;
        do_cmd(s, s->cmdbuf);
        return;
    }
    pic_set_irq(s->irq, 1);
}

static void esp_reset(void *opaque)
{
    ESPState *s = opaque;
    memset(s->rregs, 0, ESP_MAXREG);
    memset(s->wregs, 0, ESP_MAXREG);
    s->rregs[0x0e] = 0x4; // Indicate fas100a
    memset(s->espdmaregs, 0, ESPDMA_REGS * 4);
    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;
    s->dma = 0;
    s->do_cmd = 0;
}

static uint32_t esp_mem_readb(void *opaque, target_phys_addr_t addr)
{
    ESPState *s = opaque;
    uint32_t saddr;

    saddr = (addr & ESP_MAXREG) >> 2;
    DPRINTF("read reg[%d]: 0x%2.2x\n", saddr, s->rregs[saddr]);
    switch (saddr) {
    case 2:
	// FIFO
	if (s->ti_size > 0) {
	    s->ti_size--;
            if ((s->rregs[4] & 6) == 0) {
                /* Data in/out.  */
                scsi_read_data(s->current_dev, &s->rregs[2], 0);
            } else {
                s->rregs[2] = s->ti_buf[s->ti_rptr++];
            }
	    pic_set_irq(s->irq, 1);
	}
	if (s->ti_size == 0) {
            s->ti_rptr = 0;
            s->ti_wptr = 0;
        }
	break;
    case 5:
        // interrupt
        // Clear status bits except TC
        s->rregs[4] &= STAT_TC;
        pic_set_irq(s->irq, 0);
	s->espdmaregs[0] &= ~DMA_INTR;
        break;
    default:
	break;
    }
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
        if (s->do_cmd) {
            s->cmdbuf[s->cmdlen++] = val & 0xff;
        } else if ((s->rregs[4] & 6) == 0) {
            uint8_t buf;
            buf = val & 0xff;
            s->ti_size--;
            scsi_write_data(s->current_dev, &buf, 0);
        } else {
            s->ti_size++;
            s->ti_buf[s->ti_wptr++] = val & 0xff;
        }
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
            //s->ti_size = 0;
	    s->rregs[5] = INTR_FC;
	    s->rregs[6] = 0;
	    break;
	case 2:
	    DPRINTF("Chip reset (%2.2x)\n", val);
	    esp_reset(s);
	    break;
	case 3:
	    DPRINTF("Bus reset (%2.2x)\n", val);
	    s->rregs[5] = INTR_RST;
            if (!(s->wregs[8] & 0x40)) {
                s->espdmaregs[0] |= DMA_INTR;
                pic_set_irq(s->irq, 1);
            }
	    break;
	case 0x10:
	    handle_ti(s);
	    break;
	case 0x11:
	    DPRINTF("Initiator Command Complete Sequence (%2.2x)\n", val);
	    write_response(s);
	    break;
	case 0x12:
	    DPRINTF("Message Accepted (%2.2x)\n", val);
	    write_response(s);
	    s->rregs[5] = INTR_DC;
	    s->rregs[6] = 0;
	    break;
	case 0x1a:
	    DPRINTF("Set ATN (%2.2x)\n", val);
	    break;
	case 0x42:
	    DPRINTF("Set ATN (%2.2x)\n", val);
	    handle_satn(s);
	    break;
	case 0x43:
	    DPRINTF("Set ATN & stop (%2.2x)\n", val);
	    handle_satn_stop(s);
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
    case 11:
        s->rregs[saddr] = val & 0x15;
        break;
    case 12 ... 15:
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
	if (!(val & DMA_INTREN))
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
        s->espdmaregs[0] |= DMA_LOADED;
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
    qemu_get_buffer(f, s->ti_buf, TI_BUFSZ);
    qemu_get_be32s(f, &s->dma);

    return 0;
}

void esp_init(BlockDriverState **bd, int irq, uint32_t espaddr, uint32_t espdaddr)
{
    ESPState *s;
    int esp_io_memory, espdma_io_memory;
    int i;

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
    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i]) {
            s->scsi_dev[i] =
                scsi_disk_init(bs_table[i], esp_command_complete, s);
        }
    }
}

