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

static uint64_t baser_base_addr(uint64_t value, uint32_t page_sz)
{
    uint64_t result = 0;

    switch (page_sz) {
    case GITS_PAGE_SIZE_4K:
    case GITS_PAGE_SIZE_16K:
        result = FIELD_EX64(value, GITS_BASER, PHYADDR) << 12;
        break;

    case GITS_PAGE_SIZE_64K:
        result = FIELD_EX64(value, GITS_BASER, PHYADDRL_64K) << 16;
        result |= FIELD_EX64(value, GITS_BASER, PHYADDRH_64K) << 48;
        break;

    default:
        break;
    }
    return result;
}

/*
 * This function extracts the ITS Device and Collection table specific
 * parameters (like base_addr, size etc) from GITS_BASER register.
 * It is called during ITS enable and also during post_load migration
 */
static void extract_table_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint8_t  page_sz_type;
    uint8_t type;
    uint32_t page_sz = 0;
    uint64_t value;

    for (int i = 0; i < 8; i++) {
        value = s->baser[i];

        if (!value) {
            continue;
        }

        page_sz_type = FIELD_EX64(value, GITS_BASER, PAGESIZE);

        switch (page_sz_type) {
        case 0:
            page_sz = GITS_PAGE_SIZE_4K;
            break;

        case 1:
            page_sz = GITS_PAGE_SIZE_16K;
            break;

        case 2:
        case 3:
            page_sz = GITS_PAGE_SIZE_64K;
            break;

        default:
            g_assert_not_reached();
        }

        num_pages = FIELD_EX64(value, GITS_BASER, SIZE) + 1;

        type = FIELD_EX64(value, GITS_BASER, TYPE);

        switch (type) {

        case GITS_BASER_TYPE_DEVICE:
            memset(&s->dt, 0 , sizeof(s->dt));
            s->dt.valid = FIELD_EX64(value, GITS_BASER, VALID);

            if (!s->dt.valid) {
                return;
            }

            s->dt.page_sz = page_sz;
            s->dt.indirect = FIELD_EX64(value, GITS_BASER, INDIRECT);
            s->dt.entry_sz = FIELD_EX64(value, GITS_BASER, ENTRYSIZE);

            if (!s->dt.indirect) {
                s->dt.max_entries = (num_pages * page_sz) / s->dt.entry_sz;
            } else {
                s->dt.max_entries = (((num_pages * page_sz) /
                                     L1TABLE_ENTRY_SIZE) *
                                     (page_sz / s->dt.entry_sz));
            }

            s->dt.maxids.max_devids = (1UL << (FIELD_EX64(s->typer, GITS_TYPER,
                                       DEVBITS) + 1));

            s->dt.base_addr = baser_base_addr(value, page_sz);

            break;

        case GITS_BASER_TYPE_COLLECTION:
            memset(&s->ct, 0 , sizeof(s->ct));
            s->ct.valid = FIELD_EX64(value, GITS_BASER, VALID);

            /*
             * GITS_TYPER.HCC is 0 for this implementation
             * hence writes are discarded if ct.valid is 0
             */
            if (!s->ct.valid) {
                return;
            }

            s->ct.page_sz = page_sz;
            s->ct.indirect = FIELD_EX64(value, GITS_BASER, INDIRECT);
            s->ct.entry_sz = FIELD_EX64(value, GITS_BASER, ENTRYSIZE);

            if (!s->ct.indirect) {
                s->ct.max_entries = (num_pages * page_sz) / s->ct.entry_sz;
            } else {
                s->ct.max_entries = (((num_pages * page_sz) /
                                     L1TABLE_ENTRY_SIZE) *
                                     (page_sz / s->ct.entry_sz));
            }

            if (FIELD_EX64(s->typer, GITS_TYPER, CIL)) {
                s->ct.maxids.max_collids = (1UL << (FIELD_EX64(s->typer,
                                            GITS_TYPER, CIDBITS) + 1));
            } else {
                /* 16-bit CollectionId supported when CIL == 0 */
                s->ct.maxids.max_collids = (1UL << 16);
            }

            s->ct.base_addr = baser_base_addr(value, page_sz);

            break;

        default:
            break;
        }
    }
}

static void extract_cmdq_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint64_t value = s->cbaser;

    num_pages = FIELD_EX64(value, GITS_CBASER, SIZE) + 1;

    memset(&s->cq, 0 , sizeof(s->cq));
    s->cq.valid = FIELD_EX64(value, GITS_CBASER, VALID);

    if (s->cq.valid) {
        s->cq.max_entries = (num_pages * GITS_PAGE_SIZE_4K) /
                             GITS_CMDQ_ENTRY_SIZE;
        s->cq.base_addr = FIELD_EX64(value, GITS_CBASER, PHYADDR);
        s->cq.base_addr <<= R_GITS_CBASER_PHYADDR_SHIFT;
    }
}

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
    int index;

    switch (offset) {
    case GITS_CTLR:
        s->ctlr |= (value & ~(s->ctlr));

        if (s->ctlr & ITS_CTLR_ENABLED) {
            extract_table_params(s);
            extract_cmdq_params(s);
            s->creadr = 0;
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 0, 32, value);
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CBASER + 4:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 32, 32, value);
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CWRITER:
        s->cwriter = deposit64(s->cwriter, 0, 32,
                               (value & ~R_GITS_CWRITER_RETRY_MASK));
        break;
    case GITS_CWRITER + 4:
        s->cwriter = deposit64(s->cwriter, 32, 32, value);
        break;
    case GITS_CREADR:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = deposit64(s->creadr, 0, 32,
                                  (value & ~R_GITS_CREADR_STALLED_MASK));
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_CREADR + 4:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = deposit64(s->creadr, 32, 32, value);
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;

            if (offset & 7) {
                value <<= 32;
                value &= ~GITS_BASER_RO_MASK;
                s->baser[index] &= GITS_BASER_RO_MASK | MAKE_64BIT_MASK(0, 32);
                s->baser[index] |= value;
            } else {
                value &= ~GITS_BASER_RO_MASK;
                s->baser[index] &= GITS_BASER_RO_MASK | MAKE_64BIT_MASK(32, 32);
                s->baser[index] |= value;
            }
        }
        break;
    case GITS_IIDR:
    case GITS_IDREGS ... GITS_IDREGS + 0x2f:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_readl(GICv3ITSState *s, hwaddr offset,
                             uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_CTLR:
        *data = s->ctlr;
        break;
    case GITS_IIDR:
        *data = gicv3_iidr();
        break;
    case GITS_IDREGS ... GITS_IDREGS + 0x2f:
        /* ID registers */
        *data = gicv3_idreg(offset - GITS_IDREGS);
        break;
    case GITS_TYPER:
        *data = extract64(s->typer, 0, 32);
        break;
    case GITS_TYPER + 4:
        *data = extract64(s->typer, 32, 32);
        break;
    case GITS_CBASER:
        *data = extract64(s->cbaser, 0, 32);
        break;
    case GITS_CBASER + 4:
        *data = extract64(s->cbaser, 32, 32);
        break;
    case GITS_CREADR:
        *data = extract64(s->creadr, 0, 32);
        break;
    case GITS_CREADR + 4:
        *data = extract64(s->creadr, 32, 32);
        break;
    case GITS_CWRITER:
        *data = extract64(s->cwriter, 0, 32);
        break;
    case GITS_CWRITER + 4:
        *data = extract64(s->cwriter, 32, 32);
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        if (offset & 7) {
            *data = extract64(s->baser[index], 32, 32);
        } else {
            *data = extract64(s->baser[index], 0, 32);
        }
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_writell(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_BASER ... GITS_BASER + 0x3f:
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;
            s->baser[index] &= GITS_BASER_RO_MASK;
            s->baser[index] |= (value & ~GITS_BASER_RO_MASK);
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = value;
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CWRITER:
        s->cwriter = value & ~R_GITS_CWRITER_RETRY_MASK;
        break;
    case GITS_CREADR:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = value & ~R_GITS_CREADR_STALLED_MASK;
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_TYPER:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_readll(GICv3ITSState *s, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_TYPER:
        *data = s->typer;
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        *data = s->baser[index];
        break;
    case GITS_CBASER:
        *data = s->cbaser;
        break;
    case GITS_CREADR:
        *data = s->creadr;
        break;
    case GITS_CWRITER:
        *data = s->cwriter;
        break;
    default:
        result = false;
        break;
    }
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

    address_space_init(&s->gicv3->dma_as, s->gicv3->dma,
                       "gicv3-its-sysmem");

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

static void gicv3_its_post_load(GICv3ITSState *s)
{
    if (s->ctlr & ITS_CTLR_ENABLED) {
        extract_table_params(s);
        extract_cmdq_params(s);
    }
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
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);

    dc->realize = gicv3_arm_its_realize;
    device_class_set_props(dc, gicv3_its_props);
    device_class_set_parent_reset(dc, gicv3_its_reset, &ic->parent_reset);
    icc->post_load = gicv3_its_post_load;
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
