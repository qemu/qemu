/*
 * QEMU model of the Xilinx Versal CANFD Controller.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * Written-by: Vikram Garhwal<vikram.garhwal@amd.com>
 * Based on QEMU CANFD Device emulation implemented by Jin Yang, Deniz Eren and
 * Pavel Pisa.
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

#ifndef HW_CANFD_XILINX_H
#define HW_CANFD_XILINX_H

#include "hw/register.h"
#include "hw/ptimer.h"
#include "net/can_emu.h"
#include "hw/qdev-clock.h"

#define TYPE_XILINX_CANFD "xlnx.versal-canfd"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalCANFDState, XILINX_CANFD)

#define NUM_REGS_PER_MSG_SPACE 18 /* 1 ID + 1 DLC + 16 Data(DW0 - DW15) regs. */
#define MAX_NUM_RX             64
#define OFFSET_RX1_DW15        (0x4144 / 4)
#define CANFD_TIMER_MAX        0xFFFFUL
#define CANFD_DEFAULT_CLOCK    (25 * 1000 * 1000)

#define XLNX_VERSAL_CANFD_R_MAX (OFFSET_RX1_DW15 + \
                    ((MAX_NUM_RX - 1) * NUM_REGS_PER_MSG_SPACE) + 1)

typedef struct XlnxVersalCANFDState {
    SysBusDevice            parent_obj;
    MemoryRegion            iomem;

    qemu_irq                irq_canfd_int;
    qemu_irq                irq_addr_err;

    RegisterInfo            reg_info[XLNX_VERSAL_CANFD_R_MAX];
    uint32_t                regs[XLNX_VERSAL_CANFD_R_MAX];

    ptimer_state            *canfd_timer;

    CanBusClientState       bus_client;
    CanBusState             *canfdbus;

    struct {
        uint8_t             rx0_fifo;
        uint8_t             rx1_fifo;
        uint8_t             tx_fifo;
        bool                enable_rx_fifo1;
        uint32_t            ext_clk_freq;
   } cfg;

} XlnxVersalCANFDState;

typedef struct tx_ready_reg_info {
    uint32_t can_id;
    uint32_t reg_num;
} tx_ready_reg_info;

#endif
