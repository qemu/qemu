/*
 * QEMU ESP/NCR53C9x emulation
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

/*
 * On Sparc32, this is the ESP (NCR53C90) part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR53C9X.txt
 */

#ifdef DEBUG_ESP
#define DPRINTF(fmt, args...) \
do { printf("ESP: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define ESP_MAXREG 0x3f
#define TI_BUFSZ 32
/* The HBA is ID 7, so for simplicitly limit to 7 devices.  */
#define ESP_MAX_DEVS      7

typedef struct ESPState ESPState;

struct ESPState {
    BlockDriverState **bd;
    uint8_t rregs[ESP_MAXREG];
    uint8_t wregs[ESP_MAXREG];
    int32_t ti_size;
    uint32_t ti_rptr, ti_wptr;
    uint8_t ti_buf[TI_BUFSZ];
    int sense;
    int dma;
    SCSIDevice *scsi_dev[MAX_DISKS];
    SCSIDevice *current_dev;
    uint8_t cmdbuf[TI_BUFSZ];
    int cmdlen;
    int do_cmd;

    /* The amount of data left in the current DMA transfer.  */
    uint32_t dma_left;
    /* The size of the current DMA transfer.  Zero if no transfer is in
       progress.  */
    uint32_t dma_counter;
    uint8_t *async_buf;
    uint32_t async_len;
    void *dma_opaque;
};

#define STAT_DO 0x00
#define STAT_DI 0x01
#define STAT_CD 0x02
#define STAT_ST 0x03
#define STAT_MI 0x06
#define STAT_MO 0x07

#define STAT_TC 0x10
#define STAT_PE 0x20
#define STAT_GE 0x40
#define STAT_IN 0x80

#define INTR_FC 0x08
#define INTR_BS 0x10
#define INTR_DC 0x20
#define INTR_RST 0x80

#define SEQ_0 0x0
#define SEQ_CD 0x4

static int get_cmd(ESPState *s, uint8_t *buf)
{
    uint32_t dmalen;
    int target;

    dmalen = s->rregs[0] | (s->rregs[1] << 8);
    target = s->wregs[4] & 7;
    DPRINTF("get_cmd: len %d target %d\n", dmalen, target);
    if (s->dma) {
        espdma_memory_read(s->dma_opaque, buf, dmalen);
    } else {
	buf[0] = 0;
	memcpy(&buf[1], s->ti_buf, dmalen);
	dmalen++;
    }

    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;

    if (s->current_dev) {
        /* Started a new command before the old one finished.  Cancel it.  */
        scsi_cancel_io(s->current_dev, 0);
        s->async_len = 0;
    }

    if (target >= MAX_DISKS || !s->scsi_dev[target]) {
        // No such drive
	s->rregs[4] = STAT_IN;
	s->rregs[5] = INTR_DC;
	s->rregs[6] = SEQ_0;
	espdma_raise_irq(s->dma_opaque);
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
    s->ti_size = datalen;
    if (datalen != 0) {
        s->rregs[4] = STAT_IN | STAT_TC;
        s->dma_left = 0;
        s->dma_counter = 0;
        if (datalen > 0) {
            s->rregs[4] |= STAT_DI;
            scsi_read_data(s->current_dev, 0);
        } else {
            s->rregs[4] |= STAT_DO;
            scsi_write_data(s->current_dev, 0);
        }
    }
    s->rregs[5] = INTR_BS | INTR_FC;
    s->rregs[6] = SEQ_CD;
    espdma_raise_irq(s->dma_opaque);
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
        s->rregs[4] = STAT_IN | STAT_TC | STAT_CD;
        s->rregs[5] = INTR_BS | INTR_FC;
        s->rregs[6] = SEQ_CD;
        espdma_raise_irq(s->dma_opaque);
    }
}

static void write_response(ESPState *s)
{
    DPRINTF("Transfer status (sense=%d)\n", s->sense);
    s->ti_buf[0] = s->sense;
    s->ti_buf[1] = 0;
    if (s->dma) {
        espdma_memory_write(s->dma_opaque, s->ti_buf, 2);
	s->rregs[4] = STAT_IN | STAT_TC | STAT_ST;
	s->rregs[5] = INTR_BS | INTR_FC;
	s->rregs[6] = SEQ_CD;
    } else {
	s->ti_size = 2;
	s->ti_rptr = 0;
	s->ti_wptr = 0;
	s->rregs[7] = 2;
    }
    espdma_raise_irq(s->dma_opaque);
}

static void esp_dma_done(ESPState *s)
{
    s->rregs[4] |= STAT_IN | STAT_TC;
    s->rregs[5] = INTR_BS;
    s->rregs[6] = 0;
    s->rregs[7] = 0;
    s->rregs[0] = 0;
    s->rregs[1] = 0;
    espdma_raise_irq(s->dma_opaque);
}

static void esp_do_dma(ESPState *s)
{
    uint32_t len;
    int to_device;

    to_device = (s->ti_size < 0);
    len = s->dma_left;
    if (s->do_cmd) {
        DPRINTF("command len %d + %d\n", s->cmdlen, len);
        espdma_memory_read(s->dma_opaque, &s->cmdbuf[s->cmdlen], len);
        s->ti_size = 0;
        s->cmdlen = 0;
        s->do_cmd = 0;
        do_cmd(s, s->cmdbuf);
        return;
    }
    if (s->async_len == 0) {
        /* Defer until data is available.  */
        return;
    }
    if (len > s->async_len) {
        len = s->async_len;
    }
    if (to_device) {
        espdma_memory_read(s->dma_opaque, s->async_buf, len);
    } else {
        espdma_memory_write(s->dma_opaque, s->async_buf, len);
    }
    s->dma_left -= len;
    s->async_buf += len;
    s->async_len -= len;
    if (to_device)
        s->ti_size += len;
    else
        s->ti_size -= len;
    if (s->async_len == 0) {
        if (to_device) {
            // ti_size is negative
            scsi_write_data(s->current_dev, 0);
        } else {
            scsi_read_data(s->current_dev, 0);
            /* If there is still data to be read from the device then
               complete the DMA operation immeriately.  Otherwise defer
               until the scsi layer has completed.  */
            if (s->dma_left == 0 && s->ti_size > 0) {
                esp_dma_done(s);
            }
        }
    } else {
        /* Partially filled a scsi buffer. Complete immediately.  */
        esp_dma_done(s);
    }
}

static void esp_command_complete(void *opaque, int reason, uint32_t tag,
                                 uint32_t arg)
{
    ESPState *s = (ESPState *)opaque;

    if (reason == SCSI_REASON_DONE) {
        DPRINTF("SCSI Command complete\n");
        if (s->ti_size != 0)
            DPRINTF("SCSI command completed unexpectedly\n");
        s->ti_size = 0;
        s->dma_left = 0;
        s->async_len = 0;
        if (arg)
            DPRINTF("Command failed\n");
        s->sense = arg;
        s->rregs[4] = STAT_ST;
        esp_dma_done(s);
        s->current_dev = NULL;
    } else {
        DPRINTF("transfer %d/%d\n", s->dma_left, s->ti_size);
        s->async_len = arg;
        s->async_buf = scsi_get_buf(s->current_dev, 0);
        if (s->dma_left) {
            esp_do_dma(s);
        } else if (s->dma_counter != 0 && s->ti_size <= 0) {
            /* If this was the last part of a DMA transfer then the
               completion interrupt is deferred to here.  */
            esp_dma_done(s);
        }
    }
}

static void handle_ti(ESPState *s)
{
    uint32_t dmalen, minlen;

    dmalen = s->rregs[0] | (s->rregs[1] << 8);
    if (dmalen==0) {
      dmalen=0x10000;
    }
    s->dma_counter = dmalen;

    if (s->do_cmd)
        minlen = (dmalen < 32) ? dmalen : 32;
    else if (s->ti_size < 0)
        minlen = (dmalen < -s->ti_size) ? dmalen : -s->ti_size;
    else
        minlen = (dmalen < s->ti_size) ? dmalen : s->ti_size;
    DPRINTF("Transfer Information len %d\n", minlen);
    if (s->dma) {
        s->dma_left = minlen;
        s->rregs[4] &= ~STAT_TC;
        esp_do_dma(s);
    } else if (s->do_cmd) {
        DPRINTF("command len %d\n", s->cmdlen);
        s->ti_size = 0;
        s->cmdlen = 0;
        s->do_cmd = 0;
        do_cmd(s, s->cmdbuf);
        return;
    }
}

void esp_reset(void *opaque)
{
    ESPState *s = opaque;

    memset(s->rregs, 0, ESP_MAXREG);
    memset(s->wregs, 0, ESP_MAXREG);
    s->rregs[0x0e] = 0x4; // Indicate fas100a
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
                fprintf(stderr, "esp: PIO data read not implemented\n");
                s->rregs[2] = 0;
            } else {
                s->rregs[2] = s->ti_buf[s->ti_rptr++];
            }
            espdma_raise_irq(s->dma_opaque);
	}
	if (s->ti_size == 0) {
            s->ti_rptr = 0;
            s->ti_wptr = 0;
        }
	break;
    case 5:
        // interrupt
        // Clear interrupt/error status bits
        s->rregs[4] &= ~(STAT_IN | STAT_GE | STAT_PE);
	espdma_clear_irq(s->dma_opaque);
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
        s->rregs[4] &= ~STAT_TC;
        break;
    case 2:
	// FIFO
        if (s->do_cmd) {
            s->cmdbuf[s->cmdlen++] = val & 0xff;
        } else if ((s->rregs[4] & 6) == 0) {
            uint8_t buf;
            buf = val & 0xff;
            s->ti_size--;
            fprintf(stderr, "esp: PIO data write not implemented\n");
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
            /* Reload DMA counter.  */
            s->rregs[0] = s->wregs[0];
            s->rregs[1] = s->wregs[1];
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
                espdma_raise_irq(s->dma_opaque);
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

static void esp_save(QEMUFile *f, void *opaque)
{
    ESPState *s = opaque;

    qemu_put_buffer(f, s->rregs, ESP_MAXREG);
    qemu_put_buffer(f, s->wregs, ESP_MAXREG);
    qemu_put_be32s(f, &s->ti_size);
    qemu_put_be32s(f, &s->ti_rptr);
    qemu_put_be32s(f, &s->ti_wptr);
    qemu_put_buffer(f, s->ti_buf, TI_BUFSZ);
    qemu_put_be32s(f, &s->dma);
}

static int esp_load(QEMUFile *f, void *opaque, int version_id)
{
    ESPState *s = opaque;
    
    if (version_id != 2)
        return -EINVAL; // Cannot emulate 1

    qemu_get_buffer(f, s->rregs, ESP_MAXREG);
    qemu_get_buffer(f, s->wregs, ESP_MAXREG);
    qemu_get_be32s(f, &s->ti_size);
    qemu_get_be32s(f, &s->ti_rptr);
    qemu_get_be32s(f, &s->ti_wptr);
    qemu_get_buffer(f, s->ti_buf, TI_BUFSZ);
    qemu_get_be32s(f, &s->dma);

    return 0;
}

void esp_scsi_attach(void *opaque, BlockDriverState *bd, int id)
{
    ESPState *s = (ESPState *)opaque;

    if (id < 0) {
        for (id = 0; id < ESP_MAX_DEVS; id++) {
            if (s->scsi_dev[id] == NULL)
                break;
        }
    }
    if (id >= ESP_MAX_DEVS) {
        DPRINTF("Bad Device ID %d\n", id);
        return;
    }
    if (s->scsi_dev[id]) {
        DPRINTF("Destroying device %d\n", id);
        scsi_disk_destroy(s->scsi_dev[id]);
    }
    DPRINTF("Attaching block device %d\n", id);
    /* Command queueing is not implemented.  */
    s->scsi_dev[id] = scsi_disk_init(bd, 0, esp_command_complete, s);
}

void *esp_init(BlockDriverState **bd, uint32_t espaddr, void *dma_opaque)
{
    ESPState *s;
    int esp_io_memory;

    s = qemu_mallocz(sizeof(ESPState));
    if (!s)
        return NULL;

    s->bd = bd;
    s->dma_opaque = dma_opaque;

    esp_io_memory = cpu_register_io_memory(0, esp_mem_read, esp_mem_write, s);
    cpu_register_physical_memory(espaddr, ESP_MAXREG*4, esp_io_memory);

    esp_reset(s);

    register_savevm("esp", espaddr, 2, esp_save, esp_load, s);
    qemu_register_reset(esp_reset, s);

    return s;
}
