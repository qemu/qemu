/*
 * CTU CAN FD PCI device emulation
 * http://canbus.pages.fel.cvut.cz/
 *
 * Copyright (c) 2019 Jan Charvat (jancharvat.charvat@gmail.com)
 *
 * Based on Kvaser PCI CAN device (SJA1000 based) emulation implemented by
 * Jin Yang and Pavel Pisa
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
#include "qemu/log.h"
#include "chardev/char.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "ctucan_core.h"

#ifndef DEBUG_CAN
#define DEBUG_CAN 0
#endif /*DEBUG_CAN*/

#define DPRINTF(fmt, ...) \
    do { \
        if (DEBUG_CAN) { \
            qemu_log("[ctucan]: " fmt , ## __VA_ARGS__); \
        } \
    } while (0)

static void ctucan_buff2frame(const uint8_t *buff, qemu_can_frame *frame)
{
    frame->can_id = 0;
    frame->can_dlc = 0;
    frame->flags = 0;

    if (buff == NULL) {
        return;
    }
    {
        union ctu_can_fd_frame_form_w frame_form_w;
        union ctu_can_fd_identifier_w identifier_w;
        unsigned int ide;
        uint32_t w;

        w = le32_to_cpu(*(uint32_t *)buff);
        frame_form_w = (union ctu_can_fd_frame_form_w)w;
        frame->can_dlc = can_dlc2len(frame_form_w.s.dlc);

        w = le32_to_cpu(*(uint32_t *)(buff + 4));
        identifier_w = (union ctu_can_fd_identifier_w)w;

        ide = frame_form_w.s.ide;
        if (ide) {
            frame->can_id = (identifier_w.s.identifier_base << 18) |
                            identifier_w.s.identifier_ext;
            frame->can_id |= QEMU_CAN_EFF_FLAG;
        } else {
            frame->can_id = identifier_w.s.identifier_base;
        }

        if (frame_form_w.s.esi_rsv) {
            frame->flags |= QEMU_CAN_FRMF_ESI;
        }

        if (frame_form_w.s.rtr) {
            frame->can_id |= QEMU_CAN_RTR_FLAG;
        }

        if (frame_form_w.s.fdf) {   /*CAN FD*/
            frame->flags |= QEMU_CAN_FRMF_TYPE_FD;
            if (frame_form_w.s.brs) {
                frame->flags |= QEMU_CAN_FRMF_BRS;
            }
        }
    }

    memcpy(frame->data, buff + 0x10, 0x40);
}


static int ctucan_frame2buff(const qemu_can_frame *frame, uint8_t *buff)
{
    unsigned int bytes_cnt = -1;
    memset(buff, 0, CTUCAN_MSG_MAX_LEN * sizeof(*buff));

    if (frame == NULL) {
        return bytes_cnt;
    }
    {
        union ctu_can_fd_frame_form_w frame_form_w;
        union ctu_can_fd_identifier_w identifier_w;

        frame_form_w.u32 = 0;
        identifier_w.u32 = 0;

        bytes_cnt = frame->can_dlc;
        bytes_cnt = (bytes_cnt + 3) & ~3;
        bytes_cnt += 16;
        frame_form_w.s.rwcnt = (bytes_cnt >> 2) - 1;

        frame_form_w.s.dlc = can_len2dlc(frame->can_dlc);

        if (frame->can_id & QEMU_CAN_EFF_FLAG) {
            frame_form_w.s.ide = 1;
            identifier_w.s.identifier_base =
                                    (frame->can_id & 0x1FFC0000) >> 18;
            identifier_w.s.identifier_ext = frame->can_id & 0x3FFFF;
        } else {
            identifier_w.s.identifier_base = frame->can_id & 0x7FF;
        }

        if (frame->flags & QEMU_CAN_FRMF_ESI) {
            frame_form_w.s.esi_rsv = 1;
        }

        if (frame->can_id & QEMU_CAN_RTR_FLAG) {
            frame_form_w.s.rtr = 1;
        }

        if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {  /*CAN FD*/
           frame_form_w.s.fdf = 1;
            if (frame->flags & QEMU_CAN_FRMF_BRS) {
                frame_form_w.s.brs = 1;
            }
        }
        *(uint32_t *)buff = cpu_to_le32(frame_form_w.u32);
        *(uint32_t *)(buff + 4) = cpu_to_le32(identifier_w.u32);
    }

    memcpy(buff + 0x10, frame->data, 0x40);

    return bytes_cnt;
}

static void ctucan_update_irq(CtuCanCoreState *s)
{
    union ctu_can_fd_int_stat int_rq;

    int_rq.u32 = 0;

    if (s->rx_status_rx_settings.s.rxfrc) {
        int_rq.s.rbnei = 1;
    }

    int_rq.u32 &= ~s->int_mask.u32;
    s->int_stat.u32 |= int_rq.u32;
    if (s->int_stat.u32 & s->int_ena.u32) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void ctucan_update_txnf(CtuCanCoreState *s)
{
    int i;
    int txnf;
    unsigned int buff_st;

    txnf = 0;

    for (i = 0; i < CTUCAN_CORE_TXBUF_NUM; i++) {
        buff_st = (s->tx_status.u32 >> (i * 4)) & 0xf;
        if (buff_st == TXT_ETY) {
            txnf = 1;
        }
    }
    s->status.s.txnf = txnf;
}

void ctucan_hardware_reset(CtuCanCoreState *s)
{
    DPRINTF("Hardware reset in progress!!!\n");
    int i;
    unsigned int buff_st;
    uint32_t buff_st_mask;

    s->tx_status.u32 = 0;
    for (i = 0; i < CTUCAN_CORE_TXBUF_NUM; i++) {
        buff_st_mask = 0xf << (i * 4);
        buff_st = TXT_ETY;
        s->tx_status.u32 = (s->tx_status.u32 & ~buff_st_mask) |
            (buff_st << (i * 4));
    }
    s->status.s.idle = 1;

    ctucan_update_txnf(s);

    s->rx_status_rx_settings.u32 = 0;
    s->rx_tail_pos = 0;
    s->rx_cnt = 0;
    s->rx_frame_rem = 0;

    /* Flush RX buffer */
    s->rx_tail_pos = 0;
    s->rx_cnt = 0;
    s->rx_frame_rem = 0;

    /* Set on progdokum reset value */
    s->mode_settings.u32 = 0;
    s->mode_settings.s.fde = 1;

    s->int_stat.u32 = 0;
    s->int_ena.u32 = 0;
    s->int_mask.u32 = 0;

    s->rx_status_rx_settings.u32 = 0;
    s->rx_status_rx_settings.s.rxe = 0;

    s->rx_fr_ctr.u32 = 0;
    s->tx_fr_ctr.u32 = 0;

    s->yolo_reg.s.yolo_val = 3735928559;

    qemu_irq_lower(s->irq);
}

static void ctucan_send_ready_buffers(CtuCanCoreState *s)
{
    qemu_can_frame frame;
    uint8_t *pf;
    int buff2tx_idx;
    uint32_t tx_prio_max;

    if (!s->mode_settings.s.ena) {
        return;
    }

    do {
        union ctu_can_fd_int_stat int_stat;
        int i;
        buff2tx_idx = -1;
        tx_prio_max = 0;

        for (i = 0; i < CTUCAN_CORE_TXBUF_NUM; i++) {
            uint32_t prio;

            if (extract32(s->tx_status.u32, i * 4, 4) != TXT_RDY) {
                continue;
            }
            prio = (s->tx_priority.u32 >> (i * 4)) & 0x7;
            if (tx_prio_max < prio) {
                tx_prio_max = prio;
                buff2tx_idx = i;
            }
        }
        if (buff2tx_idx == -1) {
            break;
        }
        int_stat.u32 = 0;
        pf = s->tx_buffer[buff2tx_idx].data;
        ctucan_buff2frame(pf, &frame);
        s->status.s.idle = 0;
        s->status.s.txs = 1;
        can_bus_client_send(&s->bus_client, &frame, 1);
        s->status.s.idle = 1;
        s->status.s.txs = 0;
        s->tx_fr_ctr.s.tx_fr_ctr_val++;
        int_stat.s.txi = 1;
        int_stat.s.txbhci = 1;
        s->int_stat.u32 |= int_stat.u32 & ~s->int_mask.u32;
        s->tx_status.u32 = deposit32(s->tx_status.u32,
                                     buff2tx_idx * 4, 4, TXT_TOK);
    } while (1);
}

#define CTUCAN_CORE_TXBUFF_SPAN \
            (CTU_CAN_FD_TXTB2_DATA_1 - CTU_CAN_FD_TXTB1_DATA_1)

void ctucan_mem_write(CtuCanCoreState *s, hwaddr addr, uint64_t val,
                       unsigned size)
{
    int              i;

    DPRINTF("write 0x%02llx addr 0x%02x\n",
            (unsigned long long)val, (unsigned int)addr);

    if (addr >= CTUCAN_CORE_MEM_SIZE) {
        return;
    }

    if (addr >= CTU_CAN_FD_TXTB1_DATA_1) {
        int buff_num;
        addr -= CTU_CAN_FD_TXTB1_DATA_1;
        buff_num = addr / CTUCAN_CORE_TXBUFF_SPAN;
        addr %= CTUCAN_CORE_TXBUFF_SPAN;
        if ((buff_num < CTUCAN_CORE_TXBUF_NUM) &&
            ((addr + size) <= sizeof(s->tx_buffer[buff_num].data))) {
            stn_le_p(s->tx_buffer[buff_num].data + addr, size, val);
        }
    } else {
        switch (addr & ~3) {
        case CTU_CAN_FD_MODE:
            s->mode_settings.u32 = (uint32_t)val;
            if (s->mode_settings.s.rst) {
                ctucan_hardware_reset(s);
                s->mode_settings.s.rst = 0;
            }
            break;
        case CTU_CAN_FD_COMMAND:
        {
            union ctu_can_fd_command command;
            command.u32 = (uint32_t)val;
            if (command.s.cdo) {
                s->status.s.dor = 0;
            }
            if (command.s.rrb) {
                s->rx_tail_pos = 0;
                s->rx_cnt = 0;
                s->rx_frame_rem = 0;
                s->rx_status_rx_settings.s.rxfrc = 0;
            }
            if (command.s.txfcrst) {
                s->tx_fr_ctr.s.tx_fr_ctr_val = 0;
            }
            if (command.s.rxfcrst) {
                s->rx_fr_ctr.s.rx_fr_ctr_val = 0;
            }
            break;
        }
        case CTU_CAN_FD_INT_STAT:
            s->int_stat.u32 &= ~(uint32_t)val;
            break;
        case CTU_CAN_FD_INT_ENA_SET:
            s->int_ena.u32 |= (uint32_t)val;
            break;
        case CTU_CAN_FD_INT_ENA_CLR:
            s->int_ena.u32 &= ~(uint32_t)val;
            break;
        case CTU_CAN_FD_INT_MASK_SET:
            s->int_mask.u32 |= (uint32_t)val;
            break;
        case CTU_CAN_FD_INT_MASK_CLR:
            s->int_mask.u32 &= ~(uint32_t)val;
            break;
        case CTU_CAN_FD_TX_COMMAND:
            if (s->mode_settings.s.ena) {
                union ctu_can_fd_tx_command tx_command;
                union ctu_can_fd_tx_command mask;
                unsigned int buff_st;
                uint32_t buff_st_mask;

                tx_command.u32 = (uint32_t)val;
                mask.u32 = 0;
                mask.s.txb1 = 1;

                for (i = 0; i < CTUCAN_CORE_TXBUF_NUM; i++) {
                    if (!(tx_command.u32 & (mask.u32 << i))) {
                        continue;
                    }
                    buff_st_mask = 0xf << (i * 4);
                    buff_st = (s->tx_status.u32 >> (i * 4)) & 0xf;
                    if (tx_command.s.txca) {
                        if (buff_st == TXT_RDY) {
                            buff_st = TXT_ABT;
                        }
                    }
                    if (tx_command.s.txcr) {
                        if ((buff_st == TXT_TOK) || (buff_st == TXT_ERR) ||
                            (buff_st == TXT_ABT) || (buff_st == TXT_ETY))
                            buff_st = TXT_RDY;
                    }
                    if (tx_command.s.txce) {
                        if ((buff_st == TXT_TOK) || (buff_st == TXT_ERR) ||
                            (buff_st == TXT_ABT))
                            buff_st = TXT_ETY;
                    }
                    s->tx_status.u32 = (s->tx_status.u32 & ~buff_st_mask) |
                                        (buff_st << (i * 4));
                }

                ctucan_send_ready_buffers(s);
                ctucan_update_txnf(s);
            }
            break;
        case CTU_CAN_FD_TX_PRIORITY:
            s->tx_priority.u32 = (uint32_t)val;
            break;
        }

        ctucan_update_irq(s);
    }

    return;
}

uint64_t ctucan_mem_read(CtuCanCoreState *s, hwaddr addr, unsigned size)
{
    uint32_t val = 0;

    DPRINTF("read addr 0x%02x ...\n", (unsigned int)addr);

    if (addr > CTUCAN_CORE_MEM_SIZE) {
        return 0;
    }

    switch (addr & ~3) {
    case CTU_CAN_FD_DEVICE_ID:
        {
            union ctu_can_fd_device_id_version idver;
            idver.u32 = 0;
            idver.s.device_id = CTU_CAN_FD_ID;
            idver.s.ver_major = 2;
            idver.s.ver_minor = 2;
            val = idver.u32;
        }
        break;
    case CTU_CAN_FD_MODE:
        val = s->mode_settings.u32;
        break;
    case CTU_CAN_FD_STATUS:
        val = s->status.u32;
        break;
    case CTU_CAN_FD_INT_STAT:
        val = s->int_stat.u32;
        break;
    case CTU_CAN_FD_INT_ENA_SET:
    case CTU_CAN_FD_INT_ENA_CLR:
        val = s->int_ena.u32;
        break;
    case CTU_CAN_FD_INT_MASK_SET:
    case CTU_CAN_FD_INT_MASK_CLR:
        val = s->int_mask.u32;
        break;
    case CTU_CAN_FD_RX_MEM_INFO:
        s->rx_mem_info.u32 = 0;
        s->rx_mem_info.s.rx_buff_size = CTUCAN_RCV_BUF_LEN >> 2;
        s->rx_mem_info.s.rx_mem_free = (CTUCAN_RCV_BUF_LEN -
                                        s->rx_cnt) >> 2;
        val = s->rx_mem_info.u32;
        break;
    case CTU_CAN_FD_RX_POINTERS:
    {
        uint32_t rx_head_pos = s->rx_tail_pos + s->rx_cnt;
        rx_head_pos %= CTUCAN_RCV_BUF_LEN;
        s->rx_pointers.s.rx_wpp = rx_head_pos;
        s->rx_pointers.s.rx_rpp = s->rx_tail_pos;
        val = s->rx_pointers.u32;
        break;
    }
    case CTU_CAN_FD_RX_STATUS:
    case CTU_CAN_FD_RX_SETTINGS:
        if (!s->rx_status_rx_settings.s.rxfrc) {
            s->rx_status_rx_settings.s.rxe = 1;
        } else {
            s->rx_status_rx_settings.s.rxe = 0;
        }
        if (((s->rx_cnt + 3) & ~3) == CTUCAN_RCV_BUF_LEN) {
            s->rx_status_rx_settings.s.rxf = 1;
        } else {
            s->rx_status_rx_settings.s.rxf = 0;
        }
        val = s->rx_status_rx_settings.u32;
        break;
    case CTU_CAN_FD_RX_DATA:
        if (s->rx_cnt) {
            memcpy(&val, s->rx_buff + s->rx_tail_pos, 4);
            val = le32_to_cpu(val);
            if (!s->rx_frame_rem) {
                union ctu_can_fd_frame_form_w frame_form_w;
                frame_form_w.u32 = val;
                s->rx_frame_rem = frame_form_w.s.rwcnt * 4 + 4;
            }
            s->rx_cnt -= 4;
            s->rx_frame_rem -= 4;
            if (!s->rx_frame_rem) {
                s->rx_status_rx_settings.s.rxfrc--;
                if (!s->rx_status_rx_settings.s.rxfrc) {
                    s->status.s.rxne = 0;
                    s->status.s.idle = 1;
                    s->status.s.rxs = 0;
                }
            }
            s->rx_tail_pos = (s->rx_tail_pos + 4) % CTUCAN_RCV_BUF_LEN;
        } else {
            val = 0;
        }
        break;
    case CTU_CAN_FD_TX_STATUS:
        val = s->tx_status.u32;
        break;
    case CTU_CAN_FD_TX_PRIORITY:
        val = s->tx_priority.u32;
        break;
    case CTU_CAN_FD_RX_FR_CTR:
        val = s->rx_fr_ctr.s.rx_fr_ctr_val;
        break;
    case CTU_CAN_FD_TX_FR_CTR:
        val = s->tx_fr_ctr.s.tx_fr_ctr_val;
        break;
    case CTU_CAN_FD_YOLO_REG:
        val = s->yolo_reg.s.yolo_val;
        break;
    }

    val >>= ((addr & 3) << 3);
    if (size < 8) {
        val &= ((uint64_t)1 << (size << 3)) - 1;
    }

    return val;
}

bool ctucan_can_receive(CanBusClientState *client)
{
    CtuCanCoreState *s = container_of(client, CtuCanCoreState, bus_client);

    if (!s->mode_settings.s.ena) {
        return false;
    }

    return true; /* always return true, when operation mode */
}

ssize_t ctucan_receive(CanBusClientState *client, const qemu_can_frame *frames,
                        size_t frames_cnt)
{
    CtuCanCoreState *s = container_of(client, CtuCanCoreState, bus_client);
    static uint8_t rcv[CTUCAN_MSG_MAX_LEN];
    int i;
    int ret = -1;
    const qemu_can_frame *frame = frames;
    union ctu_can_fd_int_stat int_stat;
    int_stat.u32 = 0;

    if (frames_cnt <= 0) {
        return 0;
    }

    ret = ctucan_frame2buff(frame, rcv);

    if (s->rx_cnt + ret > CTUCAN_RCV_BUF_LEN) { /* Data overrun. */
        s->status.s.dor = 1;
        int_stat.s.doi = 1;
        s->int_stat.u32 |= int_stat.u32 & ~s->int_mask.u32;
        ctucan_update_irq(s);
        DPRINTF("Receive FIFO overrun\n");
        return ret;
    }
    s->status.s.idle = 0;
    s->status.s.rxs = 1;
    int_stat.s.rxi = 1;
    if (((s->rx_cnt + 3) & ~3) == CTUCAN_RCV_BUF_LEN) {
        int_stat.s.rxfi = 1;
    }
    s->int_stat.u32 |= int_stat.u32 & ~s->int_mask.u32;
    s->rx_fr_ctr.s.rx_fr_ctr_val++;
    s->rx_status_rx_settings.s.rxfrc++;
    for (i = 0; i < ret; i++) {
        s->rx_buff[(s->rx_tail_pos + s->rx_cnt) % CTUCAN_RCV_BUF_LEN] = rcv[i];
        s->rx_cnt++;
    }
    s->status.s.rxne = 1;

    ctucan_update_irq(s);

    return 1;
}

static CanBusClientInfo ctucan_bus_client_info = {
    .can_receive = ctucan_can_receive,
    .receive = ctucan_receive,
};


int ctucan_connect_to_bus(CtuCanCoreState *s, CanBusState *bus)
{
    s->bus_client.info = &ctucan_bus_client_info;

    if (!bus) {
        return -EINVAL;
    }

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }

    return 0;
}

void ctucan_disconnect(CtuCanCoreState *s)
{
    can_bus_remove_client(&s->bus_client);
}

int ctucan_init(CtuCanCoreState *s, qemu_irq irq)
{
    s->irq = irq;

    qemu_irq_lower(s->irq);

    ctucan_hardware_reset(s);

    return 0;
}

const VMStateDescription vmstate_qemu_ctucan_tx_buffer = {
    .name = "qemu_ctucan_tx_buffer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, CtuCanCoreMsgBuffer, CTUCAN_CORE_MSG_MAX_LEN),
        VMSTATE_END_OF_LIST()
    }
};

static int ctucan_post_load(void *opaque, int version_id)
{
    CtuCanCoreState *s = opaque;
    ctucan_update_irq(s);
    return 0;
}

/* VMState is needed for live migration of QEMU images */
const VMStateDescription vmstate_ctucan = {
    .name = "ctucan",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ctucan_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mode_settings.u32, CtuCanCoreState),
        VMSTATE_UINT32(status.u32, CtuCanCoreState),
        VMSTATE_UINT32(int_stat.u32, CtuCanCoreState),
        VMSTATE_UINT32(int_ena.u32, CtuCanCoreState),
        VMSTATE_UINT32(int_mask.u32, CtuCanCoreState),
        VMSTATE_UINT32(brt.u32, CtuCanCoreState),
        VMSTATE_UINT32(brt_fd.u32, CtuCanCoreState),
        VMSTATE_UINT32(ewl_erp_fault_state.u32, CtuCanCoreState),
        VMSTATE_UINT32(rec_tec.u32, CtuCanCoreState),
        VMSTATE_UINT32(err_norm_err_fd.u32, CtuCanCoreState),
        VMSTATE_UINT32(ctr_pres.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_a_mask.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_a_val.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_b_mask.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_b_val.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_c_mask.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_c_val.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_ran_low.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_ran_high.u32, CtuCanCoreState),
        VMSTATE_UINT32(filter_control_filter_status.u32, CtuCanCoreState),
        VMSTATE_UINT32(rx_mem_info.u32, CtuCanCoreState),
        VMSTATE_UINT32(rx_pointers.u32, CtuCanCoreState),
        VMSTATE_UINT32(rx_status_rx_settings.u32, CtuCanCoreState),
        VMSTATE_UINT32(tx_status.u32, CtuCanCoreState),
        VMSTATE_UINT32(tx_priority.u32, CtuCanCoreState),
        VMSTATE_UINT32(err_capt_alc.u32, CtuCanCoreState),
        VMSTATE_UINT32(trv_delay_ssp_cfg.u32, CtuCanCoreState),
        VMSTATE_UINT32(rx_fr_ctr.u32, CtuCanCoreState),
        VMSTATE_UINT32(tx_fr_ctr.u32, CtuCanCoreState),
        VMSTATE_UINT32(debug_register.u32, CtuCanCoreState),
        VMSTATE_UINT32(yolo_reg.u32, CtuCanCoreState),
        VMSTATE_UINT32(timestamp_low.u32, CtuCanCoreState),
        VMSTATE_UINT32(timestamp_high.u32, CtuCanCoreState),

        VMSTATE_STRUCT_ARRAY(tx_buffer, CtuCanCoreState,
                CTUCAN_CORE_TXBUF_NUM, 0, vmstate_qemu_ctucan_tx_buffer,
                CtuCanCoreMsgBuffer),

        VMSTATE_BUFFER(rx_buff, CtuCanCoreState),
        VMSTATE_UINT32(rx_tail_pos, CtuCanCoreState),
        VMSTATE_UINT32(rx_cnt, CtuCanCoreState),
        VMSTATE_UINT32(rx_frame_rem, CtuCanCoreState),

        VMSTATE_END_OF_LIST()
    }
};
