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

#define TYPE_RISCV_IBEX_SOC "riscv.lowrisc.ibex.soc"
#define RISCV_IBEX_SOC(obj) \
    OBJECT_CHECK(LowRISCIbexSoCState, (obj), TYPE_RISCV_IBEX_SOC)

typedef struct LowRISCIbexSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    MemoryRegion flash_mem;
    MemoryRegion rom;
} LowRISCIbexSoCState;

typedef struct OpenTitanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    LowRISCIbexSoCState soc;
} OpenTitanState;

enum {
    IBEX_ROM,
    IBEX_RAM,
    IBEX_FLASH,
    IBEX_UART,
    IBEX_GPIO,
    IBEX_SPI,
    IBEX_FLASH_CTRL,
    IBEX_RV_TIMER,
    IBEX_AES,
    IBEX_HMAC,
    IBEX_PLIC,
    IBEX_PWRMGR,
    IBEX_RSTMGR,
    IBEX_CLKMGR,
    IBEX_PINMUX,
    IBEX_ALERT_HANDLER,
    IBEX_NMI_GEN,
    IBEX_USBDEV,
    IBEX_PADCTRL,
};

#endif
