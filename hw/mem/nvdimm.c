/*
 * Non-Volatile Dual In-line Memory Module Virtualization Implementation
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *  Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * Currently, it only supports PMEM Virtualization.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/pmem.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/mem/nvdimm.h"
#include "hw/qdev-properties.h"
#include "hw/mem/memory-device.h"
#include "sysemu/hostmem.h"

static void nvdimm_get_label_size(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    uint64_t value = nvdimm->label_size;

    visit_type_size(v, name, &value, errp);
}

static void nvdimm_set_label_size(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    uint64_t value;

    if (nvdimm->nvdimm_mr) {
        error_setg(errp, "cannot change property value");
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }
    if (value < MIN_NAMESPACE_LABEL_SIZE) {
        error_setg(errp, "Property '%s.%s' (0x%" PRIx64 ") is required"
                   " at least 0x%lx", object_get_typename(obj), name, value,
                   MIN_NAMESPACE_LABEL_SIZE);
        return;
    }

    nvdimm->label_size = value;
}

static void nvdimm_get_uuid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    char *value = NULL;

    value = qemu_uuid_unparse_strdup(&nvdimm->uuid);

    visit_type_str(v, name, &value, errp);
    g_free(value);
}


static void nvdimm_set_uuid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    char *value;

    if (!visit_type_str(v, name, &value, errp)) {
        return;
    }

    if (qemu_uuid_parse(value, &nvdimm->uuid) != 0) {
        error_setg(errp, "Property '%s.%s' has invalid value",
                   object_get_typename(obj), name);
    }

    g_free(value);
}


static void nvdimm_init(Object *obj)
{
    object_property_add(obj, NVDIMM_LABEL_SIZE_PROP, "int",
                        nvdimm_get_label_size, nvdimm_set_label_size, NULL,
                        NULL);

    object_property_add(obj, NVDIMM_UUID_PROP, "QemuUUID", nvdimm_get_uuid,
                        nvdimm_set_uuid, NULL, NULL);
}

static void nvdimm_finalize(Object *obj)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);

    g_free(nvdimm->nvdimm_mr);
}

static void nvdimm_prepare_memory_region(NVDIMMDevice *nvdimm, Error **errp)
{
    PCDIMMDevice *dimm = PC_DIMM(nvdimm);
    uint64_t align, pmem_size, size;
    MemoryRegion *mr;

    g_assert(!nvdimm->nvdimm_mr);

    if (!dimm->hostmem) {
        error_setg(errp, "'" PC_DIMM_MEMDEV_PROP "' property must be set");
        return;
    }

    mr = host_memory_backend_get_memory(dimm->hostmem);
    align = memory_region_get_alignment(mr);
    size = memory_region_size(mr);

    pmem_size = size - nvdimm->label_size;
    nvdimm->label_data = memory_region_get_ram_ptr(mr) + pmem_size;
    pmem_size = QEMU_ALIGN_DOWN(pmem_size, align);

    if (size <= nvdimm->label_size || !pmem_size) {
        HostMemoryBackend *hostmem = dimm->hostmem;

        error_setg(errp, "the size of memdev %s (0x%" PRIx64 ") is too "
                   "small to contain nvdimm label (0x%" PRIx64 ") and "
                   "aligned PMEM (0x%" PRIx64 ")",
                   object_get_canonical_path_component(OBJECT(hostmem)),
                   memory_region_size(mr), nvdimm->label_size, align);
        return;
    }

    if (!nvdimm->unarmed && memory_region_is_rom(mr)) {
        HostMemoryBackend *hostmem = dimm->hostmem;

        error_setg(errp, "'unarmed' property must be 'on' since memdev %s "
                   "is read-only",
                   object_get_canonical_path_component(OBJECT(hostmem)));
        return;
    }

    nvdimm->nvdimm_mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(nvdimm->nvdimm_mr, OBJECT(dimm),
                             "nvdimm-memory", mr, 0, pmem_size);
    memory_region_set_nonvolatile(nvdimm->nvdimm_mr, true);
    nvdimm->nvdimm_mr->align = align;
}

static MemoryRegion *nvdimm_md_get_memory_region(MemoryDeviceState *md,
                                                 Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(md);
    Error *local_err = NULL;

    if (!nvdimm->nvdimm_mr) {
        nvdimm_prepare_memory_region(nvdimm, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return NULL;
        }
    }
    return nvdimm->nvdimm_mr;
}

static void nvdimm_realize(PCDIMMDevice *dimm, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(dimm);
    NVDIMMClass *ndc = NVDIMM_GET_CLASS(nvdimm);

    if (!nvdimm->nvdimm_mr) {
        nvdimm_prepare_memory_region(nvdimm, errp);
    }

    if (ndc->realize) {
        ndc->realize(nvdimm, errp);
    }
}

static void nvdimm_unrealize(PCDIMMDevice *dimm)
{
    NVDIMMDevice *nvdimm = NVDIMM(dimm);
    NVDIMMClass *ndc = NVDIMM_GET_CLASS(nvdimm);

    if (ndc->unrealize) {
        ndc->unrealize(nvdimm);
    }
}

/*
 * the caller should check the input parameters before calling
 * label read/write functions.
 */
static void nvdimm_validate_rw_label_data(NVDIMMDevice *nvdimm, uint64_t size,
                                        uint64_t offset)
{
    assert((nvdimm->label_size >= size + offset) && (offset + size > offset));
}

static void nvdimm_read_label_data(NVDIMMDevice *nvdimm, void *buf,
                                   uint64_t size, uint64_t offset)
{
    nvdimm_validate_rw_label_data(nvdimm, size, offset);

    memcpy(buf, nvdimm->label_data + offset, size);
}

static void nvdimm_write_label_data(NVDIMMDevice *nvdimm, const void *buf,
                                    uint64_t size, uint64_t offset)
{
    MemoryRegion *mr;
    PCDIMMDevice *dimm = PC_DIMM(nvdimm);
    bool is_pmem = object_property_get_bool(OBJECT(dimm->hostmem),
                                            "pmem", NULL);
    uint64_t backend_offset;

    nvdimm_validate_rw_label_data(nvdimm, size, offset);

    if (!is_pmem) {
        memcpy(nvdimm->label_data + offset, buf, size);
    } else {
        pmem_memcpy_persist(nvdimm->label_data + offset, buf, size);
    }

    mr = host_memory_backend_get_memory(dimm->hostmem);
    backend_offset = memory_region_size(mr) - nvdimm->label_size + offset;
    memory_region_set_dirty(mr, backend_offset, size);
}

static Property nvdimm_properties[] = {
    DEFINE_PROP_BOOL(NVDIMM_UNARMED_PROP, NVDIMMDevice, unarmed, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvdimm_class_init(ObjectClass *oc, void *data)
{
    PCDIMMDeviceClass *ddc = PC_DIMM_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);
    NVDIMMClass *nvc = NVDIMM_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    ddc->realize = nvdimm_realize;
    ddc->unrealize = nvdimm_unrealize;
    mdc->get_memory_region = nvdimm_md_get_memory_region;
    device_class_set_props(dc, nvdimm_properties);

    nvc->read_label_data = nvdimm_read_label_data;
    nvc->write_label_data = nvdimm_write_label_data;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo nvdimm_info = {
    .name          = TYPE_NVDIMM,
    .parent        = TYPE_PC_DIMM,
    .class_size    = sizeof(NVDIMMClass),
    .class_init    = nvdimm_class_init,
    .instance_size = sizeof(NVDIMMDevice),
    .instance_init = nvdimm_init,
    .instance_finalize = nvdimm_finalize,
};

static void nvdimm_register_types(void)
{
    type_register_static(&nvdimm_info);
}

type_init(nvdimm_register_types)
