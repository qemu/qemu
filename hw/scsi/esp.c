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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/scsi/esp.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/module.h"

/*
 * On Sparc32, this is the ESP (NCR53C90) part of chip STP2000 (Master I/O),
 * also produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR53C9X.txt
 *
 * On Macintosh Quadra it is a NCR53C96.
 */

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

static void esp_raise_drq(ESPState *s)
{
    qemu_irq_raise(s->irq_data);
}

static void esp_lower_drq(ESPState *s)
{
    qemu_irq_lower(s->irq_data);
}

void esp_dma_enable(ESPState *s, int irq, int level)
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

void esp_request_cancelled(SCSIRequest *req)
{
    ESPState *s = req->hba_private;

    if (req == s->current_req) {
        scsi_req_unref(s->current_req);
        s->current_req = NULL;
        s->current_dev = NULL;
    }
}

static void set_pdma(ESPState *s, enum pdma_origin_id origin,
                     uint32_t index, uint32_t len)
{
    s->pdma_origin = origin;
    s->pdma_start = index;
    s->pdma_cur = index;
    s->pdma_len = len;
}

static uint8_t *get_pdma_buf(ESPState *s)
{
    switch (s->pdma_origin) {
    case PDMA:
        return s->pdma_buf;
    case TI:
        return s->ti_buf;
    case CMD:
        return s->cmdbuf;
    case ASYNC:
        return s->async_buf;
    }
    return NULL;
}

static int get_cmd_cb(ESPState *s)
{
    int target;

    target = s->wregs[ESP_WBUSID] & BUSID_DID;

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
        /* No such drive */
        s->rregs[ESP_RSTAT] = 0;
        s->rregs[ESP_RINTR] = INTR_DC;
        s->rregs[ESP_RSEQ] = SEQ_0;
        esp_raise_irq(s);
        return -1;
    }
    return 0;
}

static uint32_t get_cmd(ESPState *s, uint8_t *buf, uint8_t buflen)
{
    uint32_t dmalen;
    int target;

    target = s->wregs[ESP_WBUSID] & BUSID_DID;
    if (s->dma) {
        dmalen = s->rregs[ESP_TCLO];
        dmalen |= s->rregs[ESP_TCMID] << 8;
        dmalen |= s->rregs[ESP_TCHI] << 16;
        if (dmalen > buflen) {
            return 0;
        }
        if (s->dma_memory_read) {
            s->dma_memory_read(s->dma_opaque, buf, dmalen);
        } else {
            memcpy(s->pdma_buf, buf, dmalen);
            set_pdma(s, PDMA, 0, dmalen);
            esp_raise_drq(s);
            return 0;
        }
    } else {
        dmalen = s->ti_size;
        if (dmalen > TI_BUFSZ) {
            return 0;
        }
        memcpy(buf, s->ti_buf, dmalen);
        buf[0] = buf[2] >> 5;
    }
    trace_esp_get_cmd(dmalen, target);

    if (get_cmd_cb(s) < 0) {
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

static void satn_pdma_cb(ESPState *s)
{
    if (get_cmd_cb(s) < 0) {
        return;
    }
    if (s->pdma_cur != s->pdma_start) {
        do_cmd(s, get_pdma_buf(s) + s->pdma_start);
    }
}

static void handle_satn(ESPState *s)
{
    uint8_t buf[32];
    int len;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn;
        return;
    }
    s->pdma_cb = satn_pdma_cb;
    len = get_cmd(s, buf, sizeof(buf));
    if (len)
        do_cmd(s, buf);
}

static void s_without_satn_pdma_cb(ESPState *s)
{
    if (get_cmd_cb(s) < 0) {
        return;
    }
    if (s->pdma_cur != s->pdma_start) {
        do_busid_cmd(s, get_pdma_buf(s) + s->pdma_start, 0);
    }
}

static void handle_s_without_atn(ESPState *s)
{
    uint8_t buf[32];
    int len;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_s_without_atn;
        return;
    }
    s->pdma_cb = s_without_satn_pdma_cb;
    len = get_cmd(s, buf, sizeof(buf));
    if (len) {
        do_busid_cmd(s, buf, 0);
    }
}

static void satn_stop_pdma_cb(ESPState *s)
{
    if (get_cmd_cb(s) < 0) {
        return;
    }
    s->cmdlen = s->pdma_cur - s->pdma_start;
    if (s->cmdlen) {
        trace_esp_handle_satn_stop(s->cmdlen);
        s->do_cmd = 1;
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
        s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        esp_raise_irq(s);
    }
}

static void handle_satn_stop(ESPState *s)
{
    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn_stop;
        return;
    }
    s->pdma_cb = satn_stop_pdma_cb;;
    s->cmdlen = get_cmd(s, s->cmdbuf, sizeof(s->cmdbuf));
    if (s->cmdlen) {
        trace_esp_handle_satn_stop(s->cmdlen);
        s->do_cmd = 1;
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
        s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        esp_raise_irq(s);
    }
}

static void write_response_pdma_cb(ESPState *s)
{
    s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
    s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
    s->rregs[ESP_RSEQ] = SEQ_CD;
    esp_raise_irq(s);
}

static void write_response(ESPState *s)
{
    trace_esp_write_response(s->status);
    s->ti_buf[0] = s->status;
    s->ti_buf[1] = 0;
    if (s->dma) {
        if (s->dma_memory_write) {
            s->dma_memory_write(s->dma_opaque, s->ti_buf, 2);
            s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
            s->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
            s->rregs[ESP_RSEQ] = SEQ_CD;
        } else {
            set_pdma(s, TI, 0, 2);
            s->pdma_cb = write_response_pdma_cb;
            esp_raise_drq(s);
            return;
        }
    } else {
        s->ti_size = 2;
        s->ti_rptr = 0;
        s->ti_wptr = 2;
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
    s->rregs[ESP_TCHI] = 0;
    esp_raise_irq(s);
}

static void do_dma_pdma_cb(ESPState *s)
{
    int to_device = (s->ti_size < 0);
    int len = s->pdma_cur - s->pdma_start;
    if (s->do_cmd) {
        s->ti_size = 0;
        s->cmdlen = 0;
        s->do_cmd = 0;
        do_cmd(s, s->cmdbuf);
        return;
    }
    s->dma_left -= len;
    s->async_buf += len;
    s->async_len -= len;
    if (to_device) {
        s->ti_size += len;
    } else {
        s->ti_size -= len;
    }
    if (s->async_len == 0) {
        scsi_req_continue(s->current_req);
        /*
         * If there is still data to be read from the device then
         * complete the DMA operation immediately.  Otherwise defer
         * until the scsi layer has completed.
         */
        if (to_device || s->dma_left != 0 || s->ti_size == 0) {
            return;
        }
    }

    /* Partially filled a scsi buffer. Complete immediately.  */
    esp_dma_done(s);
}

static void esp_do_dma(ESPState *s)
{
    uint32_t len;
    int to_device;

    len = s->dma_left;
    if (s->do_cmd) {
        /*
         * handle_ti_cmd() case: esp_do_dma() is called only from
         * handle_ti_cmd() with do_cmd != NULL (see the assert())
         */
        trace_esp_do_dma(s->cmdlen, len);
        assert (s->cmdlen <= sizeof(s->cmdbuf) &&
                len <= sizeof(s->cmdbuf) - s->cmdlen);
        if (s->dma_memory_read) {
            s->dma_memory_read(s->dma_opaque, &s->cmdbuf[s->cmdlen], len);
        } else {
            set_pdma(s, CMD, s->cmdlen, len);
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);
            return;
        }
        trace_esp_handle_ti_cmd(s->cmdlen);
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
    to_device = (s->ti_size < 0);
    if (to_device) {
        if (s->dma_memory_read) {
            s->dma_memory_read(s->dma_opaque, s->async_buf, len);
        } else {
            set_pdma(s, ASYNC, 0, len);
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);
            return;
        }
    } else {
        if (s->dma_memory_write) {
            s->dma_memory_write(s->dma_opaque, s->async_buf, len);
        } else {
            set_pdma(s, ASYNC, 0, len);
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);
            return;
        }
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

static void esp_report_command_complete(ESPState *s, uint32_t status)
{
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

void esp_command_complete(SCSIRequest *req, uint32_t status,
                          size_t resid)
{
    ESPState *s = req->hba_private;

    if (s->rregs[ESP_RSTAT] & STAT_INT) {
        /* Defer handling command complete until the previous
         * interrupt has been handled.
         */
        trace_esp_command_complete_deferred();
        s->deferred_status = status;
        s->deferred_complete = true;
        return;
    }
    esp_report_command_complete(s, status);
}

void esp_transfer_data(SCSIRequest *req, uint32_t len)
{
    ESPState *s = req->hba_private;

    assert(!s->do_cmd);
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

    dmalen = s->rregs[ESP_TCLO];
    dmalen |= s->rregs[ESP_TCMID] << 8;
    dmalen |= s->rregs[ESP_TCHI] << 16;
    if (dmalen==0) {
      dmalen=0x10000;
    }
    s->dma_counter = dmalen;

    if (s->do_cmd)
        minlen = (dmalen < ESP_CMDBUF_SZ) ? dmalen : ESP_CMDBUF_SZ;
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
    }
}

void esp_hard_reset(ESPState *s)
{
    memset(s->rregs, 0, ESP_REGS);
    memset(s->wregs, 0, ESP_REGS);
    s->tchi_written = 0;
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
    qemu_irq_lower(s->irq_data);
    esp_hard_reset(s);
}

static void parent_esp_reset(ESPState *s, int irq, int level)
{
    if (level) {
        esp_soft_reset(s);
    }
}

uint64_t esp_reg_read(ESPState *s, uint32_t saddr)
{
    uint32_t old_val;

    trace_esp_mem_readb(saddr, s->rregs[saddr]);
    switch (saddr) {
    case ESP_FIFO:
        if ((s->rregs[ESP_RSTAT] & STAT_PIO_MASK) == 0) {
            /* Data out.  */
            qemu_log_mask(LOG_UNIMP, "esp: PIO data read not implemented\n");
            s->rregs[ESP_FIFO] = 0;
        } else if (s->ti_rptr < s->ti_wptr) {
            s->ti_size--;
            s->rregs[ESP_FIFO] = s->ti_buf[s->ti_rptr++];
        }
        if (s->ti_rptr == s->ti_wptr) {
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
        if (s->deferred_complete) {
            esp_report_command_complete(s, s->deferred_status);
            s->deferred_complete = false;
        }
        return old_val;
    case ESP_TCHI:
        /* Return the unique id if the value has never been written */
        if (!s->tchi_written) {
            return s->chip_id;
        }
    default:
        break;
    }
    return s->rregs[saddr];
}

void esp_reg_write(ESPState *s, uint32_t saddr, uint64_t val)
{
    trace_esp_mem_writeb(saddr, s->wregs[saddr], val);
    switch (saddr) {
    case ESP_TCHI:
        s->tchi_written = true;
        /* fall through */
    case ESP_TCLO:
    case ESP_TCMID:
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        break;
    case ESP_FIFO:
        if (s->do_cmd) {
            if (s->cmdlen < ESP_CMDBUF_SZ) {
                s->cmdbuf[s->cmdlen++] = val & 0xff;
            } else {
                trace_esp_error_fifo_overrun();
            }
        } else if (s->ti_wptr == TI_BUFSZ - 1) {
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
            s->rregs[ESP_TCHI] = s->wregs[ESP_TCHI];
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
    case ESP_CFG2: case ESP_CFG3:
    case ESP_RES3: case ESP_RES4:
        s->rregs[saddr] = val;
        break;
    case ESP_WCCF ... ESP_WTEST:
        break;
    default:
        trace_esp_error_invalid_write(val, saddr);
        return;
    }
    s->wregs[saddr] = val;
}

static bool esp_mem_accepts(void *opaque, hwaddr addr,
                            unsigned size, bool is_write,
                            MemTxAttrs attrs)
{
    return (size == 1) || (is_write && size == 4);
}

static bool esp_pdma_needed(void *opaque)
{
    ESPState *s = opaque;
    return s->dma_memory_read == NULL && s->dma_memory_write == NULL &&
           s->dma_enabled;
}

static const VMStateDescription vmstate_esp_pdma = {
    .name = "esp/pdma",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = esp_pdma_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(pdma_buf, ESPState),
        VMSTATE_INT32(pdma_origin, ESPState),
        VMSTATE_UINT32(pdma_len, ESPState),
        VMSTATE_UINT32(pdma_start, ESPState),
        VMSTATE_UINT32(pdma_cur, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_esp = {
    .name ="esp",
    .version_id = 4,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(rregs, ESPState),
        VMSTATE_BUFFER(wregs, ESPState),
        VMSTATE_INT32(ti_size, ESPState),
        VMSTATE_UINT32(ti_rptr, ESPState),
        VMSTATE_UINT32(ti_wptr, ESPState),
        VMSTATE_BUFFER(ti_buf, ESPState),
        VMSTATE_UINT32(status, ESPState),
        VMSTATE_UINT32(deferred_status, ESPState),
        VMSTATE_BOOL(deferred_complete, ESPState),
        VMSTATE_UINT32(dma, ESPState),
        VMSTATE_PARTIAL_BUFFER(cmdbuf, ESPState, 16),
        VMSTATE_BUFFER_START_MIDDLE_V(cmdbuf, ESPState, 16, 4),
        VMSTATE_UINT32(cmdlen, ESPState),
        VMSTATE_UINT32(do_cmd, ESPState),
        VMSTATE_UINT32(dma_left, ESPState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_esp_pdma,
        NULL
    }
};

static void sysbus_esp_mem_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    uint32_t saddr;

    saddr = addr >> sysbus->it_shift;
    esp_reg_write(&sysbus->esp, saddr, val);
}

static uint64_t sysbus_esp_mem_read(void *opaque, hwaddr addr,
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

static void sysbus_esp_pdma_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    ESPState *s = &sysbus->esp;
    uint32_t dmalen;
    uint8_t *buf = get_pdma_buf(s);

    dmalen = s->rregs[ESP_TCLO];
    dmalen |= s->rregs[ESP_TCMID] << 8;
    dmalen |= s->rregs[ESP_TCHI] << 16;
    if (dmalen == 0 || s->pdma_len == 0) {
        return;
    }
    switch (size) {
    case 1:
        buf[s->pdma_cur++] = val;
        s->pdma_len--;
        dmalen--;
        break;
    case 2:
        buf[s->pdma_cur++] = val >> 8;
        buf[s->pdma_cur++] = val;
        s->pdma_len -= 2;
        dmalen -= 2;
        break;
    }
    s->rregs[ESP_TCLO] = dmalen & 0xff;
    s->rregs[ESP_TCMID] = dmalen >> 8;
    s->rregs[ESP_TCHI] = dmalen >> 16;
    if (s->pdma_len == 0 && s->pdma_cb) {
        esp_lower_drq(s);
        s->pdma_cb(s);
        s->pdma_cb = NULL;
    }
}

static uint64_t sysbus_esp_pdma_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    ESPState *s = &sysbus->esp;
    uint8_t *buf = get_pdma_buf(s);
    uint64_t val = 0;

    if (s->pdma_len == 0) {
        return 0;
    }
    switch (size) {
    case 1:
        val = buf[s->pdma_cur++];
        s->pdma_len--;
        break;
    case 2:
        val = buf[s->pdma_cur++];
        val = (val << 8) | buf[s->pdma_cur++];
        s->pdma_len -= 2;
        break;
    }

    if (s->pdma_len == 0 && s->pdma_cb) {
        esp_lower_drq(s);
        s->pdma_cb(s);
        s->pdma_cb = NULL;
    }
    return val;
}

static const MemoryRegionOps sysbus_esp_pdma_ops = {
    .read = sysbus_esp_pdma_read,
    .write = sysbus_esp_pdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 2,
};

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
    SysBusESPState *sysbus = ESP_STATE(opaque);
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

static void sysbus_esp_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusESPState *sysbus = ESP_STATE(dev);
    ESPState *s = &sysbus->esp;

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->irq_data);
    assert(sysbus->it_shift != -1);

    s->chip_id = TCHI_FAS100A;
    memory_region_init_io(&sysbus->iomem, OBJECT(sysbus), &sysbus_esp_mem_ops,
                          sysbus, "esp-regs", ESP_REGS << sysbus->it_shift);
    sysbus_init_mmio(sbd, &sysbus->iomem);
    memory_region_init_io(&sysbus->pdma, OBJECT(sysbus), &sysbus_esp_pdma_ops,
                          sysbus, "esp-pdma", 2);
    sysbus_init_mmio(sbd, &sysbus->pdma);

    qdev_init_gpio_in(dev, sysbus_esp_gpio_demux, 2);

    scsi_bus_new(&s->bus, sizeof(s->bus), dev, &esp_scsi_info, NULL);
}

static void sysbus_esp_hard_reset(DeviceState *dev)
{
    SysBusESPState *sysbus = ESP_STATE(dev);
    esp_hard_reset(&sysbus->esp);
}

static const VMStateDescription vmstate_sysbus_esp_scsi = {
    .name = "sysbusespscsi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(esp, SysBusESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static void sysbus_esp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sysbus_esp_realize;
    dc->reset = sysbus_esp_hard_reset;
    dc->vmsd = &vmstate_sysbus_esp_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo sysbus_esp_info = {
    .name          = TYPE_ESP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusESPState),
    .class_init    = sysbus_esp_class_init,
};

static void esp_register_types(void)
{
    type_register_static(&sysbus_esp_info);
}

type_init(esp_register_types)
