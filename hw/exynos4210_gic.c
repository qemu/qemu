/*
 * Samsung exynos4210 GIC implementation. Based on hw/arm_gic.c
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "qemu-common.h"
#include "irq.h"
#include "exynos4210.h"

enum ExtGicId {
    EXT_GIC_ID_MDMA_LCD0 = 66,
    EXT_GIC_ID_PDMA0,
    EXT_GIC_ID_PDMA1,
    EXT_GIC_ID_TIMER0,
    EXT_GIC_ID_TIMER1,
    EXT_GIC_ID_TIMER2,
    EXT_GIC_ID_TIMER3,
    EXT_GIC_ID_TIMER4,
    EXT_GIC_ID_MCT_L0,
    EXT_GIC_ID_WDT,
    EXT_GIC_ID_RTC_ALARM,
    EXT_GIC_ID_RTC_TIC,
    EXT_GIC_ID_GPIO_XB,
    EXT_GIC_ID_GPIO_XA,
    EXT_GIC_ID_MCT_L1,
    EXT_GIC_ID_IEM_APC,
    EXT_GIC_ID_IEM_IEC,
    EXT_GIC_ID_NFC,
    EXT_GIC_ID_UART0,
    EXT_GIC_ID_UART1,
    EXT_GIC_ID_UART2,
    EXT_GIC_ID_UART3,
    EXT_GIC_ID_UART4,
    EXT_GIC_ID_MCT_G0,
    EXT_GIC_ID_I2C0,
    EXT_GIC_ID_I2C1,
    EXT_GIC_ID_I2C2,
    EXT_GIC_ID_I2C3,
    EXT_GIC_ID_I2C4,
    EXT_GIC_ID_I2C5,
    EXT_GIC_ID_I2C6,
    EXT_GIC_ID_I2C7,
    EXT_GIC_ID_SPI0,
    EXT_GIC_ID_SPI1,
    EXT_GIC_ID_SPI2,
    EXT_GIC_ID_MCT_G1,
    EXT_GIC_ID_USB_HOST,
    EXT_GIC_ID_USB_DEVICE,
    EXT_GIC_ID_MODEMIF,
    EXT_GIC_ID_HSMMC0,
    EXT_GIC_ID_HSMMC1,
    EXT_GIC_ID_HSMMC2,
    EXT_GIC_ID_HSMMC3,
    EXT_GIC_ID_SDMMC,
    EXT_GIC_ID_MIPI_CSI_4LANE,
    EXT_GIC_ID_MIPI_DSI_4LANE,
    EXT_GIC_ID_MIPI_CSI_2LANE,
    EXT_GIC_ID_MIPI_DSI_2LANE,
    EXT_GIC_ID_ONENAND_AUDI,
    EXT_GIC_ID_ROTATOR,
    EXT_GIC_ID_FIMC0,
    EXT_GIC_ID_FIMC1,
    EXT_GIC_ID_FIMC2,
    EXT_GIC_ID_FIMC3,
    EXT_GIC_ID_JPEG,
    EXT_GIC_ID_2D,
    EXT_GIC_ID_PCIe,
    EXT_GIC_ID_MIXER,
    EXT_GIC_ID_HDMI,
    EXT_GIC_ID_HDMI_I2C,
    EXT_GIC_ID_MFC,
    EXT_GIC_ID_TVENC,
};

enum ExtInt {
    EXT_GIC_ID_EXTINT0 = 48,
    EXT_GIC_ID_EXTINT1,
    EXT_GIC_ID_EXTINT2,
    EXT_GIC_ID_EXTINT3,
    EXT_GIC_ID_EXTINT4,
    EXT_GIC_ID_EXTINT5,
    EXT_GIC_ID_EXTINT6,
    EXT_GIC_ID_EXTINT7,
    EXT_GIC_ID_EXTINT8,
    EXT_GIC_ID_EXTINT9,
    EXT_GIC_ID_EXTINT10,
    EXT_GIC_ID_EXTINT11,
    EXT_GIC_ID_EXTINT12,
    EXT_GIC_ID_EXTINT13,
    EXT_GIC_ID_EXTINT14,
    EXT_GIC_ID_EXTINT15
};

/*
 * External GIC sources which are not from External Interrupt Combiner or
 * External Interrupts are starting from EXYNOS4210_MAX_EXT_COMBINER_OUT_IRQ,
 * which is INTG16 in Internal Interrupt Combiner.
 */

static uint32_t
combiner_grp_to_gic_id[64-EXYNOS4210_MAX_EXT_COMBINER_OUT_IRQ][8] = {
    /* int combiner groups 16-19 */
    { }, { }, { }, { },
    /* int combiner group 20 */
    { 0, EXT_GIC_ID_MDMA_LCD0 },
    /* int combiner group 21 */
    { EXT_GIC_ID_PDMA0, EXT_GIC_ID_PDMA1 },
    /* int combiner group 22 */
    { EXT_GIC_ID_TIMER0, EXT_GIC_ID_TIMER1, EXT_GIC_ID_TIMER2,
            EXT_GIC_ID_TIMER3, EXT_GIC_ID_TIMER4 },
    /* int combiner group 23 */
    { EXT_GIC_ID_RTC_ALARM, EXT_GIC_ID_RTC_TIC },
    /* int combiner group 24 */
    { EXT_GIC_ID_GPIO_XB, EXT_GIC_ID_GPIO_XA },
    /* int combiner group 25 */
    { EXT_GIC_ID_IEM_APC, EXT_GIC_ID_IEM_IEC },
    /* int combiner group 26 */
    { EXT_GIC_ID_UART0, EXT_GIC_ID_UART1, EXT_GIC_ID_UART2, EXT_GIC_ID_UART3,
            EXT_GIC_ID_UART4 },
    /* int combiner group 27 */
    { EXT_GIC_ID_I2C0, EXT_GIC_ID_I2C1, EXT_GIC_ID_I2C2, EXT_GIC_ID_I2C3,
            EXT_GIC_ID_I2C4, EXT_GIC_ID_I2C5, EXT_GIC_ID_I2C6,
            EXT_GIC_ID_I2C7 },
    /* int combiner group 28 */
    { EXT_GIC_ID_SPI0, EXT_GIC_ID_SPI1, EXT_GIC_ID_SPI2 },
    /* int combiner group 29 */
    { EXT_GIC_ID_HSMMC0, EXT_GIC_ID_HSMMC1, EXT_GIC_ID_HSMMC2,
     EXT_GIC_ID_HSMMC3, EXT_GIC_ID_SDMMC },
    /* int combiner group 30 */
    { EXT_GIC_ID_MIPI_CSI_4LANE, EXT_GIC_ID_MIPI_CSI_2LANE },
    /* int combiner group 31 */
    { EXT_GIC_ID_MIPI_DSI_4LANE, EXT_GIC_ID_MIPI_DSI_2LANE },
    /* int combiner group 32 */
    { EXT_GIC_ID_FIMC0, EXT_GIC_ID_FIMC1 },
    /* int combiner group 33 */
    { EXT_GIC_ID_FIMC2, EXT_GIC_ID_FIMC3 },
    /* int combiner group 34 */
    { EXT_GIC_ID_ONENAND_AUDI, EXT_GIC_ID_NFC },
    /* int combiner group 35 */
    { 0, 0, 0, EXT_GIC_ID_MCT_L1, EXT_GIC_ID_MCT_G0, EXT_GIC_ID_MCT_G1 },
    /* int combiner group 36 */
    { EXT_GIC_ID_MIXER },
    /* int combiner group 37 */
    { EXT_GIC_ID_EXTINT4, EXT_GIC_ID_EXTINT5, EXT_GIC_ID_EXTINT6,
     EXT_GIC_ID_EXTINT7 },
    /* groups 38-50 */
    { }, { }, { }, { }, { }, { }, { }, { }, { }, { }, { }, { }, { },
    /* int combiner group 51 */
    { EXT_GIC_ID_MCT_L0, 0, 0, 0, EXT_GIC_ID_MCT_G0, EXT_GIC_ID_MCT_G1 },
    /* group 52 */
    { },
    /* int combiner group 53 */
    { EXT_GIC_ID_WDT, 0, 0, 0, EXT_GIC_ID_MCT_G0, EXT_GIC_ID_MCT_G1 },
    /* groups 54-63 */
    { }, { }, { }, { }, { }, { }, { }, { }, { }, { }
};

#define EXYNOS4210_GIC_NIRQ 160
#define NCPU                EXYNOS4210_NCPUS

#define EXYNOS4210_EXT_GIC_CPU_REGION_SIZE     0x10000
#define EXYNOS4210_EXT_GIC_DIST_REGION_SIZE    0x10000

#define EXYNOS4210_EXT_GIC_PER_CPU_OFFSET      0x8000
#define EXYNOS4210_EXT_GIC_CPU_GET_OFFSET(n) \
    ((n) * EXYNOS4210_EXT_GIC_PER_CPU_OFFSET)
#define EXYNOS4210_EXT_GIC_DIST_GET_OFFSET(n) \
    ((n) * EXYNOS4210_EXT_GIC_PER_CPU_OFFSET)

#define EXYNOS4210_GIC_CPU_REGION_SIZE  0x100
#define EXYNOS4210_GIC_DIST_REGION_SIZE 0x1000

static void exynos4210_irq_handler(void *opaque, int irq, int level)
{
    Exynos4210Irq *s = (Exynos4210Irq *)opaque;

    /* Bypass */
    qemu_set_irq(s->board_irqs[irq], level);

    return;
}

/*
 * Initialize exynos4210 IRQ subsystem stub.
 */
qemu_irq *exynos4210_init_irq(Exynos4210Irq *s)
{
    return qemu_allocate_irqs(exynos4210_irq_handler, s,
            EXYNOS4210_MAX_INT_COMBINER_IN_IRQ);
}

/*
 * Initialize board IRQs.
 * These IRQs contain splitted Int/External Combiner and External Gic IRQs.
 */
void exynos4210_init_board_irqs(Exynos4210Irq *s)
{
    uint32_t grp, bit, irq_id, n;

    for (n = 0; n < EXYNOS4210_MAX_EXT_COMBINER_IN_IRQ; n++) {
        s->board_irqs[n] = qemu_irq_split(s->int_combiner_irq[n],
                s->ext_combiner_irq[n]);

        irq_id = 0;
        if (n == EXYNOS4210_COMBINER_GET_IRQ_NUM(1, 4) ||
                n == EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 4)) {
            /* MCT_G0 is passed to External GIC */
            irq_id = EXT_GIC_ID_MCT_G0;
        }
        if (n == EXYNOS4210_COMBINER_GET_IRQ_NUM(1, 5) ||
                n == EXYNOS4210_COMBINER_GET_IRQ_NUM(12, 5)) {
            /* MCT_G1 is passed to External and GIC */
            irq_id = EXT_GIC_ID_MCT_G1;
        }
        if (irq_id) {
            s->board_irqs[n] = qemu_irq_split(s->int_combiner_irq[n],
                    s->ext_gic_irq[irq_id-32]);
        }

    }
    for (; n < EXYNOS4210_MAX_INT_COMBINER_IN_IRQ; n++) {
        /* these IDs are passed to Internal Combiner and External GIC */
        grp = EXYNOS4210_COMBINER_GET_GRP_NUM(n);
        bit = EXYNOS4210_COMBINER_GET_BIT_NUM(n);
        irq_id = combiner_grp_to_gic_id[grp -
                     EXYNOS4210_MAX_EXT_COMBINER_OUT_IRQ][bit];

        if (irq_id) {
            s->board_irqs[n] = qemu_irq_split(s->int_combiner_irq[n],
                    s->ext_gic_irq[irq_id-32]);
        }
    }
}

/*
 * Get IRQ number from exynos4210 IRQ subsystem stub.
 * To identify IRQ source use internal combiner group and bit number
 *  grp - group number
 *  bit - bit number inside group
 */
uint32_t exynos4210_get_irq(uint32_t grp, uint32_t bit)
{
    return EXYNOS4210_COMBINER_GET_IRQ_NUM(grp, bit);
}

/********* GIC part *********/

static inline int
gic_get_current_cpu(void)
{
    return cpu_single_env->cpu_index;
}

#include "arm_gic.c"

typedef struct {
    gic_state gic;
    MemoryRegion cpu_container;
    MemoryRegion dist_container;
    MemoryRegion cpu_alias[NCPU];
    MemoryRegion dist_alias[NCPU];
    uint32_t num_cpu;
} Exynos4210GicState;

static int exynos4210_gic_init(SysBusDevice *dev)
{
    Exynos4210GicState *s = FROM_SYSBUSGIC(Exynos4210GicState, dev);
    uint32_t i;
    const char cpu_prefix[] = "exynos4210-gic-alias_cpu";
    const char dist_prefix[] = "exynos4210-gic-alias_dist";
    char cpu_alias_name[sizeof(cpu_prefix) + 3];
    char dist_alias_name[sizeof(cpu_prefix) + 3];

    gic_init(&s->gic, s->num_cpu, EXYNOS4210_GIC_NIRQ);

    memory_region_init(&s->cpu_container, "exynos4210-cpu-container",
            EXYNOS4210_EXT_GIC_CPU_REGION_SIZE);
    memory_region_init(&s->dist_container, "exynos4210-dist-container",
            EXYNOS4210_EXT_GIC_DIST_REGION_SIZE);

    for (i = 0; i < s->num_cpu; i++) {
        /* Map CPU interface per SMP Core */
        sprintf(cpu_alias_name, "%s%x", cpu_prefix, i);
        memory_region_init_alias(&s->cpu_alias[i],
                                 cpu_alias_name,
                                 &s->gic.cpuiomem[0],
                                 0,
                                 EXYNOS4210_GIC_CPU_REGION_SIZE);
        memory_region_add_subregion(&s->cpu_container,
                EXYNOS4210_EXT_GIC_CPU_GET_OFFSET(i), &s->cpu_alias[i]);

        /* Map Distributor per SMP Core */
        sprintf(dist_alias_name, "%s%x", dist_prefix, i);
        memory_region_init_alias(&s->dist_alias[i],
                                 dist_alias_name,
                                 &s->gic.iomem,
                                 0,
                                 EXYNOS4210_GIC_DIST_REGION_SIZE);
        memory_region_add_subregion(&s->dist_container,
                EXYNOS4210_EXT_GIC_DIST_GET_OFFSET(i), &s->dist_alias[i]);
    }

    sysbus_init_mmio(dev, &s->cpu_container);
    sysbus_init_mmio(dev, &s->dist_container);

    gic_cpu_write(&s->gic, 1, 0, 1);

    return 0;
}

static Property exynos4210_gic_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", Exynos4210GicState, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void exynos4210_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = exynos4210_gic_init;
    dc->props = exynos4210_gic_properties;
}

static TypeInfo exynos4210_gic_info = {
    .name          = "exynos4210.gic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210GicState),
    .class_init    = exynos4210_gic_class_init,
};

static void exynos4210_gic_register_types(void)
{
    type_register_static(&exynos4210_gic_info);
}

type_init(exynos4210_gic_register_types)

/*
 * IRQGate struct.
 * IRQ Gate represents OR gate between GICs to pass IRQ to PIC.
 */
typedef struct {
    SysBusDevice busdev;

    qemu_irq pic_irq[NCPU]; /* output IRQs to PICs */
    uint32_t gpio_level[EXYNOS4210_IRQ_GATE_NINPUTS]; /* Input levels */
} Exynos4210IRQGateState;

static const VMStateDescription vmstate_exynos4210_irq_gate = {
    .name = "exynos4210.irq_gate",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpio_level, Exynos4210IRQGateState,
                EXYNOS4210_IRQ_GATE_NINPUTS),
        VMSTATE_END_OF_LIST()
    }
};

/* Process a change in an external IRQ input.  */
static void exynos4210_irq_gate_handler(void *opaque, int irq, int level)
{
    Exynos4210IRQGateState *s =
            (Exynos4210IRQGateState *)opaque;
    uint32_t odd, even;

    if (irq & 1) {
        odd = irq;
        even = irq & ~1;
    } else {
        even = irq;
        odd = irq | 1;
    }

    assert(irq < EXYNOS4210_IRQ_GATE_NINPUTS);
    s->gpio_level[irq] = level;

    if (s->gpio_level[odd] >= 1 || s->gpio_level[even] >= 1) {
        qemu_irq_raise(s->pic_irq[even >> 1]);
    } else {
        qemu_irq_lower(s->pic_irq[even >> 1]);
    }

    return;
}

static void exynos4210_irq_gate_reset(DeviceState *d)
{
    Exynos4210IRQGateState *s = (Exynos4210IRQGateState *)d;

    memset(&s->gpio_level, 0, sizeof(s->gpio_level));
}

/*
 * IRQ Gate initialization.
 */
static int exynos4210_irq_gate_init(SysBusDevice *dev)
{
    unsigned int i;
    Exynos4210IRQGateState *s =
            FROM_SYSBUS(Exynos4210IRQGateState, dev);

    /* Allocate general purpose input signals and connect a handler to each of
     * them */
    qdev_init_gpio_in(&s->busdev.qdev, exynos4210_irq_gate_handler,
            EXYNOS4210_IRQ_GATE_NINPUTS);

    /* Connect SysBusDev irqs to device specific irqs */
    for (i = 0; i < NCPU; i++) {
        sysbus_init_irq(dev, &s->pic_irq[i]);
    }

    return 0;
}

static void exynos4210_irq_gate_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = exynos4210_irq_gate_init;
    dc->reset = exynos4210_irq_gate_reset;
    dc->vmsd = &vmstate_exynos4210_irq_gate;
}

static TypeInfo exynos4210_irq_gate_info = {
    .name          = "exynos4210.irq_gate",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210IRQGateState),
    .class_init    = exynos4210_irq_gate_class_init,
};

static void exynos4210_irq_gate_register_types(void)
{
    type_register_static(&exynos4210_irq_gate_info);
}

type_init(exynos4210_irq_gate_register_types)
