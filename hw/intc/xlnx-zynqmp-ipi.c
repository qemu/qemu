/*
 * QEMU model of the IPI Inter Processor Interrupt block
 *
 * Copyright (c) 2014 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/irq.h"

#ifndef XLNX_ZYNQMP_IPI_ERR_DEBUG
#define XLNX_ZYNQMP_IPI_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (XLNX_ZYNQMP_IPI_ERR_DEBUG >= lvl) {\
        qemu_log(TYPE_XLNX_ZYNQMP_IPI ": %s:" fmt, __func__, ## args);\
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

REG32(IPI_TRIG, 0x0)
    FIELD(IPI_TRIG, PL_3, 27, 1)
    FIELD(IPI_TRIG, PL_2, 26, 1)
    FIELD(IPI_TRIG, PL_1, 25, 1)
    FIELD(IPI_TRIG, PL_0, 24, 1)
    FIELD(IPI_TRIG, PMU_3, 19, 1)
    FIELD(IPI_TRIG, PMU_2, 18, 1)
    FIELD(IPI_TRIG, PMU_1, 17, 1)
    FIELD(IPI_TRIG, PMU_0, 16, 1)
    FIELD(IPI_TRIG, RPU_1, 9, 1)
    FIELD(IPI_TRIG, RPU_0, 8, 1)
    FIELD(IPI_TRIG, APU, 0, 1)
REG32(IPI_OBS, 0x4)
    FIELD(IPI_OBS, PL_3, 27, 1)
    FIELD(IPI_OBS, PL_2, 26, 1)
    FIELD(IPI_OBS, PL_1, 25, 1)
    FIELD(IPI_OBS, PL_0, 24, 1)
    FIELD(IPI_OBS, PMU_3, 19, 1)
    FIELD(IPI_OBS, PMU_2, 18, 1)
    FIELD(IPI_OBS, PMU_1, 17, 1)
    FIELD(IPI_OBS, PMU_0, 16, 1)
    FIELD(IPI_OBS, RPU_1, 9, 1)
    FIELD(IPI_OBS, RPU_0, 8, 1)
    FIELD(IPI_OBS, APU, 0, 1)
REG32(IPI_ISR, 0x10)
    FIELD(IPI_ISR, PL_3, 27, 1)
    FIELD(IPI_ISR, PL_2, 26, 1)
    FIELD(IPI_ISR, PL_1, 25, 1)
    FIELD(IPI_ISR, PL_0, 24, 1)
    FIELD(IPI_ISR, PMU_3, 19, 1)
    FIELD(IPI_ISR, PMU_2, 18, 1)
    FIELD(IPI_ISR, PMU_1, 17, 1)
    FIELD(IPI_ISR, PMU_0, 16, 1)
    FIELD(IPI_ISR, RPU_1, 9, 1)
    FIELD(IPI_ISR, RPU_0, 8, 1)
    FIELD(IPI_ISR, APU, 0, 1)
REG32(IPI_IMR, 0x14)
    FIELD(IPI_IMR, PL_3, 27, 1)
    FIELD(IPI_IMR, PL_2, 26, 1)
    FIELD(IPI_IMR, PL_1, 25, 1)
    FIELD(IPI_IMR, PL_0, 24, 1)
    FIELD(IPI_IMR, PMU_3, 19, 1)
    FIELD(IPI_IMR, PMU_2, 18, 1)
    FIELD(IPI_IMR, PMU_1, 17, 1)
    FIELD(IPI_IMR, PMU_0, 16, 1)
    FIELD(IPI_IMR, RPU_1, 9, 1)
    FIELD(IPI_IMR, RPU_0, 8, 1)
    FIELD(IPI_IMR, APU, 0, 1)
REG32(IPI_IER, 0x18)
    FIELD(IPI_IER, PL_3, 27, 1)
    FIELD(IPI_IER, PL_2, 26, 1)
    FIELD(IPI_IER, PL_1, 25, 1)
    FIELD(IPI_IER, PL_0, 24, 1)
    FIELD(IPI_IER, PMU_3, 19, 1)
    FIELD(IPI_IER, PMU_2, 18, 1)
    FIELD(IPI_IER, PMU_1, 17, 1)
    FIELD(IPI_IER, PMU_0, 16, 1)
    FIELD(IPI_IER, RPU_1, 9, 1)
    FIELD(IPI_IER, RPU_0, 8, 1)
    FIELD(IPI_IER, APU, 0, 1)
REG32(IPI_IDR, 0x1c)
    FIELD(IPI_IDR, PL_3, 27, 1)
    FIELD(IPI_IDR, PL_2, 26, 1)
    FIELD(IPI_IDR, PL_1, 25, 1)
    FIELD(IPI_IDR, PL_0, 24, 1)
    FIELD(IPI_IDR, PMU_3, 19, 1)
    FIELD(IPI_IDR, PMU_2, 18, 1)
    FIELD(IPI_IDR, PMU_1, 17, 1)
    FIELD(IPI_IDR, PMU_0, 16, 1)
    FIELD(IPI_IDR, RPU_1, 9, 1)
    FIELD(IPI_IDR, RPU_0, 8, 1)
    FIELD(IPI_IDR, APU, 0, 1)

/* APU
 * RPU_0
 * RPU_1
 * PMU_0
 * PMU_1
 * PMU_2
 * PMU_3
 * PL_0
 * PL_1
 * PL_2
 * PL_3
 */
int index_array[NUM_IPIS] = {0, 8, 9, 16, 17, 18, 19, 24, 25, 26, 27};
static const char *index_array_names[NUM_IPIS] = {"APU", "RPU_0", "RPU_1",
                                                  "PMU_0", "PMU_1", "PMU_2",
                                                  "PMU_3", "PL_0", "PL_1",
                                                  "PL_2", "PL_3"};

static void xlnx_zynqmp_ipi_set_trig(XlnxZynqMPIPI *s, uint32_t val)
{
    int i, ipi_index, ipi_mask;

    for (i = 0; i < NUM_IPIS; i++) {
        ipi_index = index_array[i];
        ipi_mask = (1 << ipi_index);
        DB_PRINT("Setting %s=%d\n", index_array_names[i],
                 !!(val & ipi_mask));
        qemu_set_irq(s->irq_trig_out[i], !!(val & ipi_mask));
    }
}

static void xlnx_zynqmp_ipi_set_obs(XlnxZynqMPIPI *s, uint32_t val)
{
    int i, ipi_index, ipi_mask;

    for (i = 0; i < NUM_IPIS; i++) {
        ipi_index = index_array[i];
        ipi_mask = (1 << ipi_index);
        DB_PRINT("Setting %s=%d\n", index_array_names[i],
                 !!(val & ipi_mask));
        qemu_set_irq(s->irq_obs_out[i], !!(val & ipi_mask));
    }
}

static void xlnx_zynqmp_ipi_update_irq(XlnxZynqMPIPI *s)
{
    bool pending = s->regs[R_IPI_ISR] & ~s->regs[R_IPI_IMR];

    DB_PRINT("irq=%d isr=%x mask=%x\n",
             pending, s->regs[R_IPI_ISR], s->regs[R_IPI_IMR]);
    qemu_set_irq(s->irq, pending);
}

static uint64_t xlnx_zynqmp_ipi_trig_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);

    xlnx_zynqmp_ipi_set_trig(s, val64);

    return val64;
}

static void xlnx_zynqmp_ipi_trig_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);

    /* TRIG generates a pulse on the outbound signals. We use the
     * post-write callback to bring the signal back-down.
     */
    s->regs[R_IPI_TRIG] = 0;

    xlnx_zynqmp_ipi_set_trig(s, 0);
}

static uint64_t xlnx_zynqmp_ipi_isr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);

    xlnx_zynqmp_ipi_set_obs(s, val64);

    return val64;
}

static void xlnx_zynqmp_ipi_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);

    xlnx_zynqmp_ipi_update_irq(s);
}

static uint64_t xlnx_zynqmp_ipi_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IPI_IMR] &= ~val;
    xlnx_zynqmp_ipi_update_irq(s);
    return 0;
}

static uint64_t xlnx_zynqmp_ipi_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IPI_IMR] |= val;
    xlnx_zynqmp_ipi_update_irq(s);
    return 0;
}

static const RegisterAccessInfo xlnx_zynqmp_ipi_regs_info[] = {
    {   .name = "IPI_TRIG",  .addr = A_IPI_TRIG,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xf0f0fcfe,
        .pre_write = xlnx_zynqmp_ipi_trig_prew,
        .post_write = xlnx_zynqmp_ipi_trig_postw,
    },{ .name = "IPI_OBS",  .addr = A_IPI_OBS,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xffffffff,
    },{ .name = "IPI_ISR",  .addr = A_IPI_ISR,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xf0f0fcfe,
        .w1c = 0xf0f0301,
        .pre_write = xlnx_zynqmp_ipi_isr_prew,
        .post_write = xlnx_zynqmp_ipi_isr_postw,
    },{ .name = "IPI_IMR",  .addr = A_IPI_IMR,
        .reset = 0xf0f0301,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xffffffff,
    },{ .name = "IPI_IER",  .addr = A_IPI_IER,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xf0f0fcfe,
        .pre_write = xlnx_zynqmp_ipi_ier_prew,
    },{ .name = "IPI_IDR",  .addr = A_IPI_IDR,
        .rsvd = 0xf0f0fcfe,
        .ro = 0xf0f0fcfe,
        .pre_write = xlnx_zynqmp_ipi_idr_prew,
    }
};

static void xlnx_zynqmp_ipi_reset(DeviceState *dev)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    xlnx_zynqmp_ipi_update_irq(s);
}

static void xlnx_zynqmp_ipi_handler(void *opaque, int n, int level)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(opaque);
    uint32_t val = (!!level) << n;

    DB_PRINT("IPI input irq[%d]=%d\n", n, level);

    s->regs[R_IPI_ISR] |= val;
    xlnx_zynqmp_ipi_set_obs(s, s->regs[R_IPI_ISR]);
    xlnx_zynqmp_ipi_update_irq(s);
}

static void xlnx_zynqmp_obs_handler(void *opaque, int n, int level)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(opaque);

    DB_PRINT("OBS input irq[%d]=%d\n", n, level);

    s->regs[R_IPI_OBS] &= ~(1ULL << n);
    s->regs[R_IPI_OBS] |= (level << n);
}

static const MemoryRegionOps xlnx_zynqmp_ipi_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_zynqmp_ipi_realize(DeviceState *dev, Error **errp)
{
    qdev_init_gpio_in_named(dev, xlnx_zynqmp_ipi_handler, "IPI_INPUTS", 32);
    qdev_init_gpio_in_named(dev, xlnx_zynqmp_obs_handler, "OBS_INPUTS", 32);
}

static void xlnx_zynqmp_ipi_init(Object *obj)
{
    XlnxZynqMPIPI *s = XLNX_ZYNQMP_IPI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    char *irq_name;
    int i;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_ZYNQMP_IPI,
                       R_XLNX_ZYNQMP_IPI_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), xlnx_zynqmp_ipi_regs_info,
                              ARRAY_SIZE(xlnx_zynqmp_ipi_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_zynqmp_ipi_ops,
                              XLNX_ZYNQMP_IPI_ERR_DEBUG,
                              R_XLNX_ZYNQMP_IPI_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < NUM_IPIS; i++) {
        qdev_init_gpio_out_named(dev, &s->irq_trig_out[i],
                                 index_array_names[i], 1);

        irq_name = g_strdup_printf("OBS_%s", index_array_names[i]);
        qdev_init_gpio_out_named(dev, &s->irq_obs_out[i],
                                 irq_name, 1);
        g_free(irq_name);
    }
}

static const VMStateDescription vmstate_zynqmp_pmu_ipi = {
    .name = TYPE_XLNX_ZYNQMP_IPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqMPIPI, R_XLNX_ZYNQMP_IPI_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void xlnx_zynqmp_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, xlnx_zynqmp_ipi_reset);
    dc->realize = xlnx_zynqmp_ipi_realize;
    dc->vmsd = &vmstate_zynqmp_pmu_ipi;
}

static const TypeInfo xlnx_zynqmp_ipi_info = {
    .name          = TYPE_XLNX_ZYNQMP_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxZynqMPIPI),
    .class_init    = xlnx_zynqmp_ipi_class_init,
    .instance_init = xlnx_zynqmp_ipi_init,
};

static void xlnx_zynqmp_ipi_register_types(void)
{
    type_register_static(&xlnx_zynqmp_ipi_info);
}

type_init(xlnx_zynqmp_ipi_register_types)
