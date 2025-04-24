/*
 * QEMU ESP/NCR53C9x emulation
 *
 * Copyright (c) 2005-2006 Fabrice Bellard
 * Copyright (c) 2012 Herve Poussineau
 * Copyright (c) 2023 Mark Cave-Ayland
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
    if (!(s->drq_state)) {
        qemu_irq_raise(s->drq_irq);
        trace_esp_raise_drq();
        s->drq_state = true;
    }
}

static void esp_lower_drq(ESPState *s)
{
    if (s->drq_state) {
        qemu_irq_lower(s->drq_irq);
        trace_esp_lower_drq();
        s->drq_state = false;
    }
}

static const char *esp_phase_names[8] = {
    "DATA OUT", "DATA IN", "COMMAND", "STATUS",
    "(reserved)", "(reserved)", "MESSAGE OUT", "MESSAGE IN"
};

static void esp_set_phase(ESPState *s, uint8_t phase)
{
    s->rregs[ESP_RSTAT] &= ~7;
    s->rregs[ESP_RSTAT] |= phase;

    trace_esp_set_phase(esp_phase_names[phase]);
}

static uint8_t esp_get_phase(ESPState *s)
{
    return s->rregs[ESP_RSTAT] & 7;
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

static void esp_update_drq(ESPState *s)
{
    bool to_device;

    switch (esp_get_phase(s)) {
    case STAT_MO:
    case STAT_CD:
    case STAT_DO:
        to_device = true;
        break;

    case STAT_DI:
    case STAT_ST:
    case STAT_MI:
        to_device = false;
        break;

    default:
        return;
    }

    if (s->dma) {
        /* DMA request so update DRQ according to transfer direction */
        if (to_device) {
            if (fifo8_num_free(&s->fifo) < 2) {
                esp_lower_drq(s);
            } else {
                esp_raise_drq(s);
            }
        } else {
            if (fifo8_num_used(&s->fifo) < 2) {
                esp_lower_drq(s);
            } else {
                esp_raise_drq(s);
            }
        }
    } else {
        /* Not a DMA request */
        esp_lower_drq(s);
    }
}

static void esp_fifo_push(ESPState *s, uint8_t val)
{
    if (fifo8_num_used(&s->fifo) == s->fifo.capacity) {
        trace_esp_error_fifo_overrun();
    } else {
        fifo8_push(&s->fifo, val);
    }

    esp_update_drq(s);
}

static void esp_fifo_push_buf(ESPState *s, uint8_t *buf, int len)
{
    fifo8_push_all(&s->fifo, buf, len);
    esp_update_drq(s);
}

static uint8_t esp_fifo_pop(ESPState *s)
{
    uint8_t val;

    if (fifo8_is_empty(&s->fifo)) {
        val = 0;
    } else {
        val = fifo8_pop(&s->fifo);
    }

    esp_update_drq(s);
    return val;
}

static uint32_t esp_fifo_pop_buf(ESPState *s, uint8_t *dest, int maxlen)
{
    uint32_t len = fifo8_pop_buf(&s->fifo, dest, maxlen);

    esp_update_drq(s);
    return len;
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
    uint32_t old_tc = esp_get_tc(s);

    s->rregs[ESP_TCLO] = dmalen;
    s->rregs[ESP_TCMID] = dmalen >> 8;
    s->rregs[ESP_TCHI] = dmalen >> 16;

    if (old_tc && dmalen == 0) {
        s->rregs[ESP_RSTAT] |= STAT_TC;
    }
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
    return esp_fifo_pop(s);
}

static void esp_pdma_write(ESPState *s, uint8_t val)
{
    uint32_t dmalen = esp_get_tc(s);

    esp_fifo_push(s, val);

    if (dmalen && s->drq_state) {
        dmalen--;
        esp_set_tc(s, dmalen);
    }
}

static int esp_select(ESPState *s)
{
    int target;

    target = s->wregs[ESP_WBUSID] & BUSID_DID;

    s->ti_size = 0;
    s->rregs[ESP_RSEQ] = SEQ_0;

    if (s->current_req) {
        /* Started a new command before the old one finished. Cancel it. */
        scsi_req_cancel(s->current_req);
    }

    s->current_dev = scsi_device_find(&s->bus, 0, target, 0);
    if (!s->current_dev) {
        /* No such drive */
        s->rregs[ESP_RSTAT] = 0;
        s->rregs[ESP_RINTR] = INTR_DC;
        esp_raise_irq(s);
        return -1;
    }

    /*
     * Note that we deliberately don't raise the IRQ here: this will be done
     * either in esp_transfer_data() or esp_command_complete()
     */
    return 0;
}

static void esp_do_dma(ESPState *s);
static void esp_do_nodma(ESPState *s);

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
    fifo8_pop_buf(&s->cmdfifo, buf, cmdlen);

    current_lun = scsi_device_find(&s->bus, 0, s->current_dev->id, s->lun);
    if (!current_lun) {
        /* No such drive */
        s->rregs[ESP_RSTAT] = 0;
        s->rregs[ESP_RINTR] = INTR_DC;
        s->rregs[ESP_RSEQ] = SEQ_0;
        esp_raise_irq(s);
        return;
    }

    s->current_req = scsi_req_new(current_lun, 0, s->lun, buf, cmdlen, s);
    datalen = scsi_req_enqueue(s->current_req);
    s->ti_size = datalen;
    fifo8_reset(&s->cmdfifo);
    s->data_ready = false;
    if (datalen != 0) {
        /*
         * Switch to DATA phase but wait until initial data xfer is
         * complete before raising the command completion interrupt
         */
        if (datalen > 0) {
            esp_set_phase(s, STAT_DI);
        } else {
            esp_set_phase(s, STAT_DO);
        }
        scsi_req_continue(s->current_req);
        return;
    }
}

static void do_message_phase(ESPState *s)
{
    if (s->cmdfifo_cdb_offset) {
        uint8_t message = fifo8_is_empty(&s->cmdfifo) ? 0 :
                          fifo8_pop(&s->cmdfifo);

        trace_esp_do_identify(message);
        s->lun = message & 7;
        s->cmdfifo_cdb_offset--;
    }

    /* Ignore extended messages for now */
    if (s->cmdfifo_cdb_offset) {
        int len = MIN(s->cmdfifo_cdb_offset, fifo8_num_used(&s->cmdfifo));
        fifo8_drop(&s->cmdfifo, len);
        s->cmdfifo_cdb_offset = 0;
    }
}

static void do_cmd(ESPState *s)
{
    do_message_phase(s);
    assert(s->cmdfifo_cdb_offset == 0);
    do_command_phase(s);
}

static void handle_satn(ESPState *s)
{
    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn;
        return;
    }

    if (esp_select(s) < 0) {
        return;
    }

    esp_set_phase(s, STAT_MO);

    if (s->dma) {
        esp_do_dma(s);
    } else {
        esp_do_nodma(s);
    }
}

static void handle_s_without_atn(ESPState *s)
{
    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_s_without_atn;
        return;
    }

    if (esp_select(s) < 0) {
        return;
    }

    esp_set_phase(s, STAT_CD);
    s->cmdfifo_cdb_offset = 0;

    if (s->dma) {
        esp_do_dma(s);
    } else {
        esp_do_nodma(s);
    }
}

static void handle_satn_stop(ESPState *s)
{
    if (s->dma && !s->dma_enabled) {
        s->dma_cb = handle_satn_stop;
        return;
    }

    if (esp_select(s) < 0) {
        return;
    }

    esp_set_phase(s, STAT_MO);
    s->cmdfifo_cdb_offset = 0;

    if (s->dma) {
        esp_do_dma(s);
    } else {
        esp_do_nodma(s);
    }
}

static void handle_pad(ESPState *s)
{
    if (s->dma) {
        esp_do_dma(s);
    } else {
        esp_do_nodma(s);
    }
}

static void write_response(ESPState *s)
{
    trace_esp_write_response(s->status);

    if (s->dma) {
        esp_do_dma(s);
    } else {
        esp_do_nodma(s);
    }
}

static bool esp_cdb_ready(ESPState *s)
{
    int len = fifo8_num_used(&s->cmdfifo) - s->cmdfifo_cdb_offset;
    const uint8_t *pbuf;
    uint32_t n;
    int cdblen;

    if (len <= 0) {
        return false;
    }

    pbuf = fifo8_peek_bufptr(&s->cmdfifo, len, &n);
    if (n < len) {
        /*
         * In normal use the cmdfifo should never wrap, but include this check
         * to prevent a malicious guest from reading past the end of the
         * cmdfifo data buffer below
         */
        return false;
    }

    cdblen = scsi_cdb_length((uint8_t *)&pbuf[s->cmdfifo_cdb_offset]);

    return cdblen < 0 ? false : (len >= cdblen);
}

static void esp_dma_ti_check(ESPState *s)
{
    if (esp_get_tc(s) == 0 && fifo8_num_used(&s->fifo) < 2) {
        s->rregs[ESP_RINTR] |= INTR_BS;
        esp_raise_irq(s);
    }
}

static void esp_do_dma(ESPState *s)
{
    uint32_t len, cmdlen;
    uint8_t buf[ESP_CMDFIFO_SZ];

    len = esp_get_tc(s);

    switch (esp_get_phase(s)) {
    case STAT_MO:
        if (s->dma_memory_read) {
            len = MIN(len, fifo8_num_free(&s->cmdfifo));
            s->dma_memory_read(s->dma_opaque, buf, len);
            esp_set_tc(s, esp_get_tc(s) - len);
        } else {
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
        }

        fifo8_push_all(&s->cmdfifo, buf, len);
        s->cmdfifo_cdb_offset += len;

        switch (s->rregs[ESP_CMD]) {
        case CMD_SELATN | CMD_DMA:
            if (fifo8_num_used(&s->cmdfifo) >= 1) {
                /* First byte received, switch to command phase */
                esp_set_phase(s, STAT_CD);
                s->rregs[ESP_RSEQ] = SEQ_CD;
                s->cmdfifo_cdb_offset = 1;

                if (fifo8_num_used(&s->cmdfifo) > 1) {
                    /* Process any additional command phase data */
                    esp_do_dma(s);
                }
            }
            break;

        case CMD_SELATNS | CMD_DMA:
            if (fifo8_num_used(&s->cmdfifo) == 1) {
                /* First byte received, stop in message out phase */
                s->rregs[ESP_RSEQ] = SEQ_MO;
                s->cmdfifo_cdb_offset = 1;

                /* Raise command completion interrupt */
                s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
                esp_raise_irq(s);
            }
            break;

        case CMD_TI | CMD_DMA:
            /* ATN remains asserted until TC == 0 */
            if (esp_get_tc(s) == 0) {
                esp_set_phase(s, STAT_CD);
                s->rregs[ESP_CMD] = 0;
                s->rregs[ESP_RINTR] |= INTR_BS;
                esp_raise_irq(s);
            }
            break;
        }
        break;

    case STAT_CD:
        cmdlen = fifo8_num_used(&s->cmdfifo);
        trace_esp_do_dma(cmdlen, len);
        if (s->dma_memory_read) {
            len = MIN(len, fifo8_num_free(&s->cmdfifo));
            s->dma_memory_read(s->dma_opaque, buf, len);
            fifo8_push_all(&s->cmdfifo, buf, len);
            esp_set_tc(s, esp_get_tc(s) - len);
        } else {
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);
        }
        trace_esp_handle_ti_cmd(cmdlen);
        s->ti_size = 0;
        if (esp_get_tc(s) == 0) {
            /* Command has been received */
            do_cmd(s);
        }
        break;

    case STAT_DO:
        if (!s->current_req) {
            return;
        }
        if (s->async_len == 0 && esp_get_tc(s)) {
            /* Defer until data is available.  */
            return;
        }
        if (len > s->async_len) {
            len = s->async_len;
        }

        switch (s->rregs[ESP_CMD]) {
        case CMD_TI | CMD_DMA:
            if (s->dma_memory_read) {
                s->dma_memory_read(s->dma_opaque, s->async_buf, len);
                esp_set_tc(s, esp_get_tc(s) - len);
            } else {
                /* Copy FIFO data to device */
                len = MIN(s->async_len, ESP_FIFO_SZ);
                len = MIN(len, fifo8_num_used(&s->fifo));
                len = esp_fifo_pop_buf(s, s->async_buf, len);
            }

            s->async_buf += len;
            s->async_len -= len;
            s->ti_size += len;
            break;

        case CMD_PAD | CMD_DMA:
            /* Copy TC zero bytes into the incoming stream */
            if (!s->dma_memory_read) {
                len = MIN(s->async_len, ESP_FIFO_SZ);
                len = MIN(len, fifo8_num_free(&s->fifo));
            }

            memset(s->async_buf, 0, len);

            s->async_buf += len;
            s->async_len -= len;
            s->ti_size += len;
            break;
        }

        if (s->async_len == 0 && fifo8_num_used(&s->fifo) < 2) {
            /* Defer until the scsi layer has completed */
            scsi_req_continue(s->current_req);
            return;
        }

        esp_dma_ti_check(s);
        break;

    case STAT_DI:
        if (!s->current_req) {
            return;
        }
        if (s->async_len == 0 && esp_get_tc(s)) {
            /* Defer until data is available.  */
            return;
        }
        if (len > s->async_len) {
            len = s->async_len;
        }

        switch (s->rregs[ESP_CMD]) {
        case CMD_TI | CMD_DMA:
            if (s->dma_memory_write) {
                s->dma_memory_write(s->dma_opaque, s->async_buf, len);
            } else {
                /* Copy device data to FIFO */
                len = MIN(len, fifo8_num_free(&s->fifo));
                esp_fifo_push_buf(s, s->async_buf, len);
            }

            s->async_buf += len;
            s->async_len -= len;
            s->ti_size -= len;
            esp_set_tc(s, esp_get_tc(s) - len);
            break;

        case CMD_PAD | CMD_DMA:
            /* Drop TC bytes from the incoming stream */
            if (!s->dma_memory_write) {
                len = MIN(len, fifo8_num_free(&s->fifo));
            }

            s->async_buf += len;
            s->async_len -= len;
            s->ti_size -= len;
            esp_set_tc(s, esp_get_tc(s) - len);
            break;
        }

        if (s->async_len == 0 && s->ti_size == 0 && esp_get_tc(s)) {
            /* If the guest underflows TC then terminate SCSI request */
            scsi_req_continue(s->current_req);
            return;
        }

        if (s->async_len == 0 && fifo8_num_used(&s->fifo) < 2) {
            /* Defer until the scsi layer has completed */
            scsi_req_continue(s->current_req);
            return;
        }

        esp_dma_ti_check(s);
        break;

    case STAT_ST:
        switch (s->rregs[ESP_CMD]) {
        case CMD_ICCS | CMD_DMA:
            len = MIN(len, 1);

            if (len) {
                buf[0] = s->status;

                if (s->dma_memory_write) {
                    s->dma_memory_write(s->dma_opaque, buf, len);
                } else {
                    esp_fifo_push_buf(s, buf, len);
                }

                esp_set_tc(s, esp_get_tc(s) - len);
                esp_set_phase(s, STAT_MI);

                if (esp_get_tc(s) > 0) {
                    /* Process any message in phase data */
                    esp_do_dma(s);
                }
            }
            break;

        default:
            /* Consume remaining data if the guest underflows TC */
            if (fifo8_num_used(&s->fifo) < 2) {
                s->rregs[ESP_RINTR] |= INTR_BS;
                esp_raise_irq(s);
            }
            break;
        }
        break;

    case STAT_MI:
        switch (s->rregs[ESP_CMD]) {
        case CMD_ICCS | CMD_DMA:
            len = MIN(len, 1);

            if (len) {
                buf[0] = 0;

                if (s->dma_memory_write) {
                    s->dma_memory_write(s->dma_opaque, buf, len);
                } else {
                    esp_fifo_push_buf(s, buf, len);
                }

                esp_set_tc(s, esp_get_tc(s) - len);

                /* Raise end of command interrupt */
                s->rregs[ESP_RINTR] |= INTR_FC;
                esp_raise_irq(s);
            }
            break;
        }
        break;
    }
}

static void esp_nodma_ti_dataout(ESPState *s)
{
    int len;

    if (!s->current_req) {
        return;
    }
    if (s->async_len == 0) {
        /* Defer until data is available.  */
        return;
    }
    len = MIN(s->async_len, ESP_FIFO_SZ);
    len = MIN(len, fifo8_num_used(&s->fifo));
    esp_fifo_pop_buf(s, s->async_buf, len);
    s->async_buf += len;
    s->async_len -= len;
    s->ti_size += len;

    if (s->async_len == 0) {
        scsi_req_continue(s->current_req);
        return;
    }

    s->rregs[ESP_RINTR] |= INTR_BS;
    esp_raise_irq(s);
}

static void esp_do_nodma(ESPState *s)
{
    uint8_t buf[ESP_FIFO_SZ];
    uint32_t cmdlen;
    int len;

    switch (esp_get_phase(s)) {
    case STAT_MO:
        switch (s->rregs[ESP_CMD]) {
        case CMD_SELATN:
            /* Copy FIFO into cmdfifo */
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            if (fifo8_num_used(&s->cmdfifo) >= 1) {
                /* First byte received, switch to command phase */
                esp_set_phase(s, STAT_CD);
                s->rregs[ESP_RSEQ] = SEQ_CD;
                s->cmdfifo_cdb_offset = 1;

                if (fifo8_num_used(&s->cmdfifo) > 1) {
                    /* Process any additional command phase data */
                    esp_do_nodma(s);
                }
            }
            break;

        case CMD_SELATNS:
            /* Copy one byte from FIFO into cmdfifo */
            len = esp_fifo_pop_buf(s, buf,
                                   MIN(fifo8_num_used(&s->fifo), 1));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            if (fifo8_num_used(&s->cmdfifo) >= 1) {
                /* First byte received, stop in message out phase */
                s->rregs[ESP_RSEQ] = SEQ_MO;
                s->cmdfifo_cdb_offset = 1;

                /* Raise command completion interrupt */
                s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
                esp_raise_irq(s);
            }
            break;

        case CMD_TI:
            /* Copy FIFO into cmdfifo */
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            /* ATN remains asserted until FIFO empty */
            s->cmdfifo_cdb_offset = fifo8_num_used(&s->cmdfifo);
            esp_set_phase(s, STAT_CD);
            s->rregs[ESP_CMD] = 0;
            s->rregs[ESP_RINTR] |= INTR_BS;
            esp_raise_irq(s);
            break;
        }
        break;

    case STAT_CD:
        switch (s->rregs[ESP_CMD]) {
        case CMD_TI:
            /* Copy FIFO into cmdfifo */
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            cmdlen = fifo8_num_used(&s->cmdfifo);
            trace_esp_handle_ti_cmd(cmdlen);

            /* CDB may be transferred in one or more TI commands */
            if (esp_cdb_ready(s)) {
                /* Command has been received */
                do_cmd(s);
            } else {
                /*
                 * If data was transferred from the FIFO then raise bus
                 * service interrupt to indicate transfer complete. Otherwise
                 * defer until the next FIFO write.
                 */
                if (len) {
                    /* Raise interrupt to indicate transfer complete */
                    s->rregs[ESP_RINTR] |= INTR_BS;
                    esp_raise_irq(s);
                }
            }
            break;

        case CMD_SEL | CMD_DMA:
        case CMD_SELATN | CMD_DMA:
            /* Copy FIFO into cmdfifo */
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            /* Handle when DMA transfer is terminated by non-DMA FIFO write */
            if (esp_cdb_ready(s)) {
                /* Command has been received */
                do_cmd(s);
            }
            break;

        case CMD_SEL:
        case CMD_SELATN:
            /* FIFO already contain entire CDB: copy to cmdfifo and execute */
            len = esp_fifo_pop_buf(s, buf, fifo8_num_used(&s->fifo));
            len = MIN(fifo8_num_free(&s->cmdfifo), len);
            fifo8_push_all(&s->cmdfifo, buf, len);

            do_cmd(s);
            break;
        }
        break;

    case STAT_DO:
        /* Accumulate data in FIFO until non-DMA TI is executed */
        break;

    case STAT_DI:
        if (!s->current_req) {
            return;
        }
        if (s->async_len == 0) {
            /* Defer until data is available.  */
            return;
        }
        if (fifo8_is_empty(&s->fifo)) {
            esp_fifo_push(s, s->async_buf[0]);
            s->async_buf++;
            s->async_len--;
            s->ti_size--;
        }

        if (s->async_len == 0) {
            scsi_req_continue(s->current_req);
            return;
        }

        /* If preloading the FIFO, defer until TI command issued */
        if (s->rregs[ESP_CMD] != CMD_TI) {
            return;
        }

        s->rregs[ESP_RINTR] |= INTR_BS;
        esp_raise_irq(s);
        break;

    case STAT_ST:
        switch (s->rregs[ESP_CMD]) {
        case CMD_ICCS:
            esp_fifo_push(s, s->status);
            esp_set_phase(s, STAT_MI);

            /* Process any message in phase data */
            esp_do_nodma(s);
            break;
        }
        break;

    case STAT_MI:
        switch (s->rregs[ESP_CMD]) {
        case CMD_ICCS:
            esp_fifo_push(s, 0);

            /* Raise end of command interrupt */
            s->rregs[ESP_RINTR] |= INTR_FC;
            esp_raise_irq(s);
            break;
        }
        break;
    }
}

void esp_command_complete(SCSIRequest *req, size_t resid)
{
    ESPState *s = req->hba_private;
    int to_device = (esp_get_phase(s) == STAT_DO);

    trace_esp_command_complete();

    /*
     * Non-DMA transfers from the target will leave the last byte in
     * the FIFO so don't reset ti_size in this case
     */
    if (s->dma || to_device) {
        if (s->ti_size != 0) {
            trace_esp_command_complete_unexpected();
        }
    }

    s->async_len = 0;
    if (req->status) {
        trace_esp_command_complete_fail();
    }
    s->status = req->status;

    /*
     * Switch to status phase. For non-DMA transfers from the target the last
     * byte is still in the FIFO
     */
    s->ti_size = 0;

    switch (s->rregs[ESP_CMD]) {
    case CMD_SEL | CMD_DMA:
    case CMD_SEL:
    case CMD_SELATN | CMD_DMA:
    case CMD_SELATN:
        /*
         * No data phase for sequencer command so raise deferred bus service
         * and function complete interrupt
         */
        s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
        s->rregs[ESP_RSEQ] = SEQ_CD;
        break;

    case CMD_TI | CMD_DMA:
    case CMD_TI:
        s->rregs[ESP_CMD] = 0;
        break;
    }

    /* Raise bus service interrupt to indicate change to STATUS phase */
    esp_set_phase(s, STAT_ST);
    s->rregs[ESP_RINTR] |= INTR_BS;
    esp_raise_irq(s);

    if (s->current_req) {
        scsi_req_unref(s->current_req);
        s->current_req = NULL;
        s->current_dev = NULL;
    }
}

void esp_transfer_data(SCSIRequest *req, uint32_t len)
{
    ESPState *s = req->hba_private;
    uint32_t dmalen = esp_get_tc(s);

    trace_esp_transfer_data(dmalen, s->ti_size);
    s->async_len = len;
    s->async_buf = scsi_req_get_buf(req);

    if (!s->data_ready) {
        s->data_ready = true;

        switch (s->rregs[ESP_CMD]) {
        case CMD_SEL | CMD_DMA:
        case CMD_SEL:
        case CMD_SELATN | CMD_DMA:
        case CMD_SELATN:
            /*
             * Initial incoming data xfer is complete for sequencer command
             * so raise deferred bus service and function complete interrupt
             */
             s->rregs[ESP_RINTR] |= INTR_BS | INTR_FC;
             s->rregs[ESP_RSEQ] = SEQ_CD;
             break;

        case CMD_SELATNS | CMD_DMA:
        case CMD_SELATNS:
            /*
             * Initial incoming data xfer is complete so raise command
             * completion interrupt
             */
             s->rregs[ESP_RINTR] |= INTR_BS;
             s->rregs[ESP_RSEQ] = SEQ_MO;
             break;

        case CMD_TI | CMD_DMA:
        case CMD_TI:
            /*
             * Bus service interrupt raised because of initial change to
             * DATA phase
             */
            s->rregs[ESP_CMD] = 0;
            s->rregs[ESP_RINTR] |= INTR_BS;
            break;
        }

        esp_raise_irq(s);
    }

    /*
     * Always perform the initial transfer upon reception of the next TI
     * command to ensure the DMA/non-DMA status of the command is correct.
     * It is not possible to use s->dma directly in the section below as
     * some OSs send non-DMA NOP commands after a DMA transfer. Hence if the
     * async data transfer is delayed then s->dma is set incorrectly.
     */

    if (s->rregs[ESP_CMD] == (CMD_TI | CMD_DMA)) {
        /* When the SCSI layer returns more data, raise deferred INTR_BS */
        esp_dma_ti_check(s);

        esp_do_dma(s);
    } else if (s->rregs[ESP_CMD] == CMD_TI) {
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

    if (s->dma) {
        dmalen = esp_get_tc(s);
        trace_esp_handle_ti(dmalen);
        esp_do_dma(s);
    } else {
        trace_esp_handle_ti(s->ti_size);
        esp_do_nodma(s);

        if (esp_get_phase(s) == STAT_DO) {
            esp_nodma_ti_dataout(s);
        }
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
    s->dma_cb = NULL;

    s->rregs[ESP_CFG1] = 7;
}

static void esp_soft_reset(ESPState *s)
{
    qemu_irq_lower(s->irq);
    qemu_irq_lower(s->drq_irq);
    esp_hard_reset(s);
}

static void esp_bus_reset(ESPState *s)
{
    bus_cold_reset(BUS(&s->bus));
}

static void parent_esp_reset(ESPState *s, int irq, int level)
{
    if (level) {
        esp_soft_reset(s);
    }
}

static void esp_run_cmd(ESPState *s)
{
    uint8_t cmd = s->rregs[ESP_CMD];

    if (cmd & CMD_DMA) {
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
    switch (cmd & CMD_CMD) {
    case CMD_NOP:
        trace_esp_mem_writeb_cmd_nop(cmd);
        break;
    case CMD_FLUSH:
        trace_esp_mem_writeb_cmd_flush(cmd);
        fifo8_reset(&s->fifo);
        break;
    case CMD_RESET:
        trace_esp_mem_writeb_cmd_reset(cmd);
        esp_soft_reset(s);
        break;
    case CMD_BUSRESET:
        trace_esp_mem_writeb_cmd_bus_reset(cmd);
        esp_bus_reset(s);
        if (!(s->wregs[ESP_CFG1] & CFG1_RESREPT)) {
            s->rregs[ESP_RINTR] |= INTR_RST;
            esp_raise_irq(s);
        }
        break;
    case CMD_TI:
        trace_esp_mem_writeb_cmd_ti(cmd);
        handle_ti(s);
        break;
    case CMD_ICCS:
        trace_esp_mem_writeb_cmd_iccs(cmd);
        write_response(s);
        break;
    case CMD_MSGACC:
        trace_esp_mem_writeb_cmd_msgacc(cmd);
        s->rregs[ESP_RINTR] |= INTR_DC;
        s->rregs[ESP_RSEQ] = 0;
        s->rregs[ESP_RFLAGS] = 0;
        esp_raise_irq(s);
        break;
    case CMD_PAD:
        trace_esp_mem_writeb_cmd_pad(cmd);
        handle_pad(s);
        break;
    case CMD_SATN:
        trace_esp_mem_writeb_cmd_satn(cmd);
        break;
    case CMD_RSTATN:
        trace_esp_mem_writeb_cmd_rstatn(cmd);
        break;
    case CMD_SEL:
        trace_esp_mem_writeb_cmd_sel(cmd);
        handle_s_without_atn(s);
        break;
    case CMD_SELATN:
        trace_esp_mem_writeb_cmd_selatn(cmd);
        handle_satn(s);
        break;
    case CMD_SELATNS:
        trace_esp_mem_writeb_cmd_selatns(cmd);
        handle_satn_stop(s);
        break;
    case CMD_ENSEL:
        trace_esp_mem_writeb_cmd_ensel(cmd);
        s->rregs[ESP_RINTR] = 0;
        break;
    case CMD_DISSEL:
        trace_esp_mem_writeb_cmd_dissel(cmd);
        s->rregs[ESP_RINTR] = 0;
        esp_raise_irq(s);
        break;
    default:
        trace_esp_error_unhandled_command(cmd);
        break;
    }
}

uint64_t esp_reg_read(ESPState *s, uint32_t saddr)
{
    uint32_t val;

    switch (saddr) {
    case ESP_FIFO:
        s->rregs[ESP_FIFO] = esp_fifo_pop(s);
        val = s->rregs[ESP_FIFO];
        break;
    case ESP_RINTR:
        /*
         * Clear sequence step, interrupt register and all status bits
         * except TC
         */
        val = s->rregs[ESP_RINTR];
        s->rregs[ESP_RINTR] = 0;
        esp_lower_irq(s);
        s->rregs[ESP_RSTAT] &= STAT_TC | 7;
        /*
         * According to the datasheet ESP_RSEQ should be cleared, but as the
         * emulation currently defers information transfers to the next TI
         * command leave it for now so that pedantic guests such as the old
         * Linux 2.6 driver see the correct flags before the next SCSI phase
         * transition.
         *
         * s->rregs[ESP_RSEQ] = SEQ_0;
         */
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
        if (!fifo8_is_full(&s->fifo)) {
            esp_fifo_push(s, val);
        }
        esp_do_nodma(s);
        break;
    case ESP_CMD:
        s->rregs[saddr] = val;
        esp_run_cmd(s);
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

static bool esp_is_between_version_5_and_6(void *opaque, int version_id)
{
    ESPState *s = ESP(opaque);

    version_id = MIN(version_id, s->mig_version_id);
    return version_id >= 5 && version_id <= 6;
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
    .version_id = 7,
    .minimum_version_id = 3,
    .post_load = esp_post_load,
    .fields = (const VMStateField[]) {
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
        VMSTATE_BOOL_TEST(data_ready, ESPState, esp_is_version_5),
        VMSTATE_UINT8_TEST(cmdfifo_cdb_offset, ESPState, esp_is_version_5),
        VMSTATE_FIFO8_TEST(fifo, ESPState, esp_is_version_5),
        VMSTATE_FIFO8_TEST(cmdfifo, ESPState, esp_is_version_5),
        VMSTATE_UINT8_TEST(mig_ti_cmd, ESPState,
                           esp_is_between_version_5_and_6),
        VMSTATE_UINT8_TEST(lun, ESPState, esp_is_version_6),
        VMSTATE_BOOL(drq_state, ESPState),
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
    esp_do_dma(s);
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
    esp_do_dma(s);
    return val;
}

static void *esp_load_request(QEMUFile *f, SCSIRequest *req)
{
    ESPState *s = container_of(req->bus, ESPState, bus);

    scsi_req_ref(req);
    s->current_req = req;
    return s;
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

    .load_request = esp_load_request,
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
        esp_dma_enable(s, irq, level);
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
    sysbus_init_irq(sbd, &s->drq_irq);
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_V(esp.mig_version_id, SysBusESPState, 2),
        VMSTATE_STRUCT(esp, SysBusESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static void sysbus_esp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sysbus_esp_realize;
    device_class_set_legacy_reset(dc, sysbus_esp_hard_reset);
    dc->vmsd = &vmstate_sysbus_esp_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

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

static const TypeInfo esp_info_types[] = {
    {
        .name          = TYPE_SYSBUS_ESP,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_init = sysbus_esp_init,
        .instance_size = sizeof(SysBusESPState),
        .class_init    = sysbus_esp_class_init,
    },
    {
        .name = TYPE_ESP,
        .parent = TYPE_DEVICE,
        .instance_init = esp_init,
        .instance_finalize = esp_finalize,
        .instance_size = sizeof(ESPState),
        .class_init = esp_class_init,
    },
};

DEFINE_TYPES(esp_info_types)
