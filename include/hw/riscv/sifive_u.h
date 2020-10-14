/*
 * SiFive U series machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
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

#ifndef HW_SIFIVE_U_H
#define HW_SIFIVE_U_H

#include "hw/dma/sifive_pdma.h"
#include "hw/net/cadence_gem.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_cpu.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/misc/sifive_u_otp.h"
#include "hw/misc/sifive_u_prci.h"

#define TYPE_RISCV_U_SOC "riscv.sifive.u.soc"
#define RISCV_U_SOC(obj) \
    OBJECT_CHECK(SiFiveUSoCState, (obj), TYPE_RISCV_U_SOC)

typedef struct SiFiveUSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CPUClusterState e_cluster;
    CPUClusterState u_cluster;
    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
    DeviceState *plic;
    SiFiveUPRCIState prci;
    SIFIVEGPIOState gpio;
    SiFiveUOTPState otp;
    SiFivePDMAState dma;
    CadenceGEMState gem;

    uint32_t serial;
    char *cpu_type;
} SiFiveUSoCState;

#define TYPE_RISCV_U_MACHINE MACHINE_TYPE_NAME("sifive_u")
#define RISCV_U_MACHINE(obj) \
    OBJECT_CHECK(SiFiveUState, (obj), TYPE_RISCV_U_MACHINE)

typedef struct SiFiveUState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    SiFiveUSoCState soc;

    void *fdt;
    int fdt_size;

    bool start_in_flash;
    uint32_t msel;
    uint32_t serial;
} SiFiveUState;

enum {
    SIFIVE_U_DEV_DEBUG,
    SIFIVE_U_DEV_MROM,
    SIFIVE_U_DEV_CLINT,
    SIFIVE_U_DEV_L2CC,
    SIFIVE_U_DEV_PDMA,
    SIFIVE_U_DEV_L2LIM,
    SIFIVE_U_DEV_PLIC,
    SIFIVE_U_DEV_PRCI,
    SIFIVE_U_DEV_UART0,
    SIFIVE_U_DEV_UART1,
    SIFIVE_U_DEV_GPIO,
    SIFIVE_U_DEV_OTP,
    SIFIVE_U_DEV_DMC,
    SIFIVE_U_DEV_FLASH0,
    SIFIVE_U_DEV_DRAM,
    SIFIVE_U_DEV_GEM,
    SIFIVE_U_DEV_GEM_MGMT
};

enum {
    SIFIVE_U_L2CC_IRQ0 = 1,
    SIFIVE_U_L2CC_IRQ1 = 2,
    SIFIVE_U_L2CC_IRQ2 = 3,
    SIFIVE_U_UART0_IRQ = 4,
    SIFIVE_U_UART1_IRQ = 5,
    SIFIVE_U_GPIO_IRQ0 = 7,
    SIFIVE_U_GPIO_IRQ1 = 8,
    SIFIVE_U_GPIO_IRQ2 = 9,
    SIFIVE_U_GPIO_IRQ3 = 10,
    SIFIVE_U_GPIO_IRQ4 = 11,
    SIFIVE_U_GPIO_IRQ5 = 12,
    SIFIVE_U_GPIO_IRQ6 = 13,
    SIFIVE_U_GPIO_IRQ7 = 14,
    SIFIVE_U_GPIO_IRQ8 = 15,
    SIFIVE_U_GPIO_IRQ9 = 16,
    SIFIVE_U_GPIO_IRQ10 = 17,
    SIFIVE_U_GPIO_IRQ11 = 18,
    SIFIVE_U_GPIO_IRQ12 = 19,
    SIFIVE_U_GPIO_IRQ13 = 20,
    SIFIVE_U_GPIO_IRQ14 = 21,
    SIFIVE_U_GPIO_IRQ15 = 22,
    SIFIVE_U_PDMA_IRQ0 = 23,
    SIFIVE_U_PDMA_IRQ1 = 24,
    SIFIVE_U_PDMA_IRQ2 = 25,
    SIFIVE_U_PDMA_IRQ3 = 26,
    SIFIVE_U_PDMA_IRQ4 = 27,
    SIFIVE_U_PDMA_IRQ5 = 28,
    SIFIVE_U_PDMA_IRQ6 = 29,
    SIFIVE_U_PDMA_IRQ7 = 30,
    SIFIVE_U_GEM_IRQ = 0x35
};

enum {
    SIFIVE_U_HFCLK_FREQ = 33333333,
    SIFIVE_U_RTCCLK_FREQ = 1000000
};

enum {
    MSEL_MEMMAP_QSPI0_FLASH = 1,
    MSEL_L2LIM_QSPI0_FLASH = 6,
    MSEL_L2LIM_QSPI2_SD = 11
};

#define SIFIVE_U_MANAGEMENT_CPU_COUNT   1
#define SIFIVE_U_COMPUTE_CPU_COUNT      4

#define SIFIVE_U_PLIC_HART_CONFIG "MS"
#define SIFIVE_U_PLIC_NUM_SOURCES 54
#define SIFIVE_U_PLIC_NUM_PRIORITIES 7
#define SIFIVE_U_PLIC_PRIORITY_BASE 0x04
#define SIFIVE_U_PLIC_PENDING_BASE 0x1000
#define SIFIVE_U_PLIC_ENABLE_BASE 0x2000
#define SIFIVE_U_PLIC_ENABLE_STRIDE 0x80
#define SIFIVE_U_PLIC_CONTEXT_BASE 0x200000
#define SIFIVE_U_PLIC_CONTEXT_STRIDE 0x1000

#endif
