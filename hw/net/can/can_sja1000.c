/*
 * CAN device - SJA1000 chip emulation for QEMU
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
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
#include "qemu/bitops.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "can_sja1000.h"

#ifndef DEBUG_FILTER
#define DEBUG_FILTER 0
#endif /*DEBUG_FILTER*/

#ifndef DEBUG_CAN
#define DEBUG_CAN 0
#endif /*DEBUG_CAN*/

#define DPRINTF(fmt, ...) \
    do { \
        if (DEBUG_CAN) { \
            qemu_log("[cansja]: " fmt , ## __VA_ARGS__); \
        } \
    } while (0)

static void can_sja_software_reset(CanSJA1000State *s)
{
    s->mode        &= ~0x31;
    s->mode        |= 0x01;
    s->status_pel  &= ~0x37;
    s->status_pel  |= 0x34;

    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;
}

void can_sja_hardware_reset(CanSJA1000State *s)
{
    /* Reset by hardware, p10 */
    s->mode        = 0x01;
    s->status_pel  = 0x3c;
    s->interrupt_pel = 0x00;
    s->clock       = 0x00;
    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;

    s->control     = 0x01;
    s->status_bas  = 0x0c;
    s->interrupt_bas = 0x00;

    qemu_irq_lower(s->irq);
}

static
void can_sja_single_filter(struct qemu_can_filter *filter,
            const uint8_t *acr,  const uint8_t *amr, int extended)
{
    if (extended) {
        filter->can_id = (uint32_t)acr[0] << 21;
        filter->can_id |= (uint32_t)acr[1] << 13;
        filter->can_id |= (uint32_t)acr[2] << 5;
        filter->can_id |= (uint32_t)acr[3] >> 3;
        if (acr[3] & 4) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 21;
        filter->can_mask |= (uint32_t)amr[1] << 13;
        filter->can_mask |= (uint32_t)amr[2] << 5;
        filter->can_mask |= (uint32_t)amr[3] >> 3;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_EFF_MASK;
        if (!(amr[3] & 4)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    } else {
        filter->can_id = (uint32_t)acr[0] << 3;
        filter->can_id |= (uint32_t)acr[1] >> 5;
        if (acr[1] & 0x10) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 3;
        filter->can_mask |= (uint32_t)amr[1] >> 5;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_SFF_MASK;
        if (!(amr[1] & 0x10)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    }
}

static
void can_sja_dual_filter(struct qemu_can_filter *filter,
            const uint8_t *acr,  const uint8_t *amr, int extended)
{
    if (extended) {
        filter->can_id = (uint32_t)acr[0] << 21;
        filter->can_id |= (uint32_t)acr[1] << 13;

        filter->can_mask = (uint32_t)amr[0] << 21;
        filter->can_mask |= (uint32_t)amr[1] << 13;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_EFF_MASK & ~0x1fff;
    } else {
        filter->can_id = (uint32_t)acr[0] << 3;
        filter->can_id |= (uint32_t)acr[1] >> 5;
        if (acr[1] & 0x10) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 3;
        filter->can_mask |= (uint32_t)amr[1] >> 5;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_SFF_MASK;
        if (!(amr[1] & 0x10)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    }
}

/* Details in DS-p22, what we need to do here is to test the data. */
static
int can_sja_accept_filter(CanSJA1000State *s,
                                 const qemu_can_frame *frame)
{

    struct qemu_can_filter filter;

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & (1 << 3)) { /* Single mode. */
            if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
                can_sja_single_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 1);

                if (!can_bus_filter_match(&filter, frame->can_id)) {
                    return 0;
                }
            } else { /* SFF */
                can_sja_single_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 0);

                if (!can_bus_filter_match(&filter, frame->can_id)) {
                    return 0;
                }

                if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
                    return 1;
                }

                if (frame->can_dlc == 0) {
                    return 1;
                }

                if ((frame->data[0] & ~(s->code_mask[6])) !=
                   (s->code_mask[2] & ~(s->code_mask[6]))) {
                    return 0;
                }

                if (frame->can_dlc < 2) {
                    return 1;
                }

                if ((frame->data[1] & ~(s->code_mask[7])) ==
                    (s->code_mask[3] & ~(s->code_mask[7]))) {
                    return 1;
                }

                return 0;
            }
        } else { /* Dual mode */
            if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
                can_sja_dual_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 1);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                can_sja_dual_filter(&filter,
                    s->code_mask + 2, s->code_mask + 6, 1);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                return 0;
            } else {
                can_sja_dual_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 0);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    uint8_t expect;
                    uint8_t mask;
                    expect = s->code_mask[1] << 4;
                    expect |= s->code_mask[3] & 0x0f;

                    mask = s->code_mask[5] << 4;
                    mask |= s->code_mask[7] & 0x0f;
                        mask = ~mask & 0xff;

                    if ((frame->data[0] & mask) ==
                        (expect & mask)) {
                        return 1;
                    }
                }

                can_sja_dual_filter(&filter,
                    s->code_mask + 2, s->code_mask + 6, 0);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                return 0;
            }
        }
    }

    return 1;
}

static void can_display_msg(const char *prefix, const qemu_can_frame *msg)
{
    int i;
    FILE *logfile = qemu_log_trylock();

    if (logfile) {
        fprintf(logfile, "%s%03X [%01d] %s %s",
                prefix,
                msg->can_id & QEMU_CAN_EFF_MASK,
                msg->can_dlc,
                msg->can_id & QEMU_CAN_EFF_FLAG ? "EFF" : "SFF",
                msg->can_id & QEMU_CAN_RTR_FLAG ? "RTR" : "DAT");

        for (i = 0; i < msg->can_dlc; i++) {
            fprintf(logfile, " %02X", msg->data[i]);
        }
        fprintf(logfile, "\n");
        qemu_log_unlock(logfile);
    }
}

static void buff2frame_pel(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->flags = 0;
    frame->can_id = 0;
    if (buff[0] & 0x40) { /* RTR */
        frame->can_id = QEMU_CAN_RTR_FLAG;
    }
    frame->can_dlc = buff[0] & 0x0f;

    if (frame->can_dlc > 8) {
        frame->can_dlc = 8;
    }

    if (buff[0] & 0x80) { /* Extended */
        frame->can_id |= QEMU_CAN_EFF_FLAG;
        frame->can_id |= buff[1] << 21; /* ID.28~ID.21 */
        frame->can_id |= buff[2] << 13; /* ID.20~ID.13 */
        frame->can_id |= buff[3] <<  5;
        frame->can_id |= buff[4] >>  3;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[5 + i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    } else {
        frame->can_id |= buff[1] <<  3;
        frame->can_id |= buff[2] >>  5;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[3 + i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    }
}


static void buff2frame_bas(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->flags = 0;
    frame->can_id = ((buff[0] << 3) & (0xff << 3)) + ((buff[1] >> 5) & 0x07);
    if (buff[1] & 0x10) { /* RTR */
        frame->can_id = QEMU_CAN_RTR_FLAG;
    }
    frame->can_dlc = buff[1] & 0x0f;

    if (frame->can_dlc > 8) {
        frame->can_dlc = 8;
    }

    for (i = 0; i < frame->can_dlc; i++) {
        frame->data[i] = buff[2 + i];
    }
    for (; i < 8; i++) {
        frame->data[i] = 0;
    }
}


static int frame2buff_pel(const qemu_can_frame *frame, uint8_t *buff)
{
    int i;
    int dlen = frame->can_dlc;

    if (frame->can_id & QEMU_CAN_ERR_FLAG) { /* error frame, NOT support now. */
        return -1;
    }

    if (dlen > 8) {
        return -1;
    }

    buff[0] = 0x0f & frame->can_dlc; /* DLC */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
        buff[0] |= (1 << 6);
    }
    if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
        buff[0] |= (1 << 7);
        buff[1] = extract32(frame->can_id, 21, 8); /* ID.28~ID.21 */
        buff[2] = extract32(frame->can_id, 13, 8); /* ID.20~ID.13 */
        buff[3] = extract32(frame->can_id, 5, 8);  /* ID.12~ID.05 */
        buff[4] = extract32(frame->can_id, 0, 5) << 3; /* ID.04~ID.00,xxx */
        for (i = 0; i < dlen; i++) {
            buff[5 + i] = frame->data[i];
        }
        return dlen + 5;
    } else { /* SFF */
        buff[1] = extract32(frame->can_id, 3, 8); /* ID.10~ID.03 */
        buff[2] = extract32(frame->can_id, 0, 3) << 5; /* ID.02~ID.00,xxxxx */
        for (i = 0; i < dlen; i++) {
            buff[3 + i] = frame->data[i];
        }

        return dlen + 3;
    }

    return -1;
}

static int frame2buff_bas(const qemu_can_frame *frame, uint8_t *buff)
{
    int i;
    int dlen = frame->can_dlc;

     /*
      * EFF, no support for BasicMode
      * No use for Error frames now,
      * they could be used in future to update SJA1000 error state
      */
    if ((frame->can_id & QEMU_CAN_EFF_FLAG) ||
       (frame->can_id & QEMU_CAN_ERR_FLAG)) {
        return -1;
    }

    if (dlen > 8) {
        return -1;
    }

    buff[0] = extract32(frame->can_id, 3, 8); /* ID.10~ID.03 */
    buff[1] = extract32(frame->can_id, 0, 3) << 5; /* ID.02~ID.00,xxxxx */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
        buff[1] |= (1 << 4);
    }
    buff[1] |= frame->can_dlc & 0x0f;
    for (i = 0; i < dlen; i++) {
        buff[2 + i] = frame->data[i];
    }

    return dlen + 2;
}

static void can_sja_update_pel_irq(CanSJA1000State *s)
{
    if (s->interrupt_en & s->interrupt_pel) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void can_sja_update_bas_irq(CanSJA1000State *s)
{
    if ((s->control >> 1) & s->interrupt_bas) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

void can_sja_mem_write(CanSJA1000State *s, hwaddr addr, uint64_t val,
                       unsigned size)
{
    qemu_can_frame   frame;
    uint32_t         tmp;
    uint8_t          tmp8, count;


    DPRINTF("write 0x%02llx addr 0x%02x\n",
            (unsigned long long)val, (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register */
            s->mode = 0x1f & val;
            if ((s->mode & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                if (s->mode & (1 << 3)) { /* Single mode. */
                    /* For EFF */
                    can_sja_single_filter(&s->filter[0],
                        s->code_mask + 0, s->code_mask + 4, 1);

                    /* For SFF */
                    can_sja_single_filter(&s->filter[1],
                        s->code_mask + 0, s->code_mask + 4, 0);

                    can_bus_client_set_filters(&s->bus_client, s->filter, 2);
                } else { /* Dual mode */
                    /* For EFF */
                    can_sja_dual_filter(&s->filter[0],
                        s->code_mask + 0, s->code_mask + 4, 1);

                    can_sja_dual_filter(&s->filter[1],
                        s->code_mask + 2, s->code_mask + 6, 1);

                    /* For SFF */
                    can_sja_dual_filter(&s->filter[2],
                        s->code_mask + 0, s->code_mask + 4, 0);

                    can_sja_dual_filter(&s->filter[3],
                        s->code_mask + 2, s->code_mask + 6, 0);

                    can_bus_client_set_filters(&s->bus_client, s->filter, 4);
                }

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            }
            break;

        case SJA_CMR: /* Command register. */
            if (0x01 & val) { /* Send transmission request. */
                buff2frame_pel(s->tx_buff, &frame);
                if (DEBUG_FILTER) {
                    can_display_msg("[cansja]: Tx request " , &frame);
                }

                /*
                 * Clear transmission complete status,
                 * and Transmit Buffer Status.
                 * write to the backends.
                 */
                s->status_pel &= ~(3 << 2);

                can_bus_client_send(&s->bus_client, &frame, 1);

                /*
                 * Set transmission complete status
                 * and Transmit Buffer Status.
                 */
                s->status_pel |= (3 << 2);

                /* Clear transmit status. */
                s->status_pel &= ~(1 << 5);
                s->interrupt_pel |= 0x02;
                can_sja_update_pel_irq(s);
            }
            if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                tmp8 = s->rx_buff[s->rxbuf_start]; count = 0;
                if (tmp8 & (1 << 7)) { /* EFF */
                    count += 2;
                }
                count += 3;
                if (!(tmp8 & (1 << 6))) { /* DATA */
                    count += (tmp8 & 0x0f);
                }

                if (DEBUG_FILTER) {
                    qemu_log("[cansja]: message released from "
                             "Rx FIFO cnt=%d, count=%d\n", s->rx_cnt, count);
                }

                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;

                s->rx_cnt -= count;
                s->rxmsg_cnt--;
                if (s->rxmsg_cnt == 0) {
                    s->status_pel &= ~(1 << 0);
                    s->interrupt_pel &= ~(1 << 0);
                    can_sja_update_pel_irq(s);
                }
            }
            if (0x08 & val) { /* Clear data overrun */
                s->status_pel &= ~(1 << 1);
                s->interrupt_pel &= ~(1 << 3);
                can_sja_update_pel_irq(s);
            }
            break;
        case SJA_SR: /* Status register */
        case SJA_IR: /* Interrupt register */
            break; /* Do nothing */
        case SJA_IER: /* Interrupt enable register */
            s->interrupt_en = val;
            break;
        case 16: /* RX frame information addr16-28. */
            s->status_pel |= (1 << 5); /* Set transmit status. */
            /* fallthrough */
        case 17 ... 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    s->code_mask[addr - 16] = val;
                }
            } else { /* Operation mode */
                s->tx_buff[addr - 16] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            if ((s->control & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                s->filter[0].can_id = (s->code << 3) & (0xff << 3);
                tmp = (~(s->mask << 3)) & (0xff << 3);
                tmp |= QEMU_CAN_EFF_FLAG; /* Only Basic CAN Frame. */
                s->filter[0].can_mask = tmp;
                can_bus_client_set_filters(&s->bus_client, s->filter, 1);

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            } else if (!(s->control & 0x01) && !(val & 0x01)) {
                can_sja_software_reset(s);
            }

            s->control = 0x1f & val;
            break;
        case SJA_BCAN_CMR: /* Command register, addr 1 */
            if (0x01 & val) { /* Send transmission request. */
                buff2frame_bas(s->tx_buff, &frame);
                if (DEBUG_FILTER) {
                    can_display_msg("[cansja]: Tx request " , &frame);
                }

                /*
                 * Clear transmission complete status,
                 * and Transmit Buffer Status.
                 */
                s->status_bas &= ~(3 << 2);

                /* write to the backends. */
                can_bus_client_send(&s->bus_client, &frame, 1);

                /*
                 * Set transmission complete status,
                 * and Transmit Buffer Status.
                 */
                s->status_bas |= (3 << 2);

                /* Clear transmit status. */
                s->status_bas &= ~(1 << 5);
                s->interrupt_bas |= 0x02;
                can_sja_update_bas_irq(s);
            }
            if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                tmp8 = s->rx_buff[(s->rxbuf_start + 1) % SJA_RCV_BUF_LEN];
                count = 2 + (tmp8 & 0x0f);

                if (DEBUG_FILTER) {
                    qemu_log("[cansja]: message released from "
                             "Rx FIFO cnt=%d, count=%d\n", s->rx_cnt, count);
                }

                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;
                s->rx_cnt -= count;
                s->rxmsg_cnt--;

                if (s->rxmsg_cnt == 0) {
                    s->status_bas &= ~(1 << 0);
                    s->interrupt_bas &= ~(1 << 0);
                    can_sja_update_bas_irq(s);
                }
            }
            if (0x08 & val) { /* Clear data overrun */
                s->status_bas &= ~(1 << 1);
                s->interrupt_bas &= ~(1 << 3);
                can_sja_update_bas_irq(s);
            }
            break;
        case 4:
            s->code = val;
            break;
        case 5:
            s->mask = val;
            break;
        case 10:
            s->status_bas |= (1 << 5); /* Set transmit status. */
            /* fallthrough */
        case 11 ... 19:
            if ((s->control & 0x01) == 0) { /* Operation mode */
                s->tx_buff[addr - 10] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    }
}

uint64_t can_sja_mem_read(CanSJA1000State *s, hwaddr addr, unsigned size)
{
    uint64_t temp = 0;

    DPRINTF("read addr 0x%02x ...\n", (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return 0;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register, addr 0 */
            temp = s->mode;
            break;
        case SJA_CMR: /* Command register, addr 1 */
            temp = 0x00; /* Command register, cannot be read. */
            break;
        case SJA_SR: /* Status register, addr 2 */
            temp = s->status_pel;
            break;
        case SJA_IR: /* Interrupt register, addr 3 */
            temp = s->interrupt_pel;
            s->interrupt_pel = 0;
            if (s->rxmsg_cnt) {
                s->interrupt_pel |= (1 << 0); /* Receive interrupt. */
            }
            can_sja_update_pel_irq(s);
            break;
        case SJA_IER: /* Interrupt enable register, addr 4 */
            temp = s->interrupt_en;
            break;
        case 5: /* Reserved */
        case 6: /* Bus timing 0, hardware related, not support now. */
        case 7: /* Bus timing 1, hardware related, not support now. */
        case 8: /*
                 * Output control register, hardware related,
                 * not supported for now.
                 */
        case 9: /* Test. */
        case 10 ... 15: /* Reserved */
            temp = 0x00;
            break;

        case 16 ... 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    temp = s->code_mask[addr - 16];
                } else {
                    temp = 0x00;
                }
            } else { /* Operation mode */
                temp = s->rx_buff[(s->rxbuf_start + addr - 16) %
                       SJA_RCV_BUF_LEN];
            }
            break;
        case SJA_CDR:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            temp = s->control;
            break;
        case SJA_BCAN_SR: /* Status register, addr 2 */
            temp = s->status_bas;
            break;
        case SJA_BCAN_IR: /* Interrupt register, addr 3 */
            temp = s->interrupt_bas;
            s->interrupt_bas = 0;
            if (s->rxmsg_cnt) {
                s->interrupt_bas |= (1 << 0); /* Receive interrupt. */
            }
            can_sja_update_bas_irq(s);
            break;
        case 4:
            temp = s->code;
            break;
        case 5:
            temp = s->mask;
            break;
        case 20 ... 29:
            temp = s->rx_buff[(s->rxbuf_start + addr - 20) % SJA_RCV_BUF_LEN];
            break;
        case 31:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
            break;
        }
    }
    DPRINTF("read addr 0x%02x, %d bytes, content 0x%02lx\n",
            (int)addr, size, (long unsigned int)temp);

    return temp;
}

bool can_sja_can_receive(CanBusClientState *client)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & 0x01) { /* reset mode. */
            return false;
        }
    } else { /* BasicCAN mode */
        if (s->control & 0x01) {
            return false;
        }
    }

    return true; /* always return true, when operation mode */
}

ssize_t can_sja_receive(CanBusClientState *client, const qemu_can_frame *frames,
                        size_t frames_cnt)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);
    static uint8_t rcv[SJA_MSG_MAX_LEN];
    int i;
    int ret = -1;
    const qemu_can_frame *frame = frames;

    if (frames_cnt <= 0) {
        return 0;
    }
    if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {
        if (DEBUG_FILTER) {
            can_display_msg("[cansja]: ignor fd frame ", frame);
        }
        return 1;
    }

    if (DEBUG_FILTER) {
        can_display_msg("[cansja]: receive ", frame);
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */

        /* the CAN controller is receiving a message */
        s->status_pel |= (1 << 4);

        if (can_sja_accept_filter(s, frame) == 0) {
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                qemu_log("[cansja]: filter rejects message\n");
            }
            return ret;
        }

        ret = frame2buff_pel(frame, rcv);
        if (ret < 0) {
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                qemu_log("[cansja]: message store failed\n");
            }
            return ret; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->status_pel |= (1 << 1); /* Overrun status */
            s->interrupt_pel |= (1 << 3);
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                qemu_log("[cansja]: receive FIFO overrun\n");
            }
            can_sja_update_pel_irq(s);
            return ret;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;
        if (DEBUG_FILTER) {
            qemu_log("[cansja]: message stored in receive FIFO\n");
        }

        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->status_pel |= 0x01; /* Set the Receive Buffer Status. DS-p23 */
        s->interrupt_pel |= 0x01;
        s->status_pel &= ~(1 << 4);
        can_sja_update_pel_irq(s);
    } else { /* BasicCAN mode */

        /* the CAN controller is receiving a message */
        s->status_bas |= (1 << 4);

        ret = frame2buff_bas(frame, rcv);
        if (ret < 0) {
            s->status_bas &= ~(1 << 4);
            if (DEBUG_FILTER) {
                qemu_log("[cansja]: message store failed\n");
            }
            return ret; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->status_bas |= (1 << 1); /* Overrun status */
            s->status_bas &= ~(1 << 4);
            s->interrupt_bas |= (1 << 3);
            can_sja_update_bas_irq(s);
            if (DEBUG_FILTER) {
                qemu_log("[cansja]: receive FIFO overrun\n");
            }
            return ret;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;

        if (DEBUG_FILTER) {
            qemu_log("[cansja]: message stored\n");
        }

        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->status_bas |= 0x01; /* Set the Receive Buffer Status. DS-p15 */
        s->status_bas &= ~(1 << 4);
        s->interrupt_bas |= (1 << 0);
        can_sja_update_bas_irq(s);
    }
    return 1;
}

static CanBusClientInfo can_sja_bus_client_info = {
    .can_receive = can_sja_can_receive,
    .receive = can_sja_receive,
};


int can_sja_connect_to_bus(CanSJA1000State *s, CanBusState *bus)
{
    s->bus_client.info = &can_sja_bus_client_info;

    if (!bus) {
        return -EINVAL;
    }

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }

    return 0;
}

void can_sja_disconnect(CanSJA1000State *s)
{
    can_bus_remove_client(&s->bus_client);
}

int can_sja_init(CanSJA1000State *s, qemu_irq irq)
{
    s->irq = irq;

    qemu_irq_lower(s->irq);

    can_sja_hardware_reset(s);

    return 0;
}

const VMStateDescription vmstate_qemu_can_filter = {
    .name = "qemu_can_filter",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(can_id, qemu_can_filter),
        VMSTATE_UINT32(can_mask, qemu_can_filter),
        VMSTATE_END_OF_LIST()
    }
};

static int can_sja_post_load(void *opaque, int version_id)
{
    CanSJA1000State *s = opaque;
    if (s->clock & 0x80) { /* PeliCAN Mode */
        can_sja_update_pel_irq(s);
    } else {
        can_sja_update_bas_irq(s);
    }
    return 0;
}

/* VMState is needed for live migration of QEMU images */
const VMStateDescription vmstate_can_sja = {
    .name = "can_sja",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = can_sja_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(mode, CanSJA1000State),

        VMSTATE_UINT8(status_pel, CanSJA1000State),
        VMSTATE_UINT8(interrupt_pel, CanSJA1000State),
        VMSTATE_UINT8(interrupt_en, CanSJA1000State),
        VMSTATE_UINT8(rxmsg_cnt, CanSJA1000State),
        VMSTATE_UINT8(rxbuf_start, CanSJA1000State),
        VMSTATE_UINT8(clock, CanSJA1000State),

        VMSTATE_BUFFER(code_mask, CanSJA1000State),
        VMSTATE_BUFFER(tx_buff, CanSJA1000State),

        VMSTATE_BUFFER(rx_buff, CanSJA1000State),

        VMSTATE_UINT32(rx_ptr, CanSJA1000State),
        VMSTATE_UINT32(rx_cnt, CanSJA1000State),

        VMSTATE_UINT8(control, CanSJA1000State),

        VMSTATE_UINT8(status_bas, CanSJA1000State),
        VMSTATE_UINT8(interrupt_bas, CanSJA1000State),
        VMSTATE_UINT8(code, CanSJA1000State),
        VMSTATE_UINT8(mask, CanSJA1000State),

        VMSTATE_STRUCT_ARRAY(filter, CanSJA1000State, 4, 0,
                             vmstate_qemu_can_filter, qemu_can_filter),


        VMSTATE_END_OF_LIST()
    }
};
