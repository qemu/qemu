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

#include "hw/sysbus.h"
#include "qemu-common.h"
#include "hw/irq.h"
#include "hw/arm/exynos4210.h"

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
    { EXT_GIC_ID_SPI0, EXT_GIC_ID_SPI1, EXT_GIC_ID_SPI2 , EXT_GIC_ID_USB_HOST},
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

typedef struct {
    SysBusDevice busdev;
    MemoryRegion cpu_container;
    MemoryRegion dist_container;
    MemoryRegion cpu_alias[EXYNOS4210_NCPUS];
    MemoryRegion dist_alias[EXYNOS4210_NCPUS];
    uint32_t num_cpu;
    DeviceState *gic;
} Exynos4210GicState;

static void exynos4210_gic_set_irq(void *opaque, int irq, int level)
{
    Exynos4210GicState *s = (Exynos4210GicState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static int exynos4210_gic_init(SysBusDevice *dev)
{
    Exynos4210GicState *s = FROM_SYSBUS(Exynos4210GicState, dev);
    uint32_t i;
    const char cpu_prefix[] = "exynos4210-gic-alias_cpu";
    const char dist_prefix[] = "exynos4210-gic-alias_dist";
    char cpu_alias_name[sizeof(cpu_prefix) + 3];
    char dist_alias_name[sizeof(cpu_prefix) + 3];
    SysBusDevice *busdev;

    s->gic = qdev_create(NULL, "arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(s->gic, "num-irq", EXYNOS4210_GIC_NIRQ);
    qdev_init_nofail(s->gic);
    busdev = SYS_BUS_DEVICE(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, busdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, exynos4210_gic_set_irq,
                      EXYNOS4210_GIC_NIRQ - 32);

    memory_region_init(&s->cpu_container, "exynos4210-cpu-container",
            EXYNOS4210_EXT_GIC_CPU_REGION_SIZE);
    memory_region_init(&s->dist_container, "exynos4210-dist-container",
            EXYNOS4210_EXT_GIC_DIST_REGION_SIZE);

    for (i = 0; i < s->num_cpu; i++) {
        /* Map CPU interface per SMP Core */
        sprintf(cpu_alias_name, "%s%x", cpu_prefix, i);
        memory_region_init_alias(&s->cpu_alias[i],
                                 cpu_alias_name,
                                 sysbus_mmio_get_region(busdev, 1),
                                 0,
                                 EXYNOS4210_GIC_CPU_REGION_SIZE);
        memory_region_add_subregion(&s->cpu_container,
                EXYNOS4210_EXT_GIC_CPU_GET_OFFSET(i), &s->cpu_alias[i]);

        /* Map Distributor per SMP Core */
        sprintf(dist_alias_name, "%s%x", dist_prefix, i);
        memory_region_init_alias(&s->dist_alias[i],
                                 dist_alias_name,
                                 sysbus_mmio_get_region(busdev, 0),
                                 0,
                                 EXYNOS4210_GIC_DIST_REGION_SIZE);
        memory_region_add_subregion(&s->dist_container,
                EXYNOS4210_EXT_GIC_DIST_GET_OFFSET(i), &s->dist_alias[i]);
    }

    sysbus_init_mmio(dev, &s->cpu_container);
    sysbus_init_mmio(dev, &s->dist_container);

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

static const TypeInfo exynos4210_gic_info = {
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

/* IRQ OR Gate struct.
 *
 * This device models an OR gate. There are n_in input qdev gpio lines and one
 * output sysbus IRQ line. The output IRQ level is formed as OR between all
 * gpio inputs.
 */
typedef struct {
    SysBusDevice busdev;

    uint32_t n_in;      /* inputs amount */
    uint32_t *level;    /* input levels */
    qemu_irq out;       /* output IRQ */
} Exynos4210IRQGateState;

static Property exynos4210_irq_gate_properties[] = {
    DEFINE_PROP_UINT32("n_in", Exynos4210IRQGateState, n_in, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_exynos4210_irq_gate = {
    .name = "exynos4210.irq_gate",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(level, Exynos4210IRQGateState, 1, NULL, 0, n_in),
        VMSTATE_END_OF_LIST()
    }
};

/* Process a change in IRQ input. */
static void exynos4210_irq_gate_handler(void *opaque, int irq, int level)
{
    Exynos4210IRQGateState *s = (Exynos4210IRQGateState *)opaque;
    uint32_t i;

    assert(irq < s->n_in);

    s->level[irq] = level;

    for (i = 0; i < s->n_in; i++) {
        if (s->level[i] >= 1) {
            qemu_irq_raise(s->out);
            return;
        }
    }

    qemu_irq_lower(s->out);
}

static void exynos4210_irq_gate_reset(DeviceState *d)
{
    Exynos4210IRQGateState *s =
            DO_UPCAST(Exynos4210IRQGateState, busdev.qdev, d);

    memset(s->level, 0, s->n_in * sizeof(*s->level));
}

/*
 * IRQ Gate initialization.
 */
static int exynos4210_irq_gate_init(SysBusDevice *dev)
{
    Exynos4210IRQGateState *s = FROM_SYSBUS(Exynos4210IRQGateState, dev);

    /* Allocate general purpose input signals and connect a handler to each of
     * them */
    qdev_init_gpio_in(&s->busdev.qdev, exynos4210_irq_gate_handler, s->n_in);

    s->level = g_malloc0(s->n_in * sizeof(*s->level));

    sysbus_init_irq(dev, &s->out);

    return 0;
}

static void exynos4210_irq_gate_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = exynos4210_irq_gate_init;
    dc->reset = exynos4210_irq_gate_reset;
    dc->vmsd = &vmstate_exynos4210_irq_gate;
    dc->props = exynos4210_irq_gate_properties;
}

static const TypeInfo exynos4210_irq_gate_info = {
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
