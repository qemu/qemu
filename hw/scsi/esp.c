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
    trace_esp_raise_drq();
}

static void esp_lower_drq(ESPState *s)
{
    qemu_irq_lower(s->irq_data);
    trace_esp_lower_drq();
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
        s->async_len = 0;
    }
}

static void esp_fifo_push(Fifo8 *fifo, uint8_t val)
{
    if (fifo8_num_used(fifo) == fifo->capacity) {
        trace_esp_error_fifo_overrun();
        return;
    }

    fifo8_push(fifo, val);
}

static uint8_t esp_fifo_pop(Fifo8 *fifo)
{
    if (fifo8_is_empty(fifo)) {
        return 0;
    }

    return fifo8_pop(fifo);
}

static uint32_t esp_fifo_pop_buf(Fifo8 *fifo, uint8_t *dest, int maxlen)
{
    const uint8_t *buf;
    uint32_t n;

    if (maxlen == 0) {
        return 0;
    }

    buf = fifo8_pop_buf(fifo, maxlen, &n);
    if (dest) {
        memcpy(dest, buf, n);
    }

    return n;
}

static uint32_t esp_get_tc(ESPState *s)
{
    uint32_t dmalen;

    dmalen = s->rregs[ESP_TCLO];
    dmalen |= s->rregs[ESP_TCMID] << 8;
    dmalen |= s->rregs[ESP_TCHI] << 16;

    return dmalen;
}

static void esp_set_tc(ESPState *s, uint32_t dmalen)
{
    s->rregs[ESP_TCLO] = dmalen;
    s->rregs[ESP_TCMID] = dmalen >> 8;
    s->rregs[ESP_TCHI] = dmalen >> 16;
}

static uint32_t esp_get_stc(ESPState *s)
{
    uint32_t dmalen;

    dmalen = s->wregs[ESP_TCLO];
    dmalen |= s->wregs[ESP_TCMID] << 8;
    dmalen |= s->wregs[ESP_TCHI] << 16;

    return dmalen;
}

static uint8_t esp_pdma_read(ESPState *s)
{
    uint8_t val;

    if (s->do_cmd) {
        val = esp_fifo_pop(&s->cmdfifo);
    } else {
        val = esp_fifo_pop(&s->fifo);
    }

    return val;
}

static void esp_pdma_write(ESPState *s, uint8_t val)
{
    uint32_t dmalen = esp_get_tc(s);

    if (dmalen == 0) {
        return;
    }

    if (s->do_cmd) {
        esp_fifo_push(&s->cmdfifo, val);
    } else {
        esp_fifo_push(&s->fifo, val);
    }

    dmalen--;
    esp_set_tc(s, dmalen);
}

static int esp_select(ESPState *s)
{
    int target;

    target = s->wregs[ESP_WBUSID] & BUSID_DID;

    s->ti_size = 0;
    fifo8_reset(&s->fifo);

    s->current_dev = scsi_device_find(&s->bus, 0, target, 0);
    if (!s->current_dev) {
        /* No such drive */
        s->rregs[ESP_RSTAT] = 0;
        s->rregs[ESP_RINTR] = INTR_DC;
        s->rregs[ESP_RSEQ] = SEQ_0;
        esp_raise_irq(s);
        return -1;
    }

    /*
     * Note that we deliberately don't raise the IRQ here: this will be done
     * either in do_command_phase() for DATA OUT transfers or by the deferred
     * IRQ mechanism in esp_transfer_data() for DATA IN transfers
     */
    s->rregs[ESP_RINTR] |= INTR_FC;
    s->rregs[ESP_RSEQ] = SEQ_CD;
    return 0;
}

static uint32_t get_cmd(ESPState *s, uint32_t maxlen)
{
    uint8_t buf[ESP_CMDFIFO_SZ];
    uint32_t dmalen, n;
    int target;

    if (s->current_req) {
        /* Started a new command before the old one finished.  Cancel it.  */
        scsi_req_cancel(s->current_req);
    }

    target = s->wregs[ESP_WBUSID] & BUSID_DID;
    if (s->dma) {
        dmalen = MIN(esp_get_tc(s), maxlen);
        if (dmalen == 0) {
            return 0;
        }
        if (s->dma_memory_read) {
            s->dma_memory_read(s->dma_opaque, buf, dmalen);
            dmalen = MIN(fifo8_num_free(&s->cmdfifo), dmalen);
            fifo8_push_all(&s->cmdfifo, buf, dmalen);
        } else {
            if (esp_select(s) < 0) {
                fifo8_reset(&s->cmdfifo);
                return -1;
            }
            esp_raise_drq(s);
            fifo8_reset(&s->cmdfifo);
            return 0;
        }
    } else {
        dmalen = MIN(fifo8_num_used(&s->fifo), maxlen);
        if (dmalen == 0) {
            return 0;
        }
        n = esp_fifo_pop_buf(&s->fifo, buf, dmalen);
        n = MIN(fifo8_num_free(&s->cmdfifo), n);
        fifo8_push_all(&s->cmdfifo, buf, n);
    }
    trace_esp_get_cmd(dmalen, target);

    if (esp_select(s) < 0) {
        fifo8_reset(&s->cmdfifo);
        return -1;
    }
    return dmalen;
}

static void do_command_phase(ESPState *s)
{
    uint32_t cmdlen;
    int32_t datalen;
    SCSIDevice *current_lun;
    uint8_t buf[ESP_CMDFIFO_SZ];

    trace_esp_do_command_phase(s->lun);
    cmdlen = fifo8_num_used(&s->cmdfifo);
    if (!cmdlen || !s->current_dev) {
        return;
    }
    esp_fifo_pop_buf(&s->cmdfifo, buf, cmdlen);

    current_lun = scsi_device_find(&s->bus, 0, s->current_dev->id, s->lun);
    s->current_req = scsi_req_new(current_lun, 0, s->lun, buf, s);
    datalen = scsi_req_enqueue(s->current_req);
    s->ti_size = datalen;
    fifo8_reset(&s->cmdfifo);
    if (datalen != 0) {
        s->rregs[ESP_RSTAT] = STAT_TC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        s->ti_cmd = 0;
        esp_set_tc(s, 0);
        if (datalen > 0) {
            /*
             * Switch to DATA IN phase but wait until initial data xfer is
             * complete before raising the command completion interrupt
             */
            s->data_in_ready = false;
            s->rregs[ESP_RSTAT] |= STAT_DI;
        } else {
            s->rregs[ESP_RSTAT] |= STAT_DO;
            s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
            esp_raise_irq(s);
            esp_lower_drq(s);
        }
        scsi_req_continue(s->current_req);
        return;
    }
}

static void do_message_phase(ESPState *s)
{
    if (s->cmdfifo_cdb_offset) {
        uint8_t message = esp_fifo_pop(&s->cmdfifo);

        trace_esp_do_identify(message);
        s->lun = message & 7;
        s->cmdfifo_cdb_offset--;
    }

    /* Ignore extended messages for now */
    if (s->cmdfifo_cdb_offset) {
        int len = MIN(s->cmdfifo_cdb_offset, fifo8_num_used(&s->cmdfifo));
        esp_fifo_pop_buf(&s->cmdfifo, NULL, len);
        s->cmdfifo_cdb_offset = 0;
    }
}

static void do_cmd(ESPState *s)
{
    do_message_phase(s);
    assert(s->cmdfifo_cdb_offset == 0);
    do_command_phase(s);
}

static void satn_pdma_cb(ESPState *s)
{
    if (!esp_get_tc(s) && !fifo8_is_empty(&s->cmdfifo)) {
        s->cmdfifo_cdb_offset = 1;
        s->do_cmd = 0;
        do_cmd(s);
    }
}

static void handle_satn(ESPState *s)
{
    int32_t cmdlen;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn;
        return;
    }
    s->pdma_cb = satn_pdma_cb;
    cmdlen = get_cmd(s, ESP_CMDFIFO_SZ);
    if (cmdlen > 0) {
        s->cmdfifo_cdb_offset = 1;
        s->do_cmd = 0;
        do_cmd(s);
    } else if (cmdlen == 0) {
        s->do_cmd = 1;
        /* Target present, but no cmd yet - switch to command phase */
        s->rregs[ESP_RSEQ] = SEQ_CD;
        s->rregs[ESP_RSTAT] = STAT_CD;
    }
}

static void s_without_satn_pdma_cb(ESPState *s)
{
    if (!esp_get_tc(s) && !fifo8_is_empty(&s->cmdfifo)) {
        s->cmdfifo_cdb_offset = 0;
        s->do_cmd = 0;
        do_cmd(s);
    }
}

static void handle_s_without_atn(ESPState *s)
{
    int32_t cmdlen;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_s_without_atn;
        return;
    }
    s->pdma_cb = s_without_satn_pdma_cb;
    cmdlen = get_cmd(s, ESP_CMDFIFO_SZ);
    if (cmdlen > 0) {
        s->cmdfifo_cdb_offset = 0;
        s->do_cmd = 0;
        do_cmd(s);
    } else if (cmdlen == 0) {
        s->do_cmd = 1;
        /* Target present, but no cmd yet - switch to command phase */
        s->rregs[ESP_RSEQ] = SEQ_CD;
        s->rregs[ESP_RSTAT] = STAT_CD;
    }
}

static void satn_stop_pdma_cb(ESPState *s)
{
    if (!esp_get_tc(s) && !fifo8_is_empty(&s->cmdfifo)) {
        trace_esp_handle_satn_stop(fifo8_num_used(&s->cmdfifo));
        s->do_cmd = 1;
        s->cmdfifo_cdb_offset = 1;
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
        s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        esp_raise_irq(s);
    }
}

static void handle_satn_stop(ESPState *s)
{
    int32_t cmdlen;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn_stop;
        return;
    }
    s->pdma_cb = satn_stop_pdma_cb;
    cmdlen = get_cmd(s, 1);
    if (cmdlen > 0) {
        trace_esp_handle_satn_stop(fifo8_num_used(&s->cmdfifo));
        s->do_cmd = 1;
        s->cmdfifo_cdb_offset = 1;
        s->rregs[ESP_RSTAT] = STAT_MO;
        s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_MO;
        esp_raise_irq(s);
    } else if (cmdlen == 0) {
        s->do_cmd = 1;
        /* Target present, switch to message out phase */
        s->rregs[ESP_RSEQ] = SEQ_MO;
        s->rregs[ESP_RSTAT] = STAT_MO;
    }
}

static void write_response_pdma_cb(ESPState *s)
{
    s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
    s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
    s->rregs[ESP_RSEQ] = SEQ_CD;
    esp_raise_irq(s);
}

static void write_response(ESPState *s)
{
    uint8_t buf[2];

    trace_esp_write_response(s->status);

    buf[0] = s->status;
    buf[1] = 0;

    if (s->dma) {
        if (s->dma_memory_write) {
            s->dma_memory_write(s->dma_opaque, buf, 2);
            s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
            s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
            s->rregs[ESP_RSEQ] = SEQ_CD;
        } else {
            s->pdma_cb = write_response_pdma_cb;
            esp_raise_drq(s);
            return;
        }
    } else {
        fifo8_reset(&s->fifo);
        fifo8_push_all(&s->fifo, buf, 2);
        s->rregs[ESP_RFLAGS] = 2;
    }
    esp_raise_irq(s);
}

static void esp_dma_done(ESPState *s)
{
    s->rregs[ESP_RSTAT] |= STAT_TC;
    s->rregs[ESP_RINTR] |= INTR_BS;
    s->rregs[ESP_RFLAGS] = 0;
    esp_set_tc(s, 0);
    esp_raise_irq(s);
}

static void do_dma_pdma_cb(ESPState *s)
{
    int to_device = ((s->rregs[ESP_RSTAT] & 7) == STAT_DO);
    int len;
    uint32_t n;

    if (s->do_cmd) {
        /* Ensure we have received complete command after SATN and stop */
        if (esp_get_tc(s) || fifo8_is_empty(&s->cmdfifo)) {
            return;
        }

        s->ti_size = 0;
        if ((s->rregs[ESP_RSTAT] & 7) == STAT_CD) {
            /* No command received */
            if (s->cmdfifo_cdb_offset == fifo8_num_used(&s->cmdfifo)) {
                return;
            }

            /* Command has been received */
            s->do_cmd = 0;
            do_cmd(s);
        } else {
            /*
             * Extra message out bytes received: update cmdfifo_cdb_offset
             * and then switch to commmand phase
             */
            s->cmdfifo_cdb_offset = fifo8_num_used(&s->cmdfifo);
            s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
            s->rregs[ESP_RSEQ] = SEQ_CD;
            s->rregs[ESP_RINTR] |= INTR_BS;
            esp_raise_irq(s);
        }
        return;
    }

    if (!s->current_req) {
        return;
    }

    if (to_device) {
        /* Copy FIFO data to device */
        len = MIN(s->async_len, ESP_FIFO_SZ);
        len = MIN(len, fifo8_num_used(&s->fifo));
        n = esp_fifo_pop_buf(&s->fifo, s->async_buf, len);
        s->async_buf += n;
        s->async_len -= n;
        s->ti_size += n;

        if (n < len) {
            /* Unaligned accesses can cause FIFO wraparound */
            len = len - n;
            n = esp_fifo_pop_buf(&s->fifo, s->async_buf, len);
            s->async_buf += n;
            s->async_len -= n;
            s->ti_size += n;
        }

        if (s->async_len == 0) {
            scsi_req_continue(s->current_req);
            return;
        }

        if (esp_get_tc(s) == 0) {
            esp_lower_drq(s);
            esp_dma_done(s);
        }

        return;
    } else {
        if (s->async_len == 0) {
            /* Defer until the scsi layer has completed */
            scsi_req_continue(s->current_req);
            s->data_in_ready = false;
            return;
        }

        if (esp_get_tc(s) != 0) {
            /* Copy device data to FIFO */
            len = MIN(s->async_len, esp_get_tc(s));
            len = MIN(len, fifo8_num_free(&s->fifo));
            fifo8_push_all(&s->fifo, s->async_buf, len);
            s->async_buf += len;
            s->async_len -= len;
            s->ti_size -= len;
            esp_set_tc(s, esp_get_tc(s) - len);

            if (esp_get_tc(s) == 0) {
                /* Indicate transfer to FIFO is complete */
                 s->rregs[ESP_RSTAT] |= STAT_TC;
            }
            return;
        }

        /* Partially filled a scsi buffer. Complete immediately.  */
        esp_lower_drq(s);
        esp_dma_done(s);
    }
}

static void esp_do_dma(ESPState *s)
{
    uint32_t len, cmdlen;
    int to_device = ((s->rregs[ESP_RSTAT] & 7) == STAT_DO);
    uint8_t buf[ESP_CMDFIFO_SZ];

    len = esp_get_tc(s);
    if (s->do_cmd) {
        /*
         * handle_ti_cmd() case: esp_do_dma() is called only from
         * handle_ti_cmd() with do_cmd != NULL (see the assert())
         */
        cmdlen = fifo8_num_used(&s->cmdfifo);
        trace_esp_do_dma(cmdlen, len);
        if (s->dma_memory_read) {
            len = MIN(len, fifo8_num_free(&s->cmdfifo));
            s->dma_memory_read(s->dma_opaque, buf, len);
            fifo8_push_all(&s->cmdfifo, buf, len);
        } else {
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);
            return;
        }
        trace_esp_handle_ti_cmd(cmdlen);
        s->ti_size = 0;
        if ((s->rregs[ESP_RSTAT] & 7) == STAT_CD) {
            /* No command received */
            if (s->cmdfifo_cdb_offset == fifo8_num_used(&s->cmdfifo)) {
                return;
            }

            /* Command has been received */
            s->do_cmd = 0;
            do_cmd(s);
        } else {
            /*
             * Extra message out bytes received: update cmdfifo_cdb_offset
             * and then switch to commmand phase
             */
            s->cmdfifo_cdb_offset = fifo8_num_used(&s->cmdfifo);
            s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
            s->rregs[ESP_RSEQ] = SEQ_CD;
            s->rregs[ESP_RINTR] |= INTR_BS;
            esp_raise_irq(s);
        }
        return;
    }
    if (!s->current_req) {
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
        if (s->dma_memory_read) {
            s->dma_memory_read(s->dma_opaque, s->async_buf, len);
        } else {
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);
            return;
        }
    } else {
        if (s->dma_memory_write) {
            s->dma_memory_write(s->dma_opaque, s->async_buf, len);
        } else {
            /* Adjust TC for any leftover data in the FIFO */
            if (!fifo8_is_empty(&s->fifo)) {
                esp_set_tc(s, esp_get_tc(s) - fifo8_num_used(&s->fifo));
            }

            /* Copy device data to FIFO */
            len = MIN(len, fifo8_num_free(&s->fifo));
            fifo8_push_all(&s->fifo, s->async_buf, len);
            s->async_buf += len;
            s->async_len -= len;
            s->ti_size -= len;

            /*
             * MacOS toolbox uses a TI length of 16 bytes for all commands, so
             * commands shorter than this must be padded accordingly
             */
            if (len < esp_get_tc(s) && esp_get_tc(s) <= ESP_FIFO_SZ) {
                while (fifo8_num_used(&s->fifo) < ESP_FIFO_SZ) {
                    esp_fifo_push(&s->fifo, 0);
                    len++;
                }
            }

            esp_set_tc(s, esp_get_tc(s) - len);
            s->pdma_cb = do_dma_pdma_cb;
            esp_raise_drq(s);

            /* Indicate transfer to FIFO is complete */
            s->rregs[ESP_RSTAT] |= STAT_TC;
            return;
        }
    }
    esp_set_tc(s, esp_get_tc(s) - len);
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
        if (to_device || esp_get_tc(s) != 0 || s->ti_size == 0) {
            return;
        }
    }

    /* Partially filled a scsi buffer. Complete immediately.  */
    esp_dma_done(s);
    esp_lower_drq(s);
}

static void esp_do_nodma(ESPState *s)
{
    int to_device = ((s->rregs[ESP_RSTAT] & 7) == STAT_DO);
    uint32_t cmdlen;
    int len;

    if (s->do_cmd) {
        cmdlen = fifo8_num_used(&s->cmdfifo);
        trace_esp_handle_ti_cmd(cmdlen);
        s->ti_size = 0;
        if ((s->rregs[ESP_RSTAT] & 7) == STAT_CD) {
            /* No command received */
            if (s->cmdfifo_cdb_offset == fifo8_num_used(&s->cmdfifo)) {
                return;
            }

            /* Command has been received */
            s->do_cmd = 0;
            do_cmd(s);
        } else {
            /*
             * Extra message out bytes received: update cmdfifo_cdb_offset
             * and then switch to commmand phase
             */
            s->cmdfifo_cdb_offset = fifo8_num_used(&s->cmdfifo);
            s->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
            s->rregs[ESP_RSEQ] = SEQ_CD;
            s->rregs[ESP_RINTR] |= INTR_BS;
            esp_raise_irq(s);
        }
        return;
    }

    if (!s->current_req) {
        return;
    }

    if (s->async_len == 0) {
        /* Defer until data is available.  */
        return;
    }

    if (to_device) {
        len = MIN(fifo8_num_used(&s->fifo), ESP_FIFO_SZ);
        esp_fifo_pop_buf(&s->fifo, s->async_buf, len);
        s->async_buf += len;
        s->async_len -= len;
        s->ti_size += len;
    } else {
        if (fifo8_is_empty(&s->fifo)) {
            fifo8_push(&s->fifo, s->async_buf[0]);
            s->async_buf++;
            s->async_len--;
            s->ti_size--;
        }
    }

    if (s->async_len == 0) {
        scsi_req_continue(s->current_req);
        return;
    }

    s->rregs[ESP_RINTR] |= INTR_BS;
    esp_raise_irq(s);
}

void esp_command_complete(SCSIRequest *req, size_t resid)
{
    ESPState *s = req->hba_private;
    int to_device = ((s->rregs[ESP_RSTAT] & 7) == STAT_DO);

    trace_esp_command_complete();

    /*
     * Non-DMA transfers from the target will leave the last byte in
     * the FIFO so don't reset ti_size in this case
     */
    if (s->dma || to_device) {
        if (s->ti_size != 0) {
            trace_esp_command_complete_unexpected();
        }
        s->ti_size = 0;
    }

    s->async_len = 0;
    if (req->status) {
        trace_esp_command_complete_fail();
    }
    s->status = req->status;

    /*
     * If the transfer is finished, switch to status phase. For non-DMA
     * transfers from the target the last byte is still in the FIFO
     */
    if (s->ti_size == 0) {
        s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
        esp_dma_done(s);
        esp_lower_drq(s);
    }

    if (s->current_req) {
        scsi_req_unref(s->current_req);
        s->current_req = NULL;
        s->current_dev = NULL;
    }
}

void esp_transfer_data(SCSIRequest *req, uint32_t len)
{
    ESPState *s = req->hba_private;
    int to_device = ((s->rregs[ESP_RSTAT] & 7) == STAT_DO);
    uint32_t dmalen = esp_get_tc(s);

    assert(!s->do_cmd);
    trace_esp_transfer_data(dmalen, s->ti_size);
    s->async_len = len;
    s->async_buf = scsi_req_get_buf(req);

    if (!to_device && !s->data_in_ready) {
        /*
         * Initial incoming data xfer is complete so raise command
         * completion interrupt
         */
        s->data_in_ready = true;
        s->rregs[ESP_RSTAT] |= STAT_TC;
        s->rregs[ESP_RINTR] |= INTR_BS;
        esp_raise_irq(s);
    }

    if (s->ti_cmd == 0) {
        /*
         * Always perform the initial transfer upon reception of the next TI
         * command to ensure the DMA/non-DMA status of the command is correct.
         * It is not possible to use s->dma directly in the section below as
         * some OSs send non-DMA NOP commands after a DMA transfer. Hence if the
         * async data transfer is delayed then s->dma is set incorrectly.
         */
        return;
    }

    if (s->ti_cmd == (CMD_TI | CMD_DMA)) {
        if (dmalen) {
            esp_do_dma(s);
        } else if (s->ti_size <= 0) {
            /*
             * If this was the last part of a DMA transfer then the
             * completion interrupt is deferred to here.
             */
            esp_dma_done(s);
            esp_lower_drq(s);
        }
    } else if (s->ti_cmd == CMD_TI) {
        esp_do_nodma(s);
    }
}

static void handle_ti(ESPState *s)
{
    uint32_t dmalen;

    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_ti;
        return;
    }

    s->ti_cmd = s->rregs[ESP_CMD];
    if (s->dma) {
        dmalen = esp_get_tc(s);
        trace_esp_handle_ti(dmalen);
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        esp_do_dma(s);
    } else {
        trace_esp_handle_ti(s->ti_size);
        esp_do_nodma(s);
    }
}

void esp_hard_reset(ESPState *s)
{
    memset(s->rregs, 0, ESP_REGS);
    memset(s->wregs, 0, ESP_REGS);
    s->tchi_written = 0;
    s->ti_size = 0;
    s->async_len = 0;
    fifo8_reset(&s->fifo);
    fifo8_reset(&s->cmdfifo);
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
    uint32_t val;

    switch (saddr) {
    case ESP_FIFO:
        if (s->dma_memory_read && s->dma_memory_write &&
                (s->rregs[ESP_RSTAT] & STAT_PIO_MASK) == 0) {
            /* Data out.  */
            qemu_log_mask(LOG_UNIMP, "esp: PIO data read not implemented\n");
            s->rregs[ESP_FIFO] = 0;
        } else {
            if ((s->rregs[ESP_RSTAT] & 0x7) == STAT_DI) {
                if (s->ti_size) {
                    esp_do_nodma(s);
                } else {
                    /*
                     * The last byte of a non-DMA transfer has been read out
                     * of the FIFO so switch to status phase
                     */
                    s->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
                }
            }
            s->rregs[ESP_FIFO] = esp_fifo_pop(&s->fifo);
        }
        val = s->rregs[ESP_FIFO];
        break;
    case ESP_RINTR:
        /*
         * Clear sequence step, interrupt register and all status bits
         * except TC
         */
        val = s->rregs[ESP_RINTR];
        s->rregs[ESP_RINTR] = 0;
        s->rregs[ESP_RSTAT] &= ~STAT_TC;
        /*
         * According to the datasheet ESP_RSEQ should be cleared, but as the
         * emulation currently defers information transfers to the next TI
         * command leave it for now so that pedantic guests such as the old
         * Linux 2.6 driver see the correct flags before the next SCSI phase
         * transition.
         *
         * s->rregs[ESP_RSEQ] = SEQ_0;
         */
        esp_lower_irq(s);
        break;
    case ESP_TCHI:
        /* Return the unique id if the value has never been written */
        if (!s->tchi_written) {
            val = s->chip_id;
        } else {
            val = s->rregs[saddr];
        }
        break;
     case ESP_RFLAGS:
        /* Bottom 5 bits indicate number of bytes in FIFO */
        val = fifo8_num_used(&s->fifo);
        break;
    default:
        val = s->rregs[saddr];
        break;
    }

    trace_esp_mem_readb(saddr, val);
    return val;
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
            esp_fifo_push(&s->cmdfifo, val);

            /*
             * If any unexpected message out/command phase data is
             * transferred using non-DMA, raise the interrupt
             */
            if (s->rregs[ESP_CMD] == CMD_TI) {
                s->rregs[ESP_RINTR] |= INTR_BS;
                esp_raise_irq(s);
            }
        } else {
            esp_fifo_push(&s->fifo, val);
        }
        break;
    case ESP_CMD:
        s->rregs[saddr] = val;
        if (val & CMD_DMA) {
            s->dma = 1;
            /* Reload DMA counter.  */
            if (esp_get_stc(s) == 0) {
                esp_set_tc(s, 0x10000);
            } else {
                esp_set_tc(s, esp_get_stc(s));
            }
        } else {
            s->dma = 0;
        }
        switch (val & CMD_CMD) {
        case CMD_NOP:
            trace_esp_mem_writeb_cmd_nop(val);
            break;
        case CMD_FLUSH:
            trace_esp_mem_writeb_cmd_flush(val);
            fifo8_reset(&s->fifo);
            break;
        case CMD_RESET:
            trace_esp_mem_writeb_cmd_reset(val);
            esp_soft_reset(s);
            break;
        case CMD_BUSRESET:
            trace_esp_mem_writeb_cmd_bus_reset(val);
            if (!(s->wregs[ESP_CFG1] & CFG1_RESREPT)) {
                s->rregs[ESP_RINTR] |= INTR_RST;
                esp_raise_irq(s);
            }
            break;
        case CMD_TI:
            trace_esp_mem_writeb_cmd_ti(val);
            handle_ti(s);
            break;
        case CMD_ICCS:
            trace_esp_mem_writeb_cmd_iccs(val);
            write_response(s);
            s->rregs[ESP_RINTR] |= INTR_FC;
            s->rregs[ESP_RSTAT] |= STAT_MI;
            break;
        case CMD_MSGACC:
            trace_esp_mem_writeb_cmd_msgacc(val);
            s->rregs[ESP_RINTR] |= INTR_DC;
            s->rregs[ESP_RSEQ] = 0;
            s->rregs[ESP_RFLAGS] = 0;
            esp_raise_irq(s);
            break;
        case CMD_PAD:
            trace_esp_mem_writeb_cmd_pad(val);
            s->rregs[ESP_RSTAT] = STAT_TC;
            s->rregs[ESP_RINTR] |= INTR_FC;
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

static bool esp_is_before_version_5(void *opaque, int version_id)
{
    ESPState *s = ESP(opaque);

    version_id = MIN(version_id, s->mig_version_id);
    return version_id < 5;
}

static bool esp_is_version_5(void *opaque, int version_id)
{
    ESPState *s = ESP(opaque);

    version_id = MIN(version_id, s->mig_version_id);
    return version_id >= 5;
}

static bool esp_is_version_6(void *opaque, int version_id)
{
    ESPState *s = ESP(opaque);

    version_id = MIN(version_id, s->mig_version_id);
    return version_id >= 6;
}

int esp_pre_save(void *opaque)
{
    ESPState *s = ESP(object_resolve_path_component(
                      OBJECT(opaque), "esp"));

    s->mig_version_id = vmstate_esp.version_id;
    return 0;
}

static int esp_post_load(void *opaque, int version_id)
{
    ESPState *s = ESP(opaque);
    int len, i;

    version_id = MIN(version_id, s->mig_version_id);

    if (version_id < 5) {
        esp_set_tc(s, s->mig_dma_left);

        /* Migrate ti_buf to fifo */
        len = s->mig_ti_wptr - s->mig_ti_rptr;
        for (i = 0; i < len; i++) {
            fifo8_push(&s->fifo, s->mig_ti_buf[i]);
        }

        /* Migrate cmdbuf to cmdfifo */
        for (i = 0; i < s->mig_cmdlen; i++) {
            fifo8_push(&s->cmdfifo, s->mig_cmdbuf[i]);
        }
    }

    s->mig_version_id = vmstate_esp.version_id;
    return 0;
}

const VMStateDescription vmstate_esp = {
    .name = "esp",
    .version_id = 6,
    .minimum_version_id = 3,
    .post_load = esp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(rregs, ESPState),
        VMSTATE_BUFFER(wregs, ESPState),
        VMSTATE_INT32(ti_size, ESPState),
        VMSTATE_UINT32_TEST(mig_ti_rptr, ESPState, esp_is_before_version_5),
        VMSTATE_UINT32_TEST(mig_ti_wptr, ESPState, esp_is_before_version_5),
        VMSTATE_BUFFER_TEST(mig_ti_buf, ESPState, esp_is_before_version_5),
        VMSTATE_UINT32(status, ESPState),
        VMSTATE_UINT32_TEST(mig_deferred_status, ESPState,
                            esp_is_before_version_5),
        VMSTATE_BOOL_TEST(mig_deferred_complete, ESPState,
                          esp_is_before_version_5),
        VMSTATE_UINT32(dma, ESPState),
        VMSTATE_STATIC_BUFFER(mig_cmdbuf, ESPState, 0,
                              esp_is_before_version_5, 0, 16),
        VMSTATE_STATIC_BUFFER(mig_cmdbuf, ESPState, 4,
                              esp_is_before_version_5, 16,
                              sizeof(typeof_field(ESPState, mig_cmdbuf))),
        VMSTATE_UINT32_TEST(mig_cmdlen, ESPState, esp_is_before_version_5),
        VMSTATE_UINT32(do_cmd, ESPState),
        VMSTATE_UINT32_TEST(mig_dma_left, ESPState, esp_is_before_version_5),
        VMSTATE_BOOL_TEST(data_in_ready, ESPState, esp_is_version_5),
        VMSTATE_UINT8_TEST(cmdfifo_cdb_offset, ESPState, esp_is_version_5),
        VMSTATE_FIFO8_TEST(fifo, ESPState, esp_is_version_5),
        VMSTATE_FIFO8_TEST(cmdfifo, ESPState, esp_is_version_5),
        VMSTATE_UINT8_TEST(ti_cmd, ESPState, esp_is_version_5),
        VMSTATE_UINT8_TEST(lun, ESPState, esp_is_version_6),
        VMSTATE_END_OF_LIST()
    },
};

static void sysbus_esp_mem_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    ESPState *s = ESP(&sysbus->esp);
    uint32_t saddr;

    saddr = addr >> sysbus->it_shift;
    esp_reg_write(s, saddr, val);
}

static uint64_t sysbus_esp_mem_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    ESPState *s = ESP(&sysbus->esp);
    uint32_t saddr;

    saddr = addr >> sysbus->it_shift;
    return esp_reg_read(s, saddr);
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
    ESPState *s = ESP(&sysbus->esp);

    trace_esp_pdma_write(size);

    switch (size) {
    case 1:
        esp_pdma_write(s, val);
        break;
    case 2:
        esp_pdma_write(s, val >> 8);
        esp_pdma_write(s, val);
        break;
    }
    s->pdma_cb(s);
}

static uint64_t sysbus_esp_pdma_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    SysBusESPState *sysbus = opaque;
    ESPState *s = ESP(&sysbus->esp);
    uint64_t val = 0;

    trace_esp_pdma_read(size);

    switch (size) {
    case 1:
        val = esp_pdma_read(s);
        break;
    case 2:
        val = esp_pdma_read(s);
        val = (val << 8) | esp_pdma_read(s);
        break;
    }
    if (fifo8_num_used(&s->fifo) < 2) {
        s->pdma_cb(s);
    }
    return val;
}

static const MemoryRegionOps sysbus_esp_pdma_ops = {
    .read = sysbus_esp_pdma_read,
    .write = sysbus_esp_pdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 2,
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
    SysBusESPState *sysbus = SYSBUS_ESP(opaque);
    ESPState *s = ESP(&sysbus->esp);

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
    SysBusESPState *sysbus = SYSBUS_ESP(dev);
    ESPState *s = ESP(&sysbus->esp);

    if (!qdev_realize(DEVICE(s), NULL, errp)) {
        return;
    }

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->irq_data);
    assert(sysbus->it_shift != -1);

    s->chip_id = TCHI_FAS100A;
    memory_region_init_io(&sysbus->iomem, OBJECT(sysbus), &sysbus_esp_mem_ops,
                          sysbus, "esp-regs", ESP_REGS << sysbus->it_shift);
    sysbus_init_mmio(sbd, &sysbus->iomem);
    memory_region_init_io(&sysbus->pdma, OBJECT(sysbus), &sysbus_esp_pdma_ops,
                          sysbus, "esp-pdma", 4);
    sysbus_init_mmio(sbd, &sysbus->pdma);

    qdev_init_gpio_in(dev, sysbus_esp_gpio_demux, 2);

    scsi_bus_init(&s->bus, sizeof(s->bus), dev, &esp_scsi_info);
}

static void sysbus_esp_hard_reset(DeviceState *dev)
{
    SysBusESPState *sysbus = SYSBUS_ESP(dev);
    ESPState *s = ESP(&sysbus->esp);

    esp_hard_reset(s);
}

static void sysbus_esp_init(Object *obj)
{
    SysBusESPState *sysbus = SYSBUS_ESP(obj);

    object_initialize_child(obj, "esp", &sysbus->esp, TYPE_ESP);
}

static const VMStateDescription vmstate_sysbus_esp_scsi = {
    .name = "sysbusespscsi",
    .version_id = 2,
    .minimum_version_id = 1,
    .pre_save = esp_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_V(esp.mig_version_id, SysBusESPState, 2),
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
    .name          = TYPE_SYSBUS_ESP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = sysbus_esp_init,
    .instance_size = sizeof(SysBusESPState),
    .class_init    = sysbus_esp_class_init,
};

static void esp_finalize(Object *obj)
{
    ESPState *s = ESP(obj);

    fifo8_destroy(&s->fifo);
    fifo8_destroy(&s->cmdfifo);
}

static void esp_init(Object *obj)
{
    ESPState *s = ESP(obj);

    fifo8_create(&s->fifo, ESP_FIFO_SZ);
    fifo8_create(&s->cmdfifo, ESP_CMDFIFO_SZ);
}

static void esp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* internal device for sysbusesp/pciespscsi, not user-creatable */
    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo esp_info = {
    .name = TYPE_ESP,
    .parent = TYPE_DEVICE,
    .instance_init = esp_init,
    .instance_finalize = esp_finalize,
    .instance_size = sizeof(ESPState),
    .class_init = esp_class_init,
};

static void esp_register_types(void)
{
    type_register_static(&sysbus_esp_info);
    type_register_static(&esp_info);
}

type_init(esp_register_types)
