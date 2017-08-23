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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/mem/nvdimm.h"

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
    Error *local_err = NULL;
    uint64_t value;

    if (memory_region_size(&nvdimm->nvdimm_mr)) {
        error_setg(&local_err, "cannot change property value");
        goto out;
    }

    visit_type_size(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (value < MIN_NAMESPACE_LABEL_SIZE) {
        error_setg(&local_err, "Property '%s.%s' (0x%" PRIx64 ") is required"
                   " at least 0x%lx", object_get_typename(obj),
                   name, value, MIN_NAMESPACE_LABEL_SIZE);
        goto out;
    }

    nvdimm->label_size = value;
out:
    error_propagate(errp, local_err);
}

static void nvdimm_init(Object *obj)
{
    object_property_add(obj, "label-size", "int",
                        nvdimm_get_label_size, nvdimm_set_label_size, NULL,
                        NULL, NULL);
}

static MemoryRegion *nvdimm_get_memory_region(PCDIMMDevice *dimm, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(dimm);

    return &nvdimm->nvdimm_mr;
}

static void nvdimm_realize(PCDIMMDevice *dimm, Error **errp)
{
    MemoryRegion *mr = host_memory_backend_get_memory(dimm->hostmem, errp);
    NVDIMMDevice *nvdimm = NVDIMM(dimm);
    uint64_t align, pmem_size, size = memory_region_size(mr);

    align = memory_region_get_alignment(mr);

    pmem_size = size - nvdimm->label_size;
    nvdimm->label_data = memory_region_get_ram_ptr(mr) + pmem_size;
    pmem_size = QEMU_ALIGN_DOWN(pmem_size, align);

    if (size <= nvdimm->label_size || !pmem_size) {
        HostMemoryBackend *hostmem = dimm->hostmem;
        char *path = object_get_canonical_path_component(OBJECT(hostmem));

        error_setg(errp, "the size of memdev %s (0x%" PRIx64 ") is too "
                   "small to contain nvdimm label (0x%" PRIx64 ") and "
                   "aligned PMEM (0x%" PRIx64 ")",
                   path, memory_region_size(mr), nvdimm->label_size, align);
        g_free(path);
        return;
    }

    memory_region_init_alias(&nvdimm->nvdimm_mr, OBJECT(dimm),
                             "nvdimm-memory", mr, 0, pmem_size);
    nvdimm->nvdimm_mr.align = align;
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
    uint64_t backend_offset;

    nvdimm_validate_rw_label_data(nvdimm, size, offset);

    memcpy(nvdimm->label_data + offset, buf, size);

    mr = host_memory_backend_get_memory(dimm->hostmem, &error_abort);
    backend_offset = memory_region_size(mr) - nvdimm->label_size + offset;
    memory_region_set_dirty(mr, backend_offset, size);
}

static MemoryRegion *nvdimm_get_vmstate_memory_region(PCDIMMDevice *dimm)
{
    return host_memory_backend_get_memory(dimm->hostmem, &error_abort);
}

static void nvdimm_class_init(ObjectClass *oc, void *data)
{
    PCDIMMDeviceClass *ddc = PC_DIMM_CLASS(oc);
    NVDIMMClass *nvc = NVDIMM_CLASS(oc);

    ddc->realize = nvdimm_realize;
    ddc->get_memory_region = nvdimm_get_memory_region;
    ddc->get_vmstate_memory_region = nvdimm_get_vmstate_memory_region;

    nvc->read_label_data = nvdimm_read_label_data;
    nvc->write_label_data = nvdimm_write_label_data;
}

static TypeInfo nvdimm_info = {
    .name          = TYPE_NVDIMM,
    .parent        = TYPE_PC_DIMM,
    .class_size    = sizeof(NVDIMMClass),
    .class_init    = nvdimm_class_init,
    .instance_size = sizeof(NVDIMMDevice),
    .instance_init = nvdimm_init,
};

static void nvdimm_register_types(void)
{
    type_register_static(&nvdimm_info);
}

type_init(nvdimm_register_types)
