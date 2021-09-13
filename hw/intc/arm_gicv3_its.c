/*
 * ITS emulation for a GICv3-based system
 *
 * Copyright Linaro.org 2021
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "gicv3_internal.h"
#include "qom/object.h"
#include "qapi/error.h"

typedef struct GICv3ITSClass GICv3ITSClass;
/* This is reusing the GICv3ITSState typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3ITSState, GICv3ITSClass,
                     ARM_GICV3_ITS, TYPE_ARM_GICV3_ITS)

struct GICv3ITSClass {
    GICv3ITSCommonClass parent_class;
    void (*parent_reset)(DeviceState *dev);
};

static MemTxResult gicv3_its_translation_write(void *opaque, hwaddr offset,
                                               uint64_t data, unsigned size,
                                               MemTxAttrs attrs)
{
    return MEMTX_OK;
}

static bool its_writel(GICv3ITSState *s, hwaddr offset,
                              uint64_t value, MemTxAttrs attrs)
{
    bool result = true;

    return result;
}

static bool its_readl(GICv3ITSState *s, hwaddr offset,
                             uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;

    return result;
}

static bool its_writell(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    bool result = true;

    return result;
}

static bool its_readll(GICv3ITSState *s, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;

    return result;
}

static MemTxResult gicv3_its_read(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    bool result;

    switch (size) {
    case 4:
        result = its_readl(s, offset, data, attrs);
        break;
    case 8:
        result = its_readll(s, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use false returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        *data = 0;
    }
    return MEMTX_OK;
}

static MemTxResult gicv3_its_write(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    bool result;

    switch (size) {
    case 4:
        result = its_writel(s, offset, data, attrs);
        break;
    case 8:
        result = its_writell(s, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use false returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
    }
    return MEMTX_OK;
}

static const MemoryRegionOps gicv3_its_control_ops = {
    .read_with_attrs = gicv3_its_read,
    .write_with_attrs = gicv3_its_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gicv3_its_translation_ops = {
    .write_with_attrs = gicv3_its_translation_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gicv3_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    int i;

    for (i = 0; i < s->gicv3->num_cpu; i++) {
        if (!(s->gicv3->cpu[i].gicr_typer & GICR_TYPER_PLPIS)) {
            error_setg(errp, "Physical LPI not supported by CPU %d", i);
            return;
        }
    }

    gicv3_its_init_mmio(s, &gicv3_its_control_ops, &gicv3_its_translation_ops);

    /* set the ITS default features supported */
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, PHYSICAL,
                          GITS_TYPE_PHYSICAL);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, ITT_ENTRY_SIZE,
                          ITS_ITT_ENTRY_SIZE - 1);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, IDBITS, ITS_IDBITS);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, DEVBITS, ITS_DEVBITS);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIL, 1);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIDBITS, ITS_CIDBITS);
}

static void gicv3_its_reset(DeviceState *dev)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    GICv3ITSClass *c = ARM_GICV3_ITS_GET_CLASS(s);

    c->parent_reset(dev);

    /* Quiescent bit reset to 1 */
    s->ctlr = FIELD_DP32(s->ctlr, GITS_CTLR, QUIESCENT, 1);

    /*
     * setting GITS_BASER0.Type = 0b001 (Device)
     *         GITS_BASER1.Type = 0b100 (Collection Table)
     *         GITS_BASER<n>.Type,where n = 3 to 7 are 0b00 (Unimplemented)
     *         GITS_BASER<0,1>.Page_Size = 64KB
     * and default translation table entry size to 16 bytes
     */
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, TYPE,
                             GITS_BASER_TYPE_DEVICE);
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, PAGESIZE,
                             GITS_BASER_PAGESIZE_64K);
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, ENTRYSIZE,
                             GITS_DTE_SIZE - 1);

    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, TYPE,
                             GITS_BASER_TYPE_COLLECTION);
    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, PAGESIZE,
                             GITS_BASER_PAGESIZE_64K);
    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, ENTRYSIZE,
                             GITS_CTE_SIZE - 1);
}

static Property gicv3_its_props[] = {
    DEFINE_PROP_LINK("parent-gicv3", GICv3ITSState, gicv3, "arm-gicv3",
                     GICv3State *),
    DEFINE_PROP_END_OF_LIST(),
};

static void gicv3_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    GICv3ITSClass *ic = ARM_GICV3_ITS_CLASS(klass);

    dc->realize = gicv3_arm_its_realize;
    device_class_set_props(dc, gicv3_its_props);
    device_class_set_parent_reset(dc, gicv3_its_reset, &ic->parent_reset);
}

static const TypeInfo gicv3_its_info = {
    .name = TYPE_ARM_GICV3_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .class_init = gicv3_its_class_init,
    .class_size = sizeof(GICv3ITSClass),
};

static void gicv3_its_register_types(void)
{
    type_register_static(&gicv3_its_info);
}

type_init(gicv3_its_register_types)
