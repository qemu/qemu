/*
 * QEMU ESP/NCR53C9x emulation
 *
 * Copyright (c) 2005-2006 Fabrice Bellard
 * Copyright (c) 2012 Herve Poussineau
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

#include "sysbus.h"
#include "pci.h"
#include "scsi.h"
#include "esp.h"
#include "trace.h"
#include "qemu-log.h"

/*
 * On Sparc32, this is the ESP (NCR53C90) part of chip STP2000 (Master I/O),
 * also produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR53C9X.txt
 */

#define ESP_REGS 16
#define TI_BUFSZ 16

typedef struct ESPState ESPState;

struct ESPState {
    uint8_t rregs[ESP_REGS];
    uint8_t wregs[ESP_REGS];
    qemu_irq irq;
    uint8_t chip_id;
    int32_t ti_size;
    uint32_t ti_rptr, ti_wptr;
    uint32_t status;
    uint32_t dma;
    uint8_t ti_buf[TI_BUFSZ];
    SCSIBus bus;
    SCSIDevice *current_dev;
    SCSIRequest *current_req;
    uint8_t cmdbuf[TI_BUFSZ];
    uint32_t cmdlen;
    uint32_t do_cmd;

    /* The amount of data left in the current DMA transfer.  */
    uint32_t dma_left;
    /* The size of the current DMA transfer.  Zero if no transfer is in
       progress.  */
    uint32_t dma_counter;
    int dma_enabled;

    uint32_t async_len;
    uint8_t *async_buf;

    ESPDMAMemoryReadWriteFunc dma_memory_read;
    ESPDMAMemoryReadWriteFunc dma_memory_write;
    void *dma_opaque;
    void (*dma_cb)(ESPState *s);
};

#define ESP_TCLO   0x0
#define ESP_TCMID  0x1
#define ESP_FIFO   0x2
#define ESP_CMD    0x3
#define ESP_RSTAT  0x4
#define ESP_WBUSID 0x4
#define ESP_RINTR  0x5
#define ESP_WSEL   0x5
#define ESP_RSEQ   0x6
#define ESP_WSYNTP 0x6
#define ESP_RFLAGS 0x7
#define ESP_WSYNO  0x7
#define ESP_CFG1   0x8
#define ESP_RRES1  0x9
#define ESP_WCCF   0x9
#define ESP_RRES2  0xa
#define ESP_WTEST  0xa
#define ESP_CFG2   0xb
#define ESP_CFG3   0xc
#define ESP_RES3   0xd
#define ESP_TCHI   0xe
#define ESP_RES4   0xf

#define CMD_DMA 0x80
#define CMD_CMD 0x7f

#define CMD_NOP      0x00
#define CMD_FLUSH    0x01
#define CMD_RESET    0x02
#define CMD_BUSRESET 0x03
#define CMD_TI       0x10
#define CMD_ICCS     0x11
#define CMD_MSGACC   0x12
#define CMD_PAD      0x18
#define CMD_SATN     0x1a
#define CMD_RSTATN   0x1b
#define CMD_SEL      0x41
#define CMD_SELATN   0x42
#define CMD_SELATNS  0x43
#define CMD_ENSEL    0x44
#define CMD_DISSEL   0x45

#define STAT_DO 0x00
#define STAT_DI 0x01
#define STAT_CD 0x02
#define STAT_ST 0x03
#define STAT_MO 0x06
#define STAT_MI 0x07
#define STAT_PIO_MASK 0x06

#define STAT_TC 0x10
#define STAT_PE 0x20
#define STAT_GE 0x40
#define STAT_INT 0x80

#define BUSID_DID 0x07

#define INTR_FC 0x08
#define INTR_BS 0x10
#define INTR_DC 0x20
#define INTR_RST 0x80

#define SEQ_0 0x0
#define SEQ_CD 0x4

#define CFG1_RESREPT 0x40

#define TCHI_FAS100A 0x4
#define TCHI_AM53C974 0x12

static void esp_raise_irq(ESPState *s)
{
    if (!(s->rregs[ESP_RSTAT] & STAT_INT)) {
        s->rregs[ESP_RSTAT] |= STAT_INT;
        qemu_irq_raise(s->irq);
        trace_esp_raise_irq();
    }
}

static void esp_lower_irq(ESPState *s)
{
    if (s->rregs[ESP_RSTAT] & STAT_INT) {
        s->rregs[ESP_RSTAT] &= ~STAT_INT;
        qemu_irq_lower(s->irq);
        trace_esp_lower_irq();
    }
}

static void esp_dma_enable(ESPState *s, int irq, int level)
{
    if (level) {
        s->dma_enabled = 1;
        trace_esp_dma_enable();
        if (s->dma_cb) {
            s->dma_cb(s);
            s->dma_cb = NULL;
        }
    } else {
        trace_esp_dma_disable();
        s->dma_enabled = 0;
    }
}

static void esp_request_cancelled(SCSIRequest *req)
{
    ESPState *s = req->hba_private;

    if (req == s->current_req) {
        scsi_req_unref(s->current_req);
        s->current_req = NULL;
        s->current_dev = NULL;
    }
}

static uint32_t get_cmd(ESPState *s, uint8_t *buf)
{
    uint32_t dmalen;
    int target;

    target = s->wregs[ESP_WBUSID] & BUSID_DID;
    if (s->dma) {
        dmalen = s->rregs[ESP_TCLO] | (s->rregs[ESP_TCMID] << 8);
        s->dma_memory_read(s->dma_opaque, buf, dmalen);
    } else {
        dmalen = s->ti_size;
        memcpy(buf, s->ti_buf, dmalen);
        buf[0] = buf[2] >> 5;
    }
    trace_esp_get_cmd(dmalen, target);

    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;

    if (s->current_req) {
        /* Started a new command before the old one finished.  Cancel it.  */
        scsi_req_cancel(s->current_req);
        s->async_len = 0;
    }

    s->current_dev = scsi_device_find(&s->bus, 0, target, 0);
    if (!s->current_dev) {
        // No such drive
        s->rregs[ESP_RSTAT] = 0;
        s->rregs[ESP_RINTR] = INTR_DC;
        s->rregs[ESP_RSEQ] = SEQ_0;
        esp_raise_irq(s);
        return 0;
    }
    return dmalen;
}

static void do_busid_cmd(ESPState *s, uint8_t *buf, uint8_t busid)
{
    int32_t datalen;
    int lun;
    SCSIDevice *current_lun;

    trace_esp_do_busid_cmd(busid);
    lun = busid & 7;
    current_lun = scsi_device_find(&s->bus, 0, s->current_dev->id, lun);
    s->current_req = scsi_req_new(current_lun, 0, lun, buf, s);
    datalen = scsi_req_enqueue(s->current_req);
    s->ti_size = datalen;
    if (datalen != 0) {
        s->rregs[ESP_RSTAT] = STAT_TC;
        s->dma_left = 0;
        s->dma_counter = 0;
        if (datalen > 0) {
            s->rregs[ESP_RSTAT] |= STAT_DI;
        } else {
            s->rregs[ESP_RSTAT] |= STAT_DO;
        }
        scsi_req_continue(s->current_req);
    }
    s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
    s->rregs[ESP_RSEQ] = SEQ_CD;
    esp_raise_irq(s);
}

static void do_cmd(ESPState *s, uint8_t *buf)
{
    uint8_t busid = buf[0];

    do_busid_cmd(s, &buf[1], busid);
}

static void handle_satn(ESPState *s)
{
    uint8_t buf[32];
    int len;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn;
        return;
    }
    len = get_cmd(s, buf);
    if (len)
        do_cmd(s, buf);
}

static void handle_s_without_atn(ESPState *s)
{
    uint8_t buf[32];
    int len;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_s_without_atn;
        return;
    }
    len = get_cmd(s, buf);
    if (len) {
        do_busid_cmd(s, buf, 0);
    }
}

static void handle_satn_stop(ESPState *s)
{
    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn_stop;
        return;
    }
    s->cmdlen = get_cmd(s, s->cmdbuf);
    if (s->cmdlen) {
        trace_esp_handle_satn_stop(s->cmdlen);
        s->do_cmd = 1;
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
        s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        esp_raise_irq(s);
    }
}

static void write_response(ESPState *s)
{
    trace_esp_write_response(s->status);
    s->ti_buf[0] = s->status;
    s->ti_buf[1] = 0;
    if (s->dma) {
        s->dma_memory_write(s->dma_opaque, s->ti_buf, 2);
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
        s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
    } else {
        s->ti_size = 2;
        s->ti_rptr = 0;
        s->ti_wptr = 0;
        s->rregs[ESP_RFLAGS] = 2;
    }
    esp_raise_irq(s);
}

static void esp_dma_done(ESPState *s)
{
    s->rregs[ESP_RSTAT] |= STAT_TC;
    s->rregs[ESP_RINTR] = INTR_BS;
    s->rregs[ESP_RSEQ] = 0;
    s->rregs[ESP_RFLAGS] = 0;
    s->rregs[ESP_TCLO] = 0;
    s->rregs[ESP_TCMID] = 0;
    esp_raise_irq(s);
}

static void esp_do_dma(ESPState *s)
{
    uint32_t len;
    int to_device;

    to_device = (s->ti_size < 0);
    len = s->dma_left;
    if (s->do_cmd) {
        trace_esp_do_dma(s->cmdlen, len);
        s->dma_memory_read(s->dma_opaque, &s->cmdbuf[s->cmdlen], len);
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
        s->dma_memory_read(s->dma_opaque, s->async_buf, len);
    } else {
        s->dma_memory_write(s->dma_opaque, s->async_buf, len);
    }
    s->dma_left -= len;
    s->async_buf += len;
    s->async_len -= len;
    if (to_device)
        s->ti_size += len;
    else
        s->ti_size -= len;
    if (s->async_len == 0) {
        scsi_req_continue(s->current_req);
        /* If there is still data to be read from the device then
           complete the DMA operation immediately.  Otherwise defer
           until the scsi layer has completed.  */
        if (to_device || s->dma_left != 0 || s->ti_size == 0) {
            return;
        }
    }

    /* Partially filled a scsi buffer. Complete immediately.  */
    esp_dma_done(s);
}

static void esp_command_complete(SCSIRequest *req, uint32_t status,
                                 size_t resid)
{
    ESPState *s = req->hba_private;

    trace_esp_command_complete();
    if (s->ti_size != 0) {
        trace_esp_command_complete_unexpected();
    }
    s->ti_size = 0;
    s->dma_left = 0;
    s->async_len = 0;
    if (status) {
        trace_esp_command_complete_fail();
    }
    s->status = status;
    s->rregs[ESP_RSTAT] = STAT_ST;
    esp_dma_done(s);
    if (s->current_req) {
        scsi_req_unref(s->current_req);
        s->current_req = NULL;
        s->current_dev = NULL;
    }
}

static void esp_transfer_data(SCSIRequest *req, uint32_t len)
{
    ESPState *s = req->hba_private;

    trace_esp_transfer_data(s->dma_left, s->ti_size);
    s->async_len = len;
    s->async_buf = scsi_req_get_buf(req);
    if (s->dma_left) {
        esp_do_dma(s);
    } else if (s->dma_counter != 0 && s->ti_size <= 0) {
        /* If this was the last part of a DMA transfer then the
           completion interrupt is deferred to here.  */
        esp_dma_done(s);
    }
}

static void handle_ti(ESPState *s)
{
    uint32_t dmalen, minlen;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_ti;
        return;
    }

    dmalen = s->rregs[ESP_TCLO] | (s->rregs[ESP_TCMID] << 8);
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
    trace_esp_handle_ti(minlen);
    if (s->dma) {
        s->dma_left = minlen;
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        esp_do_dma(s);
    } else if (s->do_cmd) {
        trace_esp_handle_ti_cmd(s->cmdlen);
        s->ti_size = 0;
        s->cmdlen = 0;
        s->do_cmd = 0;
        do_cmd(s, s->cmdbuf);
        return;
    }
}

static void esp_hard_reset(ESPState *s)
{
    memset(s->rregs, 0, ESP_REGS);
    memset(s->wregs, 0, ESP_REGS);
    s->rregs[ESP_TCHI] = s->chip_id;
    s->ti_size = 0;
    s->ti_rptr = 0;
    s->ti_wptr = 0;
    s->dma = 0;
    s->do_cmd = 0;
    s->dma_cb = NULL;

    s->rregs[ESP_CFG1] = 7;
}

static void esp_soft_reset(ESPState *s)
{
    qemu_irq_lower(s->irq);
    esp_hard_reset(s);
}

static void parent_esp_reset(ESPState *s, int irq, int level)
{
    if (level) {
        esp_soft_reset(s);
    }
}

static uint64_t esp_reg_read(ESPState *s, uint32_t saddr)
{
    uint32_t old_val;

    trace_esp_mem_readb(saddr, s->rregs[saddr]);
    switch (saddr) {
    case ESP_FIFO:
        if (s->ti_size > 0) {
            s->ti_size--;
            if ((s->rregs[ESP_RSTAT] & STAT_PIO_MASK) == 0) {
                /* Data out.  */
                qemu_log_mask(LOG_UNIMP,
                              "esp: PIO data read not implemented\n");
                s->rregs[ESP_FIFO] = 0;
            } else {
                s->rregs[ESP_FIFO] = s->ti_buf[s->ti_rptr++];
            }
            esp_raise_irq(s);
        }
        if (s->ti_size == 0) {
            s->ti_rptr = 0;
            s->ti_wptr = 0;
        }
        break;
    case ESP_RINTR:
        /* Clear sequence step, interrupt register and all status bits
           except TC */
        old_val = s->rregs[ESP_RINTR];
        s->rregs[ESP_RINTR] = 0;
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        esp_lower_irq(s);

        return old_val;
    default:
        break;
    }
    return s->rregs[saddr];
}

static void esp_reg_write(ESPState *s, uint32_t saddr, uint64_t val)
{
    trace_esp_mem_writeb(saddr, s->wregs[saddr], val);
    switch (saddr) {
    case ESP_TCLO:
    case ESP_TCMID:
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        break;
    case ESP_FIFO:
        if (s->do_cmd) {
            s->cmdbuf[s->cmdlen++] = val & 0xff;
        } else if (s->ti_size == TI_BUFSZ - 1) {
            trace_esp_error_fifo_overrun();
        } else {
            s->ti_size++;
            s->ti_buf[s->ti_wptr++] = val & 0xff;
        }
        break;
    case ESP_CMD:
        s->rregs[saddr] = val;
        if (val & CMD_DMA) {
            s->dma = 1;
            /* Reload DMA counter.  */
            s->rregs[ESP_TCLO] = s->wregs[ESP_TCLO];
            s->rregs[ESP_TCMID] = s->wregs[ESP_TCMID];
        } else {
            s->dma = 0;
        }
        switch(val & CMD_CMD) {
        case CMD_NOP:
            trace_esp_mem_writeb_cmd_nop(val);
            break;
        case CMD_FLUSH:
            trace_esp_mem_writeb_cmd_flush(val);
            //s->ti_size = 0;
            s->rregs[ESP_RINTR] = INTR_FC;
            s->rregs[ESP_RSEQ] = 0;
            s->rregs[ESP_RFLAGS] = 0;
            break;
        case CMD_RESET:
            trace_esp_mem_writeb_cmd_reset(val);
            esp_soft_reset(s);
            break;
        case CMD_BUSRESET:
            trace_esp_mem_writeb_cmd_bus_reset(val);
            s->rregs[ESP_RINTR] = INTR_RST;
            if (!(s->wregs[ESP_CFG1] & CFG1_RESREPT)) {
                esp_raise_irq(s);
            }
            break;
        case CMD_TI:
            handle_ti(s);
            break;
        case CMD_ICCS:
            trace_esp_mem_writeb_cmd_iccs(val);
            write_response(s);
            s->rregs[ESP_RINTR] = INTR_FC;
            s->rregs[ESP_RSTAT] |= STAT_MI;
            break;
        case CMD_MSGACC:
            trace_esp_mem_writeb_cmd_msgacc(val);
            s->rregs[ESP_RINTR] = INTR_DC;
            s->rregs[ESP_RSEQ] = 0;
            s->rregs[ESP_RFLAGS] = 0;
            esp_raise_irq(s);
            break;
        case CMD_PAD:
            trace_esp_mem_writeb_cmd_pad(val);
            s->rregs[ESP_RSTAT] = STAT_TC;
            s->rregs[ESP_RINTR] = INTR_FC;
            s->rregs[ESP_RSEQ] = 0;
            break;
        case CMD_SATN:
            trace_esp_mem_writeb_cmd_satn(val);
            break;
        case CMD_RSTATN:
            trace_esp_mem_writeb_cmd_rstatn(val);
            break;
        case CMD_SEL:
            trace_esp_mem_writeb_cmd_sel(val);
            handle_s_without_atn(s);
            break;
        case CMD_SELATN:
            trace_esp_mem_writeb_cmd_selatn(val);
            handle_satn(s);
            break;
        case CMD_SELATNS:
            trace_esp_mem_writeb_cmd_selatns(val);
            handle_satn_stop(s);
            break;
        case CMD_ENSEL:
            trace_esp_mem_writeb_cmd_ensel(val);
            s->rregs[ESP_RINTR] = 0;
            break;
        case CMD_DISSEL:
            trace_esp_mem_writeb_cmd_dissel(val);
            s->rregs[ESP_RINTR] = 0;
            esp_raise_irq(s);
            break;
        default:
            trace_esp_error_unhandled_command(val);
            break;
        }
        break;
    case ESP_WBUSID ... ESP_WSYNO:
        break;
    case ESP_CFG1:
        s->rregs[saddr] = val;
        break;
    case ESP_WCCF ... ESP_WTEST:
        break;
    case ESP_CFG2 ... ESP_RES4:
        s->rregs[saddr] = val;
        break;
    default:
        trace_esp_error_invalid_write(val, saddr);
        return;
    }
    s->wregs[saddr] = val;
}

static bool esp_mem_accepts(void *opaque, target_phys_addr_t addr,
                            unsigned size, bool is_write)
{
    return (size == 1) || (is_write && size == 4);
}

static const VMStateDescription vmstate_esp = {
    .name ="esp",
    .version_id = 3,
    .minimum_version_id = 3,
    .minimum_version_id_old = 3,
    .fields      = (VMStateField []) {
        VMSTATE_BUFFER(rregs, ESPState),
        VMSTATE_BUFFER(wregs, ESPState),
        VMSTATE_INT32(ti_size, ESPState),
        VMSTATE_UINT32(ti_rptr, ESPState),
        VMSTATE_UINT32(ti_wptr, ESPState),
        VMSTATE_BUFFER(ti_buf, ESPState),
        VMSTATE_UINT32(status, ESPState),
        VMSTATE_UINT32(dma, ESPState),
        VMSTATE_BUFFER(cmdbuf, ESPState),
        VMSTATE_UINT32(cmdlen, ESPState),
        VMSTATE_UINT32(do_cmd, ESPState),
        VMSTATE_UINT32(dma_left, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t it_shift;
    ESPState esp;
} SysBusESPState;

static void sysbus_esp_mem_write(void *opaque, target_phys_addr_t addr,
                                 uint64_t val, unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    uint32_t saddr;

    saddr = addr >> sysbus->it_shift;
    esp_reg_write(&sysbus->esp, saddr, val);
}

static uint64_t sysbus_esp_mem_read(void *opaque, target_phys_addr_t addr,
                                    unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    uint32_t saddr;

    saddr = addr >> sysbus->it_shift;
    return esp_reg_read(&sysbus->esp, saddr);
}

static const MemoryRegionOps sysbus_esp_mem_ops = {
    .read = sysbus_esp_mem_read,
    .write = sysbus_esp_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.accepts = esp_mem_accepts,
};

void esp_init(target_phys_addr_t espaddr, int it_shift,
              ESPDMAMemoryReadWriteFunc dma_memory_read,
              ESPDMAMemoryReadWriteFunc dma_memory_write,
              void *dma_opaque, qemu_irq irq, qemu_irq *reset,
              qemu_irq *dma_enable)
{
    DeviceState *dev;
    SysBusDevice *s;
    SysBusESPState *sysbus;
    ESPState *esp;

    dev = qdev_create(NULL, "esp");
    sysbus = DO_UPCAST(SysBusESPState, busdev.qdev, dev);
    esp = &sysbus->esp;
    esp->dma_memory_read = dma_memory_read;
    esp->dma_memory_write = dma_memory_write;
    esp->dma_opaque = dma_opaque;
    sysbus->it_shift = it_shift;
    /* XXX for now until rc4030 has been changed to use DMA enable signal */
    esp->dma_enabled = 1;
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_connect_irq(s, 0, irq);
    sysbus_mmio_map(s, 0, espaddr);
    *reset = qdev_get_gpio_in(dev, 0);
    *dma_enable = qdev_get_gpio_in(dev, 1);
}

static const struct SCSIBusInfo esp_scsi_info = {
    .tcq = false,
    .max_target = ESP_MAX_DEVS,
    .max_lun = 7,

    .transfer_data = esp_transfer_data,
    .complete = esp_command_complete,
    .cancel = esp_request_cancelled
};

static void sysbus_esp_gpio_demux(void *opaque, int irq, int level)
{
    DeviceState *d = opaque;
    SysBusESPState *sysbus = container_of(d, SysBusESPState, busdev.qdev);
    ESPState *s = &sysbus->esp;

    switch (irq) {
    case 0:
        parent_esp_reset(s, irq, level);
        break;
    case 1:
        esp_dma_enable(opaque, irq, level);
        break;
    }
}

static int sysbus_esp_init(SysBusDevice *dev)
{
    SysBusESPState *sysbus = FROM_SYSBUS(SysBusESPState, dev);
    ESPState *s = &sysbus->esp;

    sysbus_init_irq(dev, &s->irq);
    assert(sysbus->it_shift != -1);

    s->chip_id = TCHI_FAS100A;
    memory_region_init_io(&sysbus->iomem, &sysbus_esp_mem_ops, sysbus,
                          "esp", ESP_REGS << sysbus->it_shift);
    sysbus_init_mmio(dev, &sysbus->iomem);

    qdev_init_gpio_in(&dev->qdev, sysbus_esp_gpio_demux, 2);

    scsi_bus_new(&s->bus, &dev->qdev, &esp_scsi_info);
    return scsi_bus_legacy_handle_cmdline(&s->bus);
}

static void sysbus_esp_hard_reset(DeviceState *dev)
{
    SysBusESPState *sysbus = DO_UPCAST(SysBusESPState, busdev.qdev, dev);
    esp_hard_reset(&sysbus->esp);
}

static const VMStateDescription vmstate_sysbus_esp_scsi = {
    .name = "sysbusespscsi",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(esp, SysBusESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static void sysbus_esp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = sysbus_esp_init;
    dc->reset = sysbus_esp_hard_reset;
    dc->vmsd = &vmstate_sysbus_esp_scsi;
}

static TypeInfo sysbus_esp_info = {
    .name          = "esp",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusESPState),
    .class_init    = sysbus_esp_class_init,
};

#define DMA_CMD   0x0
#define DMA_STC   0x1
#define DMA_SPA   0x2
#define DMA_WBC   0x3
#define DMA_WAC   0x4
#define DMA_STAT  0x5
#define DMA_SMDLA 0x6
#define DMA_WMAC  0x7

#define DMA_CMD_MASK   0x03
#define DMA_CMD_DIAG   0x04
#define DMA_CMD_MDL    0x10
#define DMA_CMD_INTE_P 0x20
#define DMA_CMD_INTE_D 0x40
#define DMA_CMD_DIR    0x80

#define DMA_STAT_PWDN    0x01
#define DMA_STAT_ERROR   0x02
#define DMA_STAT_ABORT   0x04
#define DMA_STAT_DONE    0x08
#define DMA_STAT_SCSIINT 0x10
#define DMA_STAT_BCMBLT  0x20

#define SBAC_STATUS 0x1000

typedef struct PCIESPState {
    PCIDevice dev;
    MemoryRegion io;
    uint32_t dma_regs[8];
    uint32_t sbac;
    ESPState esp;
} PCIESPState;

static void esp_pci_handle_idle(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_idle(val);
    esp_dma_enable(&pci->esp, 0, 0);
}

static void esp_pci_handle_blast(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_blast(val);
    qemu_log_mask(LOG_UNIMP, "am53c974: cmd BLAST not implemented\n");
}

static void esp_pci_handle_abort(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_abort(val);
    if (pci->esp.current_req) {
        scsi_req_cancel(pci->esp.current_req);
    }
}

static void esp_pci_handle_start(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_start(val);

    pci->dma_regs[DMA_WBC] = pci->dma_regs[DMA_STC];
    pci->dma_regs[DMA_WAC] = pci->dma_regs[DMA_SPA];
    pci->dma_regs[DMA_WMAC] = pci->dma_regs[DMA_SMDLA];

    pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR | DMA_STAT_PWDN);

    esp_dma_enable(&pci->esp, 0, 1);
}

static void esp_pci_dma_write(PCIESPState *pci, uint32_t saddr, uint32_t val)
{
    trace_esp_pci_dma_write(saddr, pci->dma_regs[saddr], val);
    switch (saddr) {
    case DMA_CMD:
        pci->dma_regs[saddr] = val;
        switch (val & DMA_CMD_MASK) {
        case 0x0: /* IDLE */
            esp_pci_handle_idle(pci, val);
            break;
        case 0x1: /* BLAST */
            esp_pci_handle_blast(pci, val);
            break;
        case 0x2: /* ABORT */
            esp_pci_handle_abort(pci, val);
            break;
        case 0x3: /* START */
            esp_pci_handle_start(pci, val);
            break;
        default: /* can't happen */
            abort();
        }
        break;
    case DMA_STC:
    case DMA_SPA:
    case DMA_SMDLA:
        pci->dma_regs[saddr] = val;
        break;
    case DMA_STAT:
        if (!(pci->sbac & SBAC_STATUS)) {
            /* clear some bits on write */
            uint32_t mask = DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE;
            pci->dma_regs[DMA_STAT] &= ~(val & mask);
        }
        break;
    default:
        trace_esp_pci_error_invalid_write_dma(val, saddr);
        return;
    }
}

static uint32_t esp_pci_dma_read(PCIESPState *pci, uint32_t saddr)
{
    uint32_t val;

    val = pci->dma_regs[saddr];
    if (saddr == DMA_STAT) {
        if (pci->esp.rregs[ESP_RSTAT] & STAT_INT) {
            val |= DMA_STAT_SCSIINT;
        }
        if (pci->sbac & SBAC_STATUS) {
            pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_ERROR | DMA_STAT_ABORT |
                                         DMA_STAT_DONE);
        }
    }

    trace_esp_pci_dma_read(saddr, val);
    return val;
}

static void esp_pci_io_write(void *opaque, target_phys_addr_t addr,
                             uint64_t val, unsigned int size)
{
    PCIESPState *pci = opaque;

    if (size < 4 || addr & 3) {
        /* need to upgrade request: we only support 4-bytes accesses */
        uint32_t current = 0, mask;
        int shift;

        if (addr < 0x40) {
            current = pci->esp.wregs[addr >> 2];
        } else if (addr < 0x60) {
            current = pci->dma_regs[(addr - 0x40) >> 2];
        } else if (addr < 0x74) {
            current = pci->sbac;
        }

        shift = (4 - size) * 8;
        mask = (~(uint32_t)0 << shift) >> shift;

        shift = ((4 - (addr & 3)) & 3) * 8;
        val <<= shift;
        val |= current & ~(mask << shift);
        addr &= ~3;
        size = 4;
    }

    if (addr < 0x40) {
        /* SCSI core reg */
        esp_reg_write(&pci->esp, addr >> 2, val);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        esp_pci_dma_write(pci, (addr - 0x40) >> 2, val);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        trace_esp_pci_sbac_write(pci->sbac, val);
        pci->sbac = val;
    } else {
        trace_esp_pci_error_invalid_write((int)addr);
    }
}

static uint64_t esp_pci_io_read(void *opaque, target_phys_addr_t addr,
                                unsigned int size)
{
    PCIESPState *pci = opaque;
    uint32_t ret;

    if (addr < 0x40) {
        /* SCSI core reg */
        ret = esp_reg_read(&pci->esp, addr >> 2);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        ret = esp_pci_dma_read(pci, (addr - 0x40) >> 2);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        trace_esp_pci_sbac_read(pci->sbac);
        ret = pci->sbac;
    } else {
        /* Invalid region */
        trace_esp_pci_error_invalid_read((int)addr);
        ret = 0;
    }

    /* give only requested data */
    ret >>= (addr & 3) * 8;
    ret &= ~(~(uint64_t)0 << (8 * size));

    return ret;
}

static void esp_pci_dma_memory_rw(PCIESPState *pci, uint8_t *buf, int len,
                                  DMADirection dir)
{
    dma_addr_t addr;
    DMADirection expected_dir;

    if (pci->dma_regs[DMA_CMD] & DMA_CMD_DIR) {
        expected_dir = DMA_DIRECTION_FROM_DEVICE;
    } else {
        expected_dir = DMA_DIRECTION_TO_DEVICE;
    }

    if (dir != expected_dir) {
        trace_esp_pci_error_invalid_dma_direction();
        return;
    }

    if (pci->dma_regs[DMA_STAT] & DMA_CMD_MDL) {
        qemu_log_mask(LOG_UNIMP, "am53c974: MDL transfer not implemented\n");
    }

    addr = pci->dma_regs[DMA_SPA];
    if (pci->dma_regs[DMA_WBC] < len) {
        len = pci->dma_regs[DMA_WBC];
    }

    pci_dma_rw(&pci->dev, addr, buf, len, dir);

    /* update status registers */
    pci->dma_regs[DMA_WBC] -= len;
    pci->dma_regs[DMA_WAC] += len;
}

static void esp_pci_dma_memory_read(void *opaque, uint8_t *buf, int len)
{
    PCIESPState *pci = opaque;
    esp_pci_dma_memory_rw(pci, buf, len, DMA_DIRECTION_TO_DEVICE);
}

static void esp_pci_dma_memory_write(void *opaque, uint8_t *buf, int len)
{
    PCIESPState *pci = opaque;
    esp_pci_dma_memory_rw(pci, buf, len, DMA_DIRECTION_FROM_DEVICE);
}

static const MemoryRegionOps esp_pci_io_ops = {
    .read = esp_pci_io_read,
    .write = esp_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void esp_pci_hard_reset(DeviceState *dev)
{
    PCIESPState *pci = DO_UPCAST(PCIESPState, dev.qdev, dev);
    esp_hard_reset(&pci->esp);
    pci->dma_regs[DMA_CMD] &= ~(DMA_CMD_DIR | DMA_CMD_INTE_D | DMA_CMD_INTE_P
                              | DMA_CMD_MDL | DMA_CMD_DIAG | DMA_CMD_MASK);
    pci->dma_regs[DMA_WBC] &= ~0xffff;
    pci->dma_regs[DMA_WAC] = 0xffffffff;
    pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR);
    pci->dma_regs[DMA_WMAC] = 0xfffffffd;
}

static const VMStateDescription vmstate_esp_pci_scsi = {
    .name = "pciespscsi",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIESPState),
        VMSTATE_BUFFER_UNSAFE(dma_regs, PCIESPState, 0, 8 * sizeof(uint32_t)),
        VMSTATE_STRUCT(esp, PCIESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static void esp_pci_command_complete(SCSIRequest *req, uint32_t status,
                                     size_t resid)
{
    ESPState *s = req->hba_private;
    PCIESPState *pci = container_of(s, PCIESPState, esp);

    esp_command_complete(req, status, resid);
    pci->dma_regs[DMA_WBC] = 0;
    pci->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
}

static const struct SCSIBusInfo esp_pci_scsi_info = {
    .tcq = false,
    .max_target = ESP_MAX_DEVS,
    .max_lun = 7,

    .transfer_data = esp_transfer_data,
    .complete = esp_pci_command_complete,
    .cancel = esp_request_cancelled,
};

static int esp_pci_scsi_init(PCIDevice *dev)
{
    PCIESPState *pci = DO_UPCAST(PCIESPState, dev, dev);
    ESPState *s = &pci->esp;
    uint8_t *pci_conf;

    pci_conf = pci->dev.config;

    /* Interrupt pin A */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    s->dma_memory_read = esp_pci_dma_memory_read;
    s->dma_memory_write = esp_pci_dma_memory_write;
    s->dma_opaque = pci;
    s->chip_id = TCHI_AM53C974;
    memory_region_init_io(&pci->io, &esp_pci_io_ops, pci, "esp-io", 0x80);

    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &pci->io);
    s->irq = pci->dev.irq[0];

    scsi_bus_new(&s->bus, &dev->qdev, &esp_pci_scsi_info);
    if (!dev->qdev.hotplugged) {
        return scsi_bus_legacy_handle_cmdline(&s->bus);
    }
    return 0;
}

static void esp_pci_scsi_uninit(PCIDevice *d)
{
    PCIESPState *pci = DO_UPCAST(PCIESPState, dev, d);

    memory_region_destroy(&pci->io);
}

static void esp_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = esp_pci_scsi_init;
    k->exit = esp_pci_scsi_uninit;
    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = PCI_DEVICE_ID_AMD_SCSI;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    dc->desc = "AMD Am53c974 PCscsi-PCI SCSI adapter";
    dc->reset = esp_pci_hard_reset;
    dc->vmsd = &vmstate_esp_pci_scsi;
}

static TypeInfo esp_pci_info = {
    .name = "am53c974",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIESPState),
    .class_init = esp_pci_class_init,
};

static void esp_register_types(void)
{
    type_register_static(&sysbus_esp_info);
    type_register_static(&esp_pci_info);
}

type_init(esp_register_types)
