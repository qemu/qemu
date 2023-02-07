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
#include "hw/intc/sifive_plic.h"
#include "hw/char/ibex_uart.h"
#include "hw/timer/ibex_timer.h"
#include "hw/ssi/ibex_spi_host.h"
#include "qom/object.h"

#define TYPE_RISCV_IBEX_SOC "riscv.lowrisc.ibex.soc"
OBJECT_DECLARE_SIMPLE_TYPE(LowRISCIbexSoCState, RISCV_IBEX_SOC)

enum {
    OPENTITAN_SPI_HOST0,
    OPENTITAN_SPI_HOST1,
    OPENTITAN_NUM_SPI_HOSTS,
};

struct LowRISCIbexSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    SiFivePLICState plic;
    IbexUartState uart;
    IbexTimerState timer;
    IbexSPIHostState spi_host[OPENTITAN_NUM_SPI_HOSTS];

    uint32_t resetvec;

    MemoryRegion flash_mem;
    MemoryRegion rom;
    MemoryRegion flash_alias;
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
    IBEX_DEV_FLASH_VIRTUAL,
    IBEX_DEV_UART,
    IBEX_DEV_SPI_DEVICE,
    IBEX_DEV_SPI_HOST0,
    IBEX_DEV_SPI_HOST1,
    IBEX_DEV_GPIO,
    IBEX_DEV_I2C,
    IBEX_DEV_PATTGEN,
    IBEX_DEV_TIMER,
    IBEX_DEV_SENSOR_CTRL,
    IBEX_DEV_OTP_CTRL,
    IBEX_DEV_LC_CTRL,
    IBEX_DEV_PWRMGR,
    IBEX_DEV_RSTMGR,
    IBEX_DEV_CLKMGR,
    IBEX_DEV_PINMUX,
    IBEX_DEV_AON_TIMER,
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
    IBEX_DEV_SRAM_CTRL,
    IBEX_DEV_OTBN,
    IBEX_DEV_IBEX_CFG,
};

enum {
    IBEX_UART0_TX_WATERMARK_IRQ   = 1,
    IBEX_UART0_RX_WATERMARK_IRQ   = 2,
    IBEX_UART0_TX_EMPTY_IRQ       = 3,
    IBEX_UART0_RX_OVERFLOW_IRQ    = 4,
    IBEX_UART0_RX_FRAME_ERR_IRQ   = 5,
    IBEX_UART0_RX_BREAK_ERR_IRQ   = 6,
    IBEX_UART0_RX_TIMEOUT_IRQ     = 7,
    IBEX_UART0_RX_PARITY_ERR_IRQ  = 8,
    IBEX_TIMER_TIMEREXPIRED0_0    = 124,
    IBEX_SPI_HOST0_ERR_IRQ        = 131,
    IBEX_SPI_HOST0_SPI_EVENT_IRQ  = 132,
    IBEX_SPI_HOST1_ERR_IRQ        = 133,
    IBEX_SPI_HOST1_SPI_EVENT_IRQ  = 134,
};

#endif
