/*
 * QEMU RISC-V Board Compatible with OpenTitan FPGA platform
 *
 * Copyright (c) 2020 Western Digital
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_OPENTITAN_H
#define HW_OPENTITAN_H

#include "hw/riscv/riscv_hart.h"
#include "hw/intc/ibex_plic.h"
#include "hw/char/ibex_uart.h"
#include "qom/object.h"

#define TYPE_RISCV_IBEX_SOC "riscv.lowrisc.ibex.soc"
OBJECT_DECLARE_SIMPLE_TYPE(LowRISCIbexSoCState, RISCV_IBEX_SOC)

struct LowRISCIbexSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    IbexPlicState plic;
    IbexUartState uart;

    MemoryRegion flash_mem;
    MemoryRegion rom;
};

typedef struct OpenTitanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    LowRISCIbexSoCState soc;
} OpenTitanState;

enum {
    IBEX_DEV_ROM,
    IBEX_DEV_RAM,
    IBEX_DEV_FLASH,
    IBEX_DEV_UART,
    IBEX_DEV_GPIO,
    IBEX_DEV_SPI,
    IBEX_DEV_I2C,
    IBEX_DEV_PATTGEN,
    IBEX_DEV_RV_TIMER,
    IBEX_DEV_SENSOR_CTRL,
    IBEX_DEV_OTP_CTRL,
    IBEX_DEV_PWRMGR,
    IBEX_DEV_RSTMGR,
    IBEX_DEV_CLKMGR,
    IBEX_DEV_PINMUX,
    IBEX_DEV_PADCTRL,
    IBEX_DEV_USBDEV,
    IBEX_DEV_FLASH_CTRL,
    IBEX_DEV_PLIC,
    IBEX_DEV_AES,
    IBEX_DEV_HMAC,
    IBEX_DEV_KMAC,
    IBEX_DEV_KEYMGR,
    IBEX_DEV_CSRNG,
    IBEX_DEV_ENTROPY,
    IBEX_DEV_EDNO,
    IBEX_DEV_EDN1,
    IBEX_DEV_ALERT_HANDLER,
    IBEX_DEV_NMI_GEN,
    IBEX_DEV_OTBN,
};

enum {
    IBEX_UART_RX_PARITY_ERR_IRQ = 0x28,
    IBEX_UART_RX_TIMEOUT_IRQ = 0x27,
    IBEX_UART_RX_BREAK_ERR_IRQ = 0x26,
    IBEX_UART_RX_FRAME_ERR_IRQ = 0x25,
    IBEX_UART_RX_OVERFLOW_IRQ = 0x24,
    IBEX_UART_TX_EMPTY_IRQ = 0x23,
    IBEX_UART_RX_WATERMARK_IRQ = 0x22,
    IBEX_UART_TX_WATERMARK_IRQ = 0x21,
};

#endif
