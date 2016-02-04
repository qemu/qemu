/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_IC_H
#define BCM2835_IC_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_IC "bcm2835-ic"
#define BCM2835_IC(obj) OBJECT_CHECK(BCM2835ICState, (obj), TYPE_BCM2835_IC)

#define BCM2835_IC_GPU_IRQ "gpu-irq"
#define BCM2835_IC_ARM_IRQ "arm-irq"

typedef struct BCM2835ICState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq fiq;

    /* 64 GPU IRQs + 8 ARM IRQs = 72 total (GPU first) */
    uint64_t gpu_irq_level, gpu_irq_enable;
    uint8_t arm_irq_level, arm_irq_enable;
    bool fiq_enable;
    uint8_t fiq_select;
} BCM2835ICState;

#endif
