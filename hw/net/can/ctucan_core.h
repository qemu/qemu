/*
 * CTU CAN FD device emulation
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
#ifndef HW_CAN_CTUCAN_CORE_H
#define HW_CAN_CTUCAN_CORE_H

#include "exec/hwaddr.h"
#include "net/can_emu.h"

#if !HOST_BIG_ENDIAN
#define __LITTLE_ENDIAN_BITFIELD 1
#endif

#include "ctu_can_fd_frame.h"
#include "ctu_can_fd_regs.h"

#define CTUCAN_CORE_MEM_SIZE       0x500

/* The max size for a message in FIFO */
#define CTUCAN_MSG_MAX_LEN        (CTU_CAN_FD_DATA_1_4_W + 64)
/* The receive buffer size. */
#define CTUCAN_RCV_BUF_LEN        (1024 * 8)


/* The max size for a message buffer */
#define CTUCAN_CORE_MSG_MAX_LEN       0x50
/* The receive buffer size. */
#define CTUCAN_CORE_RCV_BUF_LEN       0x1000

#define CTUCAN_CORE_TXBUF_NUM            4

typedef struct CtuCanCoreMsgBuffer {
    uint8_t data[CTUCAN_CORE_MSG_MAX_LEN];
} CtuCanCoreMsgBuffer;

typedef struct CtuCanCoreState {
    union ctu_can_fd_mode_settings                  mode_settings;
    union ctu_can_fd_status                         status;
    union ctu_can_fd_int_stat                       int_stat;
    union ctu_can_fd_int_ena_set                    int_ena;
    union ctu_can_fd_int_mask_set                   int_mask;
    union ctu_can_fd_btr                            brt;
    union ctu_can_fd_btr_fd                         brt_fd;
    union ctu_can_fd_ewl_erp_fault_state            ewl_erp_fault_state;
    union ctu_can_fd_rec_tec                        rec_tec;
    union ctu_can_fd_err_norm_err_fd                err_norm_err_fd;
    union ctu_can_fd_ctr_pres                       ctr_pres;
    union ctu_can_fd_filter_a_mask                  filter_a_mask;
    union ctu_can_fd_filter_a_val                   filter_a_val;
    union ctu_can_fd_filter_b_mask                  filter_b_mask;
    union ctu_can_fd_filter_b_val                   filter_b_val;
    union ctu_can_fd_filter_c_mask                  filter_c_mask;
    union ctu_can_fd_filter_c_val                   filter_c_val;
    union ctu_can_fd_filter_ran_low                 filter_ran_low;
    union ctu_can_fd_filter_ran_high                filter_ran_high;
    union ctu_can_fd_filter_control_filter_status   filter_control_filter_status;
    union ctu_can_fd_rx_mem_info                    rx_mem_info;
    union ctu_can_fd_rx_pointers                    rx_pointers;
    union ctu_can_fd_rx_status_rx_settings          rx_status_rx_settings;
    union ctu_can_fd_tx_status                      tx_status;
    union ctu_can_fd_tx_priority                    tx_priority;
    union ctu_can_fd_err_capt_alc                   err_capt_alc;
    union ctu_can_fd_trv_delay_ssp_cfg              trv_delay_ssp_cfg;
    union ctu_can_fd_rx_fr_ctr                      rx_fr_ctr;
    union ctu_can_fd_tx_fr_ctr                      tx_fr_ctr;
    union ctu_can_fd_debug_register                 debug_register;
    union ctu_can_fd_yolo_reg                       yolo_reg;
    union ctu_can_fd_timestamp_low                  timestamp_low;
    union ctu_can_fd_timestamp_high                 timestamp_high;

    CtuCanCoreMsgBuffer tx_buffer[CTUCAN_CORE_TXBUF_NUM];

    uint8_t         rx_buff[CTUCAN_RCV_BUF_LEN];  /* 32~95 .. 64bytes Rx FIFO */
    uint32_t        rx_tail_pos;        /* Count by bytes. */
    uint32_t        rx_cnt;        /* Count by bytes. */
    uint32_t        rx_frame_rem;

    qemu_irq        irq;
    CanBusClientState bus_client;
} CtuCanCoreState;

void ctucan_hardware_reset(CtuCanCoreState *s);

void ctucan_mem_write(CtuCanCoreState *s, hwaddr addr, uint64_t val,
                       unsigned size);

uint64_t ctucan_mem_read(CtuCanCoreState *s, hwaddr addr, unsigned size);

int ctucan_connect_to_bus(CtuCanCoreState *s, CanBusState *bus);

void ctucan_disconnect(CtuCanCoreState *s);

int ctucan_init(CtuCanCoreState *s, qemu_irq irq);

bool ctucan_can_receive(CanBusClientState *client);

ssize_t ctucan_receive(CanBusClientState *client,
                        const qemu_can_frame *frames, size_t frames_cnt);

extern const VMStateDescription vmstate_ctucan;

#endif
