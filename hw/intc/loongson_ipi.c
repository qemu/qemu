/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/intc/loongson_ipi.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "target/mips/cpu.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    if (ase_lcsr_available(&MIPS_CPU(cpu)->env)) {
        return &MIPS_CPU(cpu)->env.iocsr.as;
    }

    return NULL;
}

static int loongson_cpu_by_arch_id(LoongsonIPICommonState *lics,
                                   int64_t arch_id, int *index, CPUState **pcs)
{
    CPUState *cs;

    cs = cpu_by_arch_id(arch_id);
    if (cs == NULL) {
        return MEMTX_ERROR;
    }

    if (index) {
        *index = cs->cpu_index;
    }

    if (pcs) {
        *pcs = cs;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps loongson_ipi_core_ops = {
    .read_with_attrs = loongson_ipi_core_readl,
    .write_with_attrs = loongson_ipi_core_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongson_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *sc = LOONGSON_IPI_COMMON(dev);
    LoongsonIPIState *s = LOONGSON_IPI(dev);
    LoongsonIPIClass *lic = LOONGSON_IPI_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;
    int i;

    lic->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (sc->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    sc->cpu = g_new0(IPICore, sc->num_cpu);
    for (i = 0; i < sc->num_cpu; i++) {
        sc->cpu[i].ipi = sc;
        qdev_init_gpio_out(dev, &sc->cpu[i].irq, 1);
    }

    s->ipi_mmio_mem = g_new0(MemoryRegion, sc->num_cpu);
    for (i = 0; i < sc->num_cpu; i++) {
        g_autofree char *name = g_strdup_printf("loongson_ipi_cpu%d_mmio", i);

        memory_region_init_io(&s->ipi_mmio_mem[i], OBJECT(dev),
                              &loongson_ipi_core_ops, &sc->cpu[i], name, 0x48);
        sysbus_init_mmio(sbd, &s->ipi_mmio_mem[i]);
    }
}

static void loongson_ipi_unrealize(DeviceState *dev)
{
    LoongsonIPIState *s = LOONGSON_IPI(dev);
    LoongsonIPIClass *k = LOONGSON_IPI_GET_CLASS(dev);

    g_free(s->ipi_mmio_mem);

    k->parent_unrealize(dev);
}

static const Property loongson_ipi_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", LoongsonIPICommonState, num_cpu, 1),
};

static void loongson_ipi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongsonIPIClass *lic = LOONGSON_IPI_CLASS(klass);
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);

    device_class_set_parent_realize(dc, loongson_ipi_realize,
                                    &lic->parent_realize);
    device_class_set_parent_unrealize(dc, loongson_ipi_unrealize,
                                      &lic->parent_unrealize);
    device_class_set_props(dc, loongson_ipi_properties);
    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = loongson_cpu_by_arch_id;
}

static const TypeInfo loongson_ipi_types[] = {
    {
        .name               = TYPE_LOONGSON_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .instance_size      = sizeof(LoongsonIPIState),
        .class_size         = sizeof(LoongsonIPIClass),
        .class_init         = loongson_ipi_class_init,
    }
};

DEFINE_TYPES(loongson_ipi_types)
