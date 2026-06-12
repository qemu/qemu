/*
 * QEMU RISC-V Virt Board Compatible with kendryte K230 SDK
 *
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the kendryte K230 SDK
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */
#ifndef HW_K230_H
#define HW_K230_H

#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/watchdog/k230_wdt.h"

#define C908_CPU_HARTID   (0)

#define TYPE_RISCV_K230_SOC "riscv.k230.soc"
#define RISCV_K230_SOC(obj) \
    OBJECT_CHECK(K230SoCState, (obj), TYPE_RISCV_K230_SOC)

typedef struct K230SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState c908_cpu; /* Small core */

    K230WdtState wdt[2];
    MemoryRegion sram;
    MemoryRegion bootrom;

    DeviceState *c908_plic;
} K230SoCState;

#define TYPE_RISCV_K230_MACHINE MACHINE_TYPE_NAME("k230")
#define RISCV_K230_MACHINE(obj) \
    OBJECT_CHECK(K230MachineState, (obj), TYPE_RISCV_K230_MACHINE)

typedef struct K230MachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    K230SoCState soc;
    Notifier machine_done;
} K230MachineState;

enum {
    K230_DEV_DDRC,
    K230_DEV_KPU_L2_CACHE,
    K230_DEV_SRAM,
    K230_DEV_KPU_CFG,
    K230_DEV_FFT,
    K230_DEV_AI_2D_ENGINE,
    K230_DEV_GSDMA,
    K230_DEV_DMA,
    K230_DEV_DECOMP_GZIP,
    K230_DEV_NON_AI_2D,
    K230_DEV_ISP,
    K230_DEV_DEWARP,
    K230_DEV_RX_CSI,
    K230_DEV_H264,
    K230_DEV_2P5D,
    K230_DEV_VO,
    K230_DEV_VO_CFG,
    K230_DEV_3D_ENGINE,
    K230_DEV_PMU,
    K230_DEV_RTC,
    K230_DEV_CMU,
    K230_DEV_RMU,
    K230_DEV_BOOT,
    K230_DEV_PWR,
    K230_DEV_MAILBOX,
    K230_DEV_IOMUX,
    K230_DEV_TIMER,
    K230_DEV_WDT0,
    K230_DEV_WDT1,
    K230_DEV_TS,
    K230_DEV_HDI,
    K230_DEV_STC,
    K230_DEV_BOOTROM,
    K230_DEV_SECURITY,
    K230_DEV_UART0,
    K230_DEV_UART1,
    K230_DEV_UART2,
    K230_DEV_UART3,
    K230_DEV_UART4,
    K230_DEV_I2C0,
    K230_DEV_I2C1,
    K230_DEV_I2C2,
    K230_DEV_I2C3,
    K230_DEV_I2C4,
    K230_DEV_PWM,
    K230_DEV_GPIO0,
    K230_DEV_GPIO1,
    K230_DEV_ADC,
    K230_DEV_CODEC,
    K230_DEV_I2S,
    K230_DEV_USB0,
    K230_DEV_USB1,
    K230_DEV_SD0,
    K230_DEV_SD1,
    K230_DEV_QSPI0,
    K230_DEV_QSPI1,
    K230_DEV_SPI,
    K230_DEV_HI_SYS_CFG,
    K230_DEV_DDRC_CFG,
    K230_DEV_FLASH,
    K230_DEV_PLIC,
    K230_DEV_CLINT,
};

enum {
    /*
     * K230 TRM v0.3.1 section 2.4 lists peripheral interrupt bits; SDK
     * DTBs expose the corresponding PLIC IDs as bit + 16.
     */
    K230_UART0_IRQ  = 16,
    K230_UART1_IRQ  = 17,
    K230_UART2_IRQ  = 18,
    K230_UART3_IRQ  = 19,
    K230_UART4_IRQ  = 20,
    K230_WDT0_IRQ   = 107,
    K230_WDT1_IRQ   = 108,
};

#define K230_UART_COUNT 5

/*
 * Integrates with the interrupt controller (PLIC),
 * which can process 208 interrupt external sources
 */
#define K230_PLIC_NUM_SOURCES 208
#define K230_PLIC_NUM_PRIORITIES 7
#define K230_PLIC_PRIORITY_BASE 0x00
#define K230_PLIC_PENDING_BASE 0x1000
#define K230_PLIC_ENABLE_BASE 0x2000
#define K230_PLIC_ENABLE_STRIDE 0x80
#define K230_PLIC_CONTEXT_BASE 0x200000
#define K230_PLIC_CONTEXT_STRIDE 0x1000

#endif
