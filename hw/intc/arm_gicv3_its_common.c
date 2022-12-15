/*
 * ITS base class for a GICv3-based system
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "qemu/log.h"
#include "qemu/module.h"

static int gicv3_its_pre_save(void *opaque)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    GICv3ITSCommonClass *c = ARM_GICV3_ITS_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }

    return 0;
}

static int gicv3_its_post_load(void *opaque, int version_id)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    GICv3ITSCommonClass *c = ARM_GICV3_ITS_COMMON_GET_CLASS(s);

    if (c->post_load) {
        c->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_its = {
    .name = "arm_gicv3_its",
    .pre_save = gicv3_its_pre_save,
    .post_load = gicv3_its_post_load,
    .priority = MIG_PRI_GICV3_ITS,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctlr, GICv3ITSState),
        VMSTATE_UINT32(iidr, GICv3ITSState),
        VMSTATE_UINT64(cbaser, GICv3ITSState),
        VMSTATE_UINT64(cwriter, GICv3ITSState),
        VMSTATE_UINT64(creadr, GICv3ITSState),
        VMSTATE_UINT64_ARRAY(baser, GICv3ITSState, 8),
        VMSTATE_END_OF_LIST()
    },
};

static MemTxResult gicv3_its_trans_read(void *opaque, hwaddr offset,
                                        uint64_t *data, unsigned size,
                                        MemTxAttrs attrs)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ITS read at offset 0x%"PRIx64"\n", offset);
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult gicv3_its_trans_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned size,
                                         MemTxAttrs attrs)
{
    if (offset == 0x0040 && ((size == 2) || (size == 4))) {
        GICv3ITSState *s = ARM_GICV3_ITS_COMMON(opaque);
        GICv3ITSCommonClass *c = ARM_GICV3_ITS_COMMON_GET_CLASS(s);
        int ret = c->send_msi(s, le64_to_cpu(value), attrs.requester_id);

        if (ret <= 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ITS: Error sending MSI: %s\n", strerror(-ret));
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ITS write at bad offset 0x%"PRIx64"\n", offset);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps gicv3_its_trans_ops = {
    .read_with_attrs = gicv3_its_trans_read,
    .write_with_attrs = gicv3_its_trans_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void gicv3_its_init_mmio(GICv3ITSState *s, const MemoryRegionOps *ops,
                         const MemoryRegionOps *tops)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    memory_region_init_io(&s->iomem_its_cntrl, OBJECT(s), ops, s,
                          "control", ITS_CONTROL_SIZE);
    memory_region_init_io(&s->iomem_its_translation, OBJECT(s),
                          tops ? tops : &gicv3_its_trans_ops, s,
                          "translation", ITS_TRANS_SIZE);

    /* Our two regions are always adjacent, therefore we now combine them
     * into a single one in order to make our users' life easier.
     */
    memory_region_init(&s->iomem_main, OBJECT(s), "gicv3_its", ITS_SIZE);
    memory_region_add_subregion(&s->iomem_main, 0, &s->iomem_its_cntrl);
    memory_region_add_subregion(&s->iomem_main, ITS_CONTROL_SIZE,
                                &s->iomem_its_translation);
    sysbus_init_mmio(sbd, &s->iomem_main);

    msi_nonbroken = true;
}

static void gicv3_its_common_reset_hold(Object *obj)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(obj);

    s->ctlr = 0;
    s->cbaser = 0;
    s->cwriter = 0;
    s->creadr = 0;
    s->iidr = 0;
    memset(&s->baser, 0, sizeof(s->baser));
}

static void gicv3_its_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = gicv3_its_common_reset_hold;
    dc->vmsd = &vmstate_its;
}

static const TypeInfo gicv3_its_common_info = {
    .name = TYPE_ARM_GICV3_ITS_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICv3ITSState),
    .class_size = sizeof(GICv3ITSCommonClass),
    .class_init = gicv3_its_common_class_init,
    .abstract = true,
};

static void gicv3_its_common_register_types(void)
{
    type_register_static(&gicv3_its_common_info);
}

type_init(gicv3_its_common_register_types)
