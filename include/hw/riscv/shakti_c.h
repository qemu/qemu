/*
 * Shakti C-class SoC emulation
 *
 * Copyright (c) 2021 Vijai Kumar K <vijai@behindbytes.com>
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

#ifndef HW_SHAKTI_C_H
#define HW_SHAKTI_C_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"
#include "hw/char/shakti_uart.h"

#define TYPE_RISCV_SHAKTI_SOC "riscv.shakti.cclass.soc"
#define RISCV_SHAKTI_SOC(obj) \
    OBJECT_CHECK(ShaktiCSoCState, (obj), TYPE_RISCV_SHAKTI_SOC)

typedef struct ShaktiCSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    ShaktiUartState uart;
    MemoryRegion rom;

} ShaktiCSoCState;

#define TYPE_RISCV_SHAKTI_MACHINE MACHINE_TYPE_NAME("shakti_c")
#define RISCV_SHAKTI_MACHINE(obj) \
    OBJECT_CHECK(ShaktiCMachineState, (obj), TYPE_RISCV_SHAKTI_MACHINE)
typedef struct ShaktiCMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    ShaktiCSoCState soc;
} ShaktiCMachineState;

enum {
    SHAKTI_C_ROM,
    SHAKTI_C_RAM,
    SHAKTI_C_UART,
    SHAKTI_C_GPIO,
    SHAKTI_C_PLIC,
    SHAKTI_C_CLINT,
    SHAKTI_C_I2C,
};

#define SHAKTI_C_PLIC_HART_CONFIG "MS"
/* Including Interrupt ID 0 (no interrupt)*/
#define SHAKTI_C_PLIC_NUM_SOURCES 28
/* Excluding Priority 0 */
#define SHAKTI_C_PLIC_NUM_PRIORITIES 2
#define SHAKTI_C_PLIC_PRIORITY_BASE 0x04
#define SHAKTI_C_PLIC_PENDING_BASE 0x1000
#define SHAKTI_C_PLIC_ENABLE_BASE 0x2000
#define SHAKTI_C_PLIC_ENABLE_STRIDE 0x80
#define SHAKTI_C_PLIC_CONTEXT_BASE 0x200000
#define SHAKTI_C_PLIC_CONTEXT_STRIDE 0x1000

#endif
