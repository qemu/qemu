/*
 * Dimm device for Memory Hotplug
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2014 Red Hat Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "hw/mem/pc-dimm.h"
#include "qemu/config-file.h"
#include "qapi/visitor.h"
#include "qemu/range.h"

static gint pc_dimm_addr_sort(gconstpointer a, gconstpointer b)
{
    PCDIMMDevice *x = PC_DIMM(a);
    PCDIMMDevice *y = PC_DIMM(b);
    Int128 diff = int128_sub(int128_make64(x->addr), int128_make64(y->addr));

    if (int128_lt(diff, int128_zero())) {
        return -1;
    } else if (int128_gt(diff, int128_zero())) {
        return 1;
    }
    return 0;
}

static int pc_dimm_built_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized DIMMs matter */
            *list = g_slist_insert_sorted(*list, dev, pc_dimm_addr_sort);
        }
    }

    object_child_foreach(obj, pc_dimm_built_list, opaque);
    return 0;
}

uint64_t pc_dimm_get_free_addr(uint64_t address_space_start,
                               uint64_t address_space_size,
                               uint64_t *hint, uint64_t size,
                               Error **errp)
{
    GSList *list = NULL, *item;
    uint64_t new_addr, ret = 0;
    uint64_t address_space_end = address_space_start + address_space_size;

    assert(address_space_end > address_space_size);
    object_child_foreach(qdev_get_machine(), pc_dimm_built_list, &list);

    if (hint) {
        new_addr = *hint;
    } else {
        new_addr = address_space_start;
    }

    /* find address range that will fit new DIMM */
    for (item = list; item; item = g_slist_next(item)) {
        PCDIMMDevice *dimm = item->data;
        uint64_t dimm_size = object_property_get_int(OBJECT(dimm),
                                                     PC_DIMM_SIZE_PROP,
                                                     errp);
        if (errp && *errp) {
            goto out;
        }

        if (ranges_overlap(dimm->addr, dimm_size, new_addr, size)) {
            if (hint) {
                DeviceState *d = DEVICE(dimm);
                error_setg(errp, "address range conflicts with '%s'", d->id);
                goto out;
            }
            new_addr = dimm->addr + dimm_size;
        }
    }
    ret = new_addr;

    if (new_addr < address_space_start) {
        error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                   "] at 0x%" PRIx64, new_addr, size, address_space_start);
    } else if ((new_addr + size) > address_space_end) {
        error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                   "] beyond 0x%" PRIx64, new_addr, size, address_space_end);
    }

out:
    g_slist_free(list);
    return ret;
}

static Property pc_dimm_properties[] = {
    DEFINE_PROP_UINT64(PC_DIMM_ADDR_PROP, PCDIMMDevice, addr, 0),
    DEFINE_PROP_UINT32(PC_DIMM_NODE_PROP, PCDIMMDevice, node, 0),
    DEFINE_PROP_INT32(PC_DIMM_SLOT_PROP, PCDIMMDevice, slot,
                      PC_DIMM_UNASSIGNED_SLOT),
    DEFINE_PROP_END_OF_LIST(),
};

static void pc_dimm_get_size(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    int64_t value;
    MemoryRegion *mr;
    PCDIMMDevice *dimm = PC_DIMM(obj);

    mr = host_memory_backend_get_memory(dimm->hostmem, errp);
    value = memory_region_size(mr);

    visit_type_int(v, &value, name, errp);
}

static void pc_dimm_check_memdev_is_busy(Object *obj, const char *name,
                                      Object *val, Error **errp)
{
    MemoryRegion *mr;

    mr = host_memory_backend_get_memory(MEMORY_BACKEND(val), errp);
    if (memory_region_is_mapped(mr)) {
        char *path = object_get_canonical_path_component(val);
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
    } else {
        qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
    }
}

static void pc_dimm_init(Object *obj)
{
    PCDIMMDevice *dimm = PC_DIMM(obj);

    object_property_add(obj, PC_DIMM_SIZE_PROP, "int", pc_dimm_get_size,
                        NULL, NULL, NULL, &error_abort);
    object_property_add_link(obj, PC_DIMM_MEMDEV_PROP, TYPE_MEMORY_BACKEND,
                             (Object **)&dimm->hostmem,
                             pc_dimm_check_memdev_is_busy,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void pc_dimm_realize(DeviceState *dev, Error **errp)
{
    PCDIMMDevice *dimm = PC_DIMM(dev);

    if (!dimm->hostmem) {
        error_setg(errp, "'" PC_DIMM_MEMDEV_PROP "' property is not set");
        return;
    }
}

static MemoryRegion *pc_dimm_get_memory_region(PCDIMMDevice *dimm)
{
    return host_memory_backend_get_memory(dimm->hostmem, &error_abort);
}

static void pc_dimm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCDIMMDeviceClass *ddc = PC_DIMM_CLASS(oc);

    dc->realize = pc_dimm_realize;
    dc->props = pc_dimm_properties;

    ddc->get_memory_region = pc_dimm_get_memory_region;
}

static TypeInfo pc_dimm_info = {
    .name          = TYPE_PC_DIMM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PCDIMMDevice),
    .instance_init = pc_dimm_init,
    .class_init    = pc_dimm_class_init,
    .class_size    = sizeof(PCDIMMDeviceClass),
};

static void pc_dimm_register_types(void)
{
    type_register_static(&pc_dimm_info);
}

type_init(pc_dimm_register_types)
