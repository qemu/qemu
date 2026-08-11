/*
 * Axiado SoC AX3000
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AXIADO_AX3000_H
#define AXIADO_AX3000_H

#include "cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/char/cadence_uart.h"
#include "hw/misc/axiado_clk.h"
#include "hw/sd/axiado_sdhci.h"
#include "hw/core/sysbus.h"
#include "qemu/units.h"

#define TYPE_AX3000_SOC "ax3000"
OBJECT_DECLARE_TYPE(Ax3000SoCState, Ax3000SoCClass, AX3000_SOC)

#define AX3000_DRAM0_BASE       0x3C000000
#define AX3000_DRAM0_SIZE       (1088 * MiB)
#define AX3000_DRAM1_BASE       0x400000000
#define AX3000_DRAM1_SIZE       (2 * GiB)

#define AX3000_GIC_DIST_BASE    0x80300000
#define AX3000_GIC_DIST_SIZE    (64 * KiB)
#define AX3000_GIC_REDIST_BASE  0x80380000
#define AX3000_GIC_REDIST_SIZE  (512 * KiB)

#define AX3000_UART0_BASE       0x80520000
#define AX3000_UART1_BASE       0x805a0000
#define AX3000_UART2_BASE       0x80620000
#define AX3000_UART3_BASE       0x80520800

#define AX3000_SDHCI0_BASE      0x86000000
#define AX3000_EMMC_PHY_BASE    0x80801C00

#define AX3000_TIMER_CTRL       0x8A020000
#define AX3000_PLL_BASE         0x80000000

enum Ax3000Configuration {
    AX3000_NUM_CPUS     = 4,
    AX3000_NUM_IRQS     = 224,
    AX3000_NUM_BANKS    = 2,
    AX3000_NUM_UARTS    = 4,
};

typedef struct Ax3000SoCState {
    SysBusDevice        parent;

    ARMCPU              cpu[AX3000_NUM_CPUS];
    GICv3State          gic;
    MemoryRegion        dram[AX3000_NUM_BANKS];
    Ax3000ClkState      ax3000_clk;
    CadenceUARTState    uart[AX3000_NUM_UARTS];
    AxiadoSDHCIState    sdhci0;
} Ax3000SoCState;

typedef struct Ax3000SoCClass {
    SysBusDeviceClass   parent;

    uint32_t            num_cpus;
} Ax3000SoCClass;

enum Ax3000Irqs {
    AX3000_UART0_IRQ    = 112,
    AX3000_UART1_IRQ    = 113,
    AX3000_UART2_IRQ    = 114,
    AX3000_UART3_IRQ    = 170,

    AX3000_SDHCI0_IRQ   = 123,
};

#endif /* AXIADO_AX3000_H */
