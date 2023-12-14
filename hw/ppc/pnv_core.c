/*
 * QEMU PowerPC PowerNV CPU Core model
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/reset.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/xics.h"
#include "hw/qdev-properties.h"
#include "helper_regs.h"

static const char *pnv_core_cpu_typename(PnvCore *pc)
{
    const char *core_type = object_class_get_name(object_get_class(OBJECT(pc)));
    int len = strlen(core_type) - strlen(PNV_CORE_TYPE_SUFFIX);
    char *s = g_strdup_printf(POWERPC_CPU_TYPE_NAME("%.*s"), len, core_type);
    const char *cpu_type = object_class_get_name(object_class_by_name(s));
    g_free(s);
    return cpu_type;
}

static void pnv_core_cpu_reset(PnvCore *pc, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(pc->chip);

    cpu_reset(cs);

    /*
     * the skiboot firmware elects a primary thread to initialize the
     * system and it can be any.
     */
    env->gpr[3] = PNV_FDT_ADDR;
    env->nip = 0x10;
    env->msr |= MSR_HVB; /* Hypervisor mode */
    env->spr[SPR_HRMOR] = pc->hrmor;
    hreg_compute_hflags(env);
    ppc_maybe_interrupt(env);

    cpu_ppc_tb_reset(env);

    pcc->intc_reset(pc->chip, cpu);
}

/*
 * These values are read by the PowerNV HW monitors under Linux
 */
#define PNV_XSCOM_EX_DTS_RESULT0     0x50000
#define PNV_XSCOM_EX_DTS_RESULT1     0x50001

static uint64_t pnv_core_power8_xscom_read(void *opaque, hwaddr addr,
                                           unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    /* The result should be 38 C */
    switch (offset) {
    case PNV_XSCOM_EX_DTS_RESULT0:
        val = 0x26f024f023f0000ull;
        break;
    case PNV_XSCOM_EX_DTS_RESULT1:
        val = 0x24f000000000000ull;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_core_power8_xscom_write(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned int width)
{
    uint32_t offset = addr >> 3;

    qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                  offset);
}

static const MemoryRegionOps pnv_core_power8_xscom_ops = {
    .read = pnv_core_power8_xscom_read,
    .write = pnv_core_power8_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};


/*
 * POWER9 core controls
 */
#define PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_HYP 0xf010d
#define PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_OTR 0xf010a

#define PNV9_XSCOM_EC_CORE_THREAD_STATE    0x10ab3

static uint64_t pnv_core_power9_xscom_read(void *opaque, hwaddr addr,
                                           unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    /* The result should be 38 C */
    switch (offset) {
    case PNV_XSCOM_EX_DTS_RESULT0:
        val = 0x26f024f023f0000ull;
        break;
    case PNV_XSCOM_EX_DTS_RESULT1:
        val = 0x24f000000000000ull;
        break;
    case PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_HYP:
    case PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_OTR:
        val = 0x0;
        break;
    case PNV9_XSCOM_EC_CORE_THREAD_STATE:
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_core_power9_xscom_write(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned int width)
{
    uint32_t offset = addr >> 3;

    switch (offset) {
    case PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_HYP:
    case PNV9_XSCOM_EC_PPM_SPECIAL_WKUP_OTR:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                      offset);
    }
}

static const MemoryRegionOps pnv_core_power9_xscom_ops = {
    .read = pnv_core_power9_xscom_read,
    .write = pnv_core_power9_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

/*
 * POWER10 core controls
 */

#define PNV10_XSCOM_EC_CORE_THREAD_STATE    0x412

static uint64_t pnv_core_power10_xscom_read(void *opaque, hwaddr addr,
                                           unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case PNV10_XSCOM_EC_CORE_THREAD_STATE:
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_core_power10_xscom_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned int width)
{
    uint32_t offset = addr >> 3;

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                      offset);
    }
}

static const MemoryRegionOps pnv_core_power10_xscom_ops = {
    .read = pnv_core_power10_xscom_read,
    .write = pnv_core_power10_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_core_cpu_realize(PnvCore *pc, PowerPCCPU *cpu, Error **errp,
                                 int thread_index)
{
    CPUPPCState *env = &cpu->env;
    int core_pir;
    ppc_spr_t *pir = &env->spr_cb[SPR_PIR];
    ppc_spr_t *tir = &env->spr_cb[SPR_TIR];
    Error *local_err = NULL;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(pc->chip);

    if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
        return;
    }

    pcc->intc_create(pc->chip, cpu, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    core_pir = object_property_get_uint(OBJECT(pc), "pir", &error_abort);

    tir->default_value = thread_index;
    pir->default_value = core_pir + thread_index;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, PNV_TIMEBASE_FREQ);
}

static void pnv_core_reset(void *dev)
{
    CPUCore *cc = CPU_CORE(dev);
    PnvCore *pc = PNV_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        pnv_core_cpu_reset(pc, pc->threads[i]);
    }
}

static void pnv_core_realize(DeviceState *dev, Error **errp)
{
    PnvCore *pc = PNV_CORE(OBJECT(dev));
    PnvCoreClass *pcc = PNV_CORE_GET_CLASS(pc);
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    const char *typename = pnv_core_cpu_typename(pc);
    Error *local_err = NULL;
    void *obj;
    int i, j;
    char name[32];

    assert(pc->chip);

    pc->threads = g_new(PowerPCCPU *, cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        PowerPCCPU *cpu;

        obj = object_new(typename);
        cpu = POWERPC_CPU(obj);

        pc->threads[i] = POWERPC_CPU(obj);

        snprintf(name, sizeof(name), "thread[%d]", i);
        object_property_add_child(OBJECT(pc), name, obj);

        cpu->machine_data = g_new0(PnvCPUState, 1);

        object_unref(obj);
    }

    for (j = 0; j < cc->nr_threads; j++) {
        pnv_core_cpu_realize(pc, pc->threads[j], &local_err, j);
        if (local_err) {
            goto err;
        }
    }

    snprintf(name, sizeof(name), "xscom-core.%d", cc->core_id);
    pnv_xscom_region_init(&pc->xscom_regs, OBJECT(dev), pcc->xscom_ops,
                          pc, name, pcc->xscom_size);

    qemu_register_reset(pnv_core_reset, pc);
    return;

err:
    while (--i >= 0) {
        obj = OBJECT(pc->threads[i]);
        object_unparent(obj);
    }
    g_free(pc->threads);
    error_propagate(errp, local_err);
}

static void pnv_core_cpu_unrealize(PnvCore *pc, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(pc->chip);

    pcc->intc_destroy(pc->chip, cpu);
    cpu_remove_sync(CPU(cpu));
    cpu->machine_data = NULL;
    g_free(pnv_cpu);
    object_unparent(OBJECT(cpu));
}

static void pnv_core_unrealize(DeviceState *dev)
{
    PnvCore *pc = PNV_CORE(dev);
    CPUCore *cc = CPU_CORE(dev);
    int i;

    qemu_unregister_reset(pnv_core_reset, pc);

    for (i = 0; i < cc->nr_threads; i++) {
        pnv_core_cpu_unrealize(pc, pc->threads[i]);
    }
    g_free(pc->threads);
}

static Property pnv_core_properties[] = {
    DEFINE_PROP_UINT32("pir", PnvCore, pir, 0),
    DEFINE_PROP_UINT64("hrmor", PnvCore, hrmor, 0),
    DEFINE_PROP_LINK("chip", PnvCore, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_core_power8_class_init(ObjectClass *oc, void *data)
{
    PnvCoreClass *pcc = PNV_CORE_CLASS(oc);

    pcc->xscom_ops = &pnv_core_power8_xscom_ops;
    pcc->xscom_size = PNV_XSCOM_EX_SIZE;
}

static void pnv_core_power9_class_init(ObjectClass *oc, void *data)
{
    PnvCoreClass *pcc = PNV_CORE_CLASS(oc);

    pcc->xscom_ops = &pnv_core_power9_xscom_ops;
    pcc->xscom_size = PNV_XSCOM_EX_SIZE;
}

static void pnv_core_power10_class_init(ObjectClass *oc, void *data)
{
    PnvCoreClass *pcc = PNV_CORE_CLASS(oc);

    pcc->xscom_ops = &pnv_core_power10_xscom_ops;
    pcc->xscom_size = PNV10_XSCOM_EC_SIZE;
}

static void pnv_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pnv_core_realize;
    dc->unrealize = pnv_core_unrealize;
    device_class_set_props(dc, pnv_core_properties);
    dc->user_creatable = false;
}

#define DEFINE_PNV_CORE_TYPE(family, cpu_model) \
    {                                           \
        .parent = TYPE_PNV_CORE,                \
        .name = PNV_CORE_TYPE_NAME(cpu_model),  \
        .class_init = pnv_core_##family##_class_init, \
    }

static const TypeInfo pnv_core_infos[] = {
    {
        .name           = TYPE_PNV_CORE,
        .parent         = TYPE_CPU_CORE,
        .instance_size  = sizeof(PnvCore),
        .class_size     = sizeof(PnvCoreClass),
        .class_init = pnv_core_class_init,
        .abstract       = true,
    },
    DEFINE_PNV_CORE_TYPE(power8, "power8e_v2.1"),
    DEFINE_PNV_CORE_TYPE(power8, "power8_v2.0"),
    DEFINE_PNV_CORE_TYPE(power8, "power8nvl_v1.0"),
    DEFINE_PNV_CORE_TYPE(power9, "power9_v2.2"),
    DEFINE_PNV_CORE_TYPE(power10, "power10_v2.0"),
};

DEFINE_TYPES(pnv_core_infos)

/*
 * POWER9 Quads
 */

#define P9X_EX_NCU_SPEC_BAR                     0x11010

static uint64_t pnv_quad_power9_xscom_read(void *opaque, hwaddr addr,
                                           unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = -1;

    switch (offset) {
    case P9X_EX_NCU_SPEC_BAR:
    case P9X_EX_NCU_SPEC_BAR + 0x400: /* Second EX */
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_quad_power9_xscom_write(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned int width)
{
    uint32_t offset = addr >> 3;

    switch (offset) {
    case P9X_EX_NCU_SPEC_BAR:
    case P9X_EX_NCU_SPEC_BAR + 0x400: /* Second EX */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                  offset);
    }
}

static const MemoryRegionOps pnv_quad_power9_xscom_ops = {
    .read = pnv_quad_power9_xscom_read,
    .write = pnv_quad_power9_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

/*
 * POWER10 Quads
 */

static uint64_t pnv_quad_power10_xscom_read(void *opaque, hwaddr addr,
                                            unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = -1;

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_quad_power10_xscom_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned int width)
{
    uint32_t offset = addr >> 3;

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                      offset);
    }
}

static const MemoryRegionOps pnv_quad_power10_xscom_ops = {
    .read = pnv_quad_power10_xscom_read,
    .write = pnv_quad_power10_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

#define P10_QME_SPWU_HYP 0x83c
#define P10_QME_SSH_HYP  0x82c

static uint64_t pnv_qme_power10_xscom_read(void *opaque, hwaddr addr,
                                            unsigned int width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = -1;

    /*
     * Forth nibble selects the core within a quad, mask it to process read
     * for any core.
     */
    switch (offset & ~0xf000) {
    case P10_QME_SPWU_HYP:
    case P10_QME_SSH_HYP:
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp read 0x%08x\n", __func__,
                      offset);
    }

    return val;
}

static void pnv_qme_power10_xscom_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned int width)
{
    uint32_t offset = addr >> 3;

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimp write 0x%08x\n", __func__,
                      offset);
    }
}

static const MemoryRegionOps pnv_qme_power10_xscom_ops = {
    .read = pnv_qme_power10_xscom_read,
    .write = pnv_qme_power10_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_quad_power9_realize(DeviceState *dev, Error **errp)
{
    PnvQuad *eq = PNV_QUAD(dev);
    PnvQuadClass *pqc = PNV_QUAD_GET_CLASS(eq);
    char name[32];

    snprintf(name, sizeof(name), "xscom-quad.%d", eq->quad_id);
    pnv_xscom_region_init(&eq->xscom_regs, OBJECT(dev),
                          pqc->xscom_ops,
                          eq, name,
                          pqc->xscom_size);
}

static void pnv_quad_power10_realize(DeviceState *dev, Error **errp)
{
    PnvQuad *eq = PNV_QUAD(dev);
    PnvQuadClass *pqc = PNV_QUAD_GET_CLASS(eq);
    char name[32];

    snprintf(name, sizeof(name), "xscom-quad.%d", eq->quad_id);
    pnv_xscom_region_init(&eq->xscom_regs, OBJECT(dev),
                          pqc->xscom_ops,
                          eq, name,
                          pqc->xscom_size);

    snprintf(name, sizeof(name), "xscom-qme.%d", eq->quad_id);
    pnv_xscom_region_init(&eq->xscom_qme_regs, OBJECT(dev),
                          pqc->xscom_qme_ops,
                          eq, name,
                          pqc->xscom_qme_size);
}

static Property pnv_quad_properties[] = {
    DEFINE_PROP_UINT32("quad-id", PnvQuad, quad_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_quad_power9_class_init(ObjectClass *oc, void *data)
{
    PnvQuadClass *pqc = PNV_QUAD_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pnv_quad_power9_realize;

    pqc->xscom_ops = &pnv_quad_power9_xscom_ops;
    pqc->xscom_size = PNV9_XSCOM_EQ_SIZE;
}

static void pnv_quad_power10_class_init(ObjectClass *oc, void *data)
{
    PnvQuadClass *pqc = PNV_QUAD_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pnv_quad_power10_realize;

    pqc->xscom_ops = &pnv_quad_power10_xscom_ops;
    pqc->xscom_size = PNV10_XSCOM_EQ_SIZE;

    pqc->xscom_qme_ops = &pnv_qme_power10_xscom_ops;
    pqc->xscom_qme_size = PNV10_XSCOM_QME_SIZE;
}

static void pnv_quad_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, pnv_quad_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_quad_infos[] = {
    {
        .name          = TYPE_PNV_QUAD,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(PnvQuad),
        .class_size    = sizeof(PnvQuadClass),
        .class_init    = pnv_quad_class_init,
        .abstract      = true,
    },
    {
        .parent = TYPE_PNV_QUAD,
        .name = PNV_QUAD_TYPE_NAME("power9"),
        .class_init = pnv_quad_power9_class_init,
    },
    {
        .parent = TYPE_PNV_QUAD,
        .name = PNV_QUAD_TYPE_NAME("power10"),
        .class_init = pnv_quad_power10_class_init,
    },
};

DEFINE_TYPES(pnv_quad_infos);
