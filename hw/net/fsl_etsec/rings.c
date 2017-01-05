/*
 * QEMU Freescale eTSEC Emulator
 *
 * Copyright (c) 2011-2013 AdaCore
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
#include "net/checksum.h"
#include "qemu/log.h"
#include "etsec.h"
#include "registers.h"

/* #define ETSEC_RING_DEBUG */
/* #define HEX_DUMP */
/* #define DEBUG_BD */

#ifdef ETSEC_RING_DEBUG
static const int debug_etsec = 1;
#else
static const int debug_etsec;
#endif

#define RING_DEBUG(fmt, ...) do {              \
 if (debug_etsec) {                            \
        qemu_log(fmt , ## __VA_ARGS__);        \
    }                                          \
    } while (0)

#ifdef DEBUG_BD

static void print_tx_bd_flags(uint16_t flags)
{
    qemu_log("      Ready: %d\n", !!(flags & BD_TX_READY));
    qemu_log("      PAD/CRC: %d\n", !!(flags & BD_TX_PADCRC));
    qemu_log("      Wrap: %d\n", !!(flags & BD_WRAP));
    qemu_log("      Interrupt: %d\n", !!(flags & BD_INTERRUPT));
    qemu_log("      Last in frame: %d\n", !!(flags & BD_LAST));
    qemu_log("      Tx CRC: %d\n", !!(flags & BD_TX_TC));
    qemu_log("      User-defined preamble / defer: %d\n",
           !!(flags & BD_TX_PREDEF));
    qemu_log("      Huge frame enable / Late collision: %d\n",
           !!(flags & BD_TX_HFELC));
    qemu_log("      Control frame / Retransmission Limit: %d\n",
           !!(flags & BD_TX_CFRL));
    qemu_log("      Retry count: %d\n",
           (flags >> BD_TX_RC_OFFSET) & BD_TX_RC_MASK);
    qemu_log("      Underrun / TCP/IP off-load enable: %d\n",
           !!(flags & BD_TX_TOEUN));
    qemu_log("      Truncation: %d\n", !!(flags & BD_TX_TR));
}

static void print_rx_bd_flags(uint16_t flags)
{
    qemu_log("      Empty: %d\n", !!(flags & BD_RX_EMPTY));
    qemu_log("      Receive software ownership: %d\n", !!(flags & BD_RX_RO1));
    qemu_log("      Wrap: %d\n", !!(flags & BD_WRAP));
    qemu_log("      Interrupt: %d\n", !!(flags & BD_INTERRUPT));
    qemu_log("      Last in frame: %d\n", !!(flags & BD_LAST));
    qemu_log("      First in frame: %d\n", !!(flags & BD_RX_FIRST));
    qemu_log("      Miss: %d\n", !!(flags & BD_RX_MISS));
    qemu_log("      Broadcast: %d\n", !!(flags & BD_RX_BROADCAST));
    qemu_log("      Multicast: %d\n", !!(flags & BD_RX_MULTICAST));
    qemu_log("      Rx frame length violation: %d\n", !!(flags & BD_RX_LG));
    qemu_log("      Rx non-octet aligned frame: %d\n", !!(flags & BD_RX_NO));
    qemu_log("      Short frame: %d\n", !!(flags & BD_RX_SH));
    qemu_log("      Rx CRC Error: %d\n", !!(flags & BD_RX_CR));
    qemu_log("      Overrun: %d\n", !!(flags & BD_RX_OV));
    qemu_log("      Truncation: %d\n", !!(flags & BD_RX_TR));
}


static void print_bd(eTSEC_rxtx_bd bd, int mode, uint32_t index)
{
    qemu_log("eTSEC %s Data Buffer Descriptor (%u)\n",
           mode == eTSEC_TRANSMIT ? "Transmit" : "Receive",
           index);
    qemu_log("   Flags   : 0x%04x\n", bd.flags);
    if (mode == eTSEC_TRANSMIT) {
        print_tx_bd_flags(bd.flags);
    } else {
        print_rx_bd_flags(bd.flags);
    }
    qemu_log("   Length  : 0x%04x\n", bd.length);
    qemu_log("   Pointer : 0x%08x\n", bd.bufptr);
}

#endif  /* DEBUG_BD */

static void read_buffer_descriptor(eTSEC         *etsec,
                                   hwaddr         addr,
                                   eTSEC_rxtx_bd *bd)
{
    assert(bd != NULL);

    RING_DEBUG("READ Buffer Descriptor @ 0x" TARGET_FMT_plx"\n", addr);
    cpu_physical_memory_read(addr,
                             bd,
                             sizeof(eTSEC_rxtx_bd));

    if (etsec->regs[DMACTRL].value & DMACTRL_LE) {
        bd->flags  = lduw_le_p(&bd->flags);
        bd->length = lduw_le_p(&bd->length);
        bd->bufptr = ldl_le_p(&bd->bufptr);
    } else {
        bd->flags  = lduw_be_p(&bd->flags);
        bd->length = lduw_be_p(&bd->length);
        bd->bufptr = ldl_be_p(&bd->bufptr);
    }
}

static void write_buffer_descriptor(eTSEC         *etsec,
                                    hwaddr         addr,
                                    eTSEC_rxtx_bd *bd)
{
    assert(bd != NULL);

    if (etsec->regs[DMACTRL].value & DMACTRL_LE) {
        stw_le_p(&bd->flags, bd->flags);
        stw_le_p(&bd->length, bd->length);
        stl_le_p(&bd->bufptr, bd->bufptr);
    } else {
        stw_be_p(&bd->flags, bd->flags);
        stw_be_p(&bd->length, bd->length);
        stl_be_p(&bd->bufptr, bd->bufptr);
    }

    RING_DEBUG("Write Buffer Descriptor @ 0x" TARGET_FMT_plx"\n", addr);
    cpu_physical_memory_write(addr,
                              bd,
                              sizeof(eTSEC_rxtx_bd));
}

static void ievent_set(eTSEC    *etsec,
                       uint32_t  flags)
{
    etsec->regs[IEVENT].value |= flags;

    if ((flags & IEVENT_TXB && etsec->regs[IMASK].value & IMASK_TXBEN)
        || (flags & IEVENT_TXF && etsec->regs[IMASK].value & IMASK_TXFEN)) {
        qemu_irq_raise(etsec->tx_irq);
        RING_DEBUG("%s Raise Tx IRQ\n", __func__);
    }

    if ((flags & IEVENT_RXB && etsec->regs[IMASK].value & IMASK_RXBEN)
        || (flags & IEVENT_RXF && etsec->regs[IMASK].value & IMASK_RXFEN)) {
        qemu_irq_raise(etsec->rx_irq);
        RING_DEBUG("%s Raise Rx IRQ\n", __func__);
    }
}

static void tx_padding_and_crc(eTSEC *etsec, uint32_t min_frame_len)
{
    int add = min_frame_len - etsec->tx_buffer_len;

    /* Padding */
    if (add > 0) {
        RING_DEBUG("pad:%u\n", add);
        etsec->tx_buffer = g_realloc(etsec->tx_buffer,
                                        etsec->tx_buffer_len + add);

        memset(etsec->tx_buffer + etsec->tx_buffer_len, 0x0, add);
        etsec->tx_buffer_len += add;
    }

    /* Never add CRC in QEMU */
}

static void process_tx_fcb(eTSEC *etsec)
{
    uint8_t flags = (uint8_t)(*etsec->tx_buffer);
    /* L3 header offset from start of frame */
    uint8_t l3_header_offset = (uint8_t)*(etsec->tx_buffer + 3);
    /* L4 header offset from start of L3 header */
    uint8_t l4_header_offset = (uint8_t)*(etsec->tx_buffer + 2);
    /* L3 header */
    uint8_t *l3_header = etsec->tx_buffer + 8 + l3_header_offset;
    /* L4 header */
    uint8_t *l4_header = l3_header + l4_header_offset;

    /* if packet is IP4 and IP checksum is requested */
    if (flags & FCB_TX_IP && flags & FCB_TX_CIP) {
        /* do IP4 checksum (TODO This function does TCP/UDP checksum
         * but not sure if it also does IP4 checksum.) */
        net_checksum_calculate(etsec->tx_buffer + 8,
                etsec->tx_buffer_len - 8);
    }
    /* TODO Check the correct usage of the PHCS field of the FCB in case the NPH
     * flag is on */

    /* if packet is IP4 and TCP or UDP */
    if (flags & FCB_TX_IP && flags & FCB_TX_TUP) {
        /* if UDP */
        if (flags & FCB_TX_UDP) {
            /* if checksum is requested */
            if (flags & FCB_TX_CTU) {
                /* do UDP checksum */

                net_checksum_calculate(etsec->tx_buffer + 8,
                        etsec->tx_buffer_len - 8);
            } else {
                /* set checksum field to 0 */
                l4_header[6] = 0;
                l4_header[7] = 0;
            }
        } else if (flags & FCB_TX_CTU) { /* if TCP and checksum is requested */
            /* do TCP checksum */
            net_checksum_calculate(etsec->tx_buffer + 8,
                                   etsec->tx_buffer_len - 8);
        }
    }
}

static void process_tx_bd(eTSEC         *etsec,
                          eTSEC_rxtx_bd *bd)
{
    uint8_t *tmp_buff = NULL;
    hwaddr tbdbth     = (hwaddr)(etsec->regs[TBDBPH].value & 0xF) << 32;

    if (bd->length == 0) {
        /* ERROR */
        return;
    }

    if (etsec->tx_buffer_len == 0) {
        /* It's the first BD */
        etsec->first_bd = *bd;
    }

    /* TODO: if TxBD[TOE/UN] skip the Tx Frame Control Block*/

    /* Load this Data Buffer */
    etsec->tx_buffer = g_realloc(etsec->tx_buffer,
                                    etsec->tx_buffer_len + bd->length);
    tmp_buff = etsec->tx_buffer + etsec->tx_buffer_len;
    cpu_physical_memory_read(bd->bufptr + tbdbth, tmp_buff, bd->length);

    /* Update buffer length */
    etsec->tx_buffer_len += bd->length;


    if (etsec->tx_buffer_len != 0 && (bd->flags & BD_LAST)) {
        if (etsec->regs[MACCFG1].value & MACCFG1_TX_EN) {
            /* MAC Transmit enabled */

            /* Process offload Tx FCB */
            if (etsec->first_bd.flags & BD_TX_TOEUN) {
                process_tx_fcb(etsec);
            }

            if (etsec->first_bd.flags & BD_TX_PADCRC
                || etsec->regs[MACCFG2].value & MACCFG2_PADCRC) {

                /* Padding and CRC (Padding implies CRC) */
                tx_padding_and_crc(etsec, 64);

            } else if (etsec->first_bd.flags & BD_TX_TC
                       || etsec->regs[MACCFG2].value & MACCFG2_CRC_EN) {

                /* Only CRC */
                /* Never add CRC in QEMU */
            }

#if defined(HEX_DUMP)
            qemu_log("eTSEC Send packet size:%d\n", etsec->tx_buffer_len);
            qemu_hexdump(etsec->tx_buffer, stderr, "", etsec->tx_buffer_len);
#endif  /* ETSEC_RING_DEBUG */

            if (etsec->first_bd.flags & BD_TX_TOEUN) {
                qemu_send_packet(qemu_get_queue(etsec->nic),
                        etsec->tx_buffer + 8,
                        etsec->tx_buffer_len - 8);
            } else {
                qemu_send_packet(qemu_get_queue(etsec->nic),
                        etsec->tx_buffer,
                        etsec->tx_buffer_len);
            }

        }

        etsec->tx_buffer_len = 0;

        if (bd->flags & BD_INTERRUPT) {
            ievent_set(etsec, IEVENT_TXF);
        }
    } else {
        if (bd->flags & BD_INTERRUPT) {
            ievent_set(etsec, IEVENT_TXB);
        }
    }

    /* Update DB flags */

    /* Clear Ready */
    bd->flags &= ~BD_TX_READY;

    /* Clear Defer */
    bd->flags &= ~BD_TX_PREDEF;

    /* Clear Late Collision */
    bd->flags &= ~BD_TX_HFELC;

    /* Clear Retransmission Limit */
    bd->flags &= ~BD_TX_CFRL;

    /* Clear Retry Count */
    bd->flags &= ~(BD_TX_RC_MASK << BD_TX_RC_OFFSET);

    /* Clear Underrun */
    bd->flags &= ~BD_TX_TOEUN;

    /* Clear Truncation */
    bd->flags &= ~BD_TX_TR;
}

void etsec_walk_tx_ring(eTSEC *etsec, int ring_nbr)
{
    hwaddr        ring_base = 0;
    hwaddr        bd_addr   = 0;
    eTSEC_rxtx_bd bd;
    uint16_t      bd_flags;

    if (!(etsec->regs[MACCFG1].value & MACCFG1_TX_EN)) {
        RING_DEBUG("%s: MAC Transmit not enabled\n", __func__);
        return;
    }

    ring_base = (hwaddr)(etsec->regs[TBASEH].value & 0xF) << 32;
    ring_base += etsec->regs[TBASE0 + ring_nbr].value & ~0x7;
    bd_addr    = etsec->regs[TBPTR0 + ring_nbr].value & ~0x7;

    do {
        read_buffer_descriptor(etsec, bd_addr, &bd);

#ifdef DEBUG_BD
        print_bd(bd,
                 eTSEC_TRANSMIT,
                 (bd_addr - ring_base) / sizeof(eTSEC_rxtx_bd));

#endif  /* DEBUG_BD */

        /* Save flags before BD update */
        bd_flags = bd.flags;

        if (!(bd_flags & BD_TX_READY)) {
            break;
        }

        process_tx_bd(etsec, &bd);
        /* Write back BD after update */
        write_buffer_descriptor(etsec, bd_addr, &bd);

        /* Wrap or next BD */
        if (bd_flags & BD_WRAP) {
            bd_addr = ring_base;
        } else {
            bd_addr += sizeof(eTSEC_rxtx_bd);
        }
    } while (TRUE);

    /* Save the Buffer Descriptor Pointers to last bd that was not
     * succesfully closed */
    etsec->regs[TBPTR0 + ring_nbr].value = bd_addr;

    /* Set transmit halt THLTx */
    etsec->regs[TSTAT].value |= 1 << (31 - ring_nbr);
}

static void fill_rx_bd(eTSEC          *etsec,
                       eTSEC_rxtx_bd  *bd,
                       const uint8_t **buf,
                       size_t         *size)
{
    uint16_t to_write;
    hwaddr   bufptr = bd->bufptr +
        ((hwaddr)(etsec->regs[TBDBPH].value & 0xF) << 32);
    uint8_t  padd[etsec->rx_padding];
    uint8_t  rem;

    RING_DEBUG("eTSEC fill Rx buffer @ 0x%016" HWADDR_PRIx
               " size:%zu(padding + crc:%u) + fcb:%u\n",
               bufptr, *size, etsec->rx_padding, etsec->rx_fcb_size);

    bd->length = 0;

    /* This operation will only write FCB */
    if (etsec->rx_fcb_size != 0) {

        cpu_physical_memory_write(bufptr, etsec->rx_fcb, etsec->rx_fcb_size);

        bufptr             += etsec->rx_fcb_size;
        bd->length         += etsec->rx_fcb_size;
        etsec->rx_fcb_size  = 0;

    }

    /* We remove padding from the computation of to_write because it is not
     * allocated in the buffer.
     */
    to_write = MIN(*size - etsec->rx_padding,
                   etsec->regs[MRBLR].value - etsec->rx_fcb_size);

    /* This operation can only write packet data and no padding */
    if (to_write > 0) {
        cpu_physical_memory_write(bufptr, *buf, to_write);

        *buf   += to_write;
        bufptr += to_write;
        *size  -= to_write;

        bd->flags  &= ~BD_RX_EMPTY;
        bd->length += to_write;
    }

    if (*size == etsec->rx_padding) {
        /* The remaining bytes are only for padding which is not actually
         * allocated in the data buffer.
         */

        rem = MIN(etsec->regs[MRBLR].value - bd->length, etsec->rx_padding);

        if (rem > 0) {
            memset(padd, 0x0, sizeof(padd));
            etsec->rx_padding -= rem;
            *size             -= rem;
            bd->length        += rem;
            cpu_physical_memory_write(bufptr, padd, rem);
        }
    }
}

static void rx_init_frame(eTSEC *etsec, const uint8_t *buf, size_t size)
{
    uint32_t fcb_size = 0;
    uint8_t  prsdep   = (etsec->regs[RCTRL].value >> RCTRL_PRSDEP_OFFSET)
        & RCTRL_PRSDEP_MASK;

    if (prsdep != 0) {
        /* Prepend FCB (FCB size + RCTRL[PAL]) */
        fcb_size = 8 + ((etsec->regs[RCTRL].value >> 16) & 0x1F);

        etsec->rx_fcb_size = fcb_size;

        /* TODO: fill_FCB(etsec); */
        memset(etsec->rx_fcb, 0x0, sizeof(etsec->rx_fcb));

    } else {
        etsec->rx_fcb_size = 0;
    }

    g_free(etsec->rx_buffer);

    /* Do not copy the frame for now */
    etsec->rx_buffer     = (uint8_t *)buf;
    etsec->rx_buffer_len = size;

    /* CRC padding (We don't have to compute the CRC) */
    etsec->rx_padding = 4;

    /*
     * Ensure that payload length + CRC length is at least 802.3
     * minimum MTU size bytes long (64)
     */
    if (etsec->rx_buffer_len < 60) {
        etsec->rx_padding += 60 - etsec->rx_buffer_len;
    }

    etsec->rx_first_in_frame = 1;
    etsec->rx_remaining_data = etsec->rx_buffer_len;
    RING_DEBUG("%s: rx_buffer_len:%u rx_padding+crc:%u\n", __func__,
               etsec->rx_buffer_len, etsec->rx_padding);
}

ssize_t etsec_rx_ring_write(eTSEC *etsec, const uint8_t *buf, size_t size)
{
    int ring_nbr = 0;           /* Always use ring0 (no filer) */

    if (etsec->rx_buffer_len != 0) {
        RING_DEBUG("%s: We can't receive now,"
                   " a buffer is already in the pipe\n", __func__);
        return 0;
    }

    if (etsec->regs[RSTAT].value & 1 << (23 - ring_nbr)) {
        RING_DEBUG("%s: The ring is halted\n", __func__);
        return -1;
    }

    if (etsec->regs[DMACTRL].value & DMACTRL_GRS) {
        RING_DEBUG("%s: Graceful receive stop\n", __func__);
        return -1;
    }

    if (!(etsec->regs[MACCFG1].value & MACCFG1_RX_EN)) {
        RING_DEBUG("%s: MAC Receive not enabled\n", __func__);
        return -1;
    }

    if ((etsec->regs[RCTRL].value & RCTRL_RSF) && (size < 60)) {
        /* CRC is not in the packet yet, so short frame is below 60 bytes */
        RING_DEBUG("%s: Drop short frame\n", __func__);
        return -1;
    }

    rx_init_frame(etsec, buf, size);

    etsec_walk_rx_ring(etsec, ring_nbr);

    return size;
}

void etsec_walk_rx_ring(eTSEC *etsec, int ring_nbr)
{
    hwaddr         ring_base     = 0;
    hwaddr         bd_addr       = 0;
    hwaddr         start_bd_addr = 0;
    eTSEC_rxtx_bd  bd;
    uint16_t       bd_flags;
    size_t         remaining_data;
    const uint8_t *buf;
    uint8_t       *tmp_buf;
    size_t         size;

    if (etsec->rx_buffer_len == 0) {
        /* No frame to send */
        RING_DEBUG("No frame to send\n");
        return;
    }

    remaining_data = etsec->rx_remaining_data + etsec->rx_padding;
    buf            = etsec->rx_buffer
        + (etsec->rx_buffer_len - etsec->rx_remaining_data);
    size           = etsec->rx_buffer_len + etsec->rx_padding;

    ring_base = (hwaddr)(etsec->regs[RBASEH].value & 0xF) << 32;
    ring_base += etsec->regs[RBASE0 + ring_nbr].value & ~0x7;
    start_bd_addr  = bd_addr = etsec->regs[RBPTR0 + ring_nbr].value & ~0x7;

    do {
        read_buffer_descriptor(etsec, bd_addr, &bd);

#ifdef DEBUG_BD
        print_bd(bd,
                 eTSEC_RECEIVE,
                 (bd_addr - ring_base) / sizeof(eTSEC_rxtx_bd));

#endif  /* DEBUG_BD */

        /* Save flags before BD update */
        bd_flags = bd.flags;

        if (bd_flags & BD_RX_EMPTY) {
            fill_rx_bd(etsec, &bd, &buf, &remaining_data);

            if (etsec->rx_first_in_frame) {
                bd.flags |= BD_RX_FIRST;
                etsec->rx_first_in_frame = 0;
                etsec->rx_first_bd = bd;
            }

            /* Last in frame */
            if (remaining_data == 0) {

                /* Clear flags */

                bd.flags &= ~0x7ff;

                bd.flags |= BD_LAST;

                /* NOTE: non-octet aligned frame is impossible in qemu */

                if (size >= etsec->regs[MAXFRM].value) {
                    /* frame length violation */
                    qemu_log("%s frame length violation: size:%zu MAXFRM:%d\n",
                           __func__, size, etsec->regs[MAXFRM].value);

                    bd.flags |= BD_RX_LG;
                }

                if (size  < 64) {
                    /* Short frame */
                    bd.flags |= BD_RX_SH;
                }

                /* TODO: Broadcast and Multicast */

                if (bd.flags & BD_INTERRUPT) {
                    /* Set RXFx */
                    etsec->regs[RSTAT].value |= 1 << (7 - ring_nbr);

                    /* Set IEVENT */
                    ievent_set(etsec, IEVENT_RXF);
                }

            } else {
                if (bd.flags & BD_INTERRUPT) {
                    /* Set IEVENT */
                    ievent_set(etsec, IEVENT_RXB);
                }
            }

            /* Write back BD after update */
            write_buffer_descriptor(etsec, bd_addr, &bd);
        }

        /* Wrap or next BD */
        if (bd_flags & BD_WRAP) {
            bd_addr = ring_base;
        } else {
            bd_addr += sizeof(eTSEC_rxtx_bd);
        }
    } while (remaining_data != 0
             && (bd_flags & BD_RX_EMPTY)
             && bd_addr != start_bd_addr);

    /* Reset ring ptr */
    etsec->regs[RBPTR0 + ring_nbr].value = bd_addr;

    /* The frame is too large to fit in the Rx ring */
    if (remaining_data > 0) {

        /* Set RSTAT[QHLTx] */
        etsec->regs[RSTAT].value |= 1 << (23 - ring_nbr);

        /* Save remaining data to send the end of the frame when the ring will
         * be restarted
         */
        etsec->rx_remaining_data = remaining_data;

        /* Copy the frame */
        tmp_buf = g_malloc(size);
        memcpy(tmp_buf, etsec->rx_buffer, size);
        etsec->rx_buffer = tmp_buf;

        RING_DEBUG("no empty RxBD available any more\n");
    } else {
        etsec->rx_buffer_len = 0;
        etsec->rx_buffer     = NULL;
        if (etsec->need_flush) {
            qemu_flush_queued_packets(qemu_get_queue(etsec->nic));
        }
    }

    RING_DEBUG("eTSEC End of ring_write: remaining_data:%zu\n", remaining_data);
}
